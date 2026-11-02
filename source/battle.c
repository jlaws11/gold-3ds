#include "battle.h"
#include <math.h>
#include <string.h>

#define PIC_SCALE 0.8f        /* FireRed pictures are 64 px; 0.8 keeps them a little over Red's height */
#define SINK_MAX  (64 * PIC_SCALE)

static void stage(Battle *b);

static int be16(const GoldView *g, uint16_t a) { return (gv8(g, a) << 8) | gv8(g, a + 1); }

/* Gold's shiny rule: Defense, Speed and Special DVs of 10, Attack DV 2,3,6,7,10,11,14 or 15 */
static bool shiny(const GoldView *g, uint16_t dvs) {
	uint8_t a = gv8(g, dvs), b = gv8(g, dvs + 1);
	return (a & 15) == 10 && (b >> 4) == 10 && (b & 15) == 10 && ((a >> 4) & 2);
}

void battle_begin(Battle *b, const GoldView *g, float px, float pz) {
	memset(b, 0, sizeof *b);
	b->active = true;
	b->mode = gv8(g, wBattleMode);
	b->cx = px + 8; b->cz = pz + 8;
	switch (gv8(g, wPlayerDirection) & 0x0C) {
	case 0x04: b->dx = 0;  b->dz = -1; break;
	case 0x08: b->dx = -1; b->dz = 0;  break;
	case 0x0C: b->dx = 1;  b->dz = 0;  break;
	default:   b->dx = 0;  b->dz = 1;  break;
	}
	b->start_enemy = gv8(g, wEnemyMonSpecies);
	b->ally_is_red = true;
	stage(b);
}

static void positions(const Battle *b, float *ex, float *ez, float *ax, float *az) {
	*ax = b->cx + b->ax; *az = b->cz + b->az;
	*ex = b->cx + b->ex; *ez = b->cz + b->ez;
}

/* how clear a spot is: the square and a ring around it */
static int openness(float x, float z) {
	int n = 0;
	for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) n += world_open(x + i * 14, z + j * 14);
	return n;
}

/* Stage the fight: our Pokémon next to the player, the opponent a few squares away
 * on the clearest side, everything filmed from the south like the overworld. */
static void stage(Battle *b) {
	static const float spots[][2] = {   /* enemy offsets from the player, best first */
		{ 24, -56 }, { -24, -56 }, { 0, -60 }, { 60, -24 }, { -60, -24 },
		{ 64, 0 }, { -64, 0 }, { 56, 24 }, { -56, 24 },
	};
	int best = 0, score = -1;
	for (unsigned i = 0; i < sizeof spots / sizeof spots[0]; i++) {
		int s = openness(b->cx + spots[i][0], b->cz + spots[i][1]) * 4 - (int)i;   /* prefer earlier spots */
		/* trainers: favour the side they are on (the way the player faces) */
		if (spots[i][0] * b->dx + spots[i][1] * b->dz > 0) s += 6;
		if (s > score) { score = s; best = i; }
	}
	b->ex = spots[best][0]; b->ez = spots[best][1];
	/* our Pokémon steps out a little toward the opponent, on the camera side */
	float l = sqrtf(b->ex * b->ex + b->ez * b->ez);
	b->ax = b->ex / l * 6 - (b->ex > 0 ? 6 : -6); b->az = b->ez / l * 6 + 12;
}

void battle_update(Battle *b, const GoldView *g) {
	b->frames++;
	int en = gv8(g, wEnemyMonSpecies), al = gv8(g, wBattleMonSpecies);
	/* wild Pokémon are there from the start; a trainer's appears once it is sent out */
	bool enemy_out = b->mode == 1 || en != b->start_enemy || al != 0;
	if (!enemy_out || en < 1 || en > 251) en = 0;
	if (al < 1 || al > 251) al = 0;

	int ehp = be16(g, wEnemyMonHP), ahp = be16(g, wBattleMonHP);
	if (en != b->enemy) { b->enemy = en; b->enemy_in = 0; b->enemy_sink = 0; b->enemy_hp = ehp; }
	if (al != b->ally)  { b->ally = al;  b->ally_in = 0;  b->ally_sink = 0;  b->ally_hp = ahp; }
	b->ally_is_red = al == 0;
	b->enemy_shiny = en && shiny(g, wEnemyMonDVs);
	b->ally_shiny = al && shiny(g, wBattleMonDVs);

	if (b->enemy_in < 1) b->enemy_in = fminf(1, b->enemy_in + 1.0f / 14);
	if (b->ally_in < 1)  b->ally_in  = fminf(1, b->ally_in + 1.0f / 14);
	if (en && ehp < b->enemy_hp) b->enemy_shake = 18;   /* took a hit */
	if (al && ahp < b->ally_hp)  b->ally_shake = 18;
	b->enemy_hp = ehp; b->ally_hp = ahp;
	if (b->enemy_shake) b->enemy_shake--;
	if (b->ally_shake)  b->ally_shake--;
	if (en && ehp == 0) b->enemy_sink = fminf(SINK_MAX, b->enemy_sink + 1.8f);   /* fainted */
	if (al && ahp == 0) b->ally_sink  = fminf(SINK_MAX, b->ally_sink + 1.8f);
}

BattlePic battle_enemy_pic(const Battle *b) {
	BattlePic p = { b->enemy, false, b->enemy_shiny, false };
	return p;
}

BattlePic battle_ally_pic(const Battle *b) {
	BattlePic p = { b->ally, true, b->ally_shiny, b->ally_is_red };
	return p;
}

void battle_camera(const Battle *b, float *tx, float *tz, float *out_yaw, float *pitch, float *dist) {
	float ex, ez, ax, az;
	positions(b, &ex, &ez, &ax, &az);
	*tx = (ex + ax) / 2; *tz = (ez + az) / 2 + 6;
	/* Look roughly from behind ours toward theirs, but swung to the side so they stand
	 * apart on screen: ours lower-left, theirs upper-right, like the handheld games. */
	float vx = ex - ax, vz = ez - az;
	float behind = atan2f(-vx, -vz);           /* camera straight behind ours */
	float yaw = behind - 0.62f;
	float rx = cosf(yaw), rz = -sinf(yaw);
	if (rx * vx + rz * vz < 0) yaw = behind + 0.62f;   /* keep theirs on the right */
	/* stay mostly south-facing so the view matches the overworld's lighting and layout */
	if (yaw > 1.1f) yaw = 1.1f;
	if (yaw < -1.1f) yaw = -1.1f;
	*out_yaw = yaw;
	*pitch = 0.52f;
	*dist = 138.0f;
}

static int billboard(WVert *o, float x, float z, float yaw, float u0, float u1, int bottom,
                     float grow, float sink, int shake) {
	float rx = cosf(yaw), rz = -sinf(yaw);
	float size = 64 * PIC_SCALE * grow, half = size / 2;
	float wob = shake ? sinf(shake * 1.9f) * 3.0f : 0.0f;
	x += rx * wob; z += rz * wob;
	float yb = -(63 - bottom) * PIC_SCALE * grow - sink, yt = yb + size;
	float x0 = x - rx * half, z0 = z - rz * half, x1 = x + rx * half, z1 = z + rz * half;
	WVert q[6] = {
		{ x0, yt, z0, u0, 0, 1, 0 }, { x0, yb, z0, u0, 1, 1, 0 }, { x1, yb, z1, u1, 1, 1, 0 },
		{ x0, yt, z0, u0, 0, 1, 0 }, { x1, yb, z1, u1, 1, 1, 0 }, { x1, yt, z1, u1, 0, 1, 0 },
	};
	memcpy(o, q, sizeof q);
	return 6;
}

int battle_quads(const Battle *b, float yaw, WVert *out, int cap) {
	float ex, ez, ax, az;
	int n = 0;
	positions(b, &ex, &ez, &ax, &az);
	if (b->enemy && n + 6 <= cap)
		n += billboard(out + n, ex, ez, yaw, 0.0f, 0.5f, b->enemy_bottom, b->enemy_in, b->enemy_sink, b->enemy_shake);
	if ((b->ally || b->ally_is_red) && n + 6 <= cap)
		n += billboard(out + n, ax, az, yaw, 0.5f, 1.0f, b->ally_bottom, b->ally_is_red ? 1 : b->ally_in, b->ally_sink, b->ally_shake);
	return n;
}

static int blob(WVert *o, float x, float z, float r) {
	float x0 = x - r, x1 = x + r, z0 = z - r * 0.6f, z1 = z + r * 0.6f, y = 0.35f;
	WVert q[6] = {
		{ x0, y, z0, 0, 0, 0, 0 }, { x0, y, z1, 0, 1, 0, 0 }, { x1, y, z1, 1, 1, 0, 0 },
		{ x0, y, z0, 0, 0, 0, 0 }, { x1, y, z1, 1, 1, 0, 0 }, { x1, y, z0, 1, 0, 0, 0 },
	};
	memcpy(o, q, sizeof q);
	return 6;
}

int battle_shadows(const Battle *b, WVert *out, int cap) {
	float ex, ez, ax, az;
	int n = 0;
	positions(b, &ex, &ez, &ax, &az);
	if (b->enemy && b->enemy_sink < SINK_MAX && n + 6 <= cap) n += blob(out + n, ex, ez, 20 * b->enemy_in);
	if (b->ally && b->ally_sink < SINK_MAX && n + 6 <= cap) n += blob(out + n, ax, az, 18 * b->ally_in);
	return n;
}
