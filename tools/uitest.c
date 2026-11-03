/* PC check for frui.c: draws the FireRed-style interface for a RAM dump into a
 * 400x240 RGBA image, exactly as the 3DS lays it out.
 * Usage: uitest firered.gba dump.bin field(0/1) battle(0/1) out.rgba  (prints the result) */
#include <stdio.h>
#include <stdlib.h>
#include "../source/frui.h"

static uint8_t dump[0x8000];
static uint8_t peek(void *c, uint16_t a) { (void)c; return a >= 0x8000 ? dump[a - 0x8000] : 0; }
static FILE *fr;
static bool rd(void *c, uint32_t off, void *buf, size_t n) { (void)c; return fseek(fr, off, SEEK_SET) == 0 && fread(buf, 1, n, fr) == n; }

int main(int argc, char **argv) {
	if (argc < 6) return 2;
	fr = fopen(argv[1], "rb");
	FILE *d = fopen(argv[2], "rb");
	if (!fr || !d || fread(dump, 1, sizeof dump, d) != sizeof dump) { perror("open"); return 1; }
	uint8_t h[0xC0];
	if (fread(h, 1, sizeof h, fr) != sizeof h) return 1;
	static uint32_t atlas[FRUI_TEX * FRUI_TEX];
	if (!frui_load(rd, NULL, fr_tables(h, sizeof h), atlas)) { fprintf(stderr, "load failed\n"); return 1; }
	GoldView g = { NULL, 0, peek, NULL };
	static UiList l;
	FruiResult r = frui_plan(&g, atoi(argv[3]), atoi(argv[4]), &l);
	printf("%s quads=%d\n", r == FRUI_DRAWN ? "drawn" : r == FRUI_FALLBACK ? "fallback" : "none", l.n);
	static uint32_t img[400 * 240];
	for (int i = 0; i < l.n; i++) {
		UiQuad *q = &l.q[i];
		for (int y = 0; y < (int)q->h; y++) for (int x = 0; x < (int)q->w; x++) {
			int X = (int)q->x + x, Y = (int)q->y + y;
			if (X < 0 || Y < 0 || X >= 400 || Y >= 240) continue;
			int u = (int)(q->u0 + (x + 0.5f) / q->w * (q->u1 - q->u0));
			int v = (int)(q->v0 + (y + 0.5f) / q->h * (q->v1 - q->v0));
			uint32_t p = atlas[v * FRUI_TEX + u];
			if ((p & 0xFF) > 0x80) img[Y * 400 + X] = p;
		}
	}
	FILE *o = fopen(argv[5], "wb");
	fwrite(img, 4, 400 * 240, o); fclose(o);
	if (argc > 6) { FILE *a = fopen(argv[6], "wb"); fwrite(atlas, 4, FRUI_TEX * FRUI_TEX, a); fclose(a); }
	return 0;
}
