/* Pokémon battle pictures read from the player's own FireRed/LeafGreen ROM.
 * FireRed holds front and back pictures (64x64) with normal and shiny palettes
 * for every Pokémon in Gold; its species numbers 1-251 match Gold's. Nothing
 * from either game ships with Gold 3DS. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define FR_PIC 64   /* pictures are FR_PIC x FR_PIC */

typedef struct {
	const char *name;
	uint32_t front, back, pal, shiny_pal;   /* tables of 8-byte entries, file offsets */
	uint32_t trainer_back, trainer_back_pal;
	/* interface graphics (see frui.c) */
	uint32_t font, font_widths, font_small, font_small_widths;
	uint32_t msg_window, window_pals, user_frame, user_frame_pal;
	uint32_t down_arrow;   /* sDownArrowTiles: the "more text" arrow */
	uint32_t icons, icon_pal_idx, icon_pal_table;   /* gMonIconTable, gMonIconPaletteIndices, gMonIconPaletteTable */
} FrTables;

/* Battle interface graphics: the same file offsets in all four versions */
#define FR_BTEXTBOX_GFX   0xD00000
#define FR_BTEXTBOX_PAL   0xD004D8
#define FR_BTEXTBOX_MAP   0xD0051C
#define FR_HEALTHBOX_PAL  0xD11B84
#define FR_HEALTHBAR_PAL  0xD11BA4
#define FR_BATTLE_ELEMS   0xD11BC4
#define FR_HB_PLAYER_GFX  0xD1F340
#define FR_HB_OPP_GFX     0xD1F604

/* Reads n bytes at file offset off; false if out of range. */
typedef bool (*FrRead)(void *ctx, uint32_t off, void *buf, size_t n);

/* Reads an LZ77-compressed block at a file offset into dst; returns its size or 0. */
size_t fr_read_lz(FrRead rd, void *ctx, uint32_t off, uint8_t *dst, size_t cap);

const FrTables *fr_tables(const uint8_t *header, size_t n);

/* Decodes a picture into FR_PIC*FR_PIC RGBA (top row first, colour 0 transparent).
 * *bottom gets the lowest non-empty row, so feet can be put on the ground. */
bool fr_mon_pic(FrRead rd, void *ctx, const FrTables *t, int species, bool back, bool shiny,
                uint32_t *rgba, int *bottom);
bool fr_red_back(FrRead rd, void *ctx, const FrTables *t, uint32_t *rgba, int *bottom);

/* A party-menu icon, 32x32 RGBA (frame 0 or 1 of its little animation). */
bool fr_mon_icon(FrRead rd, void *ctx, const FrTables *t, int species, int frame, uint32_t *rgba);

/* GBA BIOS LZ77 (type 0x10). Returns decompressed size, 0 on error. */
size_t fr_lz77(const uint8_t *src, size_t src_n, uint8_t *dst, size_t dst_cap);
