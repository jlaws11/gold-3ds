/* 3D battles: when a battle starts, the camera swings down behind the player
 * and both Pokémon appear standing in the overworld, using FireRed's pictures.
 * The bottom screen keeps Gold's own battle screen (moves, HP, text).
 * Platform independent: produces quads; the caller loads pictures. */
#pragma once
#include "world.h"

#define BATTLE_TEX_W 128   /* enemy picture in the left half, ours in the right */
#define BATTLE_TEX_H 64

typedef struct {
	bool active;
	int mode;                       /* wBattleMode: 1 wild, 2 trainer */
	float cx, cz, dx, dz;           /* player centre and facing at the start */
	float ex, ez, ax, az;           /* enemy / ally offsets from the player centre */
	int frames;
	/* what is on the field, read from Gold each frame */
	int enemy, ally;                /* species shown, 0 = none */
	bool enemy_shiny, ally_shiny, ally_is_red;
	int start_enemy;                /* wEnemyMonSpecies when the battle began (may be stale) */
	/* animation */
	float enemy_in, ally_in;        /* 0..1 send-out grow */
	float enemy_sink, ally_sink;    /* faint: slides into the ground */
	int enemy_shake, ally_shake;
	int enemy_hp, ally_hp;
	int enemy_bottom, ally_bottom;  /* lowest picture row, set by the caller after loading */
} Battle;

/* What the battle needs drawn; compare with the last loaded to know when to reload. */
typedef struct { int species; bool back, shiny, red; } BattlePic;

void battle_begin(Battle *b, const GoldView *g, float player_x, float player_z);
void battle_update(Battle *b, const GoldView *g);
BattlePic battle_enemy_pic(const Battle *b);
BattlePic battle_ally_pic(const Battle *b);

/* Camera for the battle (look-at point, yaw around it, pitch, distance). */
void battle_camera(const Battle *b, float *tx, float *tz, float *yaw, float *pitch, float *dist);

/* Billboards facing the camera yaw, plus round shadows (blob texture). */
int battle_quads(const Battle *b, float yaw, WVert *out, int cap);
int battle_shadows(const Battle *b, WVert *out, int cap);
