#pragma once
#include "world.h"

typedef struct {
	float x, z;          /* world point the camera looks at (player square top-left) */
	float pitch, dist;   /* fixed angle looking north, like the original game */
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

/* One frame. voxel=false shows the Game Boy picture on the top screen instead. */
void render_frame(bool voxel, const Camera *cam, int sprite_verts, int shadow_verts, const Light *light);
