/* Builds the voxel overworld from Gold's live RAM + ROM.
 * Platform independent: produces triangle lists and RGBA tile atlases that the
 * 3DS renderer (or a PC test tool) uploads. World units are Game Boy pixels:
 * x = east, y = up, z = south; one map square is 16 x 16. */
#pragma once
#include "gold.h"

typedef struct { float x, y, z, u, v, shade; } WVert;

#define WORLD_MARGIN 6                 /* squares drawn beyond the map edge (border blocks) */
#define WORLD_MAX_SQ_W (64 + 2 * WORLD_MARGIN)
#define WORLD_MAX_SQ_H (64 + 2 * WORLD_MARGIN)

typedef struct {
	WVert *verts;          /* triangle list, caller-provided buffer */
	int cap, count;
	int sq_w, sq_h;        /* squares incl. margin */
	int origin;            /* = WORLD_MARGIN: map square (0,0) sits at grid (origin,origin) */
	/* start vertex of each grid square, row-major; start[sq_w*sq_h] = count.
	 * Squares in one row are contiguous, so a row segment is one draw range. */
	int start[WORLD_MAX_SQ_W * WORLD_MAX_SQ_H + 1];
	uint32_t key;          /* hash of what the mesh was built from */
	int buildings;
} WorldMesh;

/* Hash of map id, layout, tileset: rebuild the mesh when this changes. */
uint32_t world_key(const GoldView *g);

/* Rebuild mesh. Returns false if the buffer was too small (mesh truncated). */
bool world_build(const GoldView *g, WorldMesh *m);

/* Vertex range for grid squares [x0,x1] in row y (clamped). */
void world_row_range(const WorldMesh *m, int y, int x0, int x1, int *first, int *count);

/* 128x128 RGBA atlas of the 256 BG tile numbers, coloured with Gold's own GBC
 * palettes for the current time of day. Row-major, top row first. */
void world_bg_atlas(const GoldView *g, uint32_t *rgba);

/* 128x128 RGBA atlas of the 256 sprite tiles (0x8000-0x8FFF), colour 0 transparent. */
void world_obj_atlas(const GoldView *g, uint32_t *rgba);

/* Upright sprite quads for the player and NPCs, from OAM. cam_x/cam_z is where the
 * player is drawn (world pixels, top-left of the player's square). */
int world_sprites(const GoldView *g, float cam_x, float cam_z, WVert *out, int cap);

/* UV of a tile in either atlas (v grows downward, 0 = top row of atlas). */
static inline void atlas_uv(int tile, float *u0, float *v0, float *u1, float *v1) {
	const float e = 0.02f / 128.0f;   /* tiny inset against bleeding */
	*u0 = (tile % 16) * 8 / 128.0f + e; *u1 = *u0 + 8 / 128.0f - 2 * e;
	*v0 = (tile / 16) * 8 / 128.0f + e; *v1 = *v0 + 8 / 128.0f - 2 * e;
}

#define RGBA(r, g, b, a) (((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(a))
