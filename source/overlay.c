#include "overlay.h"

/* tiles Gold uses for text box frames (0x79-0x7e) and blank text space (0x7f);
 * field tilesets never use them */
static bool texty(uint8_t t) { return t >= 0x79 && t <= 0x7f; }

static uint8_t tile(const GoldView *g, int row, int col) { return gv8(g, wTilemap + row * 20 + col); }

static void add(Overlay *o, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh) {
	if (o->n >= 4) return;
	OverlayRect r = { sx, sy, sw, sh, dx, dy, dw, dh };
	o->r[o->n++] = r;
}

/* the whole Game Boy picture, 1.5x, centred */
static void full(Overlay *o) {
	o->n = 0;
	add(o, 0, 0, 160, 144, 80, 12, 240, 216);
	o->dim = true;
}

/* Game Boy rows [row0, 18) at `scale`, centred along the bottom of the top screen */
static void bottom_rows_at(Overlay *o, int row0, float scale) {
	float h = (144 - row0 * 8) * scale, w = 160 * scale;
	add(o, 0, row0 * 8, 160, 144 - row0 * 8, (400 - w) / 2, 240 - h, w, h);
}

static void bottom_rows(Overlay *o, int row0) { bottom_rows_at(o, row0, 2.0f); }

void overlay_plan(const GoldView *g, bool field, bool battle3d, Overlay *o) {
	o->n = 0; o->dim = false;
	bool textbox = tile(g, 12, 0) == 0x79 && tile(g, 17, 0) == 0x7D;

	if (battle3d) {
		bool hp_boxes = tile(g, 2, 1) == 0x6D;                     /* the opponent's HP box is drawn */
		bool their_pic = tile(g, 0, 12) == 0x00 && tile(g, 1, 12) == 0x01;   /* the battle scene, not a menu */
		/* the move list: a TYPE/PP box (rows 8-12) sits on top of the move box (rows 12-17) */
		bool move_list = hp_boxes && tile(g, 8, 0) == 0x79 && tile(g, 12, 0) == 0x7D && tile(g, 17, 19) == 0x7E;
		if (move_list) {
			add(o, 0, 0, 96, 32, 6, 6, 144, 48);                     /* theirs */
			bottom_rows_at(o, 8, 1.5f);                              /* TYPE/PP, our HP and the moves */
		} else if (hp_boxes && textbox) {
			add(o, 0, 0, 96, 32, 6, 6, 144, 48);                     /* theirs: name, level, HP (1.5x) */
			if (tile(g, 9, 10) == 0x60)                              /* ours is out: its HP bar starts here */
				add(o, 72, 56, 88, 40, 400 - 132 - 6, 240 - 96 - 60 - 2, 132, 60);   /* ours: name, level, HP numbers */
			bottom_rows(o, 12);                                      /* text, FIGHT/PKMN/PACK/RUN, moves */
		} else if (their_pic && textbox) {
			bottom_rows(o, 12);                                      /* "Wild SENTRET appeared!" */
		} else {
			full(o);                                                 /* party, bag, move learning... */
		}
		return;
	}
	if (!field) return;

	/* any text-box tiles above the bottom box? find the highest row they reach */
	int top = 18, count = 0;
	for (int r = 0; r < 12; r++)
		for (int c = 0; c < 20; c++)
			if (texty(tile(g, r, c))) { count++; if (r < top) top = r; }

	if (count == 0) {
		if (textbox) bottom_rows(o, 12);
	} else if (top >= 3 && textbox) {
		bottom_rows(o, top);       /* YES/NO and other small boxes sit just above the text */
	} else {
		full(o);                   /* START menu and full-screen menus */
	}
}
