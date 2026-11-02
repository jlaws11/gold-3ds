#include <3ds.h>
#include <citro3d.h>
#include <string.h>
#include <math.h>
#include "render.h"
#include "scene_shbin.h"

#define DISPLAY_TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
	 GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
	 GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

#define MESH_CAP   240000
#define SPRITE_CAP (40 * 6)

static C3D_RenderTarget *top_l, *top_r, *bottom;
static DVLB_s *dvlb;
static shaderProgram_s prog;
static int u_proj, u_mv;
static C3D_Tex tex_bg, tex_obj, tex_lcd;
static WorldMesh mesh;
static WVert *spr, *screen_quads;
static uint32_t *lcd_rgba;

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
		{ x0, y0, 0, 0,  0,  1 }, { x0, y1, 0, 0,  v1, 1 }, { x1, y1, 0, u1, v1, 1 },
		{ x0, y0, 0, 0,  0,  1 }, { x1, y1, 0, u1, v1, 1 }, { x1, y0, 0, u1, 0,  1 },
	};
	memcpy(q, v, sizeof v);
}

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

	C3D_AttrInfo *ai = C3D_GetAttrInfo();
	AttrInfo_Init(ai);
	AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3);   /* position */
	AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 2);   /* texcoord */
	AttrInfo_AddLoader(ai, 2, GPU_FLOAT, 1);   /* shade */

	C3D_TexEnv *env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
	C3D_AlphaTest(true, GPU_GREATER, 0x80);
	C3D_CullFace(GPU_CULL_NONE);

	tex_new(&tex_bg, 128, 128);
	tex_new(&tex_obj, 128, 128);
	tex_new(&tex_lcd, 256, 256);

	mesh.cap = MESH_CAP;
	mesh.verts = linearAlloc(sizeof(WVert) * MESH_CAP);
	mesh.count = 0; mesh.sq_w = mesh.sq_h = 0; mesh.key = 0;
	spr = linearAlloc(sizeof(WVert) * SPRITE_CAP);
	screen_quads = linearAlloc(sizeof(WVert) * 12);
	lcd_rgba = linearAlloc(160 * 144 * 4);
	/* Game Boy picture at 1.5x: 240x216, centred on each screen */
	set_quad(screen_quads,     80, 12, 320, 228, 160 / 256.0f, 144 / 256.0f);
	set_quad(screen_quads + 6, 40, 12, 280, 228, 160 / 256.0f, 144 / 256.0f);
	GSPGPU_FlushDataCache(screen_quads, sizeof(WVert) * 12);
}

void render_exit(void) {
	C3D_TexDelete(&tex_bg); C3D_TexDelete(&tex_obj); C3D_TexDelete(&tex_lcd);
	linearFree(mesh.verts); linearFree(spr); linearFree(screen_quads); linearFree(lcd_rgba);
	shaderProgramFree(&prog);
	DVLB_Free(dvlb);
	C3D_Fini();
}

WorldMesh *render_mesh(void) { return &mesh; }
void render_mesh_uploaded(void) { GSPGPU_FlushDataCache(mesh.verts, sizeof(WVert) * mesh.count); }
void render_bg_atlas(const uint32_t *rgba) { upload(&tex_bg, rgba, 128, 128, 128); }
void render_obj_atlas(const uint32_t *rgba) { upload(&tex_obj, rgba, 128, 128, 128); }
WVert *render_sprite_buf(int *cap) { *cap = SPRITE_CAP; return spr; }

void render_lcd(const uint8_t *lcd) {
	/* soft cream-to-ink ramp until the colour core lands */
	static const uint32_t shade[4] = {
		RGBA(240, 238, 214, 255), RGBA(168, 186, 140, 255), RGBA(84, 104, 92, 255), RGBA(28, 30, 40, 255) };
	for (int i = 0; i < 160 * 144; i++) lcd_rgba[i] = shade[lcd[i] & 3];
	upload(&tex_lcd, lcd_rgba, 160, 144, 160);
}

static void use_buffer(WVert *base) {
	C3D_BufInfo *bi = C3D_GetBufInfo();
	BufInfo_Init(bi);
	BufInfo_Add(bi, base, sizeof(WVert), 3, 0x210);
}

static void draw_world(const Camera *cam, float iod, int sprite_verts) {
	C3D_Mtx proj, mv;
	float focal = cam->dist;
	Mtx_PerspStereoTilt(&proj, C3D_AngleFromDegrees(42.0f), C3D_AspectRatioTop, 6.0f, 1600.0f, iod, focal, false);
	float tx = cam->x + 8, tz = cam->z + 8, ty = 6;
	float ex = tx + sinf(cam->yaw) * cosf(cam->pitch) * cam->dist;
	float ey = ty + sinf(cam->pitch) * cam->dist;
	float ez = tz + cosf(cam->yaw) * cosf(cam->pitch) * cam->dist;
	Mtx_LookAt(&mv, FVec3_New(ex, ey, ez), FVec3_New(tx, ty, tz), FVec3_New(0, 1, 0), false);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_proj, &proj);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_mv, &mv);
	C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);

	/* only the squares around the player: whole rows of the grid are contiguous */
	C3D_TexBind(0, &tex_bg);
	use_buffer(mesh.verts);
	int gx = (int)floorf(cam->x / 16) + mesh.origin, gy = (int)floorf(cam->z / 16) + mesh.origin;
	int spread = 15 + (int)(cam->dist / 60);
	for (int y = gy - spread; y <= gy + 8; y++) {
		int first, count;
		world_row_range(&mesh, y, gx - spread, gx + spread, &first, &count);
		if (count > 0) C3D_DrawArrays(GPU_TRIANGLES, first, count);
	}
	if (sprite_verts > 0) {
		C3D_TexBind(0, &tex_obj);
		use_buffer(spr);
		C3D_DrawArrays(GPU_TRIANGLES, 0, sprite_verts);
	}
}

static void draw_picture(bool top) {
	C3D_Mtx proj, id;
	Mtx_OrthoTilt(&proj, 0.0f, top ? 400.0f : 320.0f, 240.0f, 0.0f, 1.0f, -1.0f, true);
	Mtx_Identity(&id);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_proj, &proj);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_mv, &id);
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	C3D_TexBind(0, &tex_lcd);
	use_buffer(screen_quads);
	C3D_DrawArrays(GPU_TRIANGLES, top ? 0 : 6, 6);
}

void render_frame(bool voxel, const Camera *cam, int sprite_verts, uint32_t sky) {
	if (sprite_verts > 0) GSPGPU_FlushDataCache(spr, sizeof(WVert) * sprite_verts);
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
	if (voxel) {
		float iod = osGet3DSliderState() * cam->dist / 24.0f;
		C3D_RenderTargetClear(top_l, C3D_CLEAR_ALL, sky, 0);
		C3D_FrameDrawOn(top_l);
		draw_world(cam, -iod, sprite_verts);
		if (iod > 0.0f) {
			C3D_RenderTargetClear(top_r, C3D_CLEAR_ALL, sky, 0);
			C3D_FrameDrawOn(top_r);
			draw_world(cam, iod, sprite_verts);
		}
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
