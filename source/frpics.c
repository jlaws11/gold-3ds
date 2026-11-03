#include "frpics.h"
#include <string.h>

/* From pret/pokefirered's symbol files (gMonFrontPicTable, gMonBackPicTable,
 * gMonPaletteTable, gMonShinyPaletteTable, gTrainerBackPicTable,
 * gTrainerBackPicPaletteTable), as file offsets. */
static const struct { char code[5]; uint8_t rev; FrTables t; } versions[] = {
	/* name, front, back, pal, shiny, trainer back, its pal,
	 * font, widths, small font, widths, message window, window palettes, user frame 1, its pal
	 * (sFontNormalLatinGlyphs, sFontSmallLatinGlyphs, gMenuMessageWindow_Gfx,
	 *  gTextWindowPalettes, sUserFrame_Type1_Gfx/_Pal, sDownArrowTiles,
 *  gMonIconTable, gMonIconPaletteIndices, gMonIconPaletteTable) */
	{ "BPRE", 0, { "FireRed",          0x2350AC, 0x23654C, 0x23730C, 0x2380CC, 0x239FA4, 0x239FD4,
	                0x1FF300, 0x207300, 0x1EAF00, 0x1EEF00, 0x41F1C8, 0x471DEC, 0x470D6C, 0x47190C, 0x1EA14C, 0x3D37A0, 0x3D3E80, 0x3D4038 } },
	{ "BPRE", 1, { "FireRed (v1.1)",   0x23511C, 0x2365BC, 0x23737C, 0x23813C, 0x23A014, 0x23A044,
	                0x1FF370, 0x207370, 0x1EAF70, 0x1EEF70, 0x41F238, 0x471E4C, 0x470DCC, 0x47196C, 0x1EA1BC, 0x3D3810, 0x3D3EF0, 0x3D40A8 } },
	{ "BPGE", 0, { "LeafGreen",        0x235088, 0x236528, 0x2372E8, 0x2380A8, 0x239F80, 0x239FB0,
	                0x1FF2DC, 0x2072DC, 0x1EAEDC, 0x1EEEDC, 0x41F004, 0x4716C8, 0x470648, 0x4711E8, 0x1EA128, 0x3D35DC, 0x3D3CBC, 0x3D3E74 } },
	{ "BPGE", 1, { "LeafGreen (v1.1)", 0x2350F8, 0x236598, 0x237358, 0x238118, 0x239FF0, 0x23A020,
	                0x1FF34C, 0x20734C, 0x1EAF4C, 0x1EEF4C, 0x41F074, 0x471738, 0x4706B8, 0x471258, 0x1EA198, 0x3D364C, 0x3D3D2C, 0x3D3EE4 } },
};

const FrTables *fr_tables(const uint8_t *h, size_t n) {
	if (n < 0xC0) return NULL;
	for (unsigned i = 0; i < sizeof versions / sizeof versions[0]; i++)
		if (memcmp(h + 0xAC, versions[i].code, 4) == 0 && h[0xBC] == versions[i].rev)
			return &versions[i].t;
	return NULL;
}

size_t fr_lz77(const uint8_t *s, size_t sn, uint8_t *d, size_t cap) {
	if (sn < 4 || s[0] != 0x10) return 0;
	size_t size = s[1] | (s[2] << 8) | (s[3] << 16), si = 4, di = 0;
	if (size > cap) return 0;
	while (di < size) {
		if (si >= sn) return 0;
		uint8_t flags = s[si++];
		for (int b = 0; b < 8 && di < size; b++, flags <<= 1) {
			if (flags & 0x80) {
				if (si + 1 >= sn) return 0;
				int len = (s[si] >> 4) + 3, disp = (((s[si] & 15) << 8) | s[si + 1]) + 1;
				si += 2;
				if ((size_t)disp > di) return 0;
				for (int k = 0; k < len && di < size; k++, di++) d[di] = d[di - disp];
			} else {
				if (si >= sn) return 0;
				d[di++] = s[si++];
			}
		}
	}
	return size;
}

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

/* table entry i -> file offset of its data */
static bool entry(FrRead rd, void *ctx, uint32_t table, int i, uint32_t *off) {
	uint8_t e[8];
	if (!rd(ctx, table + i * 8, e, 8)) return false;
	uint32_t p = rd32(e);
	if (p < 0x08000000 || p >= 0x0A000000) return false;
	*off = p - 0x08000000;
	return true;
}

size_t fr_read_lz(FrRead rd, void *ctx, uint32_t off, uint8_t *dst, size_t cap) {
	static uint8_t buf[0x2000];
	size_t n = sizeof buf;
	while (n > 64 && !rd(ctx, off, buf, n)) n /= 2;   /* near the end of the ROM */
	return fr_lz77(buf, n, dst, cap);
}

static bool read_lz(FrRead rd, void *ctx, uint32_t off, uint8_t *out, size_t want) {
	return fr_read_lz(rd, ctx, off, out, want) == want;
}

static void palette(const uint8_t *p, uint32_t c[16]) {
	for (int i = 0; i < 16; i++) {
		uint16_t v = p[i * 2] | (p[i * 2 + 1] << 8);
		int r = v & 31, g = (v >> 5) & 31, b = (v >> 10) & 31;
		c[i] = ((uint32_t)(r * 255 / 31) << 24) | ((uint32_t)(g * 255 / 31) << 16) | ((uint32_t)(b * 255 / 31) << 8) | 0xFF;
	}
	c[0] = 0;
}

static void tiles_4bpp(const uint8_t *pic, const uint32_t c[16], uint32_t *rgba, int *bottom) {
	*bottom = 0;
	for (int t = 0; t < 64; t++) {             /* 8x8 tiles, left to right, top to bottom */
		const uint8_t *tile = pic + t * 32;
		int ox = (t % 8) * 8, oy = (t / 8) * 8;
		for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
			uint8_t b = tile[y * 4 + x / 2], i = (x & 1) ? b >> 4 : b & 15;
			rgba[(oy + y) * FR_PIC + ox + x] = c[i];
			if (i && oy + y > *bottom) *bottom = oy + y;
		}
	}
}

bool fr_mon_pic(FrRead rd, void *ctx, const FrTables *t, int species, bool back, bool shiny, uint32_t *rgba, int *bottom) {
	if (species < 1 || species > 251) return false;
	uint32_t po, ko;
	static uint8_t pic[2048], pal[32];
	if (!entry(rd, ctx, back ? t->back : t->front, species, &po)) return false;
	if (!entry(rd, ctx, shiny ? t->shiny_pal : t->pal, species, &ko)) return false;
	if (!read_lz(rd, ctx, po, pic, sizeof pic) || !read_lz(rd, ctx, ko, pal, sizeof pal)) return false;
	uint32_t c[16]; palette(pal, c);
	tiles_4bpp(pic, c, rgba, bottom);
	return true;
}

bool fr_red_back(FrRead rd, void *ctx, const FrTables *t, uint32_t *rgba, int *bottom) {
	uint32_t po, ko;
	static uint8_t pic[2048], pal[32];
	if (!entry(rd, ctx, t->trainer_back, 0, &po) || !entry(rd, ctx, t->trainer_back_pal, 0, &ko)) return false;
	if (!rd(ctx, po, pic, sizeof pic)) return false;      /* first frame, stored uncompressed */
	if (!read_lz(rd, ctx, ko, pal, sizeof pal)) return false;
	uint32_t c[16]; palette(pal, c);
	tiles_4bpp(pic, c, rgba, bottom);
	return true;
}

bool fr_mon_icon(FrRead rd, void *ctx, const FrTables *t, int species, int frame, uint32_t *rgba) {
	if (species < 1 || species > 251) return false;
	uint8_t e[4], idx, pe[8], p[32];
	static uint8_t pic[512];
	if (!rd(ctx, t->icons + species * 4, e, 4)) return false;
	uint32_t po = rd32(e);
	if (po < 0x08000000 || po >= 0x0A000000) return false;
	if (!rd(ctx, po - 0x08000000 + (frame & 1) * 512, pic, 512)) return false;
	if (!rd(ctx, t->icon_pal_idx + species, &idx, 1) || idx > 5) return false;
	if (!rd(ctx, t->icon_pal_table + idx * 8, pe, 8)) return false;
	uint32_t pp = rd32(pe);
	if (pp < 0x08000000 || pp >= 0x0A000000 || !rd(ctx, pp - 0x08000000, p, 32)) return false;
	uint32_t c[16]; palette(p, c);
	for (int tl = 0; tl < 16; tl++)                 /* 32x32 sprite: 4x4 tiles, row by row */
		for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
			uint8_t b = pic[tl * 32 + y * 4 + x / 2], i = (x & 1) ? b >> 4 : b & 15;
			rgba[((tl / 4) * 8 + y) * 32 + (tl % 4) * 8 + x] = c[i];
		}
	return true;
}
