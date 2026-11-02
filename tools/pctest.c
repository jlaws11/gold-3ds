/* PC check for world.c + light.c: builds the same mesh the 3DS draws, from a RAM dump.
 * Usage: pctest rom.gbc dump.bin out.bin [hour minute]
 * dump.bin = bytes of 0x8000-0xFFFF. The optional time overrides the game clock.
 * out.bin = header, mesh verts, sprite verts, shadow verts, light, 2 atlases. */
#include <stdio.h>
#include <stdlib.h>
#include "../source/world.h"

static uint8_t *dump;
static uint8_t peek(void *ctx, uint16_t a) { (void)ctx; return a >= 0x8000 ? dump[a - 0x8000] : 0; }

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
	int ns = world_sprites(&g, cx, cz, spr, 40 * 6);
	int nsh = world_sprite_shadows(&g, cx, cz, shd, 40 * 6);
	static uint32_t bg[128 * 128], obj[128 * 128];
	world_bg_atlas(&g, bg); world_obj_atlas(&g, obj);
	fprintf(stderr, "%02d:%02d ok=%d verts=%d buildings=%d sprites=%d shadows=%d sun=(%.2f %.2f %.2f) shadow=%.2f\n",
	        hour, minute, ok, m.count, m.buildings, ns, nsh, L.sun[0], L.sun[1], L.sun[2], L.shadow);
	FILE *o = fopen(argv[3], "wb");
	int32_t hdr[6] = { m.count, ns, nsh, (int32_t)cx, (int32_t)cz, (int32_t)L.sky };
	fwrite(hdr, 4, 6, o);
	float lu[6] = { L.ambient[0], L.ambient[1], L.ambient[2], L.glow[0], L.glow[1], L.glow[2] };
	fwrite(lu, 4, 6, o);
	fwrite(m.verts, sizeof(WVert), m.count, o);
	fwrite(spr, sizeof(WVert), ns, o);
	fwrite(shd, sizeof(WVert), nsh, o);
	fwrite(bg, 4, 128 * 128, o); fwrite(obj, 4, 128 * 128, o);
	fclose(o);
	return 0;
}
