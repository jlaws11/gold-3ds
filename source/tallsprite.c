#include "tallsprite.h"
#include <string.h>

/* Addresses from pret/pokefirered's symbol files (gObjectEventPal_Player,
 * gObjectEventPic_RedNormal), as offsets into the ROM file. */
static const struct { char code[5]; uint8_t rev; TallSource src; } sources[] = {
	{ "BPRE", 0, { 0x35B968, 0x35BB68, "FireRed" } },
	{ "BPRE", 1, { 0x35B9D8, 0x35BBD8, "FireRed (v1.1)" } },
	{ "BPGE", 0, { 0x35B948, 0x35BB48, "LeafGreen" } },
	{ "BPGE", 1, { 0x35B9B8, 0x35BBB8, "LeafGreen (v1.1)" } },
};

const TallSource *tall_source(const uint8_t *h, size_t n) {
	if (n < 0xC0) return NULL;
	for (unsigned i = 0; i < sizeof sources / sizeof sources[0]; i++)
		if (memcmp(h + 0xAC, sources[i].code, 4) == 0 && h[0xBC] == sources[i].rev)
			return &sources[i].src;
	return NULL;
}

void tall_decode(const uint8_t *pal, const uint8_t *pic, uint32_t *rgba) {
	uint32_t c[16];
	for (int i = 0; i < 16; i++) {
		uint16_t v = pal[i * 2] | (pal[i * 2 + 1] << 8);
		int r = v & 31, g = (v >> 5) & 31, b = (v >> 10) & 31;
		c[i] = ((uint32_t)(r * 255 / 31) << 24) | ((uint32_t)(g * 255 / 31) << 16) | ((uint32_t)(b * 255 / 31) << 8) | 0xFF;
	}
	c[0] = 0;   /* colour 0 is transparent */
	memset(rgba, 0, TALL_TEX_W * TALL_TEX_H * 4);
	for (int f = 0; f < 9; f++)            /* 9 frames, each 2x4 tiles of 8x8, 4 bits per pixel */
		for (int t = 0; t < 8; t++) {
			const uint8_t *tile = pic + f * 256 + t * 32;
			int ox = f * 16 + (t % 2) * 8, oy = (t / 2) * 8;
			for (int y = 0; y < 8; y++)
				for (int x = 0; x < 8; x++) {
					uint8_t b = tile[y * 4 + x / 2];
					rgba[(oy + y) * TALL_TEX_W + ox + x] = c[(x & 1) ? b >> 4 : b & 15];
				}
		}
}

int tall_frame(uint8_t direction, int phase, bool walking, bool *flip) {
	int base, walk;
	*flip = false;
	switch (direction & 0x0C) {
	case 0x04: base = 1; walk = 5; break;                  /* north */
	case 0x08: base = 2; walk = 7; break;                  /* west */
	case 0x0C: base = 2; walk = 7; *flip = true; break;    /* east */
	default:   base = 0; walk = 3; break;                  /* south */
	}
	/* like FireRed's walk cycle: step, stand, other step, stand (8 px each) */
	if (!walking || (phase & 1)) return base;
	return walk + (phase == 2 ? 1 : 0);
}
