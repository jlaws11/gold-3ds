#include "frui.h"
#include <string.h>
#include "charmap_fr.h"

#define RGBA(r, g, b, a) (((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(a))

/* ---- atlas layout (pixels) ------------------------------------------------- */
#define AT_FONT_DARK_Y   0     /* normal font, 128 slots of 16x16, 16 per row: dark text for white boxes */
#define AT_FONT_LIGHT_Y  128   /* the same glyphs in white, for the battle message panel */
#define AT_SMALL_X       256   /* small font for the HP boxes: 128 slots of 8x16, 32 per row */
#define AT_SMALL_Y       0
#define AT_DLG_X         256   /* dialogue frame: 20 tiles in a row */
#define AT_DLG_Y         64
#define AT_MENU_X        256   /* menu frame (frame style 1): 9 tiles */
#define AT_MENU_Y        72
#define AT_ELEM_X        256   /* HP bar pieces: 120 tiles, 16 per row */
#define AT_ELEM_Y        80
#define AT_HBP_X         384   /* player's HP box, 128x64 (both halves) */
#define AT_HBP_Y         80
#define AT_HBO_X         256   /* opponent's HP box, 128x32 */
#define AT_HBO_Y         144
#define AT_PANEL_Y       256   /* battle panels, 240x48 each: message, action menu, move menu */
#define AT_ARROW_X       384   /* the "more text" arrow, 10x12 */
#define AT_ARROW_Y       144
#define ARROW_CODE       0x7A  /* Gold's ▼ maps to this FireRed code; drawn as the arrow */

enum { PANEL_MSG, PANEL_ACTION, PANEL_MOVES };
enum { FONT_DARK, FONT_LIGHT, FONT_SMALL };

static int16_t slot_of[256];          /* FireRed glyph code -> atlas slot, -1 if absent */
static uint8_t width_normal[256], width_small[256];
static bool loaded;

/* ---- decoding ---------------------------------------------------------------- */

static uint32_t c555(const uint8_t *p) {
	uint16_t v = p[0] | (p[1] << 8);
	int r = v & 31, g = (v >> 5) & 31, b = (v >> 10) & 31;
	return RGBA(r * 255 / 31, g * 255 / 31, b * 255 / 31, 255);
}

static void palette(const uint8_t *p, uint32_t c[16]) {
	for (int i = 0; i < 16; i++) c[i] = c555(p + i * 2);
}

/* 4bpp tile, colour 0 see-through */
static void tile4(const uint8_t *t, const uint32_t pal[16], uint32_t *dst) {
	for (int y = 0; y < 8; y++)
		for (int x = 0; x < 8; x++) {
			uint8_t b = t[y * 4 + x / 2], i = (x & 1) ? b >> 4 : b & 15;
			dst[y * FRUI_TEX + x] = i ? pal[i] : 0;
		}
}

/* FireRed font tile: 8 rows of 16 bits, high byte = left 4 pixels, 2 bits each
 * (1 = text, 2 = shadow, 0/3 = background) */
static void glyph_tile(const uint8_t *t, uint32_t fg, uint32_t sh, uint32_t *dst) {
	for (int r = 0; r < 8; r++) {
		uint8_t halves[2] = { t[r * 2 + 1], t[r * 2] };
		for (int h = 0; h < 2; h++)
			for (int k = 0; k < 4; k++) {
				int v = (halves[h] >> (6 - 2 * k)) & 3;
				dst[r * FRUI_TEX + h * 4 + k] = v == 1 ? fg : v == 2 ? sh : 0;
			}
	}
}

/* ---- loading ------------------------------------------------------------------ */

bool frui_load(FrRead rd, void *ctx, const FrTables *t, uint32_t *atlas) {
	static uint8_t buf[0x2000];
	uint8_t g[64], p[32];
	uint32_t pal[16];
	loaded = false;
	if (!t) return false;
	memset(atlas, 0, FRUI_TEX * FRUI_TEX * 4);

	/* colours: text on white boxes, on the battle message panel, and on the HP boxes */
	uint32_t win_pal[16], bt_pal[2][16], hb_pal[16], bar_pal[16];
	if (!rd(ctx, t->window_pals, p, 32)) return false;
	palette(p, win_pal);
	if (fr_read_lz(rd, ctx, FR_BTEXTBOX_PAL, buf, 64) != 64) return false;
	palette(buf, bt_pal[0]); palette(buf + 32, bt_pal[1]);
	if (!rd(ctx, FR_HEALTHBOX_PAL, p, 32)) return false;
	palette(p, hb_pal);
	if (!rd(ctx, FR_HEALTHBAR_PAL, p, 32)) return false;
	palette(p, bar_pal);

	/* fonts: only the glyphs Gold's text can map to */
	if (!rd(ctx, t->font_widths, width_normal, 256) || !rd(ctx, t->font_small_widths, width_small, 256)) return false;
	for (int i = 0; i < 256; i++) slot_of[i] = -1;
	int slots = 0;
	for (int c = 0; c < 256; c++) for (int k = 0; k < 2; k++) {
		uint8_t code = gold_to_fr[c][k];
		if (code != 0xFF && slot_of[code] < 0 && slots < 128) slot_of[code] = slots++;
	}
	for (int code = 0; code < 256; code++) {
		int s = slot_of[code];
		if (s < 0) continue;
		if (!rd(ctx, t->font + code * 64, g, 64)) return false;
		for (int q = 0; q < 4; q++) {
			int ox = (s % 16) * 16 + (q & 1) * 8, oy = (s / 16) * 16 + (q >> 1) * 8;
			glyph_tile(g + q * 16, win_pal[2], win_pal[3], atlas + (AT_FONT_DARK_Y + oy) * FRUI_TEX + ox);
			glyph_tile(g + q * 16, bt_pal[0][1], bt_pal[0][6], atlas + (AT_FONT_LIGHT_Y + oy) * FRUI_TEX + ox);
		}
		if (!rd(ctx, t->font_small + code * 32, g, 32)) return false;
		for (int q = 0; q < 2; q++) {
			int ox = AT_SMALL_X + (s % 32) * 8, oy = AT_SMALL_Y + (s / 32) * 16 + q * 8;
			glyph_tile(g + q * 16, hb_pal[1], hb_pal[3], atlas + oy * FRUI_TEX + ox);
		}
	}

	/* dialogue frame (20 tiles) and menu frame style 1 (9 tiles) */
	if (!rd(ctx, t->msg_window, buf, 20 * 32)) return false;
	for (int i = 0; i < 20; i++) tile4(buf + i * 32, win_pal, atlas + AT_DLG_Y * FRUI_TEX + AT_DLG_X + i * 8);
	if (!rd(ctx, t->user_frame, buf, 9 * 32) || !rd(ctx, t->user_frame_pal, p, 32)) return false;
	palette(p, pal);
	for (int i = 0; i < 9; i++) tile4(buf + i * 32, pal, atlas + AT_MENU_Y * FRUI_TEX + AT_MENU_X + i * 8);

	/* the "more text" arrow: first frame of a 128x16 strip of tiles, 16 tiles per row */
	if (!rd(ctx, t->down_arrow, buf, 0x400)) return false;
	for (int ty = 0; ty < 2; ty++) for (int tx = 0; tx < 2; tx++)
		tile4(buf + (ty * 16 + tx) * 32, win_pal, atlas + (AT_ARROW_Y + ty * 8) * FRUI_TEX + AT_ARROW_X + tx * 8);

	/* HP bar pieces */
	if (!rd(ctx, FR_BATTLE_ELEMS, buf, 120 * 32)) return false;
	for (int i = 0; i < 120; i++)
		tile4(buf + i * 32, bar_pal, atlas + (AT_ELEM_Y + (i / 16) * 8) * FRUI_TEX + AT_ELEM_X + (i % 16) * 8);

	/* HP boxes: two 64-wide halves side by side; the second half starts 64 tiles in
	 * for the player's (64x64 halves) and 32 tiles in for the opponent's (64x32) */
	if (fr_read_lz(rd, ctx, FR_HB_PLAYER_GFX, buf, 0x1000) != 0x1000) return false;
	for (int half = 0; half < 2; half++)
		for (int i = 0; i < 64; i++)
			tile4(buf + (half * 64 + i) * 32, hb_pal,
			      atlas + (AT_HBP_Y + (i / 8) * 8) * FRUI_TEX + AT_HBP_X + half * 64 + (i % 8) * 8);
	if (fr_read_lz(rd, ctx, FR_HB_OPP_GFX, buf, 0x800) != 0x800) return false;
	for (int half = 0; half < 2; half++)
		for (int i = 0; i < 32; i++)
			tile4(buf + (half * 32 + i) * 32, hb_pal,
			      atlas + (AT_HBO_Y + (i / 8) * 8) * FRUI_TEX + AT_HBO_X + half * 64 + (i % 8) * 8);

	/* battle panels, drawn from the battle text box tilemap (32 tiles wide):
	 * rows 14-19 message, 34-39 action menu, 54-59 move menu */
	static uint8_t gfx[0x2000], map[0x1000];
	if (fr_read_lz(rd, ctx, FR_BTEXTBOX_GFX, gfx, sizeof gfx) == 0) return false;
	if (fr_read_lz(rd, ctx, FR_BTEXTBOX_MAP, map, sizeof map) != sizeof map) return false;
	static const int panel_row[3] = { 14, 34, 54 };
	for (int pnl = 0; pnl < 3; pnl++)
		for (int r = 0; r < 6; r++)
			for (int c = 0; c < 30; c++) {
				int i = (panel_row[pnl] + r) * 32 + c;
				uint16_t e = map[i * 2] | (map[i * 2 + 1] << 8);
				int tn = e & 0x3FF, hf = (e >> 10) & 1, vf = (e >> 11) & 1, pn = (e >> 12) & 1;
				uint32_t tmp[8 * FRUI_TEX];
				tile4(gfx + tn * 32, bt_pal[pn], tmp);
				uint32_t *dst = atlas + (AT_PANEL_Y + pnl * 48 + r * 8) * FRUI_TEX + c * 8;
				for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++)
					dst[y * FRUI_TEX + x] = tmp[(vf ? 7 - y : y) * FRUI_TEX + (hf ? 7 - x : x)];
			}
	loaded = true;
	return true;
}

/* ---- drawing primitives ----------------------------------------------------- */

static void quad(UiList *l, float x, float y, float w, float h, float u0, float v0, float u1, float v1) {
	if (l->n >= FRUI_MAX_QUADS) return;
	UiQuad q = { x, y, w, h, u0, v0, u1, v1 };
	l->q[l->n++] = q;
}

/* a w x h piece of the atlas at (ax, ay), drawn 1:1; vflip mirrors it vertically */
static void piece(UiList *l, float x, float y, int ax, int ay, int w, int h, bool vflip) {
	if (vflip) quad(l, x, y, w, h, ax, ay + h, ax + w, ay);
	else       quad(l, x, y, w, h, ax, ay, ax + w, ay + h);
}

static int glyph_width(int font, uint8_t code) {
	int w = font == FONT_SMALL ? width_small[code] : width_normal[code];
	return w ? w : 6;
}

/* FireRed glyph codes at (x, y): the top of the 16-px glyph cell. Returns the width. */
static int text_fr(UiList *l, int font, float x, float y, const uint8_t *codes, int n) {
	float x0 = x;
	for (int i = 0; i < n; i++) {
		uint8_t c = codes[i];
		int s = slot_of[c];
		if (c == ARROW_CODE && font != FONT_SMALL) {
			piece(l, x + 1, y + 2, AT_ARROW_X, AT_ARROW_Y, 10, 12, false);
			x += 10;
			continue;
		}
		if (s >= 0 && c != 0x00) {
			if (font == FONT_SMALL)
				piece(l, x, y, AT_SMALL_X + (s % 32) * 8, AT_SMALL_Y + (s / 32) * 16, 8, 16, false);
			else
				piece(l, x, y, (s % 16) * 16, (font == FONT_LIGHT ? AT_FONT_LIGHT_Y : AT_FONT_DARK_Y) + (s / 16) * 16, 16, 16, false);
		}
		x += glyph_width(font, c);
	}
	return (int)(x - x0);
}

/* Gold text codes -> FireRed codes; returns the count */
static int gold_text(const uint8_t *gold, int n, uint8_t *out, int cap) {
	int m = 0;
	for (int i = 0; i < n && m < cap; i++)
		for (int k = 0; k < 2 && m < cap; k++)
			if (gold_to_fr[gold[i]][k] != 0xFF) out[m++] = gold_to_fr[gold[i]][k];
	return m;
}

static uint8_t tm(const GoldView *g, int r, int c) { return gv8(g, wTilemap + r * 20 + c); }

static bool is_frame(uint8_t t) { return t >= 0x79 && t <= 0x7E; }
static bool is_blank(uint8_t t) { return t == 0x7F || gold_to_fr[t][0] == 0x00; }

/* One row of Gold's screen, cols [c0, c1], as text runs. Runs keep Gold's column
 * positions (6 px per column, FireRed's average glyph width) so menus stay
 * aligned; within a run the glyphs are proportional. */
static void gold_row(UiList *l, const GoldView *g, int font, float x, float y, int r, int c0, int c1) {
	int c = c0;
	while (c <= c1) {
		while (c <= c1 && is_blank(tm(g, r, c))) c++;
		if (c > c1) break;
		int start = c;
		uint8_t run[24]; int n = 0;
		/* a run ends at two blanks in a row (single spaces are part of the text) */
		while (c <= c1 && !(is_blank(tm(g, r, c)) && (c == c1 || is_blank(tm(g, r, c + 1))))) {
			if (n < 24) run[n++] = tm(g, r, c);
			c++;
		}
		uint8_t fr[48];
		int m = gold_text(run, n, fr, 48);
		text_fr(l, font, x + (start - c0) * 6, y, fr, m);
	}
}

static bool row_has_text(const GoldView *g, int r, int c0, int c1) {
	for (int c = c0; c <= c1; c++) if (!is_blank(tm(g, r, c)) && !is_frame(tm(g, r, c))) return true;
	return false;
}

/* 9-slice from 8x8 tiles: tl, top, tr, left, fill, right, bl, bottom, br */
static void frame9(UiList *l, float x, float y, int w, int h, int ax, int ay, const int tiles[9]) {
	int cols = w / 8, rows = h / 8;
	for (int r = 0; r < rows; r++)
		for (int c = 0; c < cols; c++) {
			int k = (r == 0 ? 0 : r == rows - 1 ? 6 : 3) + (c == 0 ? 0 : c == cols - 1 ? 2 : 1);
			piece(l, x + c * 8, y + r * 8, ax + tiles[k] * 8, ay, 8, 8, false);
		}
}

/* FireRed menu window around an interior of iw x ih */
static void menu_window(UiList *l, float x, float y, int iw, int ih) {
	static const int t[9] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
	iw = (iw + 7) / 8 * 8; ih = (ih + 7) / 8 * 8;
	frame9(l, x, y, iw + 16, ih + 16, AT_MENU_X, AT_MENU_Y, t);
}

/* FireRed dialogue box: interior iw x 32, frame 16 px left/right and 8 top/bottom */
static void dialogue_box(UiList *l, float x, float y, int iw) {
	/* rows: top, 4 body rows, bottom; tiles per FireRed's DrawDialogueFrame */
	static const int8_t rows[6][5] = {
		{ 0, 1, 2, 3, 4 }, { 5, 6, 7, 8, 9 }, { 10, 11, 7, 12, 13 },
		{ 10, 11, 7, 12, 13 }, { 5, 6, 7, 8, 9 }, { 0, 1, 2, 3, 4 } };
	int cols = iw / 8;
	for (int r = 0; r < 6; r++) {
		bool vf = r >= 3;
		for (int c = 0; c < cols + 4; c++) {
			int k = c == 0 ? 0 : c == 1 ? 1 : c == cols + 2 ? 3 : c == cols + 3 ? 4 : 2;
			int t = rows[r][k];
			if (r >= 1 && r <= 4 && k == 2) t = 7;   /* white inside */
			piece(l, x + c * 8, y + r * 8, AT_DLG_X + t * 8, AT_DLG_Y, 8, 8, vf);
		}
	}
}

/* a battle panel, 1:1 */
static void panel(UiList *l, float x, float y, int which) {
	piece(l, x, y, 0, AT_PANEL_Y + which * 48, 240, 48, false);
}

/* a box cut from a battle panel (source x0..x1, 48 tall), stretched to w x h by
 * repeating its middle in 8-px steps */
static void panel_box(UiList *l, float x, float y, int w, int h, int which, int sx0, int sx1) {
	int ay = AT_PANEL_Y + which * 48;
	int cols = w / 8, rows = h / 8;
	for (int r = 0; r < rows; r++)
		for (int c = 0; c < cols; c++) {
			int sc = c == 0 ? sx0 : c == cols - 1 ? sx1 - 8 : sx0 + 8 + ((c - 1) % ((sx1 - sx0 - 16) / 8)) * 8;
			int sr = r == 0 ? 0 : r == rows - 1 ? 40 : 8 + ((r - 1) % 4) * 8;
			piece(l, x + c * 8, y + r * 8, sc, ay + sr, 8, 8, false);
		}
}

/* ---- HP boxes ------------------------------------------------------------------ */

static int be16(const GoldView *g, uint16_t a) { return (gv8(g, a) << 8) | gv8(g, a + 1); }

static void hp_bar(UiList *l, float x, float y, int hp, int max) {
	int px = max > 0 ? (hp * 48 + max - 1) / max : 0;
	if (hp > 0 && px == 0) px = 1;
	if (px > 48) px = 48;
	int base = px > 24 ? 3 : px > 9 ? 47 : 56;        /* green / yellow / red, FireRed's thresholds */
	piece(l, x, y, AT_ELEM_X + 1 * 8, AT_ELEM_Y, 16, 8, false);   /* "HP" */
	for (int i = 0; i < 6; i++) {
		int f = px - i * 8; f = f < 0 ? 0 : f > 8 ? 8 : f;
		int e = base + f;
		piece(l, x + 16 + i * 8, y, AT_ELEM_X + (e % 16) * 8, AT_ELEM_Y + (e / 16) * 8, 8, 8, false);
	}
}

static void exp_bar(UiList *l, float x, float y, int px) {
	for (int i = 0; i < 8; i++) {
		int f = px - i * 8; f = f < 0 ? 0 : f > 8 ? 8 : f;
		int e = 12 + f;
		piece(l, x + i * 8, y, AT_ELEM_X + (e % 16) * 8, AT_ELEM_Y + (e / 16) * 8, 8, 8, false);
	}
}

/* name, level and gender from Gold's own HP box row: name from c0, ":L" + digits after */
static void name_level(UiList *l, const GoldView *g, int name_row, int name_c0, int lv_row,
                       float nx, float ny, float lx) {
	uint8_t run[12], fr[24]; int n = 0;
	for (int c = name_c0; c < 20 && n < 11; c++) {
		uint8_t t = tm(g, name_row, c);
		if (is_blank(t)) break;
		run[n++] = t;
	}
	int w = text_fr(l, FONT_SMALL, nx, ny, fr, gold_text(run, n, fr, 24));
	uint8_t lv[4], gender = 0; int k = 0;
	for (int c = 0; c < 20; c++) {
		uint8_t t = tm(g, lv_row, c);
		if (t == 0x6E) { for (int d = c + 1; d < 20 && k < 3 && tm(g, lv_row, d) >= 0xF6; d++) lv[k++] = tm(g, lv_row, d); }
		if (t == 0xEF || t == 0xF5) gender = t;
	}
	if (gender) text_fr(l, FONT_SMALL, nx + w, ny, fr, gold_text(&gender, 1, fr, 2));
	text_fr(l, FONT_SMALL, lx, ny, fr, gold_text(lv, k, fr, 8));
}

static void number(UiList *l, float right_x, float y, int v) {
	uint8_t d[5]; int n = 0;
	do { d[n++] = 0xA1 + v % 10; v /= 10; } while (v && n < 5);
	uint8_t s[5];
	for (int i = 0; i < n; i++) s[i] = d[n - 1 - i];
	int w = 0;
	for (int i = 0; i < n; i++) w += glyph_width(FONT_SMALL, s[i]);
	text_fr(l, FONT_SMALL, right_x - w, y, s, n);
}

static void enemy_box(UiList *l, const GoldView *g, float x, float y) {
	piece(l, x, y, AT_HBO_X, AT_HBO_Y, 128, 32, false);
	name_level(l, g, 0, 1, 1, x + 7, y + 3, x + 66);
	hp_bar(l, x + 24, y + 17, be16(g, wEnemyMonHP), be16(g, wEnemyMonMaxHP));
}

static void player_box(UiList *l, const GoldView *g, float x, float y) {
	piece(l, x, y, AT_HBP_X, AT_HBP_Y, 128, 48, false);
	name_level(l, g, 7, 10, 8, x + 15, y + 3, x + 74);
	int hp = be16(g, wBattleMonHP), max = be16(g, wBattleMonMaxHP);
	hp_bar(l, x + 32, y + 17, hp, max);
	number(l, x + 63, y + 22, hp);
	number(l, x + 90, y + 22, max);
	/* EXP: Gold's own bar on row 11 (tiles 0x62 empty .. 0x6A full) */
	int px = 0;
	for (int c = 11; c <= 18; c++) { int t = tm(g, 11, c) - 0x62; px += t < 0 ? 0 : t > 8 ? 8 : t; }
	exp_bar(l, x + 32, y + 33, px);
}

/* ---- screens -------------------------------------------------------------------- */

#define SCREEN_W 400
#define SCREEN_H 240
#define PANEL_X ((SCREEN_W - 240) / 2)
#define PANEL_Y (SCREEN_H - 48)

typedef struct { int c0, r0, c1, r1; } Win;

/* Gold's windows: ┌ ... ┐ / └ ... ┘ rectangles in the tilemap */
static int find_windows(const GoldView *g, Win *w, int cap) {
	int n = 0;
	for (int r = 0; r < 17; r++)
		for (int c = 0; c < 19; c++) {
			if (tm(g, r, c) != 0x79 || n >= cap) continue;
			int c1 = c + 1;
			while (c1 < 20 && tm(g, r, c1) == 0x7A) c1++;
			if (c1 >= 20 || (tm(g, r, c1) != 0x7B && tm(g, r, c1) != 0x79)) continue;
			int r1 = r + 1;
			while (r1 < 18 && (tm(g, r1, c) == 0x7C || tm(g, r1, c) == 0x79)) r1++;
			if (r1 >= 18 || tm(g, r1, c) != 0x7D) continue;
			Win x = { c, r, c1, r1 };
			w[n++] = x;
		}
	return n;
}

/* letters or frame pieces outside every window = a screen this can't restyle
 * (party, bag, Pokédex...). Field tiles are always below 0x60 on Gold, and Gold
 * blanks parts of the screen with 0x7F behind menus, so those don't count. */
static bool stray_text(const GoldView *g, const Win *w, int n) {
	for (int r = 0; r < 18; r++)
		for (int c = 0; c < 20; c++) {
			uint8_t t = tm(g, r, c);
			bool ui = (t >= 0x79 && t <= 0x7E) || (t >= 0x80 && gold_to_fr[t][0] != 0xFF);
			if (!ui) continue;
			bool inside = false;
			for (int i = 0; i < n && !inside; i++)
				inside = r >= w[i].r0 && r <= w[i].r1 && c >= w[i].c0 && c <= w[i].c1;
			if (!inside) return true;
		}
	return false;
}

static void field_screen(const GoldView *g, UiList *l, const Win *w, int n) {
	float dialog_top = SCREEN_H;
	for (int i = 0; i < n; i++) {
		const Win *b = &w[i];
		int first = -1, last = -1;
		for (int r = b->r0 + 1; r < b->r1; r++)
			if (row_has_text(g, r, b->c0 + 1, b->c1 - 1)) { if (first < 0) first = r; last = r; }
		if (b->c0 == 0 && b->c1 == 19 && b->r1 == 17) {
			/* the text box: FireRed's dialogue box along the bottom */
			float x = PANEL_X, y = SCREEN_H - 48;
			dialogue_box(l, x, y, 208);
			dialog_top = y;
			if (first >= 0)
				for (int r = first; r <= last; r++)
					gold_row(l, g, FONT_DARK, x + 16 + 2, y + 8 + (r - first) * 8 - 1, r, b->c0 + 1, b->c1 - 1);
			continue;
		}
		/* any other window: FireRed's menu frame, sized to Gold's text */
		int iw = (b->c1 - b->c0 - 1) * 6 + 6;
		int ih = first < 0 ? 16 : (last - first) * 8 + 16;
		float W = ((iw + 7) / 8 * 8) + 16, H = ((ih + 7) / 8 * 8) + 16;
		float x = b->c1 == 19 && b->c0 > 0 ? SCREEN_W - 8 - W : b->c0 == 0 ? 8 : b->c0 * 2.5f;
		float y = b->r0 == 0 ? 8 : (b->r1 + 1) * (SCREEN_H / 18.0f) - H;
		if (b->r1 < 12 && y + H > dialog_top - 4 && dialog_top < SCREEN_H) y = dialog_top - 4 - H;
		if (b->r1 < 12) y = y > SCREEN_H - 48 - 4 - H ? SCREEN_H - 48 - 4 - H : y;
		menu_window(l, x, y, iw, ih);
		if (first >= 0)
			for (int r = first; r <= last; r++)
				gold_row(l, g, FONT_DARK, x + 8 + 2, y + 8 + (r - first) * 8 - 1, r, b->c0 + 1, b->c1 - 1);
	}
}

FruiResult frui_plan(const GoldView *g, bool field, bool battle3d, UiList *l) {
	l->n = 0;
	if (!loaded) return FRUI_FALLBACK;
	bool textbox = tm(g, 12, 0) == 0x79 && tm(g, 17, 0) == 0x7D && tm(g, 17, 19) == 0x7E;

	if (battle3d) {
		bool enemy_hp = tm(g, 2, 1) == 0x6D;                         /* Gold has drawn the opponent's HP box */
		bool ours_out = gv8(g, wBattleMonSpecies) != 0 && enemy_hp;
		bool move_list = enemy_hp && tm(g, 8, 0) == 0x79 && tm(g, 12, 0) == 0x7D && tm(g, 17, 19) == 0x7E;
		bool action = textbox && tm(g, 12, 8) == 0x79 && tm(g, 17, 8) == 0x7D;
		if (!textbox && !move_list) return FRUI_FALLBACK;             /* party, bag, ... */

		if (enemy_hp) enemy_box(l, g, 8, 8);
		if (ours_out) player_box(l, g, SCREEN_W - 8 - 104 - 8, PANEL_Y - 48 - (move_list ? 40 : 0));

		if (move_list) {
			/* Gold's four moves, one per row, in FireRed's white battle box; PP and type beside it */
			float bw = 152, bh = 80, x = PANEL_X - 8, y = SCREEN_H - bh;
			panel_box(l, x, y, bw, bh, PANEL_MOVES, 0, 160);
			for (int r = 13; r <= 16; r++)
				gold_row(l, g, FONT_DARK, x + 10, y + 8 + (r - 13) * 16 - 1, r, 5, 18);
			float px = x + bw, py = SCREEN_H - 48;
			panel_box(l, px, py, 96, 48, PANEL_MOVES, 160, 240);
			uint8_t pp[8], fr[16]; int n = 0;
			for (int c = 1; c <= 9 && n < 8; c++) if (!is_blank(tm(g, 11, c))) pp[n++] = tm(g, 11, c);
			static const uint8_t PPlabel[2] = { 0xCA, 0xCA };   /* "PP" */
			int w = text_fr(l, FONT_DARK, px + 10, py + 7, PPlabel, 2);
			text_fr(l, FONT_DARK, px + 10 + w + 6, py + 7, fr, gold_text(pp, n, fr, 16));
			gold_row(l, g, FONT_DARK, px + 10, py + 23, 10, 1, 9);   /* the type, under "TYPE/" */
			return FRUI_DRAWN;
		}
		if (action) {
			/* FireRed's action panel: Gold's FIGHT / PKMN / PACK / RUN in the white box */
			panel(l, PANEL_X, PANEL_Y, PANEL_ACTION);
			for (int r = 14; r <= 16; r += 2) {
				gold_row(l, g, FONT_DARK, PANEL_X + 136, PANEL_Y + 8 + (r - 14) * 8 - 1, r, 9, 14);
				gold_row(l, g, FONT_DARK, PANEL_X + 184, PANEL_Y + 8 + (r - 14) * 8 - 1, r, 15, 18);
			}
			return FRUI_DRAWN;
		}
		/* messages: white text on FireRed's message panel */
		panel(l, PANEL_X, PANEL_Y, PANEL_MSG);
		for (int r = 14; r <= 16; r += 2)
			gold_row(l, g, FONT_LIGHT, PANEL_X + 10, PANEL_Y + 8 + (r - 14) * 8 - 1, r, 1, 18);
		return FRUI_DRAWN;
	}

	if (!field) return FRUI_NONE;
	Win w[8];
	int n = find_windows(g, w, 8);
	if (n == 0) return FRUI_NONE;
	if (stray_text(g, w, n)) return FRUI_FALLBACK;
	/* a window covering most of the screen is a full-screen menu */
	for (int i = 0; i < n; i++)
		if ((w[i].c1 - w[i].c0 + 1) * (w[i].r1 - w[i].r0 + 1) > 200 && !(w[i].c0 == 0 && w[i].r1 == 17 && w[i].r0 >= 12))
			return FRUI_FALLBACK;
	field_screen(g, l, w, n);
	return FRUI_DRAWN;
}
