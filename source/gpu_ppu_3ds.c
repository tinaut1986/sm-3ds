#include "gpu_ppu_3ds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>
#include <citro3d.h>

#include "gpu_ppu_shbin.h"

// Two 256x256 RGBA8 targets with depth+stencil: the main screen and the subscreen
// (the SNES colour-math source). Each band is drawn like the CPU renderer composes a
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

enum { kMaxVerts = (kGpuMaxQuads + 256) * 6, kTexW = 256, kTexH = 256 };

static bool g_ready, g_failed;
static DVLB_s *g_dvlb;
static shaderProgram_s g_prog;
static int g_uloc_proj;
static C3D_AttrInfo g_attr;
static C3D_BufInfo g_buf;
static C3D_Tex g_main_tex, g_sub_tex;
static C3D_RenderTarget *g_rt_main, *g_rt_sub, *g_rt_top;
static C3D_Mtx g_proj_tex, g_proj_top;
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

static void BatchDraw(void) {
  const int n = g_nverts - g_batch_first;
  if (n <= 0) return;
  GSPGPU_FlushDataCache(&g_vbo[g_batch_first], (u32)n * sizeof(Vtx));
  C3D_DrawArrays(GPU_TRIANGLES, g_batch_first, n);
  g_batch_first = g_nverts;
}

static void SolidRect(int x0, int y0, int x1, int y1, int level) {
  BatchBegin();
  PushQuad((float)x0, (float)y0, (float)x1, (float)y1, LevelZ(level), 0, 0, 0, 0);
  BatchDraw();
}

// v for row `y` of a texture: row 0 is at v = 1 (see GpuTexelIndex and mzm's atlas).
static inline float TexV(const GpuTex *t, int y) { return 1.0f - (float)y / (float)t->h; }

// Row `y` of a texture the GPU rendered into.
static inline float RtV(int y) { return g_rt_flip_v ? (float)y / kTexH : 1.0f - (float)y / kTexH; }

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
  int state = -1;   // 0 sprites, 1 sprites with math, 2 BG, 3 BG with math
  BatchBegin();
  for (; q < first + count; q++) {
    const GpuQuad *qd = &f->quads[q];
    const GpuTex *t = f->tex[qd->tex];
    const bool obj = qd->flags & kGpuQuadObj, math = track_math && (qd->flags & kGpuQuadMath);
    const int st = (obj ? 0 : 2) + math;
    if (t != bound || st != state) {
      BatchDraw();
      if (t != bound) {
        C3D_TexBind(0, (C3D_Tex *)t->impl);
        bound = t;
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
    float u0 = (float)qd->sx / t->w, u1 = (float)(qd->sx + qd->w) / t->w;
    float v0 = TexV(t, qd->sy), v1 = TexV(t, qd->sy + qd->h);
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
  SolidRect(0, b->y0, 256, b->y1, 0);
  DrawQuads(f, b->sub_first, b->sub_count, false);
}

static void MathPass(const GpuBand *b) {
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
    PushQuad(0, b->y0, 256, b->y1, 0, 0, v0, 1, v1);
    BatchDraw();
    // Subscreen backdrop (alpha 0): the fixed colour, never halved.
    EnvConstRgbTexAlpha(ColorFrom555(b->fixed, 255));
    C3D_AlphaTest(true, GPU_EQUAL, 0);
    C3D_AlphaBlend(eq, eq, GPU_ONE, GPU_ONE, GPU_ZERO, GPU_ONE);
    BatchBegin();
    PushQuad(0, b->y0, 256, b->y1, 0, 0, v0, 1, v1);
    BatchDraw();
  } else {
    EnvSolid(ColorFrom555(b->fixed, 255));
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    if (b->half) C3D_AlphaBlend(eq, eq, GPU_CONSTANT_ALPHA, GPU_CONSTANT_ALPHA, GPU_ZERO, GPU_ONE);
    else C3D_AlphaBlend(eq, eq, GPU_ONE, GPU_ONE, GPU_ZERO, GPU_ONE);
    SolidRect(0, b->y0, 256, b->y1, 0);
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
    SolidRect(0, b->y0, 256, b->y1, 0);
    return;
  }
  // Backdrop: level 0, math bit as the backdrop's.
  EnvSolid(ColorFrom555(b->backdrop, 255));
  C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_ALL);
  C3D_StencilTest(true, GPU_ALWAYS, b->backdrop_math ? 2 : 0, 0, 0xff);
  SolidRect(0, b->y0, 256, b->y1, 0);
  DrawQuads(f, b->main_first, b->main_count, true);
  if (b->clip) {   // colours to black, stencil and depth kept
    EnvSolid(0xff000000);
    C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);
    SolidRect(0, b->y0, 256, b->y1, 0);
  }
  if (b->math) MathPass(b);
}

// ---- Frame --------------------------------------------------------------------------------

static bool g_any_frame;

void GpuPpu3ds_DrawAndPresent(const GpuFrame *f) {
  if (!g_ready) return;
  C3D_FrameBegin(0);
  g_nverts = 0;
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

  // Top screen: 256x224 scaled to 274x240, centred, like the CPU path (DrawPpuFrame).
  C3D_RenderTargetClear(g_rt_top, C3D_CLEAR_ALL, 0, 0);
  SetTarget(g_rt_top, &g_proj_top);
  C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
  C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);
  C3D_AlphaTest(false, GPU_ALWAYS, 0);
  BlendOff();
  C3D_TexBind(0, &g_main_tex);
  const float x0 = (400 - 274) / 2, x1 = x0 + 274;
  for (int i = 0; i < f->band_count; i++) {
    const GpuBand *b = &f->bands[i];
    const int bright = b->black ? 0 : b->brightness * 255 / 15;
    EnvModulate((uint32_t)bright | (uint32_t)bright << 8 | (uint32_t)bright << 16 | 0xff000000u);
    const float y0 = b->y0 * 240.0f / 224.0f, y1 = b->y1 * 240.0f / 224.0f;
    BatchBegin();
    PushQuad(x0, y0, x1, y1, 0, 0, RtV(b->y0), 1, RtV(b->y1));
    BatchDraw();
  }
  C3D_FrameEnd(0);
  g_any_frame = true;
}


// An empty frame: C3D_FrameBegin waits until the GPU has finished the previous one.
static void WaitGpu(void) {
  C3D_FrameBegin(0);
  C3D_FrameEnd(0);
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

// Readback words are 0xRRGGBBAA.
static inline uint32_t ReadPixel(int x, int y) {
  return g_readback[(g_readback_flipped ? kTexH - 1 - y : y) * kTexW + x];
}

bool GpuPpu3ds_ReadBack(uint8_t *out, int pitch) {
  if (!g_ready || !g_any_frame) return false;
  ReadMain();
  for (int y = 0; y < kGpuRows; y++) {
    uint32_t *dst = (uint32_t *)(out + y * pitch);
    for (int x = 0; x < 256; x++) {
      const uint32_t p = ReadPixel(x, y);
      dst[x] = (p >> 8 & 0xff) | (p >> 16 & 0xff) << 8 | (p >> 24) << 16;   // b | g << 8 | r << 16
    }
  }
  return true;
}

// ---- Start-up ------------------------------------------------------------------------------

static bool IsRed(uint32_t p) { return (p >> 24) > 0xc0 && ((p >> 16) & 0xff) < 0x40; }
static bool IsGreen(uint32_t p) { return ((p >> 16) & 0xff) > 0xc0 && (p >> 24) < 0x40; }

// See the comment at the top. Leaves g_depth_greater, g_rt_flip_v and
// g_readback_flipped set from what the GPU actually did.
static void Calibrate(void) {
  // 1. Depth: level 5 red, then level 10 green with the "greater" test.
  C3D_FrameBegin(0);
  g_nverts = 0;
  BlendOff();
  C3D_RenderTargetClear(g_rt_main, C3D_CLEAR_ALL, 0, 0);
  SetTarget(g_rt_main, &g_proj_tex);
  C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);
  EnvSolid(0xff0000ff);
  C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_ALL);
  SolidRect(0, 0, 256, 256, 5);
  EnvSolid(0xff00ff00);
  C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
  SolidRect(0, 0, 256, 256, 10);
  C3D_FrameEnd(0);
  ReadMain();
  const uint32_t mid = g_readback[128 * kTexW + 128];
  g_depth_greater = IsGreen(mid) ? GPU_GREATER : GPU_LESS;

  // 2. Orientation: red on rows 0..7 of the main target only. Where the readback has it
  // says how a transfer orders rows; sampling rows 0..7 into the subscreen target and
  // reading that back says whether rendered textures come back upside down.
  C3D_FrameBegin(0);
  g_nverts = 0;
  C3D_RenderTargetClear(g_rt_main, C3D_CLEAR_ALL, 0, 0);
  SetTarget(g_rt_main, &g_proj_tex);
  C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
  EnvSolid(0xff0000ff);
  SolidRect(0, 0, 256, 8, 0);
  C3D_FrameEnd(0);
  ReadMain();
  g_readback_flipped = !IsRed(g_readback[4 * kTexW + 128]) && IsRed(g_readback[(kTexH - 5) * kTexW + 128]);

  C3D_FrameBegin(0);
  g_nverts = 0;
  C3D_RenderTargetClear(g_rt_sub, C3D_CLEAR_ALL, 0, 0);
  SetTarget(g_rt_sub, &g_proj_tex);
  C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
  C3D_TexBind(0, &g_main_tex);
  EnvTexture();
  g_rt_flip_v = false;
  BatchBegin();
  PushQuad(0, 0, 256, 256, 0, 0, RtV(0), 1, RtV(8));   // rows 0..7 stretched over everything
  BatchDraw();
  C3D_FrameEnd(0);
  WaitGpu();
  C3D_SyncDisplayTransfer((u32 *)g_sub_tex.data, GX_BUFFER_DIM(kTexW, kTexH), (u32 *)g_readback,
                          GX_BUFFER_DIM(kTexW, kTexH), DISPLAY_TRANSFER_FLAGS);
  GSPGPU_InvalidateDataCache(g_readback, kTexW * kTexH * 4);
  g_rt_flip_v = !IsRed(g_readback[128 * kTexW + 128]);
  snprintf(g_calib_text, sizeof(g_calib_text), "depth %s, rt %s, readback %s",
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
  g_rt_main = C3D_RenderTargetCreateFromTex(&g_main_tex, GPU_TEXFACE_2D, 0, GPU_RB_DEPTH24_STENCIL8);
  g_rt_sub = C3D_RenderTargetCreateFromTex(&g_sub_tex, GPU_TEXFACE_2D, 0, GPU_RB_DEPTH24_STENCIL8);
  g_rt_top = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, -1);
  if (!g_rt_main || !g_rt_sub || !g_rt_top) return false;
  C3D_RenderTargetSetOutput(g_rt_top, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

  Mtx_Ortho(&g_proj_tex, 0, kTexW, kTexH, 0, 1, -1, true);
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
  GpuPpu3ds_WaitIdle();
  C3D_RenderTargetDelete(g_rt_top);
  C3D_RenderTargetDelete(g_rt_sub);
  C3D_RenderTargetDelete(g_rt_main);
  C3D_TexDelete(&g_main_tex);
  C3D_TexDelete(&g_sub_tex);
  linearFree(g_vbo);
  linearFree(g_readback);
  shaderProgramFree(&g_prog);
  DVLB_Free(g_dvlb);
  C3D_Fini();
  g_ready = false;
}
