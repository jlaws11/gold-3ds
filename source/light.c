#include "light.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct { float h; float r, g, b; } Key;

/* scene tint on top of Gold's own palettes (which already dim at night) */
static const Key ambient_keys[] = {
	{  0.0f, 0.80f, 0.84f, 1.00f },
	{  4.5f, 0.82f, 0.84f, 1.00f },
	{  6.0f, 1.00f, 0.86f, 0.76f },   /* dawn */
	{  8.0f, 1.00f, 0.97f, 0.92f },
	{ 12.0f, 1.00f, 1.00f, 1.00f },
	{ 16.0f, 1.00f, 0.97f, 0.90f },
	{ 17.8f, 1.00f, 0.78f, 0.58f },   /* sunset */
	{ 19.3f, 0.82f, 0.82f, 1.00f },
	{ 24.0f, 0.80f, 0.84f, 1.00f },
};
static const Key sky_keys[] = {
	{  0.0f, 0.09f, 0.11f, 0.24f },
	{  4.5f, 0.16f, 0.17f, 0.34f },
	{  6.0f, 0.95f, 0.70f, 0.55f },
	{  8.0f, 0.62f, 0.80f, 0.92f },
	{ 12.0f, 0.52f, 0.75f, 0.92f },
	{ 16.0f, 0.60f, 0.76f, 0.90f },
	{ 17.8f, 0.96f, 0.56f, 0.36f },
	{ 19.3f, 0.22f, 0.20f, 0.40f },
	{ 24.0f, 0.09f, 0.11f, 0.24f },
};

static void sample(const Key *k, int n, float h, float out[3]) {
	for (int i = 0; i < n - 1; i++) {
		if (h >= k[i].h && h <= k[i + 1].h) {
			float t = (h - k[i].h) / (k[i + 1].h - k[i].h);
			out[0] = k[i].r + (k[i + 1].r - k[i].r) * t;
			out[1] = k[i].g + (k[i + 1].g - k[i].g) * t;
			out[2] = k[i].b + (k[i + 1].b - k[i].b) * t;
			return;
		}
	}
	out[0] = k[0].r; out[1] = k[0].g; out[2] = k[0].b;
}

static float smooth(float a, float b, float x) {
	float t = (x - a) / (b - a);
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	return t * t * (3 - 2 * t);
}

static void set_dir(Light *L, float x, float y, float z) {
	float l = sqrtf(x * x + y * y + z * z);
	L->sun[0] = x / l; L->sun[1] = y / l; L->sun[2] = z / l;
}

int light_env(uint8_t e) {
	switch (e) {
	case 1: case 2: case 5: return ENV_OUTDOOR;   /* TOWN, ROUTE, ENVIRONMENT_5 */
	case 4: case 7: return ENV_CAVE;              /* CAVE, DUNGEON */
	default: return ENV_INDOOR;                   /* INDOOR, GATE */
	}
}

void light_compute(int hour, int minute, int env, Light *L) {
	float h = (hour % 24) + (minute % 60) / 60.0f;

	if (env != ENV_OUTDOOR) {
		/* rooms: soft light from above and slightly behind the camera */
		set_dir(L, 0.25f, 1.0f, 0.45f);
		L->shadow = env == ENV_CAVE ? 0.45f : 0.30f;
		L->ambient[0] = L->ambient[1] = L->ambient[2] = 1.0f;
		L->glow[0] = L->glow[1] = L->glow[2] = 0.0f;
		L->sky = env == ENV_CAVE ? 0x08080CFF : 0x14161CFF;
		L->key = 1000 + env;
		return;
	}

	/* sun rises in the east at 5:00, sets in the west at 19:00. It passes a little
	 * north of overhead so shadows fall toward the camera, where they can be seen. */
	float p = (h - 5.0f) / 14.0f;
	float day = smooth(4.6f, 6.0f, h) * (1.0f - smooth(18.2f, 19.4f, h));
	if (p > 0.0f && p < 1.0f) {
		float el = (12.0f + 52.0f * sinf((float)M_PI * p)) * (float)M_PI / 180.0f;
		float ax = cosf((float)M_PI * p), az = -0.45f * sinf((float)M_PI * p);
		float al = sqrtf(ax * ax + az * az);
		set_dir(L, ax / al * cosf(el), sinf(el), az / al * cosf(el));
	} else {
		set_dir(L, -0.35f, 1.0f, -0.4f);   /* moon */
	}
	L->shadow = 0.18f + 0.27f * day;

	sample(ambient_keys, sizeof ambient_keys / sizeof ambient_keys[0], h, L->ambient);
	float sky[3];
	sample(sky_keys, sizeof sky_keys / sizeof sky_keys[0], h, sky);
	L->sky = ((uint32_t)(sky[0] * 255) << 24) | ((uint32_t)(sky[1] * 255) << 16) | ((uint32_t)(sky[2] * 255) << 8) | 0xFF;

	float lamps = 1.0f - day;
	L->glow[0] = 0.95f * lamps; L->glow[1] = 0.70f * lamps; L->glow[2] = 0.30f * lamps;

	/* rebuild baked shadows every 20 game minutes */
	L->key = (int)(h * 3.0f);
}
