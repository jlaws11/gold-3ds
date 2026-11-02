#pragma once
#include "world.h"
#include "battle.h"
#include "overlay.h"

typedef struct {
	float tx, tz;        /* world point the camera looks at */
	float yaw;           /* 0 = looking north like the original game; battles turn it a little */
	float pitch, dist;
} Camera;

void render_init(void);
void render_exit(void);

WorldMesh *render_mesh(void);          /* the mesh buffer (vertices live in GPU-visible memory) */
void render_mesh_uploaded(void);       /* call after world_build() wrote into render_mesh() */
void render_bg_atlas(const uint32_t *rgba);
void render_obj_atlas(const uint32_t *rgba);
void render_lcd(const uint8_t *lcd);   /* 160x144 shades from core_lcd() */
WVert *render_sprite_buf(int *cap);
WVert *render_shadow_buf(int *cap);
WVert *render_player_buf(void);              /* 6 vertices for the tall player */
void render_tall_sheet(const uint32_t *rgba); /* TALL_TEX_W x TALL_TEX_H */
WVert *render_battle_buf(void);               /* 12 vertices: the two Pokémon */
void render_battle_sheet(const uint32_t *rgba); /* BATTLE_TEX_W x BATTLE_TEX_H */

/* One frame. voxel=false shows the Game Boy picture on the top screen instead.
 * ov (single-screen mode, may be NULL): parts of the Game Boy picture drawn over the 3D. */
void render_frame(bool voxel, const Camera *cam, int sprite_verts, int shadow_verts, int player_verts, int battle_verts,
                  const Light *light, const Overlay *ov);
