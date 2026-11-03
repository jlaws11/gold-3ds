/* Single-screen mode: which parts of the Game Boy picture to show on the top
 * screen over the 3D view, so the game can be played without the bottom screen
 * (Azahar's single-screen layout, a Steam Deck, a TV).
 *
 * Decided each frame from Gold's screen buffer (wTilemap, 20x18 tiles):
 *  - nothing open: 3D only
 *  - a text box, and small boxes above it (YES/NO, quantity): those rows at 2x along the bottom
 *  - big menus and full screens (START, party, bag, Pokédex): the whole picture, 3D dimmed behind
 *  - 3D battles: the HP boxes as a HUD plus the text/move box along the bottom */
#pragma once
#include "gold.h"


typedef struct { float sx, sy, sw, sh;  /* source rectangle in Game Boy pixels */
                 float dx, dy, dw, dh;  /* destination on the 400x240 top screen */ } OverlayRect;

typedef struct {
	int n;
	OverlayRect r[4];
	bool dim;          /* darken the 3D behind */
} Overlay;

/* field: the 3D overworld is on screen; battle3d: a 3D battle is on screen */
void overlay_plan(const GoldView *g, bool field, bool battle3d, Overlay *o);
