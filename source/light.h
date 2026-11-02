/* Sun, shadows and day/night colour, driven by the game's own clock.
 * Gold already swaps its palettes at 4:00 (morning), 10:00 (day) and 18:00
 * (night); this adds a moving sun, cast shadows, a sky that changes through the
 * day, warm dawn and sunset light, and lit windows after dark. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
	float sun[3];        /* unit vector toward the light (x east, y up, z south) */
	float shadow;        /* how dark cast shadows are, 0..1 */
	float ambient[3];    /* colour multiplier for the whole scene */
	float glow[3];       /* colour added to lit windows */
	uint32_t sky;        /* RGBA clear colour */
	int key;             /* changes when baked shadows need rebuilding */
} Light;

enum { ENV_OUTDOOR, ENV_INDOOR, ENV_CAVE };

void light_compute(int hour, int minute, int env, Light *out);
int light_env(uint8_t wEnvironment_value);
