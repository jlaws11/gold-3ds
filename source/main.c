/* Gold 3DS: Pokemon Gold with a 3D overworld, built from your own ROM.
 *
 * Top screen: the overworld rebuilt in 3D from the game's live map data.
 * Bottom screen: the original Game Boy picture (text, menus, battles).
 *
 * Controls: A/B/START/SELECT/D-pad/Circle Pad as on Game Boy.
 *   X      switch the top screen between 3D and the original picture
 *   Y      cycle camera angle
 *   L / R  rotate the camera,  ZL / ZR (New 3DS) zoom,  C-stick (New 3DS) orbit
 *   3D slider: stereoscopic depth
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

#define APP_DIR  "sdmc:/3ds/gold3ds"
#define ROM_PATH APP_DIR "/gold.gbc"
#define SAV_PATH APP_DIR "/gold.sav"

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

typedef struct { float pitch, dist; } Preset;
static const Preset presets[] = { { 0.95f, 230.0f }, { 0.55f, 170.0f }, { 1.35f, 300.0f } };

static float approach(float cur, float target) {
	float d = target - cur;
	if (fabsf(d) > 48) return target;               /* warps, map changes */
	float step = fabsf(d) > 18 ? 2.0f : 1.0f;        /* walk 1 px/frame, bike 2 */
	if (fabsf(d) <= step) return target;
	return cur + (d > 0 ? step : -step);
}

static const uint32_t sky_by_time[4] = { 0xF4D2A0FF, 0x84BEE6FF, 0x2C3A64FF, 0x101420FF };

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
	GoldView g = { rom, rom_size, core_peek, NULL };
	WorldMesh *mesh = render_mesh();
	static uint32_t atlas[128 * 128];

	Camera cam = { 0, 0, 0, presets[0].pitch, presets[0].dist };
	int preset = 0;
	bool voxel = true;
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
		if (down & KEY_Y) { preset = (preset + 1) % 3; cam.pitch = presets[preset].pitch; cam.dist = presets[preset].dist; cam.yaw = 0; }
		if (held & KEY_L) cam.yaw += 0.03f;
		if (held & KEY_R) cam.yaw -= 0.03f;
		if (held & KEY_ZL) cam.dist = fminf(420, cam.dist + 3);
		if (held & KEY_ZR) cam.dist = fmaxf(90, cam.dist - 3);
		circlePosition cs; hidCstickRead(&cs);
		cam.yaw -= cs.dx * 0.0003f;
		cam.pitch = fminf(1.45f, fmaxf(0.3f, cam.pitch + cs.dy * 0.0002f));
		if (cam.yaw > 1.2f) cam.yaw = 1.2f;
		if (cam.yaw < -1.2f) cam.yaw = -1.2f;

		core_frame(b);

		bool overworld = gv8(&g, wMapStatus) == MAPSTATUS_HANDLE && gv8(&g, wBattleMode) == 0 && gv8(&g, wMapGroup) != 0;
		if (overworld) {
			if (world_key(&g) != mesh->key) {
				world_build(&g, mesh);
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
			cam.x = approach(cam.x, gv8(&g, wXCoord) * 16.0f);
			cam.z = approach(cam.z, gv8(&g, wYCoord) * 16.0f);
		}
		int cap, nspr = 0;
		WVert *sbuf = render_sprite_buf(&cap);
		if (overworld) nspr = world_sprites(&g, cam.x, cam.z, sbuf, cap);

		render_lcd(core_lcd());
		render_frame(voxel && overworld && mesh->count > 0, &cam, nspr, sky_by_time[gv8(&g, wTimeOfDayPal) & 3]);

		/* save once the game's save data has changed and then stayed still for 2 seconds */
		if (++frame % 120 == 0) {
			uint32_t h = core_sram_hash();
			if (h != saved_hash && h == last_hash && save_sram()) saved_hash = h;
			last_hash = h;
		}
	}

	if (core_sram_hash() != saved_hash) save_sram();
	render_exit();
	gfxExit();
	free(rom);
	return 0;
}
