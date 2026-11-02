/* Optional taller player: Red's overworld sprite, read from the player's own
 * FireRed or LeafGreen ROM (sdmc:/3ds/gold3ds/firered.gba). Nothing from that
 * game ships with Gold 3DS; without the file the normal Gold sprite is used.
 *
 * The sheet has 9 frames of 16x32: 0 south, 1 north, 2 west,
 * 3/4 south walk, 5/6 north walk, 7/8 west walk (east = west mirrored). */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define TALL_PAL_SIZE 0x20
#define TALL_PIC_SIZE 0x900
#define TALL_TEX_W 256
#define TALL_TEX_H 32

typedef struct { uint32_t pal, pic; const char *name; } TallSource;

/* From the first 0xC0 bytes of a GBA ROM: where Red's palette and frames are,
 * or NULL if this isn't a supported FireRed/LeafGreen. */
const TallSource *tall_source(const uint8_t *header, size_t n);

/* Decode into a TALL_TEX_W x TALL_TEX_H RGBA image (frames side by side, top row first). */
void tall_decode(const uint8_t *pal, const uint8_t *pic, uint32_t *rgba);

/* Frame for a Gold facing (wPlayerDirection: 0 down, 4 up, 8 left, 12 right) and a
 * walk phase 0..3 (step, stand, other step, stand). Sets *flip for east. */
int tall_frame(uint8_t direction, int walk_phase, bool walking, bool *flip);
