/* Gold 3DS: Pokemon Gold with a 3D overworld, built from your own ROM.
 *
 * Top screen: the overworld rebuilt in 3D from the game's live map data.
 * Bottom screen: the original Game Boy picture (text, menus, battles).
 *
 * Controls: A/B/START/SELECT/D-pad/Circle Pad as on Game Boy.
 *   X        switch the top screen between 3D and the original picture
 *   Y        single screen on/off: text, menus and battle boxes shown on the top screen
 *            (for Azahar's single-screen layout, a Steam Deck or a TV); remembered
 *   ZL / ZR  (New 3DS) zoom out / in
 *   3D slider: stereoscopic depth
 * The camera is fixed: it follows the player from the south, like the original.
 * Lighting follows the game's clock: moving sun and shadows, sunset, lit windows at night.
 * Optional: put your own FireRed/LeafGreen ROM at /3ds/gold3ds/firered.gba for a
 * taller player sprite (Red), 3D battles (both Pokémon appear in the world, using
 * FireRed's pictures) and, in single-screen mode, FireRed's menus, text boxes and
 * HP boxes, all read from that ROM.
 */
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include "core.h"
#include "world.h"
#include "render.h"
#include "light.h"
#include "tallsprite.h"
#include "frpics.h"
#include "battle.h"
#include "overlay.h"
#include "frui.h"

#define APP_DIR  "sdmc:/3ds/gold3ds"
#define ROM_PATH APP_DIR "/gold.gbc"
#define SAV_PATH APP_DIR "/gold.sav"
#define TALL_PATH APP_DIR "/firered.gba"   /* optional: your own FireRed or LeafGreen */
#define SETTINGS_PATH APP_DIR "/settings.txt"

static uint8_t *load_file(const char *path, size_t *n) {
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END); long len = ftell(f); rewind(f);
	if (len <= 0) { fclose(f); return NULL; }
	uint8_t *buf = malloc(len);
	if (buf && fread(buf, 1, len, f) != (size_t)len) { free(buf); buf = NULL; }
	fclose(f);
	*n = (size_t)len;
	return buf;
}

static bool save_sram(void) {
	mkdir(APP_DIR, 0777);
	FILE *f = fopen(SAV_PATH ".tmp", "wb");
	if (!f) return false;
	bool ok = fwrite(core_sram(), 1, CORE_SRAM_SIZE, f) == CORE_SRAM_SIZE;
	ok = (fclose(f) == 0) && ok;
	if (ok) { remove(SAV_PATH); ok = rename(SAV_PATH ".tmp", SAV_PATH) == 0; }
	return ok;
}

static void fatal(const char *msg) {
	consoleInit(GFX_BOTTOM, NULL);
	printf("\n  Gold 3DS\n\n  %s\n\n  Press START to exit.\n", msg);
	while (aptMainLoop()) {
		hidScanInput();
		if (hidKeysDown() & KEY_START) break;
		gfxFlushBuffers(); gfxSwapBuffers(); gspWaitForVBlank();
	}
	gfxExit();
	exit(0);
}

#define CAM_PITCH 0.72f    /* about 41 degrees above the ground */
#define CAM_DIST  215.0f

/* The player's own FireRed/LeafGreen, kept open for reading pictures on demand. */
static FILE *gba;
static bool gba_read(void *ctx, uint32_t off, void *buf, size_t n) {
	(void)ctx;
	return gba && fseek(gba, off, SEEK_SET) == 0 && fread(buf, 1, n, gba) == n;
}

/* Red's walking sprite sheet from that ROM. */
static bool load_tall_sprite(void) {
	static uint8_t hdr[0xC0], pal[TALL_PAL_SIZE], pic[TALL_PIC_SIZE];
	static uint32_t sheet[TALL_TEX_W * TALL_TEX_H];
	if (!gba_read(NULL, 0, hdr, sizeof hdr)) return false;
	const TallSource *src = tall_source(hdr, sizeof hdr);
	if (!src || !gba_read(NULL, src->pal, pal, sizeof pal) || !gba_read(NULL, src->pic, pic, sizeof pic)) return false;
	tall_decode(pal, pic, sheet);
	render_tall_sheet(sheet);
	return true;
}

/* Load the two battle pictures into the battle texture when they change. */
static void load_battle_pics(const FrTables *t, Battle *b) {
	static BattlePic shown[2] = { { -1, 0, 0, 0 }, { -1, 0, 0, 0 } };
	static uint32_t tex[BATTLE_TEX_W * BATTLE_TEX_H], pic[FR_PIC * FR_PIC];
	BattlePic want[2] = { battle_enemy_pic(b), battle_ally_pic(b) };
	bool changed = false;
	for (int i = 0; i < 2; i++) {
		if (want[i].species == shown[i].species && want[i].back == shown[i].back &&
		    want[i].shiny == shown[i].shiny && want[i].red == shown[i].red) continue;
		int bottom = 63;
		bool ok = want[i].red ? fr_red_back(gba_read, NULL, t, pic, &bottom)
		        : want[i].species ? fr_mon_pic(gba_read, NULL, t, want[i].species, want[i].back, want[i].shiny, pic, &bottom)
		        : false;
		for (int y = 0; y < FR_PIC; y++) {
			uint32_t *dst = tex + y * BATTLE_TEX_W + i * FR_PIC;
			if (ok) memcpy(dst, pic + y * FR_PIC, FR_PIC * 4); else memset(dst, 0, FR_PIC * 4);
		}
		if (i == 0) b->enemy_bottom = bottom; else b->ally_bottom = bottom;
		shown[i] = want[i];
		changed = true;
	}
	if (changed) render_battle_sheet(tex);
}

static float approach(float cur, float target) {
	float d = target - cur;
	if (fabsf(d) > 48) return target;               /* warps, map changes */
	float step = fabsf(d) > 18 ? 2.0f : 1.0f;        /* walk 1 px/frame, bike 2 */
	if (fabsf(d) <= step) return target;
	return cur + (d > 0 ? step : -step);
}

static float lerp(float a, float b, float t) { return a + (b - a) * t; }

/* settings.txt: one "name=value" per line */
static bool load_single_screen(void) {
	FILE *f = fopen(SETTINGS_PATH, "r");
	if (!f) return false;
	char line[64]; bool on = false;
	while (fgets(line, sizeof line, f)) if (strncmp(line, "single_screen=", 14) == 0) on = line[14] == '1';
	fclose(f);
	return on;
}

static void save_single_screen(bool on) {
	mkdir(APP_DIR, 0777);
	FILE *f = fopen(SETTINGS_PATH, "w");
	if (!f) return;
	fprintf(f, "single_screen=%d\n", on ? 1 : 0);
	fclose(f);
}

int main(void) {
	gfxInitDefault();
	gfxSet3D(true);
	osSetSpeedupEnable(true);

	size_t rom_size = 0, sav_size = 0;
	uint8_t *rom = load_file(ROM_PATH, &rom_size);
	if (!rom) fatal("Put your own Pokemon Gold\n  (USA/Europe) ROM at:\n\n  /3ds/gold3ds/gold.gbc");
	const char *why = gold_rom_check(rom, rom_size);
	if (why) fatal(why);
	uint8_t *sav = load_file(SAV_PATH, &sav_size);
	why = core_init(rom, rom_size, sav, sav_size);
	if (why) fatal(why);
	free(sav);
	time_t now = time(NULL);
	core_set_clock(localtime(&now));

	render_init();
	gba = fopen(TALL_PATH, "rb");
	const FrTables *frt = NULL;
	bool tall = false;
	if (gba) {
		uint8_t hdr[0xC0];
		if (gba_read(NULL, 0, hdr, sizeof hdr)) frt = fr_tables(hdr, sizeof hdr);
		tall = load_tall_sprite();
		if (!frt && !tall) { fclose(gba); gba = NULL; }
	}

	/* FireRed-style interface for single-screen mode, from the same ROM */
	bool fr_ui = false;
	if (frt) {
		uint32_t *ui_atlas = malloc(FRUI_TEX * FRUI_TEX * 4);
		if (ui_atlas && frui_load(gba_read, NULL, frt, ui_atlas)) { render_ui_atlas(ui_atlas); fr_ui = true; }
		free(ui_atlas);
	}
	static UiList ui;

	GoldView g = { rom, rom_size, core_peek, NULL };
	WorldMesh *mesh = render_mesh();
	static uint32_t atlas[BG_ATLAS_W * BG_ATLAS_H];   /* also used for the 128x128 sprite atlas */

	float pl_x = 0, pl_z = 0;     /* where the player is drawn (smoothed), world pixels */
	float zoom = CAM_DIST;
	float walked = 0;             /* distance walked, drives the step animation */
	int idle = 99;                /* frames since the player last moved */
	Battle battle = { 0 };
	float bt = 0;                 /* 0 = overworld camera, 1 = battle camera */
	Camera cam = { 0, 0, 0, CAM_PITCH, CAM_DIST };
	Light light;
	light_compute(12, 0, ENV_OUTDOOR, &light);
	bool voxel = true;
	bool single = load_single_screen();   /* everything on the top screen (Azahar single screen, Steam Deck, TV) */
	Overlay overlay;
	uint32_t atlas_key = 0, saved_hash = core_sram_hash(), last_hash = saved_hash;
	unsigned frame = 0;

	while (aptMainLoop()) {
		hidScanInput();
		u32 held = hidKeysHeld(), down = hidKeysDown();

		uint8_t b = 0;
		if (held & KEY_A)      b |= BTN_A;
		if (held & KEY_B)      b |= BTN_B;
		if (held & KEY_START)  b |= BTN_START;
		if (held & KEY_SELECT) b |= BTN_SELECT;
		if (held & KEY_UP)     b |= BTN_UP;      /* D-pad or Circle Pad */
		if (held & KEY_DOWN)   b |= BTN_DOWN;
		if (held & KEY_LEFT)   b |= BTN_LEFT;
		if (held & KEY_RIGHT)  b |= BTN_RIGHT;

		if (down & KEY_X) voxel = !voxel;
		if (down & KEY_Y) { single = !single; save_single_screen(single); }
		if (held & KEY_ZL) zoom = fminf(320, zoom + 2);
		if (held & KEY_ZR) zoom = fmaxf(130, zoom - 2);

		core_frame(b);

		bool in_battle = gv8(&g, wBattleMode) != 0;
		bool overworld = gv8(&g, wMapStatus) == MAPSTATUS_HANDLE && !in_battle && gv8(&g, wMapGroup) != 0;
		if (overworld) {
			light_compute(gv8(&g, hHours), gv8(&g, hMinutes), light_env(gv8(&g, wEnvironment)), &light);
			if (world_key(&g) != mesh->key || light.key != mesh->light_key) {
				world_build(&g, mesh, &light);   /* shadows are baked into the mesh */
				render_mesh_uploaded();
			}
			if ((frame & 3) == 0) {   /* tile animations, time of day, tileset swaps */
				uint32_t k = 2166136261u;
				for (uint16_t a = 0x8000; a < 0x9800; a += 7) k = (k ^ core_peek(NULL, a)) * 16777619u;
				k = (k ^ gv8(&g, wTimeOfDayPal)) * 16777619u;
				k = (k ^ gv16(&g, wTilesetPalettes)) * 16777619u;
				if (k != atlas_key) {
					atlas_key = k;
					world_bg_atlas(&g, atlas);  render_bg_atlas(atlas);
					world_obj_atlas(&g, atlas); render_obj_atlas(atlas);
				}
			}
			float ox = pl_x, oz = pl_z;
			pl_x = approach(pl_x, gv8(&g, wXCoord) * 16.0f);
			pl_z = approach(pl_z, gv8(&g, wYCoord) * 16.0f);
			float moved = fabsf(pl_x - ox) + fabsf(pl_z - oz);
			if (moved > 0 && moved < 8) { walked += moved; idle = 0; } else idle++;
		}

		/* 3D battles need FireRed's pictures and a world to stand in */
		bool battle3d = in_battle && frt && mesh->count > 0;
		if (battle3d) {
			if (!battle.active) battle_begin(&battle, &g, pl_x, pl_z);
			battle_update(&battle, &g);
			load_battle_pics(frt, &battle);
		} else {
			battle.active = false;
		}
		bt = fminf(1, fmaxf(0, bt + (battle3d ? 0.05f : -0.05f)));
		float s = bt * bt * (3 - 2 * bt);
		float btx = pl_x + 8, btz = pl_z + 8, byaw = 0, bpitch = CAM_PITCH, bdist = zoom;
		if (battle.active || bt > 0) battle_camera(&battle, &btx, &btz, &byaw, &bpitch, &bdist);
		cam.tx = lerp(pl_x + 8, btx, s); cam.tz = lerp(pl_z + 8, btz, s);
		cam.yaw = lerp(0, byaw, s); cam.pitch = lerp(CAM_PITCH, bpitch, s); cam.dist = lerp(zoom, bdist, s);

		int cap, nspr = 0, nshadow = 0, nplayer = 0, nbattle = 0;
		WVert *shbuf = render_shadow_buf(&cap);
		if (battle.active) {
			nshadow = battle_shadows(&battle, shbuf, cap);
			nbattle = battle_quads(&battle, cam.yaw, render_battle_buf(), 12);
		} else if (overworld) {
			WVert *sbuf = render_sprite_buf(&cap);
			/* tall player only when walking: the Gold sprite still shows bike and surf */
			bool tall_now = tall && gv8(&g, wPlayerState) == 0;
			nspr = world_sprites(&g, pl_x, pl_z, tall_now, sbuf, cap);
			if (tall_now) {
				bool flip;
				int fr = tall_frame(gv8(&g, wPlayerDirection), ((int)(walked / 8)) & 3, idle < 3, &flip);
				nplayer = world_tall_player(pl_x, pl_z, fr, flip, render_player_buf());
			}
			WVert *sh2 = render_shadow_buf(&cap);
			nshadow = world_sprite_shadows(&g, pl_x, pl_z, sh2, cap);
		}

		render_lcd(core_lcd());
		bool show3d = voxel && (overworld || battle.active) && mesh->count > 0;
		const UiList *uip = NULL;
		if (single) {
			overlay_plan(&g, overworld, battle.active, &overlay);
			if (fr_ui) {
				/* FireRed's look where it can restyle the screen; Gold's picture where it can't */
				FruiResult r = frui_plan(&g, overworld, battle.active, &ui);
				if (r == FRUI_DRAWN) { overlay.n = 0; overlay.dim = false; uip = &ui; }
				else if (r == FRUI_NONE) { overlay.n = 0; overlay.dim = false; }
			}
		}
		render_frame(show3d, &cam, nspr, nshadow, nplayer, nbattle, &light, single ? &overlay : NULL, uip);

		/* save once the game's save data has changed and then stayed still for 2 seconds */
		if (++frame % 120 == 0) {
			uint32_t h = core_sram_hash();
			if (h != saved_hash && h == last_hash && save_sram()) saved_hash = h;
			last_hash = h;
		}
	}

	if (core_sram_hash() != saved_hash) save_sram();
	if (gba) fclose(gba);
	render_exit();
	gfxExit();
	free(rom);
	return 0;
}
