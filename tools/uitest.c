/* PC check for frui.c: draws the FireRed-style interface for a RAM dump into a
 * 400x240 RGBA image, exactly as the 3DS lays it out.
 * Usage: uitest firered.gba dump.bin field(0/1) battle(0/1) out.rgba [gold-screen.ppm] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
	const FrTables *t = fr_tables(h, sizeof h);
	static uint32_t atlas[FRUI_TEX * FRUI_TEX], lcd[256 * 256], pic[64 * 64], icons[256 * 32];
	if (!frui_load(rd, NULL, t, atlas)) { fprintf(stderr, "load failed\n"); return 1; }
	if (argc > 6) {   /* Gold's picture, with the app's cream-to-ink colours */
		FILE *p = fopen(argv[6], "rb"); char hdr[32]; int w, hh, mx;
		if (p && fscanf(p, "%2s %d %d %d", hdr, &w, &hh, &mx) == 4) {
			fgetc(p);
			static const uint32_t ramp[4] = { 0xF0EED600, 0xA8BA8CFF, 0x54685CFF, 0x1C1E28FF };
			for (int y = 0; y < 144; y++) for (int x = 0; x < 160; x++) {
				int r = fgetc(p); fgetc(p); fgetc(p);
				lcd[y * 256 + x] = ramp[r == 0xE0 ? 0 : r == 0xA8 ? 1 : r == 0x60 ? 2 : 3];
			}
		}
	}
	GoldView g = { NULL, 0, peek, NULL };
	static UiList l;
	FruiResult r = frui_plan(&g, atoi(argv[3]), atoi(argv[4]), &l);
	int bottom;
	if (l.pic_species) fr_mon_pic(rd, NULL, t, l.pic_species, false, l.pic_shiny, pic, &bottom);
	for (int i = 0; i < 6; i++) if (l.icon_species[i]) {
		static uint32_t ic[32 * 32];
		if (fr_mon_icon(rd, NULL, t, l.icon_species[i], 0, ic))
			for (int y = 0; y < 32; y++) memcpy(icons + y * 256 + i * 32, ic + y * 32, 128);
	}
	printf("%s quads=%d pic=%d icons=%d,%d dim=%d\n", r == FRUI_DRAWN ? "drawn" : r == FRUI_FALLBACK ? "fallback" : "none",
	       l.n, l.pic_species, l.icon_species[0], l.icon_species[1], l.dim);
	static uint32_t img[400 * 240];
	for (int i = 0; i < l.n; i++) {
		UiQuad *q = &l.q[i];
		const uint32_t *src = q->tex == UI_TEX_ATLAS ? atlas : q->tex == UI_TEX_LCD ? lcd : q->tex == UI_TEX_PIC ? pic : icons;
		int sw = q->tex == UI_TEX_ATLAS ? FRUI_TEX : q->tex == UI_TEX_PIC ? 64 : 256;
		for (int y = 0; y < (int)(q->h + 0.5f); y++) for (int x = 0; x < (int)(q->w + 0.5f); x++) {
			int X = (int)(q->x + 0.5f) + x, Y = (int)(q->y + 0.5f) + y;
			if (X < 0 || Y < 0 || X >= 400 || Y >= 240) continue;
			int u = (int)(q->u0 + (x + 0.5f) / q->w * (q->u1 - q->u0));
			int v = (int)(q->v0 + (y + 0.5f) / q->h * (q->v1 - q->v0));
			if (u < 0) u = 0;
			if (v < 0) v = 0;
			if (u >= sw) u = sw - 1;
			uint32_t p = src[v * sw + u];
			if ((p & 0xFF) > 0x80) img[Y * 400 + X] = p;
		}
	}
	FILE *o = fopen(argv[5], "wb");
	fwrite(img, 4, 400 * 240, o); fclose(o);
	return 0;
}
