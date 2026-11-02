#include "world.h"
#include <string.h>

enum { CL_GROUND, CL_GRASS, CL_WATER, CL_TREE, CL_ROOF, CL_SOLID, CL_DOOR, CL_LEDGE };
enum { PAL_GRAY, PAL_RED, PAL_GREEN, PAL_WATER, PAL_YELLOW, PAL_BROWN, PAL_ROOF, PAL_TEXT };

#define H_BUILDING  (-1000)    /* marks squares owned by a building */
#define TREE_H      22
#define SOLID_H     8
#define LEDGE_H     4
#define WATER_H     (-3)
#define MAX_BLD     64

typedef struct {
	const GoldView *g;
	int w, h, stride;
	uint32_t meta, coll, palmap;
	const uint8_t *perm;
} Ctx;

typedef struct { int x0, x1, y0, y1, wy1; } Bld;   /* grid coords: roof rows y0..y1, walls y1+1..wy1 */

static int fdiv2(int v) { return (v + 64) / 2 - 32; }   /* floor(v/2) for v >= -64 */

static uint8_t block_at(Ctx *c, int bx, int by) {
	if (bx < -3) bx = -3; if (by < -3) by = -3;
	if (bx > c->w + 2) bx = c->w + 2; if (by > c->h + 2) by = c->h + 2;
	return gv8(c->g, wOverworldMapBlocks + (by + 3) * c->stride + bx + 3);
}

static void sq_tiles(Ctx *c, int sx, int sy, uint8_t t[4]) {
	int bx = fdiv2(sx), by = fdiv2(sy);
	const uint8_t *mt = c->g->rom + c->meta + block_at(c, bx, by) * 16;
	int qx = (sx - bx * 2) * 2, qy = (sy - by * 2) * 2;
	t[0] = mt[qy * 4 + qx];       t[1] = mt[qy * 4 + qx + 1];
	t[2] = mt[(qy + 1) * 4 + qx]; t[3] = mt[(qy + 1) * 4 + qx + 1];
}

static uint8_t sq_coll(Ctx *c, int sx, int sy) {
	int bx = fdiv2(sx), by = fdiv2(sy);
	const uint8_t *cl = c->g->rom + c->coll + block_at(c, bx, by) * 4;
	return cl[(sy - by * 2) * 2 + (sx - bx * 2)];
}

static int tile_pal(const GoldView *g, uint32_t palmap, int t) {
	if (!palmap || t >= 0x60) return PAL_GRAY;
	uint8_t b = g->rom[palmap + t / 2];
	return ((t & 1) ? b >> 4 : b) & 7;
}

static uint32_t current_palmap(const GoldView *g) {
	uint16_t a = gv16(g, wTilesetPalettes);
	if (a < 0x4000 || a >= 0x8000) return 0;
	uint32_t o = rom_offset(0x02, a);
	return o + 0x80 <= g->rom_size ? o : 0;
}

static bool ctx_init(Ctx *c, const GoldView *g) {
	memset(c, 0, sizeof *c);
	c->g = g;
	c->w = gv8(g, wMapWidth); c->h = gv8(g, wMapHeight);
	if (c->w == 0 || c->h == 0 || c->w > 64 || c->h > 64) return false;
	c->stride = c->w + 6;
	c->meta = rom_offset(gv8(g, wTilesetBlocksBank), gv16(g, wTilesetBlocksAddress));
	c->coll = rom_offset(gv8(g, wTilesetCollisionBank), gv16(g, wTilesetCollisionAddress));
	if (c->meta + 256 * 16 > g->rom_size || c->coll + 256 * 4 > g->rom_size) return false;
	c->perm = g->rom + rom_offset(ROM_COLLISION_PERMS_BANK, ROM_COLLISION_PERMS_ADDR);
	c->palmap = current_palmap(g);
	return true;
}

static int classify(Ctx *c, int sx, int sy) {
	uint8_t t[4]; sq_tiles(c, sx, sy, t);
	int cnt[8] = {0}, dom = 0;
	for (int i = 0; i < 4; i++) cnt[tile_pal(c->g, c->palmap, t[i])]++;
	for (int p = 1; p < 8; p++) if (cnt[p] > cnt[dom]) dom = p;
	uint8_t col = sq_coll(c, sx, sy);
	uint8_t perm = c->perm[col] & 0x0F;
	if (col == 0x71) return CL_DOOR;
	if (perm == 0x01) return CL_WATER;
	if (col == 0x18 || col == 0x14) return CL_GRASS;
	if (col >= 0xA0 && col <= 0xA7) return CL_LEDGE;
	if (perm == 0x00) return CL_GROUND;
	if (dom == PAL_GREEN) return CL_TREE;
	if (dom == PAL_ROOF) return CL_ROOF;
	return CL_SOLID;
}

uint32_t world_key(const GoldView *g) {
	uint32_t h = 2166136261u;
	uint16_t hdr[] = { wMapGroup, wMapNumber, wMapTileset, wMapWidth, wMapHeight, wTimeOfDayPal };
	for (unsigned i = 0; i < sizeof hdr / sizeof hdr[0]; i++) h = (h ^ gv8(g, hdr[i])) * 16777619u;
	int n = (gv8(g, wMapWidth) + 6) * (gv8(g, wMapHeight) + 6);
	if (n > 1300) n = 1300;
	for (int i = 0; i < n; i++) h = (h ^ gv8(g, wOverworldMapBlocks + i)) * 16777619u;
	return h;
}

/* ---- vertex emission ------------------------------------------------- */

typedef struct { float x, y, z; } P3;

static bool room(WorldMesh *m, int n) { return m->count + n <= m->cap; }

static void put(WorldMesh *m, P3 p, float u, float v, float s) {
	WVert *o = &m->verts[m->count++];
	o->x = p.x; o->y = p.y; o->z = p.z; o->u = u; o->v = v; o->shade = s;
}

/* a=top-left b=top-right c=bottom-right d=bottom-left (as the tile is drawn) */
static void quad(WorldMesh *m, P3 a, P3 b, P3 c, P3 d, int tile, float shade) {
	if (!room(m, 6)) return;
	float u0, v0, u1, v1; atlas_uv(tile, &u0, &v0, &u1, &v1);
	put(m, a, u0, v0, shade); put(m, d, u0, v1, shade); put(m, c, u1, v1, shade);
	put(m, a, u0, v0, shade); put(m, c, u1, v1, shade); put(m, b, u1, v0, shade);
}

static void tri(WorldMesh *m, P3 a, P3 b, P3 c, int tile, float shade) {
	if (!room(m, 3)) return;
	float u0, v0, u1, v1; atlas_uv(tile, &u0, &v0, &u1, &v1);
	put(m, a, u0, v1, shade); put(m, b, (u0 + u1) / 2, v0, shade); put(m, c, u1, v1, shade);
}

static P3 p3(float x, float y, float z) { P3 p = { x, y, z }; return p; }

/* top face of a square at height y, 4 tiles */
static void top_face(WorldMesh *m, float X, float Z, float y, const uint8_t t[4], float shade) {
	for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) {
		float x0 = X + i * 8, z0 = Z + j * 8;
		quad(m, p3(x0, y, z0), p3(x0 + 8, y, z0), p3(x0 + 8, y, z0 + 8), p3(x0, y, z0 + 8), t[j * 2 + i], shade);
	}
}

/* ---- the build --------------------------------------------------------- */

bool world_build(const GoldView *g, WorldMesh *m) {
	static uint8_t cls[WORLD_MAX_SQ_H][WORLD_MAX_SQ_W];
	static short hgt[WORLD_MAX_SQ_H][WORLD_MAX_SQ_W];
	static uint8_t tiles[WORLD_MAX_SQ_H][WORLD_MAX_SQ_W][4];
	static short bld_id[WORLD_MAX_SQ_H][WORLD_MAX_SQ_W];
	static Bld blds[MAX_BLD];
	static short stack[WORLD_MAX_SQ_W * WORLD_MAX_SQ_H][2];

	Ctx c;
	m->count = 0; m->buildings = 0; m->key = world_key(g);
	if (!ctx_init(&c, g)) { m->sq_w = m->sq_h = 0; m->start[0] = 0; return true; }
	const int O = WORLD_MARGIN;
	m->origin = O;
	m->sq_w = c.w * 2 + 2 * O; m->sq_h = c.h * 2 + 2 * O;
	const int W = m->sq_w, H = m->sq_h;

	for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
		cls[y][x] = classify(&c, x - O, y - O);
		sq_tiles(&c, x - O, y - O, tiles[y][x]);
		bld_id[y][x] = -1;
		switch (cls[y][x]) {
		case CL_WATER: hgt[y][x] = WATER_H; break;
		case CL_TREE:  hgt[y][x] = TREE_H; break;
		case CL_SOLID: case CL_ROOF: hgt[y][x] = SOLID_H; break;
		case CL_LEDGE: hgt[y][x] = LEDGE_H; break;
		default:       hgt[y][x] = 0; break;
		}
	}

	/* buildings: connected roof squares + the solid/door rows directly below them */
	int nb = 0;
	for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
		if (cls[y][x] != CL_ROOF || bld_id[y][x] != -1 || nb >= MAX_BLD) continue;
		int sp = 0, x0 = x, x1 = x, y0 = y, y1 = y;
		bld_id[y][x] = nb; stack[sp][0] = x; stack[sp][1] = y; sp++;
		while (sp) {
			sp--; int cx = stack[sp][0], cy = stack[sp][1];
			if (cx < x0) x0 = cx; if (cx > x1) x1 = cx; if (cy < y0) y0 = cy; if (cy > y1) y1 = cy;
			static const int d[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
			for (int k = 0; k < 4; k++) {
				int nx = cx + d[k][0], ny = cy + d[k][1];
				if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
				if (cls[ny][nx] == CL_ROOF && bld_id[ny][nx] == -1) {
					bld_id[ny][nx] = nb; stack[sp][0] = nx; stack[sp][1] = ny; sp++;
				}
			}
		}
		int wy = y1 + 1;
		while (wy < H) {
			bool all = true;
			for (int xx = x0; xx <= x1; xx++) if (cls[wy][xx] != CL_SOLID && cls[wy][xx] != CL_DOOR) all = false;
			if (!all) break;
			wy++;
		}
		Bld b = { x0, x1, y0, y1, wy - 1 };
		if (b.wy1 == b.y1) {   /* no wall rows: just a roof-coloured block (e.g. a roof edge), keep as solid */
			for (int yy = y0; yy <= y1; yy++) for (int xx = x0; xx <= x1; xx++) if (bld_id[yy][xx] == nb) bld_id[yy][xx] = -1;
			for (int yy = y0; yy <= y1; yy++) for (int xx = x0; xx <= x1; xx++) if (cls[yy][xx] == CL_ROOF) cls[yy][xx] = CL_SOLID;
			continue;
		}
		for (int yy = y0; yy <= b.wy1; yy++) for (int xx = x0; xx <= x1; xx++) {
			bld_id[yy][xx] = nb; hgt[yy][xx] = H_BUILDING;
		}
		blds[nb++] = b;
	}
	m->buildings = nb;

	for (int gy = 0; gy < H; gy++) for (int gx = 0; gx < W; gx++) {
		m->start[gy * W + gx] = m->count;
		const float X = (gx - O) * 16.0f, Z = (gy - O) * 16.0f;
		const uint8_t *t = tiles[gy][gx];

		if (bld_id[gy][gx] >= 0) {
			const Bld *b = &blds[bld_id[gy][gx]];
			if (gy != b->wy1) continue;            /* building column emitted from its front row */
			const int roof_tiles = (b->y1 - b->y0 + 1) * 2, wall_tiles = (b->wy1 - b->y1) * 2;
			const float zb = (b->y0 - O) * 16.0f, zf = (b->wy1 + 1 - O) * 16.0f;
			const float half = (zf - zb) / 2, wall_h = wall_tiles * 8.0f, rise = half * 0.6f;
			for (int i = 0; i < 2; i++) {
				const float x0 = X + i * 8, x1 = x0 + 8;
				for (int r = 0; r < wall_tiles; r++) {     /* facade: wall art stands up */
					int tile = tiles[b->y1 + 1 + r / 2][gx][(r % 2) * 2 + i];
					float yt = wall_h - r * 8, yb = yt - 8;
					quad(m, p3(x0, yt, zf), p3(x1, yt, zf), p3(x1, yb, zf), p3(x0, yb, zf), tile, 0.92f);
				}
				for (int k = 0; k < roof_tiles; k++) {     /* gable roof: eave at the front, ridge mid-depth */
					int tile = tiles[b->y0 + k / 2][gx][(k % 2) * 2 + i];
					float tt = 1.0f - (float)k / roof_tiles, tb = 1.0f - (float)(k + 1) / roof_tiles;
					float yt = wall_h + tt * rise, yb = wall_h + tb * rise;
					quad(m, p3(x0, yt, zf - tt * half), p3(x1, yt, zf - tt * half),
					        p3(x1, yb, zf - tb * half), p3(x0, yb, zf - tb * half), tile, 1.0f);
					quad(m, p3(x0, yt, zb + tt * half), p3(x1, yt, zb + tt * half),
					        p3(x1, yb, zb + tb * half), p3(x0, yb, zb + tb * half), tile, 0.72f);
				}
			}
			for (int side = 0; side < 2; side++) {            /* end walls + gables */
				if ((side == 0 && gx != b->x0) || (side == 1 && gx != b->x1)) continue;
				float x = side ? X + 16 : X;
				int edge = tiles[b->y1 + 1][gx][side ? 1 : 0];
				int roofedge = tiles[b->y0][gx][side ? 1 : 0];
				float s = side ? 0.7f : 0.8f;
				quad(m, p3(x, wall_h, zb), p3(x, wall_h, zf), p3(x, 0, zf), p3(x, 0, zb), edge, s);
				tri(m, p3(x, wall_h, zb), p3(x, wall_h + rise, zb + half), p3(x, wall_h, zf), roofedge, s * 0.9f);
			}
			continue;
		}

		const int h = hgt[gy][gx];
		top_face(m, X, Z, (float)h, t, 1.0f);
		/* sides wherever a neighbour is lower */
		static const int nd[4][2] = { {0,1}, {1,0}, {-1,0}, {0,-1} };   /* S E W N */
		static const float ns[4] = { 0.82f, 0.68f, 0.74f, 0.6f };
		for (int k = 0; k < 4; k++) {
			int nx = gx + nd[k][0], ny = gy + nd[k][1];
			int hn = (nx < 0 || ny < 0 || nx >= W || ny >= H) ? h : hgt[ny][nx];
			if (hn == H_BUILDING || hn >= h) continue;
			float yt = (float)h, yb = (float)hn;
			for (int i = 0; i < 2; i++) {
				switch (k) {
				case 0: quad(m, p3(X + i*8, yt, Z + 16), p3(X + i*8 + 8, yt, Z + 16), p3(X + i*8 + 8, yb, Z + 16), p3(X + i*8, yb, Z + 16), t[2 + i], ns[k]); break;
				case 1: quad(m, p3(X + 16, yt, Z + i*8 + 8), p3(X + 16, yt, Z + i*8), p3(X + 16, yb, Z + i*8), p3(X + 16, yb, Z + i*8 + 8), t[1 + 2*i], ns[k]); break;
				case 2: quad(m, p3(X, yt, Z + i*8), p3(X, yt, Z + i*8 + 8), p3(X, yb, Z + i*8 + 8), p3(X, yb, Z + i*8), t[2*i], ns[k]); break;
				case 3: quad(m, p3(X + i*8 + 8, yt, Z), p3(X + i*8, yt, Z), p3(X + i*8, yb, Z), p3(X + i*8 + 8, yb, Z), t[i], ns[k]); break;
				}
			}
		}
	}
	m->start[W * H] = m->count;
	return m->count < m->cap;
}

void world_row_range(const WorldMesh *m, int y, int x0, int x1, int *first, int *count) {
	*first = 0; *count = 0;
	if (y < 0 || y >= m->sq_h || m->sq_w == 0) return;
	if (x0 < 0) x0 = 0; if (x1 >= m->sq_w) x1 = m->sq_w - 1;
	if (x1 < x0) return;
	int a = m->start[y * m->sq_w + x0], b = m->start[y * m->sq_w + x1 + 1];
	*first = a; *count = b - a;
}

/* ---- atlases ------------------------------------------------------------ */

static void tile_row(const GoldView *g, uint16_t base, int row, uint8_t px[8]) {
	uint8_t lo = gv8(g, base + row * 2), hi = gv8(g, base + row * 2 + 1);
	for (int b = 0; b < 8; b++) px[b] = ((lo >> (7 - b)) & 1) | (((hi >> (7 - b)) & 1) << 1);
}

void world_bg_atlas(const GoldView *g, uint32_t *rgba) {
	uint32_t palmap = current_palmap(g);
	int tod = gv8(g, wTimeOfDayPal) & 3;
	uint32_t po = rom_offset(ROM_BG_PALETTES_BANK, ROM_BG_PALETTES_ADDR) + tod * 64;
	uint32_t pal[8][4];
	for (int p = 0; p < 8; p++) for (int i = 0; i < 4; i++) {
		uint16_t v = g->rom[po + p * 8 + i * 2] | (g->rom[po + p * 8 + i * 2 + 1] << 8);
		int r = v & 31, gg = (v >> 5) & 31, b = (v >> 10) & 31;
		pal[p][i] = RGBA(r * 255 / 31, gg * 255 / 31, b * 255 / 31, 255);
	}
	for (int t = 0; t < 256; t++) {
		uint16_t base = t < 0x80 ? 0x9000 + t * 16 : 0x8800 + (t - 0x80) * 16;
		int p = tile_pal(g, palmap, t);
		for (int row = 0; row < 8; row++) {
			uint8_t px[8]; tile_row(g, base, row, px);
			uint32_t *dst = rgba + ((t / 16) * 8 + row) * 128 + (t % 16) * 8;
			for (int x = 0; x < 8; x++) dst[x] = pal[p][px[x]];
		}
	}
}

void world_obj_atlas(const GoldView *g, uint32_t *rgba) {
	static const uint32_t pal[4] = { 0, RGBA(250, 246, 236, 255), RGBA(214, 96, 64, 255), RGBA(30, 28, 40, 255) };
	for (int t = 0; t < 256; t++) {
		uint16_t base = 0x8000 + t * 16;
		for (int row = 0; row < 8; row++) {
			uint8_t px[8]; tile_row(g, base, row, px);
			uint32_t *dst = rgba + ((t / 16) * 8 + row) * 128 + (t % 16) * 8;
			for (int x = 0; x < 8; x++) dst[x] = pal[px[x]];
		}
	}
}

int world_sprites(const GoldView *g, float cam_x, float cam_z, WVert *out, int cap) {
	int sx[40], sy[40], n = 0, cnt = 0;
	uint8_t tl[40], at[40];
	for (int i = 0; i < 40; i++) {
		uint8_t y = gv8(g, 0xFE00 + i * 4), x = gv8(g, 0xFE01 + i * 4);
		if (y == 0 || y >= 160 || x == 0 || x >= 168) continue;
		sx[n] = x - 8; sy[n] = y - 16; tl[n] = gv8(g, 0xFE02 + i * 4); at[n] = gv8(g, 0xFE03 + i * 4); n++;
	}
	for (int i = 0; i < n && cnt + 6 <= cap; i++) {
		int bottom = sy[i] + 8;
		for (int j = 0; j < n; j++) if (sx[j] == sx[i] && sy[j] == sy[i] + 8) bottom = sy[i] + 16;
		float x0 = cam_x + (sx[i] - 64), x1 = x0 + 8;
		float z = cam_z + (bottom - 64) + 3;
		float yt = (float)(bottom - sy[i]), yb = yt - 8;
		float u0, v0, u1, v1; atlas_uv(tl[i], &u0, &v0, &u1, &v1);
		if (at[i] & 0x20) { float s = u0; u0 = u1; u1 = s; }
		if (at[i] & 0x40) { float s = v0; v0 = v1; v1 = s; }
		WVert q[6] = {
			{ x0, yt, z, u0, v0, 1 }, { x0, yb, z, u0, v1, 1 }, { x1, yb, z, u1, v1, 1 },
			{ x0, yt, z, u0, v0, 1 }, { x1, yb, z, u1, v1, 1 }, { x1, yt, z, u1, v0, 1 },
		};
		memcpy(out + cnt, q, sizeof q); cnt += 6;
	}
	return cnt;
}

const char *gold_rom_check(const uint8_t *rom, size_t size) {
	if (size != 0x200000) return "The ROM should be exactly 2 MB.";
	if (memcmp(rom + 0x134, "POKEMON_GLDAAUE", 15) != 0) return "This is not Pokemon Gold (USA/Europe).";
	if (rom[0x14E] != 0x68 || rom[0x14F] != 0x2D) return "This Gold ROM is modified or a different revision.";
	return NULL;
}
