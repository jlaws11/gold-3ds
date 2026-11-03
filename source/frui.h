/* FireRed-style interface for single-screen mode.
 *
 * Gold's own windows and battle screens are redrawn in FireRed's look: its
 * font, dialogue box, menu frame, battle text panels and HP boxes. All of it is
 * read from the player's own FireRed/LeafGreen ROM at startup, and nothing from
 * that game ships with Gold 3DS. Gold still runs its menus: the text, cursor and
 * numbers are read from Gold's screen buffer and battle memory each frame.
 *
 * Everything is drawn at the top screen's full resolution (400x240), like
 * Emerald 3DS's HD battle interface. */
#pragma once
#include "gold.h"
#include "frpics.h"

#define FRUI_TEX 512            /* the UI atlas is FRUI_TEX x FRUI_TEX RGBA */
#define FRUI_MAX_QUADS 900

typedef struct {
	float x, y, w, h;           /* on the 400x240 top screen */
	float u0, v0, u1, v1;       /* atlas pixels */
} UiQuad;

typedef struct {
	int n;
	UiQuad q[FRUI_MAX_QUADS];
} UiList;

typedef enum {
	FRUI_NONE,                  /* nothing to show (just the 3D) */
	FRUI_DRAWN,                 /* the list holds the FireRed-style interface */
	FRUI_FALLBACK               /* a screen this can't restyle: show Gold's picture */
} FruiResult;

/* Decodes the interface graphics into atlas (FRUI_TEX^2 RGBA). False if the ROM
 * isn't a supported FireRed/LeafGreen or a read fails. */
bool frui_load(FrRead rd, void *ctx, const FrTables *t, uint32_t *atlas);

/* Lays out this frame's interface. field: the 3D overworld is on screen;
 * battle3d: a 3D battle is on screen. */
FruiResult frui_plan(const GoldView *g, bool field, bool battle3d, UiList *out);
