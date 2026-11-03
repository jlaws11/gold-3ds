#include <3ds.h>
#include <citro3d.h>
#include <string.h>
#include <math.h>
#include "render.h"
#include "scene_shbin.h"
#include "tallsprite.h"
#include "overlay.h"

#define DISPLAY_TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
	 GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
	 GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

#define MESH_CAP   360000   /* the largest Gold map (Ilex Forest) needs about 295,000 */
#define SPRITE_CAP (40 * 6)

static C3D_RenderTarget *top_l, *top_r, *bottom;
static DVLB_s *dvlb;
static shaderProgram_s prog;
static int u_proj, u_mv, u_amb, u_glow;
static C3D_Tex tex_bg, tex_obj, tex_lcd, tex_blob, tex_tall, tex_battle, tex_dim, tex_ui;
static C3D_Tex tex_lcd_cut, tex_pic, tex_icons;   /* full screens: Gold's art with its lightest shade see-through, a Pokémon picture, party icons */
static WVert *screen_dim;
static bool ui_ready;
static WVert *ui_quads;   /* double-buffered like the overlay */
static WorldMesh meshes[2];   /* drawn one and the one being built (when memory allows) */
static int cur_mesh;
static bool two_meshes, mesh_hidden;
static float mesh_dx, mesh_dz;  /* shift for drawing the old map while the new one builds */
#define mesh (meshes[cur_mesh])
static WVert *spr, *shadows, *player, *battle_q, *screen_quads, *ov_quads;
static int ov_flip;
static uint32_t *lcd_rgba, *lcd_cut;

/* PICA200 textures are stored in 8x8 tiles with Morton (Z-order) texels. */
static inline uint32_t tiled_index(int x, int y, int w) {
	uint32_t m = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
	return (((y >> 3) * (w >> 3) + (x >> 3)) << 6) + m;
}

static void upload(C3D_Tex *t, const uint32_t *rgba, int w, int h, int stride) {
	uint32_t *dst = (uint32_t *)t->data;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			dst[tiled_index(x, y, t->width)] = rgba[y * stride + x];
	C3D_TexFlush(t);
}

static void tex_new(C3D_Tex *t, int w, int h) {
	C3D_TexInit(t, w, h, GPU_RGBA8);
	memset(t->data, 0, t->size);
	C3D_TexSetFilter(t, GPU_NEAREST, GPU_NEAREST);
	C3D_TexSetWrap(t, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
}

static void set_quad(WVert *q, float x0, float y0, float x1, float y1, float u1, float v1) {
	WVert v[6] = {
		{ x0, y0, 0, 0,  0,  1, 0 }, { x0, y1, 0, 0,  v1, 1, 0 }, { x1, y1, 0, u1, v1, 1, 0 },
		{ x0, y0, 0, 0,  0,  1, 0 }, { x1, y1, 0, u1, v1, 1, 0 }, { x1, y0, 0, u1, 0,  1, 0 },
	};
	memcpy(q, v, sizeof v);
}

static void quad_uv(WVert *q, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1);

void render_init(void) {
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	top_l  = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	top_r  = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	bottom = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(top_l,  GFX_TOP,    GFX_LEFT,  DISPLAY_TRANSFER_FLAGS);
	C3D_RenderTargetSetOutput(top_r,  GFX_TOP,    GFX_RIGHT, DISPLAY_TRANSFER_FLAGS);
	C3D_RenderTargetSetOutput(bottom, GFX_BOTTOM, GFX_LEFT,  DISPLAY_TRANSFER_FLAGS);

	dvlb = DVLB_ParseFile((u32 *)scene_shbin, scene_shbin_size);
	shaderProgramInit(&prog);
	shaderProgramSetVsh(&prog, &dvlb->DVLE[0]);
	C3D_BindProgram(&prog);
	u_proj = shaderInstanceGetUniformLocation(prog.vertexShader, "projection");
	u_mv   = shaderInstanceGetUniformLocation(prog.vertexShader, "modelView");
	u_amb  = shaderInstanceGetUniformLocation(prog.vertexShader, "ambient");
	u_glow = shaderInstanceGetUniformLocation(prog.vertexShader, "glowClr");

	C3D_AttrInfo *ai = C3D_GetAttrInfo();
	AttrInfo_Init(ai);
	AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3);   /* position */
	AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 2);   /* texcoord */
	AttrInfo_AddLoader(ai, 2, GPU_FLOAT, 2);   /* shade, glow */

	C3D_TexEnv *env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
	C3D_AlphaTest(true, GPU_GREATER, 0x80);
	C3D_CullFace(GPU_CULL_NONE);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);

	tex_new(&tex_bg, BG_ATLAS_W, BG_ATLAS_H);
	tex_new(&tex_obj, 128, 128);
	tex_new(&tex_lcd, 256, 256);
	tex_new(&tex_lcd_cut, 256, 256);
	tex_new(&tex_pic, 64, 64);
	tex_new(&tex_icons, 256, 32);
	tex_new(&tex_blob, 16, 16);
	tex_new(&tex_tall, TALL_TEX_W, TALL_TEX_H);
	tex_new(&tex_battle, BATTLE_TEX_W, BATTLE_TEX_H);
	tex_new(&tex_dim, 8, 8);
	ui_quads = linearAlloc(sizeof(WVert) * 6 * FRUI_MAX_QUADS * 2);
	{   /* translucent black, to darken the 3D behind a full-screen menu */
		static uint32_t dim[64];
		for (int i = 0; i < 64; i++) dim[i] = RGBA(0, 0, 0, 150);
		upload(&tex_dim, dim, 8, 8, 8);
	}
	{   /* soft round shadow, darkest in the middle */
		static uint32_t blob[16 * 16];
		for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
			float dx = (x - 7.5f) / 8.0f, dy = (y - 7.5f) / 8.0f, r = sqrtf(dx * dx + dy * dy);
			float a = r >= 1 ? 0 : (1 - r * r) * 0.55f;
			blob[y * 16 + x] = RGBA(0, 0, 0, (int)(a * 255));
		}
		C3D_TexSetFilter(&tex_blob, GPU_LINEAR, GPU_LINEAR);
		upload(&tex_blob, blob, 16, 16, 16);
	}

	for (int i = 0; i < 2; i++) {
		meshes[i].cap = MESH_CAP;
		meshes[i].verts = linearAlloc(sizeof(WVert) * MESH_CAP);
		meshes[i].count = 0; meshes[i].sq_w = meshes[i].sq_h = 0; meshes[i].key = 0;
	}
	two_meshes = meshes[1].verts != NULL;
	if (!meshes[0].verts) svcBreak(USERBREAK_PANIC);
	spr = linearAlloc(sizeof(WVert) * SPRITE_CAP);
	shadows = linearAlloc(sizeof(WVert) * SPRITE_CAP);
	player = linearAlloc(sizeof(WVert) * 6);
	battle_q = linearAlloc(sizeof(WVert) * 12);
	screen_quads = linearAlloc(sizeof(WVert) * 12);
	ov_quads = linearAlloc(sizeof(WVert) * 2 * 30);   /* double-buffered: written while the GPU may read the other half */
	lcd_rgba = linearAlloc(160 * 144 * 4);
	lcd_cut = linearAlloc(160 * 144 * 4);
	screen_dim = linearAlloc(sizeof(WVert) * 6);
	quad_uv(screen_dim, 0, 0, 400, 240, 0, 0, 1, 1);
	GSPGPU_FlushDataCache(screen_dim, sizeof(WVert) * 6);
	/* Game Boy picture at 1.5x: 240x216, centred on each screen */
	set_quad(screen_quads,     80, 12, 320, 228, 160 / 256.0f, 144 / 256.0f);
	set_quad(screen_quads + 6, 40, 12, 280, 228, 160 / 256.0f, 144 / 256.0f);
	GSPGPU_FlushDataCache(screen_quads, sizeof(WVert) * 12);
}

void render_exit(void) {
	C3D_TexDelete(&tex_bg); C3D_TexDelete(&tex_obj); C3D_TexDelete(&tex_lcd); C3D_TexDelete(&tex_blob); C3D_TexDelete(&tex_tall); C3D_TexDelete(&tex_battle); C3D_TexDelete(&tex_dim); linearFree(ov_quads); linearFree(ui_quads);
	C3D_TexDelete(&tex_lcd_cut); C3D_TexDelete(&tex_pic); C3D_TexDelete(&tex_icons); linearFree(lcd_cut); linearFree(screen_dim);
	if (ui_ready) C3D_TexDelete(&tex_ui);
	linearFree(player); linearFree(battle_q);
	linearFree(meshes[0].verts); if (meshes[1].verts) linearFree(meshes[1].verts); linearFree(spr); linearFree(shadows); linearFree(screen_quads); linearFree(lcd_rgba);
	shaderProgramFree(&prog);
	DVLB_Free(dvlb);
	C3D_Fini();
}

WorldMesh *render_mesh(void) { return &mesh; }
WorldMesh *render_mesh_back(void) { return two_meshes ? &meshes[cur_mesh ^ 1] : &mesh; }
bool render_can_build_behind(void) { return two_meshes; }
void render_mesh_uploaded(void) { GSPGPU_FlushDataCache(mesh.verts, sizeof(WVert) * mesh.count); }
void render_mesh_swap(void) {
	WorldMesh *b = render_mesh_back();
	GSPGPU_FlushDataCache(b->verts, sizeof(WVert) * b->count);
	if (two_meshes) cur_mesh ^= 1;
	mesh_dx = mesh_dz = 0; mesh_hidden = false;
}
void render_mesh_shift(float dx, float dz) { mesh_dx = dx; mesh_dz = dz; }
void render_mesh_hide(bool hide) { mesh_hidden = hide; }
void render_bg_atlas(const uint32_t *rgba) { upload(&tex_bg, rgba, BG_ATLAS_W, BG_ATLAS_H, BG_ATLAS_W); }
void render_obj_atlas(const uint32_t *rgba) { upload(&tex_obj, rgba, 128, 128, 128); }
WVert *render_sprite_buf(int *cap) { *cap = SPRITE_CAP; return spr; }
WVert *render_shadow_buf(int *cap) { *cap = SPRITE_CAP; return shadows; }
WVert *render_player_buf(void) { return player; }
void render_tall_sheet(const uint32_t *rgba) { upload(&tex_tall, rgba, TALL_TEX_W, TALL_TEX_H, TALL_TEX_W); }
WVert *render_battle_buf(void) { return battle_q; }
void render_ui_pic(const uint32_t *rgba) { upload(&tex_pic, rgba, 64, 64, 64); }
void render_ui_icons(const uint32_t *rgba) { upload(&tex_icons, rgba, 256, 32, 256); }
void render_ui_atlas(const uint32_t *rgba) {
	if (!ui_ready) { tex_new(&tex_ui, FRUI_TEX, FRUI_TEX); ui_ready = true; }
	upload(&tex_ui, rgba, FRUI_TEX, FRUI_TEX, FRUI_TEX);
}
void render_battle_sheet(const uint32_t *rgba) { upload(&tex_battle, rgba, BATTLE_TEX_W, BATTLE_TEX_H, BATTLE_TEX_W); }

void render_lcd(const uint8_t *lcd) {
	/* soft cream-to-ink ramp until the colour core lands */
	static const uint32_t shade[4] = {
		RGBA(240, 238, 214, 255), RGBA(168, 186, 140, 255), RGBA(84, 104, 92, 255), RGBA(28, 30, 40, 255) };
	for (int i = 0; i < 160 * 144; i++) { lcd_rgba[i] = shade[lcd[i] & 3]; lcd_cut[i] = (lcd[i] & 3) ? lcd_rgba[i] : 0; }
	upload(&tex_lcd, lcd_rgba, 160, 144, 160);
	upload(&tex_lcd_cut, lcd_cut, 160, 144, 160);
}

static void use_buffer(WVert *base) {
	C3D_BufInfo *bi = C3D_GetBufInfo();
	BufInfo_Init(bi);
	BufInfo_Add(bi, base, sizeof(WVert), 3, 0x210);
}

static void draw_world(const Camera *cam, float iod, int sprite_verts, int shadow_verts, int player_verts, int battle_verts, const Light *L) {
	C3D_Mtx proj, mv;
	float focal = cam->dist;
	Mtx_PerspStereoTilt(&proj, C3D_AngleFromDegrees(42.0f), C3D_AspectRatioTop, 6.0f, 1600.0f, iod, focal, false);
	float tx = cam->tx, tz = cam->tz, ty = 6;
	float ex = tx + sinf(cam->yaw) * cosf(cam->pitch) * cam->dist;
	float ey = ty + sinf(cam->pitch) * cam->dist;
	float ez = tz + cosf(cam->yaw) * cosf(cam->pitch) * cam->dist;
	Mtx_LookAt(&mv, FVec3_New(ex, ey, ez), FVec3_New(tx, ty, tz), FVec3_New(0, 1, 0), false);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_proj, &proj);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_mv, &mv);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_amb, L->ambient[0], L->ambient[1], L->ambient[2], 1.0f);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_glow, L->glow[0], L->glow[1], L->glow[2], 0.0f);
	C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
	C3D_AlphaTest(true, GPU_GREATER, 0x80);

	if (mesh_hidden) return;   /* a warp: the new map is still being built */

	/* only the squares around the player: whole rows of the grid are contiguous */
	C3D_Mtx mvm = mv;
	Mtx_Translate(&mvm, mesh_dx, 0, mesh_dz, true);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_mv, &mvm);
	C3D_TexBind(0, &tex_bg);
	use_buffer(mesh.verts);
	int gx = (int)floorf((cam->tx - mesh_dx) / 16) + mesh.origin, gy = (int)floorf((cam->tz - mesh_dz) / 16) + mesh.origin;
	int spread = 14 + (int)(cam->dist / 60);
	/* looking north: most of the view is ahead; a turned (battle) camera needs all round */
	int behind = fabsf(cam->yaw) > 0.05f ? spread : 7;
	for (int y = gy - spread - 4; y <= gy + behind; y++) {
		int first, count;
		world_row_range(&mesh, y, gx - spread, gx + spread, &first, &count);
		if (count > 0) C3D_DrawArrays(GPU_TRIANGLES, first, count);
	}
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_mv, &mv);
	if (shadow_verts > 0) {   /* blended, no depth write, drawn before the sprites */
		C3D_TexBind(0, &tex_blob);
		use_buffer(shadows);
		C3D_AlphaTest(false, GPU_ALWAYS, 0);
		C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_COLOR);
		C3D_DrawArrays(GPU_TRIANGLES, 0, shadow_verts);
		C3D_AlphaTest(true, GPU_GREATER, 0x80);
		C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
	}
	if (sprite_verts > 0) {
		C3D_TexBind(0, &tex_obj);
		use_buffer(spr);
		C3D_DrawArrays(GPU_TRIANGLES, 0, sprite_verts);
	}
	if (player_verts > 0) {
		C3D_TexBind(0, &tex_tall);
		use_buffer(player);
		C3D_DrawArrays(GPU_TRIANGLES, 0, player_verts);
	}
	if (battle_verts > 0) {
		C3D_TexBind(0, &tex_battle);
		use_buffer(battle_q);
		C3D_DrawArrays(GPU_TRIANGLES, 0, battle_verts);
	}
}

static void draw_picture(bool top) {
	C3D_Mtx proj, id;
	Mtx_OrthoTilt(&proj, 0.0f, top ? 400.0f : 320.0f, 240.0f, 0.0f, 1.0f, -1.0f, true);
	Mtx_Identity(&id);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_proj, &proj);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_mv, &id);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_amb, 1, 1, 1, 1);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_glow, 0, 0, 0, 0);
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	C3D_TexBind(0, &tex_lcd);
	use_buffer(screen_quads);
	C3D_DrawArrays(GPU_TRIANGLES, top ? 0 : 6, 6);
}

static void quad_uv(WVert *q, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1) {
	WVert v[6] = {
		{ x0, y0, 0, u0, v0, 1, 0 }, { x0, y1, 0, u0, v1, 1, 0 }, { x1, y1, 0, u1, v1, 1, 0 },
		{ x0, y0, 0, u0, v0, 1, 0 }, { x1, y1, 0, u1, v1, 1, 0 }, { x1, y0, 0, u1, v0, 1, 0 },
	};
	memcpy(q, v, sizeof v);
}

/* Game Boy picture regions over the top screen (single-screen mode) */
static int build_overlay(const Overlay *ov, WVert *q, bool *dim) {
	int n = 0;
	*dim = ov && ov->dim;
	if (!ov) return 0;
	if (ov->dim) { quad_uv(q, 0, 0, 400, 240, 0, 0, 1, 1); n += 6; }
	for (int i = 0; i < ov->n; i++, n += 6) {
		const OverlayRect *r = &ov->r[i];
		quad_uv(q + n, r->dx, r->dy, r->dx + r->dw, r->dy + r->dh,
		        r->sx / 256.0f, r->sy / 256.0f, (r->sx + r->sw) / 256.0f, (r->sy + r->sh) / 256.0f);
	}
	return n;
}

static void draw_overlay(WVert *q, int n, bool dim) {
	if (n == 0) return;
	C3D_Mtx proj, id;
	Mtx_OrthoTilt(&proj, 0.0f, 400.0f, 240.0f, 0.0f, 1.0f, -1.0f, true);
	Mtx_Identity(&id);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_proj, &proj);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_mv, &id);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_amb, 1, 1, 1, 1);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_glow, 0, 0, 0, 0);
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	use_buffer(q);
	int first = 0;
	if (dim) {
		C3D_AlphaTest(false, GPU_ALWAYS, 0);
		C3D_TexBind(0, &tex_dim);
		C3D_DrawArrays(GPU_TRIANGLES, 0, 6);
		C3D_AlphaTest(true, GPU_GREATER, 0x80);
		first = 6;
	}
	C3D_TexBind(0, &tex_lcd);
	C3D_DrawArrays(GPU_TRIANGLES, first, n - first);
}

/* the FireRed-style interface: quads over several textures, in top-screen pixels */
static uint8_t ui_tex[FRUI_MAX_QUADS];
static bool ui_dim;

static int build_ui(const UiList *ui, WVert *q) {
	if (!ui || !ui_ready) return 0;
	ui_dim = ui->dim;
	for (int i = 0; i < ui->n; i++) {
		const UiQuad *u = &ui->q[i];
		float kw, kh;
		switch (u->tex) {
		case UI_TEX_LCD:   kw = kh = 1.0f / 256; break;
		case UI_TEX_PIC:   kw = kh = 1.0f / 64; break;
		case UI_TEX_ICONS: kw = 1.0f / 256; kh = 1.0f / 32; break;
		default:           kw = kh = 1.0f / FRUI_TEX; break;
		}
		ui_tex[i] = u->tex;
		quad_uv(q + i * 6, u->x, u->y, u->x + u->w, u->y + u->h, u->u0 * kw, u->v0 * kh, u->u1 * kw, u->v1 * kh);
	}
	return ui->n * 6;
}

static C3D_Tex *ui_texture(int t) {
	switch (t) {
	case UI_TEX_LCD:   return &tex_lcd_cut;
	case UI_TEX_PIC:   return &tex_pic;
	case UI_TEX_ICONS: return &tex_icons;
	default:           return &tex_ui;
	}
}

static void draw_ui(WVert *q, int n) {
	if (n == 0) return;
	C3D_Mtx proj, id;
	Mtx_OrthoTilt(&proj, 0.0f, 400.0f, 240.0f, 0.0f, 1.0f, -1.0f, true);
	Mtx_Identity(&id);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_proj, &proj);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_mv, &id);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_amb, 1, 1, 1, 1);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_glow, 0, 0, 0, 0);
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	if (ui_dim) {   /* darken the world behind a full screen */
		use_buffer(screen_dim);
		C3D_AlphaTest(false, GPU_ALWAYS, 0);
		C3D_TexBind(0, &tex_dim);
		C3D_DrawArrays(GPU_TRIANGLES, 0, 6);
	}
	C3D_AlphaTest(true, GPU_GREATER, 0x80);
	use_buffer(q);
	int quads = n / 6, i = 0;
	while (i < quads) {   /* runs of quads on the same texture */
		int j = i;
		while (j < quads && ui_tex[j] == ui_tex[i]) j++;
		C3D_TexBind(0, ui_texture(ui_tex[i]));
		C3D_DrawArrays(GPU_TRIANGLES, i * 6, (j - i) * 6);
		i = j;
	}
}

void render_frame(bool voxel, const Camera *cam, int sprite_verts, int shadow_verts, int player_verts, int battle_verts, const Light *L, const Overlay *ov, const UiList *ui) {
	uint32_t sky = L->sky;
	ov_flip ^= 1;
	WVert *oq = ov_quads + ov_flip * 30;
	bool dim;
	int ovn = voxel ? build_overlay(ov, oq, &dim) : 0;
	if (ovn) GSPGPU_FlushDataCache(oq, sizeof(WVert) * ovn);
	WVert *uq = ui_quads + ov_flip * 6 * FRUI_MAX_QUADS;
	int uin = build_ui(ui, uq);
	if (uin) GSPGPU_FlushDataCache(uq, sizeof(WVert) * uin);
	if (sprite_verts > 0) GSPGPU_FlushDataCache(spr, sizeof(WVert) * sprite_verts);
	if (shadow_verts > 0) GSPGPU_FlushDataCache(shadows, sizeof(WVert) * shadow_verts);
	if (player_verts > 0) GSPGPU_FlushDataCache(player, sizeof(WVert) * player_verts);
	if (battle_verts > 0) GSPGPU_FlushDataCache(battle_q, sizeof(WVert) * battle_verts);
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
	if (voxel) {
		float iod = osGet3DSliderState() * cam->dist / 24.0f;
		C3D_RenderTargetClear(top_l, C3D_CLEAR_ALL, sky, 0);
		C3D_FrameDrawOn(top_l);
		draw_world(cam, -iod, sprite_verts, shadow_verts, player_verts, battle_verts, L);
		draw_overlay(oq, ovn, dim);
		draw_ui(uq, uin);
		if (iod > 0.0f) {
			C3D_RenderTargetClear(top_r, C3D_CLEAR_ALL, sky, 0);
			C3D_FrameDrawOn(top_r);
			draw_world(cam, iod, sprite_verts, shadow_verts, player_verts, battle_verts, L);
			draw_overlay(oq, ovn, dim);
			draw_ui(uq, uin);
		}
	} else if (uin) {   /* a restyled full screen with no world behind it */
		C3D_RenderTargetClear(top_l, C3D_CLEAR_ALL, 0x101218FF, 0);
		C3D_FrameDrawOn(top_l);
		draw_ui(uq, uin);
	} else {
		C3D_RenderTargetClear(top_l, C3D_CLEAR_ALL, 0x101218FF, 0);
		C3D_FrameDrawOn(top_l);
		draw_picture(true);
	}
	C3D_RenderTargetClear(bottom, C3D_CLEAR_ALL, 0x101218FF, 0);
	C3D_FrameDrawOn(bottom);
	draw_picture(false);
	C3D_FrameEnd(0);
}
