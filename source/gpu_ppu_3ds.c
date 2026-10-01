#include "gpu_ppu_3ds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>
#include <citro3d.h>

#include "debug_tools.h"
#include "gpu_ppu_shbin.h"

// Two 512x256 RGBA8 targets with depth+stencil: the main screen and the subscreen
// (the SNES colour-math source). Screen pixel (x, y) is target pixel (x + g_off_x,
// y + kTexOffY), so the WIDE margins and extra rows (GpuFrame.x0, y0) fit; g_off_x follows
// each frame's left margin (they lean to one side next to a room edge). Each band is drawn like the CPU renderer composes a
// line (GpuBand in gpu_ppu.h):
//   depth   = priority level; BG quads pass where theirs is greater
//   stencil = bit 0: a sprite already owns the pixel (first in OAM order wins)
//             bit 1: colour math applies to the pixel's current owner
// then colour math as blended passes where stencil bit 1 is set, and the result is
// drawn scaled to the top screen with the brightness as a constant multiply.
//
// Things that cannot be known without running on the GPU are measured once at start-
// up (Calibrate): which depth test means "higher level wins", whether a texture
// rendered to is read back the same way up, and how a display transfer orders rows.

typedef struct {
  float x, y, z, u, v;
} Vtx;

enum { kMaxVerts = (kGpuMaxQuads + 256) * 6, kTexW = 512, kTexH = 256, kTexOffXCalib = 128,
       kTexOffY = kGpuMaxExtraRows };

static bool g_ready, g_failed;
static DVLB_s *g_dvlb;
static shaderProgram_s g_prog;
static int g_uloc_proj;
static C3D_AttrInfo g_attr;
static C3D_BufInfo g_buf;
static C3D_Tex g_main_tex, g_sub_tex;
static C3D_RenderTarget *g_rt_main, *g_rt_sub, *g_rt_top;
static C3D_Mtx g_proj_tex, g_proj_top;
static int g_off_x = kTexOffXCalib;   // target column of screen column 0

static void SetTexOffset(int off_x) {
  g_off_x = off_x;
  // Screen (x, y) -> target (x + g_off_x, y + kTexOffY).
  Mtx_Ortho(&g_proj_tex, -off_x, kTexW - off_x, kTexH - kTexOffY, -kTexOffY, 1, -1, true);
}
static Vtx *g_vbo;
static int g_nverts;
static GPU_TESTFUNC g_depth_greater = GPU_GREATER;
static bool g_rt_flip_v;          // a rendered texture comes back upside down when sampled
static bool g_readback_flipped;   // display transfer output starts at the bottom row
static char g_calib_text[64] = "not started";
static uint32_t *g_readback;      // 256x256 linear RGBA8

#define DISPLAY_TRANSFER_FLAGS                                                                          \
  (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |                      \
   GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |       \
   GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

// ---- Textures (GpuBackend_*) --------------------------------------------------------------

bool GpuBackend_TexCreate(GpuTex *t, int w, int h) {
  C3D_Tex *tex = (C3D_Tex *)calloc(1, sizeof(C3D_Tex));
  if (!tex) return false;
  // Linear memory (not VRAM): the CPU writes the texels in place.
  if (!C3D_TexInit(tex, w, h, GPU_RGBA5551)) {
    free(tex);
    return false;
  }
  C3D_TexSetFilter(tex, GPU_NEAREST, GPU_NEAREST);
  C3D_TexSetWrap(tex, GPU_REPEAT, GPU_REPEAT);
  memset(tex->data, 0, (size_t)w * h * 2);
  GSPGPU_FlushDataCache(tex->data, (u32)w * h * 2);
  t->px = (uint16_t *)tex->data;
  t->w = w;
  t->h = h;
  t->impl = tex;
  return true;
}

void GpuBackend_TexFree(GpuTex *t) {
  if (t->impl) {
    C3D_TexDelete((C3D_Tex *)t->impl);
    free(t->impl);
  }
  memset(t, 0, sizeof(*t));
}

void GpuBackend_TexWritten(GpuTex *t, int y0, int y1) {
  if (y0 < 0) y0 = 0;
  if (y1 > t->h) y1 = t->h;
  if (y1 <= y0) return;
  // Texels are stored by 8-row blocks of the whole width.
  const int b0 = y0 >> 3, b1 = (y1 + 7) >> 3;
  GSPGPU_FlushDataCache(t->px + b0 * 8 * t->w, (u32)(b1 - b0) * 8 * t->w * 2);
}

// ---- Drawing helpers ------------------------------------------------------------------------

static inline uint32_t Expand5(int c) { return (uint32_t)((c << 3) | (c >> 2)); }

// citro3d colours are 0xAABBGGRR.
static uint32_t ColorFrom555(uint16_t c, int alpha) {
  return Expand5(c & 31) | Expand5((c >> 5) & 31) << 8 | Expand5((c >> 10) & 31) << 16 | (uint32_t)alpha << 24;
}

static void EnvTexture(void) {
  C3D_TexEnv *e = C3D_GetTexEnv(0);
  C3D_TexEnvInit(e);
  C3D_TexEnvSrc(e, C3D_Both, GPU_TEXTURE0, 0, 0);
  C3D_TexEnvFunc(e, C3D_Both, GPU_REPLACE);
}

static void EnvSolid(uint32_t rgba) {
  C3D_TexEnv *e = C3D_GetTexEnv(0);
  C3D_TexEnvInit(e);
  C3D_TexEnvSrc(e, C3D_Both, GPU_CONSTANT, 0, 0);
  C3D_TexEnvFunc(e, C3D_Both, GPU_REPLACE);
  C3D_TexEnvColor(e, rgba);
}

// Colour from the constant, alpha from the texture (to alpha-test on it).
static void EnvConstRgbTexAlpha(uint32_t rgba) {
  C3D_TexEnv *e = C3D_GetTexEnv(0);
  C3D_TexEnvInit(e);
  C3D_TexEnvSrc(e, C3D_RGB, GPU_CONSTANT, 0, 0);
  C3D_TexEnvSrc(e, C3D_Alpha, GPU_TEXTURE0, 0, 0);
  C3D_TexEnvFunc(e, C3D_Both, GPU_REPLACE);
  C3D_TexEnvColor(e, rgba);
}

static void EnvModulate(uint32_t rgba) {
  C3D_TexEnv *e = C3D_GetTexEnv(0);
  C3D_TexEnvInit(e);
  C3D_TexEnvSrc(e, C3D_Both, GPU_TEXTURE0, GPU_CONSTANT, 0);
  C3D_TexEnvFunc(e, C3D_Both, GPU_MODULATE);
  C3D_TexEnvColor(e, rgba);
}

static void BlendOff(void) {
  C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
}

// Depth for a priority level (0 = backdrop .. 15).
static inline float LevelZ(int level) { return -((float)level + 0.5f) / 16.0f; }

static int g_batch_first;

static void BatchBegin(void) { g_batch_first = g_nverts; }

static void PushQuad(float x0, float y0, float x1, float y1, float z, float u0, float v0, float u1, float v1) {
  if (g_nverts + 6 > kMaxVerts) return;
  Vtx *v = &g_vbo[g_nverts];
  v[0] = (Vtx){ x0, y0, z, u0, v0 };
  v[1] = (Vtx){ x1, y0, z, u1, v0 };
  v[2] = (Vtx){ x0, y1, z, u0, v1 };
  v[3] = (Vtx){ x1, y0, z, u1, v0 };
  v[4] = (Vtx){ x1, y1, z, u1, v1 };
  v[5] = (Vtx){ x0, y1, z, u0, v1 };
  g_nverts += 6;
}

// The GPU reads the vertices only once the frame is submitted, so the cache is flushed
// once per frame (EndFrame) rather than per batch: each flush is a system call, and a
// frame has dozens of batches (2DS: ~3 ms of submit time per frame).
// Like PushQuad, with texture coordinates at each corner: top-left, top-right,
// bottom-left, bottom-right (an affine, mode 7, quad).
static void PushQuad4(float x0, float y0, float x1, float y1, float z, const float uv[4][2]) {
  if (g_nverts + 6 > kMaxVerts) return;
  Vtx *v = &g_vbo[g_nverts];
  v[0] = (Vtx){ x0, y0, z, uv[0][0], uv[0][1] };
  v[1] = (Vtx){ x1, y0, z, uv[1][0], uv[1][1] };
  v[2] = (Vtx){ x0, y1, z, uv[2][0], uv[2][1] };
  v[3] = (Vtx){ x1, y0, z, uv[1][0], uv[1][1] };
  v[4] = (Vtx){ x1, y1, z, uv[3][0], uv[3][1] };
  v[5] = (Vtx){ x0, y1, z, uv[2][0], uv[2][1] };
  g_nverts += 6;
}

// Mode 7 quad: the GPU samples pixel (i, r) at its centre, so the coordinate at the
// top-left corner is half a step (per pixel and per row) before the position of pixel
// (0, 0). A small bias keeps exact texel boundaries on the right side of the GPU's
// limited precision: with every value a multiple of 64 (identity or simple scales)
// positions are quarter texels and 1/8 texel is safe; otherwise half a unit (1/512).
static void PushAffine(const GpuQuad *qd) {
  enum { kPlane = 1024 * 256 };
  const double bias = ((qd->ax | qd->ay | qd->adx | qd->ady | qd->ardx | qd->ardy) & 63) == 0 ? 32.0 : 0.5;
  double px = qd->ax, py = qd->ay;
  if (!(qd->flags & kGpuQuadBorder)) {   // it wraps: only the position within the plane matters
    px = (double)((uint32_t)qd->ax % kPlane);
    py = (double)((uint32_t)qd->ay % kPlane);
  }
  float uv[4][2];
  for (int c = 0; c < 4; c++) {
    const double i = (c & 1) ? qd->w - 0.5 : -0.5, r = (c & 2) ? qd->h - 0.5 : -0.5;
    const double tx = px + qd->adx * i + qd->ardx * r + bias, ty = py + qd->ady * i + qd->ardy * r + bias;
    uv[c][0] = (float)(tx / kPlane);
    uv[c][1] = (float)(1.0 - ty / kPlane);
  }
  PushQuad4(qd->x, qd->y, qd->x + qd->w, qd->y + qd->h, LevelZ(qd->level), uv);
}

static void BatchDraw(void) {
  const int n = g_nverts - g_batch_first;
  if (n <= 0) return;
  C3D_DrawArrays(GPU_TRIANGLES, g_batch_first, n);
  g_batch_first = g_nverts;
}

static void EndFrame(void) {
  if (g_nverts) GSPGPU_FlushDataCache(g_vbo, (u32)g_nverts * sizeof(Vtx));
  C3D_FrameEnd(0);
}

static void SolidRect(int x0, int y0, int x1, int y1, int level) {
  BatchBegin();
  PushQuad((float)x0, (float)y0, (float)x1, (float)y1, LevelZ(level), 0, 0, 0, 0);
  BatchDraw();
}

// v for row `y` of a texture: row 0 is at v = 1 (see GpuTexelIndex and mzm's atlas).
static inline float TexV(const GpuTex *t, int y) { return 1.0f - (float)y / (float)t->h; }

// Screen row `y` of a texture the GPU rendered into.
static inline float RtV(int y) {
  const float t = (float)(y + kTexOffY) / kTexH;
  return g_rt_flip_v ? t : 1.0f - t;
}

static void SetTarget(C3D_RenderTarget *rt, const C3D_Mtx *proj) {
  C3D_FrameDrawOn(rt);
  C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g_uloc_proj, proj);
}

// ---- One screen of one band ---------------------------------------------------------------

// Draws the band's quads [first, first+count) over what is already there.
static void DrawQuads(const GpuFrame *f, int first, int count, bool track_math) {
  // Sprites: the first one on a pixel wins (stencil bit 0), they take the pixel from
  // anything below whatever their level, as the CPU renderer writes them first.
  EnvTexture();
  C3D_AlphaTest(true, GPU_GREATER, 0);
  int q = first;
  const GpuTex *bound = NULL;
  int bound_wrap = -1;   // the texture's wrap mode: 0 repeat, 1 clamp to a transparent border
  float inv_w = 0, inv_h = 0;   // of the bound texture: no divisions per quad
  int state = -1;   // 0 sprites, 1 sprites with math, 2 BG, 3 BG with math
  BatchBegin();
  for (; q < first + count; q++) {
    const GpuQuad *qd = &f->quads[q];
    const GpuTex *t = f->tex[qd->tex];
    const bool obj = qd->flags & kGpuQuadObj, math = track_math && (qd->flags & kGpuQuadMath);
    const int st = (obj ? 0 : 2) + math;
    const int wrap = (qd->flags & kGpuQuadBorder) ? 1 : 0;
    if (t != bound || st != state || wrap != bound_wrap) {
      BatchDraw();
      if (t != bound || wrap != bound_wrap) {
        C3D_Tex *tex = (C3D_Tex *)t->impl;
        const GPU_TEXTURE_WRAP_PARAM w = wrap ? GPU_CLAMP_TO_BORDER : GPU_REPEAT;
        tex->border = 0;
        C3D_TexSetWrap(tex, w, w);
        C3D_TexBind(0, tex);
        bound = t;
        bound_wrap = wrap;
        inv_w = 1.0f / t->w;
        inv_h = 1.0f / t->h;
      }
      if (st != state) {
        state = st;
        if (obj) {
          C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_ALL);
          C3D_StencilTest(true, GPU_NOTEQUAL, 1 | (math ? 2 : 0), 1, 3);
          C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_REPLACE);
        } else {
          C3D_DepthTest(true, g_depth_greater, GPU_WRITE_ALL);
          C3D_StencilTest(true, GPU_ALWAYS, math ? 2 : 0, 0, 2);
          C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_REPLACE);
        }
      }
    }
    if (qd->flags & kGpuQuadAffine) {
      PushAffine(qd);
      continue;
    }
    float u0 = qd->sx * inv_w, u1 = (qd->sx + qd->w) * inv_w;
    float v0 = 1.0f - qd->sy * inv_h, v1 = 1.0f - (qd->sy + qd->h) * inv_h;   // TexV
    if (qd->flags & kGpuQuadFlipX) { float s = u0; u0 = u1; u1 = s; }
    if (qd->flags & kGpuQuadFlipY) { float s = v0; v0 = v1; v1 = s; }
    PushQuad(qd->x, qd->y, qd->x + qd->w, qd->y + qd->h, LevelZ(qd->level), u0, v0, u1, v1);
  }
  BatchDraw();
  C3D_AlphaTest(false, GPU_ALWAYS, 0);
}

static void DrawBandSub(const GpuFrame *f, const GpuBand *b) {
  // Backdrop: the fixed colour, alpha 0 = "no subscreen pixel here".
  EnvSolid(ColorFrom555(b->fixed, 0));
  C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_ALL);
  C3D_StencilTest(true, GPU_ALWAYS, 0, 0, 0xff);
  C3D_StencilOp(GPU_STENCIL_REPLACE, GPU_STENCIL_REPLACE, GPU_STENCIL_REPLACE);
  SolidRect(f->x0, b->y0, f->x1, b->y1, 0);
  DrawQuads(f, b->sub_first, b->sub_count, false);
}

// Texture u of screen column x in a render target.
static inline float RtU(int x) { return (float)(x + g_off_x) / kTexW; }

static void MathPass(const GpuFrame *f, const GpuBand *b) {
  // Only pixels whose owner has math on; no depth.
  C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_COLOR);
  C3D_StencilTest(true, GPU_EQUAL, 2, 2, 0);
  C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
  const GPU_BLENDEQUATION eq = b->subtract ? GPU_BLEND_REVERSE_SUBTRACT : GPU_BLEND_ADD;
  C3D_BlendingColor(0x80000000);   // constant alpha 0.5 for the halved cases
  const float v0 = RtV(b->y0), v1 = RtV(b->y1);
  if (b->add_subscreen) {
    C3D_TexBind(0, &g_sub_tex);
    // Subscreen pixels (alpha 1): halved if `half`.
    EnvTexture();
    C3D_AlphaTest(true, GPU_GREATER, 0);
    if (b->half) C3D_AlphaBlend(eq, eq, GPU_CONSTANT_ALPHA, GPU_CONSTANT_ALPHA, GPU_ZERO, GPU_ONE);
    else C3D_AlphaBlend(eq, eq, GPU_ONE, GPU_ONE, GPU_ZERO, GPU_ONE);
    BatchBegin();
    PushQuad(f->x0, b->y0, f->x1, b->y1, 0, RtU(f->x0), v0, RtU(f->x1), v1);
    BatchDraw();
    // Subscreen backdrop (alpha 0): the fixed colour, never halved.
    EnvConstRgbTexAlpha(ColorFrom555(b->fixed, 255));
    C3D_AlphaTest(true, GPU_EQUAL, 0);
    C3D_AlphaBlend(eq, eq, GPU_ONE, GPU_ONE, GPU_ZERO, GPU_ONE);
    BatchBegin();
    PushQuad(f->x0, b->y0, f->x1, b->y1, 0, RtU(f->x0), v0, RtU(f->x1), v1);
    BatchDraw();
  } else {
    EnvSolid(ColorFrom555(b->fixed, 255));
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    if (b->half) C3D_AlphaBlend(eq, eq, GPU_CONSTANT_ALPHA, GPU_CONSTANT_ALPHA, GPU_ZERO, GPU_ONE);
    else C3D_AlphaBlend(eq, eq, GPU_ONE, GPU_ONE, GPU_ZERO, GPU_ONE);
    SolidRect(f->x0, b->y0, f->x1, b->y1, 0);
  }
  C3D_AlphaTest(false, GPU_ALWAYS, 0);
  BlendOff();
}

static void DrawBandMain(const GpuFrame *f, const GpuBand *b) {
  C3D_StencilOp(GPU_STENCIL_REPLACE, GPU_STENCIL_REPLACE, GPU_STENCIL_REPLACE);
  if (b->black) {
    EnvSolid(0xff000000);
    C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_ALL);
    C3D_StencilTest(true, GPU_ALWAYS, 0, 0, 0xff);
    SolidRect(f->x0, b->y0, f->x1, b->y1, 0);
    return;
  }
  // Backdrop: level 0, math bit as the backdrop's.
  EnvSolid(ColorFrom555(b->backdrop, 255));
  C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_ALL);
  C3D_StencilTest(true, GPU_ALWAYS, b->backdrop_math ? 2 : 0, 0, 0xff);
  SolidRect(f->x0, b->y0, f->x1, b->y1, 0);
  DrawQuads(f, b->main_first, b->main_count, true);
  if (b->clip) {   // colours to black, stencil and depth kept
    EnvSolid(0xff000000);
    C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);
    SolidRect(f->x0, b->y0, f->x1, b->y1, 0);
  }
  // A colour window that splits lines: clip its rectangles to black (colour only, as
  // above), and clear the "math applies" stencil bit where it prevents math.
  // Each kind in one batch: a power-bomb-like shape is hundreds of rectangles.
  if (b->cw_count) {
    EnvSolid(0xff000000);
    C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);
    BatchBegin();
    for (int i = b->cw_first; i < b->cw_first + b->cw_count; i++) {
      const GpuCwRect *c = &f->cw[i];
      if (c->clip) PushQuad(c->x, c->y, c->x + c->w, c->y + c->h, LevelZ(0), 0, 0, 0, 0);
    }
    BatchDraw();
    if (b->math) {
      C3D_DepthTest(true, GPU_ALWAYS, 0);   // no colour, no depth
      C3D_StencilTest(true, GPU_ALWAYS, 0, 0, 2);
      C3D_StencilOp(GPU_STENCIL_REPLACE, GPU_STENCIL_REPLACE, GPU_STENCIL_REPLACE);
      BatchBegin();
      for (int i = b->cw_first; i < b->cw_first + b->cw_count; i++) {
        const GpuCwRect *c = &f->cw[i];
        if (c->no_math) PushQuad(c->x, c->y, c->x + c->w, c->y + c->h, LevelZ(0), 0, 0, 0, 0);
      }
      BatchDraw();
    }
  }
  if (b->math) MathPass(f, b);
}

// ---- Frame --------------------------------------------------------------------------------

static bool g_any_frame;

// The FPS overlay: on the CPU path it is drawn into the framebuffer, here it is a texture
// drawn over the top screen's left margin.
enum { kOverlaySize = 64 };
static C3D_Tex g_overlay_tex;
static bool g_overlay_on;

void GpuPpu3ds_SetOverlay(const uint32_t *px) {
  g_overlay_on = px && g_ready;
  if (!g_overlay_on) return;
  uint32_t *dst = (uint32_t *)g_overlay_tex.data;
  for (int x = 0; x < kOverlaySize; x++) {
    const uint32_t *col = px + x * kOverlaySize + (kOverlaySize - 1);   // y = 0 is the last word
    for (int y = 0; y < kOverlaySize; y++) dst[GpuTexelIndex(x, y, kOverlaySize)] = col[-y];
  }
  GSPGPU_FlushDataCache(dst, kOverlaySize * kOverlaySize * 4);
}

static u64 g_wait_ticks, g_submit_ticks;

void GpuPpu3ds_LastTimes(float *wait_ms, float *submit_ms) {
  *wait_ms = (float)((double)g_wait_ticks * 1000.0 / SYSCLOCK_ARM11);
  *submit_ms = (float)((double)g_submit_ticks * 1000.0 / SYSCLOCK_ARM11);
}

void GpuPpu3ds_DrawAndPresent(const GpuFrame *f, bool pixel_perfect) {
  if (!g_ready) return;
  const u64 t0 = svcGetSystemTick();
  C3D_FrameBegin(0);   // waits for the GPU to finish the previous frame
  const u64 t1 = svcGetSystemTick();
  g_wait_ticks = t1 - t0;
  g_nverts = 0;
  SetTexOffset(-f->x0);   // at most 512 - 256 - the right margin: GpuPpu_SetMargins
  BlendOff();
  bool any_sub = false;
  for (int i = 0; i < f->band_count; i++) any_sub |= !f->bands[i].black && f->bands[i].math && f->bands[i].add_subscreen;
  if (any_sub) {
    C3D_RenderTargetClear(g_rt_sub, C3D_CLEAR_ALL, 0, 0);
    SetTarget(g_rt_sub, &g_proj_tex);
    for (int i = 0; i < f->band_count; i++) {
      const GpuBand *b = &f->bands[i];
      if (!b->black && b->math && b->add_subscreen) DrawBandSub(f, b);
    }
  }
  C3D_RenderTargetClear(g_rt_main, C3D_CLEAR_ALL, 0, 0);
  SetTarget(g_rt_main, &g_proj_tex);
  for (int i = 0; i < f->band_count; i++) DrawBandMain(f, &f->bands[i]);
  if (f->mask_count) {
    EnvSolid(0xff000000);
    C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);
    BatchBegin();
    for (int i = 0; i < f->mask_count; i++) {
      const GpuMaskRect *m = &f->mask[i];
      PushQuad(m->x, m->y, m->x + m->w, m->y + m->h, LevelZ(0), 0, 0, 0, 0);
    }
    BatchDraw();
  }

  // Top screen: the 256x224 picture centred, scaled to 274x240 or 1:1, like the CPU path
  // (DrawPpuFrame); WIDE margins at the same scale on each side, cut by the screen edge.
  C3D_RenderTargetClear(g_rt_top, C3D_CLEAR_ALL, 0, 0);
  SetTarget(g_rt_top, &g_proj_top);
  C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_COLOR);
  C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);
  C3D_AlphaTest(false, GPU_ALWAYS, 0);
  BlendOff();
  C3D_TexBind(0, &g_main_tex);
  // The frame's columns centred on the screen (uneven margins: not the 256 px view).
  const float x_scale = pixel_perfect ? 1.0f : 274.0f / 256.0f, mid = (f->x0 + f->x1) * 0.5f;
  const float x0 = 200 + (f->x0 - mid) * x_scale, x1 = 200 + (f->x1 - mid) * x_scale;
  const float u0 = RtU(f->x0), u1 = RtU(f->x1);
  const float y_scale = pixel_perfect ? 1.0f : 240.0f / 224.0f, y_off = pixel_perfect ? (240 - 224) / 2 : 0;
  for (int i = 0; i < f->band_count; i++) {
    const GpuBand *b = &f->bands[i];
    const int bright = b->black ? 0 : b->brightness * 255 / 15;
    EnvModulate((uint32_t)bright | (uint32_t)bright << 8 | (uint32_t)bright << 16 | 0xff000000u);
    const float y0 = y_off + b->y0 * y_scale, y1 = y_off + b->y1 * y_scale;
    BatchBegin();
    PushQuad(x0, y0, x1, y1, 0, u0, RtV(b->y0), u1, RtV(b->y1));
    BatchDraw();
  }
  if (g_overlay_on) {
    C3D_TexBind(0, &g_overlay_tex);
    EnvTexture();
    BatchBegin();
    PushQuad(0, 0, kOverlaySize, kOverlaySize, 0, 0, 1, 1, 0);
    BatchDraw();
  }
  EndFrame();
  g_submit_ticks = svcGetSystemTick() - t1;
  g_any_frame = true;
}


// An empty frame: C3D_FrameBegin waits until the GPU has finished the previous one.
static void WaitGpu(void) {
  C3D_FrameBegin(0);
  EndFrame();
}

// Detiles the main target into g_readback (blocks until the GPU is done).
static void ReadMain(void) {
  WaitGpu();
  C3D_SyncDisplayTransfer((u32 *)g_main_tex.data, GX_BUFFER_DIM(kTexW, kTexH), (u32 *)g_readback,
                          GX_BUFFER_DIM(kTexW, kTexH), DISPLAY_TRANSFER_FLAGS);
  GSPGPU_InvalidateDataCache(g_readback, kTexW * kTexH * 4);
}

void GpuPpu3ds_WaitIdle(void) {
  if (!g_ready || !g_any_frame) return;
  WaitGpu();
}

// Byte order of a readback word, found by Calibrate: 0xRRGGBBAA, or 0xAABBGGRR.
static bool g_readback_abgr;

static void Unpack(uint32_t p, int *r, int *g, int *b) {
  if (g_readback_abgr) *r = p & 0xff, *g = (p >> 8) & 0xff, *b = (p >> 16) & 0xff;
  else *r = p >> 24, *g = (p >> 16) & 0xff, *b = (p >> 8) & 0xff;
}

// Screen pixel (x, y) of the last readback, read as top-down or bottom-up rows.
static inline uint32_t RawPixel(int x, int y, bool flipped) {
  const int row = y + kTexOffY;
  return g_readback[(flipped ? kTexH - 1 - row : row) * kTexW + x + g_off_x];
}

// Screen pixel (x, y) of the last readback.
static inline uint32_t ReadPixel(int x, int y) {
  return RawPixel(x, y, g_readback_flipped);
}

bool GpuPpu3ds_ReadBack(uint8_t *out, int pitch) {
  if (!g_ready || !g_any_frame) return false;
  ReadMain();
  for (int y = 0; y < kGpuRows; y++) {
    uint32_t *dst = (uint32_t *)(out + y * pitch);
    for (int x = 0; x < 256; x++) {
      int r, g, b;
      Unpack(ReadPixel(x, y), &r, &g, &b);
      dst[x] = (uint32_t)b | (uint32_t)g << 8 | (uint32_t)r << 16;
    }
  }
  return true;
}

// ---- Start-up ------------------------------------------------------------------------------

static bool IsRed(uint32_t p) {
  int r, g, b;
  Unpack(p, &r, &g, &b);
  return r > 0xc0 && g < 0x40 && b < 0x40;
}

static bool IsGreen(uint32_t p) {
  int r, g, b;
  Unpack(p, &r, &g, &b);
  return g > 0xc0 && r < 0x40 && b < 0x40;
}

static void CalibFrameBegin(C3D_RenderTarget *rt) {
  C3D_FrameBegin(0);
  g_nverts = 0;
  BlendOff();
  C3D_RenderTargetClear(rt, C3D_CLEAR_ALL, 0, 0);
  SetTarget(rt, &g_proj_tex);
  C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);
  C3D_AlphaTest(false, GPU_ALWAYS, 0);
}

// See the comment at the top. Sets g_readback_abgr, g_depth_greater,
// g_readback_flipped and g_rt_flip_v from what the GPU actually did.
static void Calibrate(void) {
  // 1. Byte order: all green.
  CalibFrameBegin(g_rt_main);
  EnvSolid(0xff00ff00);
  C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_ALL);
  SolidRect(0, 0, 256, 256, 0);
  EndFrame();
  ReadMain();
  const uint32_t green = RawPixel(128, 128, false);
  g_readback_abgr = ((green >> 8) & 0xff) > 0xc0 && ((green >> 16) & 0xff) < 0x40;

  // 2. Depth: level 5 red, then level 10 green with the "greater" test.
  CalibFrameBegin(g_rt_main);
  EnvSolid(0xff0000ff);
  C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_ALL);
  SolidRect(0, 0, 256, 256, 5);
  EnvSolid(0xff00ff00);
  C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
  SolidRect(0, 0, 256, 256, 10);
  EndFrame();
  ReadMain();
  const uint32_t mid = RawPixel(128, 128, false);
  g_depth_greater = IsGreen(mid) ? GPU_GREATER : GPU_LESS;

  // 3. Orientation: red on rows 0..7 of the main target only. Where the readback has it
  // says how a transfer orders rows; sampling rows 0..7 into the subscreen target and
  // reading that back says whether rendered textures come back upside down.
  CalibFrameBegin(g_rt_main);
  C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_COLOR);
  EnvSolid(0xff0000ff);
  SolidRect(0, 0, 256, 8, 0);
  EndFrame();
  ReadMain();
  g_readback_flipped = !IsRed(RawPixel(128, 4, false)) && IsRed(RawPixel(128, 4, true));

  CalibFrameBegin(g_rt_sub);
  C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_COLOR);
  C3D_TexBind(0, &g_main_tex);
  EnvTexture();
  g_rt_flip_v = false;
  BatchBegin();
  PushQuad(0, 0, 256, 256, 0, 0, RtV(0), 1, RtV(8));   // rows 0..7 stretched over everything
  BatchDraw();
  EndFrame();
  WaitGpu();
  C3D_SyncDisplayTransfer((u32 *)g_sub_tex.data, GX_BUFFER_DIM(kTexW, kTexH), (u32 *)g_readback,
                          GX_BUFFER_DIM(kTexW, kTexH), DISPLAY_TRANSFER_FLAGS);
  GSPGPU_InvalidateDataCache(g_readback, kTexW * kTexH * 4);
  g_rt_flip_v = !IsRed(RawPixel(128, 128, false));
  snprintf(g_calib_text, sizeof(g_calib_text), "%s, depth %s, rt %s, readback %s", g_readback_abgr ? "ABGR" : "RGBA",
           g_depth_greater == GPU_GREATER ? "GREATER" : "LESS", g_rt_flip_v ? "FLIPPED" : "ok",
           g_readback_flipped ? "bottom-up" : "top-down");
}

bool GpuPpu3ds_Init(void) {
  if (g_ready) return true;
  if (g_failed) return false;
  g_failed = true;   // until everything below worked
  if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 2)) return false;
  g_dvlb = DVLB_ParseFile((u32 *)gpu_ppu_shbin, gpu_ppu_shbin_size);
  if (!g_dvlb) return false;
  shaderProgramInit(&g_prog);
  shaderProgramSetVsh(&g_prog, &g_dvlb->DVLE[0]);
  g_uloc_proj = shaderInstanceGetUniformLocation(g_prog.vertexShader, "projection");

  g_vbo = (Vtx *)linearAlloc(sizeof(Vtx) * kMaxVerts);
  g_readback = (uint32_t *)linearAlloc(kTexW * kTexH * 4);
  if (!g_vbo || !g_readback) return false;

  if (!C3D_TexInitVRAM(&g_main_tex, kTexW, kTexH, GPU_RGBA8) || !C3D_TexInitVRAM(&g_sub_tex, kTexW, kTexH, GPU_RGBA8))
    return false;
  C3D_TexSetFilter(&g_main_tex, GPU_NEAREST, GPU_NEAREST);
  C3D_TexSetFilter(&g_sub_tex, GPU_NEAREST, GPU_NEAREST);
  if (!C3D_TexInit(&g_overlay_tex, kOverlaySize, kOverlaySize, GPU_RGBA8)) return false;
  C3D_TexSetFilter(&g_overlay_tex, GPU_NEAREST, GPU_NEAREST);
  g_rt_main = C3D_RenderTargetCreateFromTex(&g_main_tex, GPU_TEXFACE_2D, 0, GPU_RB_DEPTH24_STENCIL8);
  g_rt_sub = C3D_RenderTargetCreateFromTex(&g_sub_tex, GPU_TEXFACE_2D, 0, GPU_RB_DEPTH24_STENCIL8);
  g_rt_top = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH16);
  if (!g_rt_main || !g_rt_sub || !g_rt_top) return false;
  C3D_RenderTargetSetOutput(g_rt_top, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

  SetTexOffset(kTexOffXCalib);
  Mtx_OrthoTilt(&g_proj_top, 0, 400, 240, 0, 1, -1, true);

  C3D_BindProgram(&g_prog);
  AttrInfo_Init(&g_attr);
  AttrInfo_AddLoader(&g_attr, 0, GPU_FLOAT, 3);   // v0 = position
  AttrInfo_AddLoader(&g_attr, 1, GPU_FLOAT, 2);   // v1 = texcoord
  C3D_SetAttrInfo(&g_attr);
  BufInfo_Init(&g_buf);
  BufInfo_Add(&g_buf, g_vbo, sizeof(Vtx), 2, 0x10);
  C3D_SetBufInfo(&g_buf);
  C3D_CullFace(GPU_CULL_NONE);
  for (int i = 1; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));

  g_ready = true;
  g_failed = false;
  Calibrate();
  return true;
}

bool GpuPpu3ds_Ready(void) { return g_ready; }

const char *GpuPpu3ds_CalibrationText(void) { return g_calib_text; }

void GpuPpu3ds_Exit(void) {
  if (!g_ready) return;
  // Nothing of citro3d is torn down: closing from the HOME menu hung on "Closing
  // software" in every call that waits for the GPU queue (an empty frame first, then
  // C3D_RenderTargetDelete; 2DS logs, 2026-10-01). After HOME, citro3d's APT suspend
  // hook has stopped its vblank handling and the queue never drains. The app is exiting:
  // the system reclaims the GPU memory with the process, and gfxExit (SDL_Quit) stops
  // the GSP event thread that citro3d's callbacks run on.
  Debug_Log("exit: citro3d left as is");
  g_ready = false;
}
