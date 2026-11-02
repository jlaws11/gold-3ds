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
} FrTables;

/* Reads n bytes at file offset off; false if out of range. */
typedef bool (*FrRead)(void *ctx, uint32_t off, void *buf, size_t n);

const FrTables *fr_tables(const uint8_t *header, size_t n);

/* Decodes a picture into FR_PIC*FR_PIC RGBA (top row first, colour 0 transparent).
 * *bottom gets the lowest non-empty row, so feet can be put on the ground. */
bool fr_mon_pic(FrRead rd, void *ctx, const FrTables *t, int species, bool back, bool shiny,
                uint32_t *rgba, int *bottom);
bool fr_red_back(FrRead rd, void *ctx, const FrTables *t, uint32_t *rgba, int *bottom);

/* GBA BIOS LZ77 (type 0x10). Returns decompressed size, 0 on error. */
size_t fr_lz77(const uint8_t *src, size_t src_n, uint8_t *dst, size_t dst_cap);
