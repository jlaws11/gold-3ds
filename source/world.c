#include "world.h"
#include <string.h>
#include <math.h>

enum { CL_GROUND, CL_GRASS, CL_WATER, CL_TREE, CL_ROOF, CL_SOLID, CL_DOOR, CL_LEDGE };
enum { PAL_GRAY, PAL_RED, PAL_GREEN, PAL_WATER, PAL_YELLOW, PAL_BROWN, PAL_ROOF, PAL_TEXT };

#define H_BUILDING  (-1000)    /* marks squares owned by a building */
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

/* ---- scene state shared by the build helpers ------------------------------ */

static uint8_t cls[WORLD_MAX_SQ_H][WORLD_MAX_SQ_W];
static short hgt[WORLD_MAX_SQ_H][WORLD_MAX_SQ_W];
static uint8_t tiles[WORLD_MAX_SQ_H][WORLD_MAX_SQ_W][4];
static short bld_id[WORLD_MAX_SQ_H][WORLD_MAX_SQ_W];
static Bld blds[MAX_BLD];
static short fill_stack[WORLD_MAX_SQ_W * WORLD_MAX_SQ_H][2];
static int GW, GH, GO;                 /* grid size and origin of the current build */
static const Light *LT;
static float sun_h[2], sun_slope;      /* horizontal sun direction and rise per pixel */

/* Building shape, shared by geometry and shadows. */
typedef struct { float zb, zf, half, wall_h, rise; int roof_tiles, wall_tiles; } BShape;
static BShape bshape(const Bld *b) {
	BShape s;
	s.roof_tiles = (b->y1 - b->y0 + 1) * 2; s.wall_tiles = (b->wy1 - b->y1) * 2;
	s.zb = (b->y0 - GO) * 16.0f; s.zf = (b->wy1 + 1 - GO) * 16.0f;
	s.half = (s.zf - s.zb) / 2; s.wall_h = s.wall_tiles * 8.0f; s.rise = s.half * 0.6f;
	return s;
}

/* Tree crown: an 8-sided rounded canopy on a short trunk, one per square.
 * Rings of (height, radius) from bottom to top, then a point at the apex. */
#define TREE_SEG 8
static const float crown_y[4] = { 5.5f, 10.5f, 16.5f, 21.0f };
static const float crown_r[4] = { 4.6f, 8.2f, 7.2f, 4.2f };
#define CROWN_APEX 23.0f

static uint32_t sq_hash(int x, int y) {
	uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return h ^ (h >> 16);
}

/* small per-tree variation so a forest doesn't look stamped */
static float tree_scale(int gx, int gy) { return 0.92f + (sq_hash(gx, gy) & 15) / 100.0f; }

/* Height of the world surface at a point (world pixels). */
static float height_at(float x, float z) {
	int gx = (int)floorf(x / 16) + GO, gy = (int)floorf(z / 16) + GO;
	if (gx < 0 || gy < 0 || gx >= GW || gy >= GH) return 0;
	if (cls[gy][gx] == CL_TREE) {
		float dx = x - ((gx - GO) * 16 + 8), dz = z - ((gy - GO) * 16 + 8), r = sqrtf(dx * dx + dz * dz);
		float k = tree_scale(gx, gy);
		if (r > crown_r[1]) return 0;
		if (r < crown_r[3]) return CROWN_APEX * k;
		if (r < crown_r[2]) return (crown_y[3] + (crown_y[2] - crown_y[3]) * (r - crown_r[3]) / (crown_r[2] - crown_r[3])) * k;
		return (crown_y[2] + (crown_y[1] - crown_y[2]) * (r - crown_r[2]) / (crown_r[1] - crown_r[2])) * k;
	}
	if (hgt[gy][gx] != H_BUILDING) return hgt[gy][gx];
	BShape s = bshape(&blds[bld_id[gy][gx]]);
	float d = s.zf - z;
	if (d < 0) return 0;
	float t = d <= s.half ? d / s.half : (2 * s.half - d) / s.half;
	return s.wall_h + s.rise * (t < 0 ? 0 : t);
}

/* Is this point in the shadow of something between it and the sun? */
static bool in_shadow(float x, float y, float z, float start) {
	if (sun_slope > 20.0f) return false;     /* sun almost overhead */
	for (float t = start; t <= 120.0f; t += 3.0f) {
		float py = y + 0.6f + sun_slope * t;
		if (py > 60.0f) return false;          /* above anything in the world */
		if (height_at(x + sun_h[0] * t, z + sun_h[1] * t) > py) return true;
	}
	return false;
}

/* Diffuse light for a face normal. */
static float lambert(float nx, float ny, float nz) {
	float d = nx * LT->sun[0] + ny * LT->sun[1] + nz * LT->sun[2];
	return 0.60f + 0.40f * (d > 0 ? d : 0);
}

/* ---- vertex emission ------------------------------------------------- */

typedef struct { float x, y, z; } P3;

static bool room(WorldMesh *m, int n) { return m->count + n <= m->cap; }

static void put(WorldMesh *m, P3 p, float u, float v, float s, float glow) {
	WVert *o = &m->verts[m->count++];
	o->x = p.x; o->y = p.y; o->z = p.z; o->u = u; o->v = v; o->shade = s; o->glow = glow;
}

/* a=top-left b=top-right c=bottom-right d=bottom-left (as the tile is drawn); one shade per corner */
static void quad4(WorldMesh *m, P3 a, P3 b, P3 c, P3 d, int tile, float sa, float sb, float sc, float sd, float glow) {
	if (!room(m, 6)) return;
	float u0, v0, u1, v1; atlas_uv(tile, &u0, &v0, &u1, &v1);
	put(m, a, u0, v0, sa, glow); put(m, d, u0, v1, sd, glow); put(m, c, u1, v1, sc, glow);
	put(m, a, u0, v0, sa, glow); put(m, c, u1, v1, sc, glow); put(m, b, u1, v0, sb, glow);
}

static void quad(WorldMesh *m, P3 a, P3 b, P3 c, P3 d, int tile, float shade) {
	quad4(m, a, b, c, d, tile, shade, shade, shade, shade, 0);
}

static void tri(WorldMesh *m, P3 a, P3 b, P3 c, int tile, float shade) {
	if (!room(m, 3)) return;
	float u0, v0, u1, v1; atlas_uv(tile, &u0, &v0, &u1, &v1);
	put(m, a, u0, v1, shade, 0); put(m, b, (u0 + u1) / 2, v0, shade, 0); put(m, c, u1, v1, shade, 0);
}

static P3 p3(float x, float y, float z) { P3 p = { x, y, z }; return p; }

/* top face of a square at height y, 4 tiles, shadows tested at every corner */
static void top_face(WorldMesh *m, float X, float Z, float y, const uint8_t t[4], float dim) {
	float lit = lambert(0, 1, 0) * dim, dark = lit * (1.0f - LT->shadow);
	float s[3][3];
	for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++)
		s[j][i] = in_shadow(X + i * 8, y, Z + j * 8, 2.0f) ? dark : lit;
	for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) {
		float x0 = X + i * 8, z0 = Z + j * 8;
		quad4(m, p3(x0, y, z0), p3(x0 + 8, y, z0), p3(x0 + 8, y, z0 + 8), p3(x0, y, z0 + 8), t[j * 2 + i],
		      s[j][i], s[j][i + 1], s[j + 1][i + 1], s[j + 1][i], 0);
	}
}

/* Rounded tree: trunk, three bands of an 8-sided crown and a pointed cap,
 * textured with the tree's own tiles and lit/shadowed per vertex. */
static void emit_tree(WorldMesh *m, float X, float Z, const uint8_t t[4], int gx, int gy) {
	const float cx = X + 8, cz = Z + 8, k = tree_scale(gx, gy);
	/* inside a forest the trunk and lower crown are hidden by the neighbours */
	bool inner = gx > 0 && gy > 0 && gx < GW - 1 && gy < GH - 1 &&
	             cls[gy][gx - 1] == CL_TREE && cls[gy][gx + 1] == CL_TREE &&
	             cls[gy - 1][gx] == CL_TREE && cls[gy + 1][gx] == CL_TREE;
	P3 ring[4][TREE_SEG + 1];
	for (int i = 0; i < 4; i++) for (int s = 0; s <= TREE_SEG; s++) {
		float a = (s + 0.5f) * 6.2831853f / TREE_SEG;
		ring[i][s] = p3(cx + cosf(a) * crown_r[i], crown_y[i] * k, cz + sinf(a) * crown_r[i]);
	}
	/* trunk */
	if (!inner) {
	float tl = lambert(0, 0, 1) * 0.55f;
	quad(m, p3(cx - 2, crown_y[0] * k, cz + 2), p3(cx + 2, crown_y[0] * k, cz + 2), p3(cx + 2, 0, cz + 2), p3(cx - 2, 0, cz + 2), t[2], tl);
	quad(m, p3(cx + 2, crown_y[0] * k, cz + 2), p3(cx + 2, crown_y[0] * k, cz - 2), p3(cx + 2, 0, cz - 2), p3(cx + 2, 0, cz + 2), t[3], lambert(1, 0, 0) * 0.55f);
	quad(m, p3(cx - 2, crown_y[0] * k, cz - 2), p3(cx - 2, crown_y[0] * k, cz + 2), p3(cx - 2, 0, cz + 2), p3(cx - 2, 0, cz - 2), t[2], lambert(-1, 0, 0) * 0.55f);
	}
	/* crown bands */
	for (int i = inner ? 1 : 0; i < 3; i++) {
		float dy = (crown_y[i + 1] - crown_y[i]) * k, dr = crown_r[i] - crown_r[i + 1];
		for (int s = 0; s < TREE_SEG; s++) {
			float am = (s + 1) * 6.2831853f / TREE_SEG;
			float nx = cosf(am) * dy, ny = dr, nz = sinf(am) * dy, nl = sqrtf(nx * nx + ny * ny + nz * nz);
			float lit = lambert(nx / nl, ny / nl, nz / nl), dark = lit * (1.0f - LT->shadow);
			P3 a = ring[i + 1][s], b = ring[i + 1][s + 1], c = ring[i][s + 1], d = ring[i][s];
			float sa = in_shadow(a.x, a.y, a.z, 10) ? dark : lit, sb = in_shadow(b.x, b.y, b.z, 10) ? dark : lit;
			float sc = in_shadow(c.x, c.y, c.z, 10) ? dark : lit, sd = in_shadow(d.x, d.y, d.z, 10) ? dark : lit;
			int tile = i == 0 ? t[2 + (s & 1)] : t[s & 1];
			quad4(m, a, b, c, d, tile, sa, sb, sc, sd, 0);
		}
	}
	/* pointed cap */
	P3 apex = p3(cx, CROWN_APEX * k, cz);
	float cl = lambert(0, 1, 0);
	for (int s = 0; s < TREE_SEG; s++) tri(m, ring[3][s], apex, ring[3][s + 1], t[s & 1], cl);
}

/* ---- the build --------------------------------------------------------- */

bool world_build(const GoldView *g, WorldMesh *m, const Light *light) {
	Ctx c;
	m->count = 0; m->buildings = 0; m->key = world_key(g); m->light_key = light->key;
	LT = light;
	{
		float hl = sqrtf(light->sun[0] * light->sun[0] + light->sun[2] * light->sun[2]);
		if (hl < 0.01f) { sun_h[0] = sun_h[1] = 0; sun_slope = 1000; }
		else { sun_h[0] = light->sun[0] / hl; sun_h[1] = light->sun[2] / hl; sun_slope = light->sun[1] / hl; }
	}
	if (!ctx_init(&c, g)) { m->sq_w = m->sq_h = 0; m->start[0] = 0; return true; }
	const int O = WORLD_MARGIN;
	m->origin = O;
	m->sq_w = c.w * 2 + 2 * O; m->sq_h = c.h * 2 + 2 * O;
	const int W = m->sq_w, H = m->sq_h;
	GW = W; GH = H; GO = O;

	for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
		cls[y][x] = classify(&c, x - O, y - O);
		sq_tiles(&c, x - O, y - O, tiles[y][x]);
		bld_id[y][x] = -1;
		switch (cls[y][x]) {
		case CL_WATER: hgt[y][x] = WATER_H; break;
		case CL_TREE:  hgt[y][x] = 0; break;    /* ground under the crown; crown via height_at */
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
		bld_id[y][x] = nb; fill_stack[sp][0] = x; fill_stack[sp][1] = y; sp++;
		while (sp) {
			sp--; int cx = fill_stack[sp][0], cy = fill_stack[sp][1];
			if (cx < x0) x0 = cx; if (cx > x1) x1 = cx; if (cy < y0) y0 = cy; if (cy > y1) y1 = cy;
			static const int d[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
			for (int k = 0; k < 4; k++) {
				int nx = cx + d[k][0], ny = cy + d[k][1];
				if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
				if (cls[ny][nx] == CL_ROOF && bld_id[ny][nx] == -1) {
					bld_id[ny][nx] = nb; fill_stack[sp][0] = nx; fill_stack[sp][1] = ny; sp++;
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
		if (b.wy1 == b.y1) {   /* no wall rows: just a roof-coloured block, keep as solid */
			for (int yy = y0; yy <= y1; yy++) for (int xx = x0; xx <= x1; xx++) {
				if (bld_id[yy][xx] == nb) bld_id[yy][xx] = -1;
				if (cls[yy][xx] == CL_ROOF) cls[yy][xx] = CL_SOLID;
			}
			continue;
		}
		for (int yy = y0; yy <= b.wy1; yy++) for (int xx = x0; xx <= x1; xx++) {
			bld_id[yy][xx] = nb; hgt[yy][xx] = H_BUILDING;
		}
		blds[nb++] = b;
	}
	m->buildings = nb;

	const float s_facade = lambert(0, 0, 1);
	for (int gy = 0; gy < H; gy++) for (int gx = 0; gx < W; gx++) {
		m->start[gy * W + gx] = m->count;
		const float X = (gx - O) * 16.0f, Z = (gy - O) * 16.0f;
		const uint8_t *t = tiles[gy][gx];

		if (bld_id[gy][gx] >= 0) {
			const Bld *b = &blds[bld_id[gy][gx]];
			if (gy != b->wy1) continue;            /* building column emitted from its front row */
			BShape s = bshape(b);
			float rl = sqrtf(s.half * s.half + s.rise * s.rise);
			float s_front = lambert(0, s.half / rl, s.rise / rl), s_back = lambert(0, s.half / rl, -s.rise / rl);
			for (int i = 0; i < 2; i++) {
				const float x0 = X + i * 8, x1 = x0 + 8;
				for (int r = 0; r < s.wall_tiles; r++) {   /* facade: wall art stands up; windows light up at night */
					int tile = tiles[b->y1 + 1 + r / 2][gx][(r % 2) * 2 + i];
					float glow = tile_pal(g, c.palmap, tile) == PAL_YELLOW ? 1.0f : 0.0f;
					float yt = s.wall_h - r * 8, yb = yt - 8;
					float sh = s_facade * 0.95f;
					quad4(m, p3(x0, yt, s.zf), p3(x1, yt, s.zf), p3(x1, yb, s.zf), p3(x0, yb, s.zf), tile, sh, sh, sh, sh, glow);
				}
				for (int k = 0; k < s.roof_tiles; k++) {    /* gable roof: eave at the front, ridge mid-depth */
					int tile = tiles[b->y0 + k / 2][gx][(k % 2) * 2 + i];
					float tt = 1.0f - (float)k / s.roof_tiles, tb = 1.0f - (float)(k + 1) / s.roof_tiles;
					float yt = s.wall_h + tt * s.rise, yb = s.wall_h + tb * s.rise;
					quad(m, p3(x0, yt, s.zf - tt * s.half), p3(x1, yt, s.zf - tt * s.half),
					        p3(x1, yb, s.zf - tb * s.half), p3(x0, yb, s.zf - tb * s.half), tile, s_front);
					quad(m, p3(x0, yt, s.zb + tt * s.half), p3(x1, yt, s.zb + tt * s.half),
					        p3(x1, yb, s.zb + tb * s.half), p3(x0, yb, s.zb + tb * s.half), tile, s_back);
				}
			}
			for (int side = 0; side < 2; side++) {          /* end walls + gables */
				if ((side == 0 && gx != b->x0) || (side == 1 && gx != b->x1)) continue;
				float x = side ? X + 16 : X;
				int edge = tiles[b->y1 + 1][gx][side ? 1 : 0];
				int roofedge = tiles[b->y0][gx][side ? 1 : 0];
				float sl = lambert(side ? 1.0f : -1.0f, 0, 0);
				quad(m, p3(x, s.wall_h, s.zb), p3(x, s.wall_h, s.zf), p3(x, 0, s.zf), p3(x, 0, s.zb), edge, sl);
				tri(m, p3(x, s.wall_h, s.zb), p3(x, s.wall_h + s.rise, s.zb + s.half), p3(x, s.wall_h, s.zf), roofedge, sl * 0.92f);
			}
			continue;
		}

		const int h = hgt[gy][gx];
		top_face(m, X, Z, (float)h, t, cls[gy][gx] == CL_TREE ? 0.62f : 1.0f);
		if (cls[gy][gx] == CL_TREE) emit_tree(m, X, Z, t, gx, gy);
		/* sides wherever a neighbour is lower, lit by how much they face the sun */
		static const int nd[4][2] = { {0,1}, {1,0}, {-1,0}, {0,-1} };   /* S E W N */
		for (int k = 0; k < 4; k++) {
			int nx = gx + nd[k][0], ny = gy + nd[k][1];
			int hn = (nx < 0 || ny < 0 || nx >= W || ny >= H) ? h : hgt[ny][nx];
			if (hn == H_BUILDING || hn >= h) continue;
			float yt = (float)h, yb = (float)hn;
			float sl = lambert((float)nd[k][0], 0, (float)nd[k][1]) * 0.92f;
			for (int i = 0; i < 2; i++) {
				switch (k) {
				case 0: quad(m, p3(X + i*8, yt, Z + 16), p3(X + i*8 + 8, yt, Z + 16), p3(X + i*8 + 8, yb, Z + 16), p3(X + i*8, yb, Z + 16), t[2 + i], sl); break;
				case 1: quad(m, p3(X + 16, yt, Z + i*8 + 8), p3(X + 16, yt, Z + i*8), p3(X + 16, yb, Z + i*8), p3(X + 16, yb, Z + i*8 + 8), t[1 + 2*i], sl); break;
				case 2: quad(m, p3(X, yt, Z + i*8), p3(X, yt, Z + i*8 + 8), p3(X, yb, Z + i*8 + 8), p3(X, yb, Z + i*8), t[2*i], sl); break;
				case 3: quad(m, p3(X + i*8 + 8, yt, Z), p3(X + i*8, yt, Z), p3(X + i*8, yb, Z), p3(X + i*8 + 8, yb, Z), t[i], sl); break;
				}
			}
		}
	}
	m->start[W * H] = m->count;
	return m->count < m->cap;
}

bool world_open(float x, float z) {
	int gx = (int)floorf(x / 16) + GO, gy = (int)floorf(z / 16) + GO;
	if (gx < 0 || gy < 0 || gx >= GW || gy >= GH) return false;
	return cls[gy][gx] == CL_GROUND || cls[gy][gx] == CL_GRASS;
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

int world_sprites(const GoldView *g, float cam_x, float cam_z, bool hide_player, WVert *out, int cap) {
	int sx[40], sy[40], n = 0, cnt = 0;
	uint8_t tl[40], at[40];
	for (int i = 0; i < 40; i++) {
		uint8_t y = gv8(g, 0xFE00 + i * 4), x = gv8(g, 0xFE01 + i * 4);
		if (y == 0 || y >= 160 || x == 0 || x >= 168) continue;
		/* the player always sits at screen (64,60), 16x16; ledge hops lift it a little */
		if (hide_player && (x - 8 == 64 || x - 8 == 72) && y - 16 >= 44 && y - 16 <= 68) continue;
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
			{ x0, yt, z, u0, v0, 1, 0 }, { x0, yb, z, u0, v1, 1, 0 }, { x1, yb, z, u1, v1, 1, 0 },
			{ x0, yt, z, u0, v0, 1, 0 }, { x1, yb, z, u1, v1, 1, 0 }, { x1, yt, z, u1, v0, 1, 0 },
		};
		memcpy(out + cnt, q, sizeof q); cnt += 6;
	}
	return cnt;
}

int world_tall_player(float cam_x, float cam_z, int frame, bool flip, WVert *out) {
	float x0 = cam_x, x1 = cam_x + 16, z = cam_z + 15, yt = 32, yb = 0;
	float u0 = frame * 16 / 256.0f + 0.0001f, u1 = (frame + 1) * 16 / 256.0f - 0.0001f, v0 = 0, v1 = 1;
	if (flip) { float s = u0; u0 = u1; u1 = s; }
	WVert q[6] = {
		{ x0, yt, z, u0, v0, 1, 0 }, { x0, yb, z, u0, v1, 1, 0 }, { x1, yb, z, u1, v1, 1, 0 },
		{ x0, yt, z, u0, v0, 1, 0 }, { x1, yb, z, u1, v1, 1, 0 }, { x1, yt, z, u1, v0, 1, 0 },
	};
	memcpy(out, q, sizeof q);
	return 6;
}

int world_sprite_shadows(const GoldView *g, float cam_x, float cam_z, WVert *out, int cap) {
	int sx[40], sy[40], n = 0, cnt = 0;
	for (int i = 0; i < 40; i++) {
		uint8_t y = gv8(g, 0xFE00 + i * 4), x = gv8(g, 0xFE01 + i * 4);
		if (y == 0 || y >= 160 || x == 0 || x >= 168) continue;
		sx[n] = x - 8; sy[n] = y - 16; n++;
	}
	/* one shadow per actor: at its bottom-left 8x8 sprite */
	for (int i = 0; i < n && cnt + 6 <= cap; i++) {
		bool below = false, left = false, above = false;
		for (int j = 0; j < n; j++) {
			if (sx[j] == sx[i] && sy[j] == sy[i] + 8) below = true;
			if (sx[j] == sx[i] - 8 && sy[j] == sy[i]) left = true;
			if (sx[j] == sx[i] && sy[j] == sy[i] - 8) above = true;
		}
		if (below || left || !above) continue;
		float cx = cam_x + (sx[i] - 64) + 8, z = cam_z + (sy[i] + 8 - 64) + 3;
		float x0 = cx - 9, x1 = cx + 9, z0 = z - 5, z1 = z + 4, y = 0.35f;
		WVert q[6] = {
			{ x0, y, z0, 0, 0, 0, 0 }, { x0, y, z1, 0, 1, 0, 0 }, { x1, y, z1, 1, 1, 0, 0 },
			{ x0, y, z0, 0, 0, 0, 0 }, { x1, y, z1, 1, 1, 0, 0 }, { x1, y, z0, 1, 0, 0, 0 },
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
