/* PC check for world.c + light.c: builds the same mesh the 3DS draws, from a RAM dump.
 * Usage: pctest rom.gbc dump.bin out.bin [hour minute [firered.gba [enemy ally]]]
 *   with firered.gba alone: the player is drawn as Red, like on the 3DS
 *   with firered.gba: fakes a wild battle against `enemy` with `ally` out, as the 3DS draws it
 * dump.bin = bytes of 0x8000-0xFFFF. The optional time overrides the game clock.
 * out.bin = header, mesh verts, sprite verts, shadow verts, light, 2 atlases. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "../source/world.h"
#include "../source/battle.h"
#include "../source/frpics.h"
#include "../source/tallsprite.h"

static uint8_t *dump;
static uint8_t peek(void *ctx, uint16_t a) { (void)ctx; return a >= 0x8000 ? dump[a - 0x8000] : 0; }

static FILE *fr;
static bool fr_read(void *c, uint32_t off, void *buf, size_t n) {
	(void)c; return fseek(fr, off, SEEK_SET) == 0 && fread(buf, 1, n, fr) == n;
}

static uint8_t *slurp(const char *p, long *n) {
	FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(1); }
	fseek(f, 0, SEEK_END); *n = ftell(f); rewind(f);
	uint8_t *b = malloc(*n);
	if (fread(b, 1, *n, f) != (size_t)*n) { perror(p); exit(1); }
	fclose(f); return b;
}

int main(int argc, char **argv) {
	if (argc < 4) { fprintf(stderr, "usage: pctest rom dump out [hour minute]\n"); return 2; }
	long rn, dn;
	uint8_t *rom = slurp(argv[1], &rn); dump = slurp(argv[2], &dn);
	const char *why = gold_rom_check(rom, rn);
	if (why) { fprintf(stderr, "%s\n", why); return 1; }
	GoldView g = { rom, (size_t)rn, peek, NULL };
	int hour = argc > 5 ? atoi(argv[4]) : gv8(&g, hHours), minute = argc > 5 ? atoi(argv[5]) : gv8(&g, hMinutes);
	if (argc > 5)   /* match Gold's own palette switch: morn 4-10, day 10-18, nite otherwise */
		dump[wTimeOfDayPal - 0x8000] = hour < 4 ? 2 : hour < 10 ? 0 : hour < 18 ? 1 : 2;
	Light L;
	light_compute(hour, minute, light_env(gv8(&g, wEnvironment)), &L);
	static WorldMesh m;
	m.cap = 400000; m.verts = malloc(sizeof(WVert) * m.cap);
	bool ok = world_build(&g, &m, &L);
	float cx = gv8(&g, wXCoord) * 16.0f, cz = gv8(&g, wYCoord) * 16.0f;
	static WVert spr[40 * 6], shd[40 * 6];
	int ns = world_sprites(&g, cx, cz, false, spr, 40 * 6);
	int nsh = world_sprite_shadows(&g, cx, cz, shd, 40 * 6);
	static uint32_t bg[BG_ATLAS_W * BG_ATLAS_H], obj[128 * 128];
	world_bg_atlas(&g, bg); world_obj_atlas(&g, obj);
	fprintf(stderr, "%02d:%02d ok=%d verts=%d buildings=%d sprites=%d shadows=%d sun=(%.2f %.2f %.2f) shadow=%.2f\n",
	        hour, minute, ok, m.count, m.buildings, ns, nsh, L.sun[0], L.sun[1], L.sun[2], L.shadow);
	/* optional fake battle */
	float cam[5] = { cx + 8, cz + 8, 0, 0.72f, 215 };
	if (getenv("CAMDIST")) cam[4] = atof(getenv("CAMDIST"));     /* overview shots of whole maps */
	if (getenv("CAMPITCH")) cam[3] = atof(getenv("CAMPITCH"));
	static WVert bq[12], bs[12];
	static uint32_t btex[BATTLE_TEX_W * BATTLE_TEX_H];
	int nbq = 0, nbs = 0;
	if (argc == 7) {   /* overworld with the tall player: Red's south frame in the battle texture's right half */
		fr = fopen(argv[6], "rb");
		uint8_t h[0xC0], pal[TALL_PAL_SIZE], pic[TALL_PIC_SIZE];
		static uint32_t sheet[TALL_TEX_W * TALL_TEX_H];
		const TallSource *src = fr && fread(h, 1, sizeof h, fr) == sizeof h ? tall_source(h, sizeof h) : NULL;
		if (!src || !fr_read(NULL, src->pal, pal, sizeof pal) || !fr_read(NULL, src->pic, pic, sizeof pic)) { fprintf(stderr, "no FireRed\n"); return 1; }
		tall_decode(pal, pic, sheet);
		for (int y = 0; y < 32; y++) memcpy(btex + y * BATTLE_TEX_W + 64, sheet + y * TALL_TEX_W, 16 * 4);
		ns = world_sprites(&g, cx, cz, true, spr, 40 * 6);
		nbq = world_tall_player(cx, cz, 0, false, bq);
		for (int i = 0; i < nbq; i++) { bq[i].u = bq[i].u < 0.03f ? 64 / 128.0f : 80 / 128.0f; bq[i].v *= 0.5f; }
	}
	if (argc > 8) {
		fr = fopen(argv[6], "rb");
		uint8_t h[0xC0];
		if (!fr || fread(h, 1, sizeof h, fr) != sizeof h) { perror(argv[6]); return 1; }
		const FrTables *t = fr_tables(h, sizeof h);
		if (!t) { fprintf(stderr, "not FireRed/LeafGreen\n"); return 1; }
		int en = atoi(argv[7]), al = atoi(argv[8]);
		dump[wBattleMode - 0x8000] = 1;
		dump[wEnemyMonSpecies - 0x8000] = en; dump[wEnemyMonHP - 0x8000 + 1] = 20;
		dump[wBattleMonSpecies - 0x8000] = 0;
		Battle b; battle_begin(&b, &g, cx, cz);
		for (int i = 0; i < 20; i++) battle_update(&b, &g);
		dump[wBattleMonSpecies - 0x8000] = al; dump[wBattleMonHP - 0x8000 + 1] = 20;
		for (int i = 0; i < 20; i++) battle_update(&b, &g);
		static uint32_t pic[FR_PIC * FR_PIC];
		BattlePic pe = battle_enemy_pic(&b), pa = battle_ally_pic(&b);
		if (pe.species && fr_mon_pic(fr_read, NULL, t, pe.species, false, pe.shiny, pic, &b.enemy_bottom))
			for (int y = 0; y < 64; y++) memcpy(btex + y * BATTLE_TEX_W, pic + y * 64, 64 * 4);
		bool ok2 = pa.red ? fr_red_back(fr_read, NULL, t, pic, &b.ally_bottom)
		                  : fr_mon_pic(fr_read, NULL, t, pa.species, true, pa.shiny, pic, &b.ally_bottom);
		if (ok2) for (int y = 0; y < 64; y++) memcpy(btex + y * BATTLE_TEX_W + 64, pic + y * 64, 64 * 4);
		battle_camera(&b, &cam[0], &cam[1], &cam[2], &cam[3], &cam[4]);
		nbq = battle_quads(&b, cam[2], bq, 12);
		nbs = battle_shadows(&b, bs, 12);
		ns = 0; nsh = 0;   /* overworld sprites are hidden during battles */
		fprintf(stderr, "battle: %s, enemy %d ally %d, quads %d\n", t->name, b.enemy, b.ally, nbq);
	}

	FILE *o = fopen(argv[3], "wb");
	int32_t hdr[6] = { m.count, ns, nsh, (int32_t)cx, (int32_t)cz, (int32_t)L.sky };
	fwrite(hdr, 4, 6, o);
	float lu[6] = { L.ambient[0], L.ambient[1], L.ambient[2], L.glow[0], L.glow[1], L.glow[2] };
	fwrite(lu, 4, 6, o);
	fwrite(m.verts, sizeof(WVert), m.count, o);
	fwrite(spr, sizeof(WVert), ns, o);
	fwrite(shd, sizeof(WVert), nsh, o);
	fwrite(bg, 4, BG_ATLAS_W * BG_ATLAS_H, o); fwrite(obj, 4, 128 * 128, o);
	fwrite(cam, 4, 5, o);
	int32_t bn[2] = { nbq, nbs };
	fwrite(bn, 4, 2, o);
	fwrite(bq, sizeof(WVert), nbq, o); fwrite(bs, sizeof(WVert), nbs, o);
	fwrite(btex, 4, BATTLE_TEX_W * BATTLE_TEX_H, o);
	fclose(o);
	return 0;
}
