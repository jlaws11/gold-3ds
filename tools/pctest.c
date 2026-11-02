/* PC check for world.c: builds the same mesh the 3DS draws, from a RAM dump.
 * Usage: pctest rom.gbc dump.bin out.bin
 * dump.bin = bytes of 0x8000-0xFFFF. out.bin = header + verts + sprites + 2 atlases. */
#include <stdio.h>
#include <stdlib.h>
#include "../source/world.h"

static uint8_t *dump;
static uint8_t peek(void *ctx, uint16_t a) { (void)ctx; return a >= 0x8000 ? dump[a - 0x8000] : 0; }

static uint8_t *slurp(const char *p, long *n) {
	FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(1); }
	fseek(f, 0, SEEK_END); *n = ftell(f); rewind(f);
	uint8_t *b = malloc(*n); fread(b, 1, *n, f); fclose(f); return b;
}

int main(int argc, char **argv) {
	long rn, dn;
	uint8_t *rom = slurp(argv[1], &rn); dump = slurp(argv[2], &dn);
	const char *why = gold_rom_check(rom, rn);
	if (why) { fprintf(stderr, "%s\n", why); return 1; }
	GoldView g = { rom, (size_t)rn, peek, NULL };
	static WorldMesh m;
	m.cap = 400000; m.verts = malloc(sizeof(WVert) * m.cap);
	bool ok = world_build(&g, &m);
	float cx = gv8(&g, wXCoord) * 16.0f, cz = gv8(&g, wYCoord) * 16.0f;
	static WVert spr[40 * 6];
	int ns = world_sprites(&g, cx, cz, spr, 40 * 6);
	static uint32_t bg[128 * 128], obj[128 * 128];
	world_bg_atlas(&g, bg); world_obj_atlas(&g, obj);
	fprintf(stderr, "ok=%d verts=%d squares=%dx%d buildings=%d sprite_verts=%d player=(%g,%g)\n",
	        ok, m.count, m.sq_w, m.sq_h, m.buildings, ns, cx, cz);
	FILE *o = fopen(argv[3], "wb");
	int32_t hdr[4] = { m.count, ns, (int32_t)cx, (int32_t)cz };
	fwrite(hdr, 4, 4, o);
	fwrite(m.verts, sizeof(WVert), m.count, o);
	fwrite(spr, sizeof(WVert), ns, o);
	fwrite(bg, 4, 128 * 128, o); fwrite(obj, 4, 128 * 128, o);
	fclose(o);
	return 0;
}
