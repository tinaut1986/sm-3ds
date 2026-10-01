// GPU renderer for the SNES PPU: turns one captured frame (the per-line register
// states from ppu_runLine, plus VRAM/CGRAM/OAM) into textures and a list of
// textured rectangles that a backend draws. docs/gpu-ppu-design.md explains the
// approach; this file is the part shared by every backend:
//
//  - gpu_ppu_3ds.c draws the list with citro3d on the console;
//  - gpu_ppu_ref.c draws it in software with the exact same rules, which is how the
//    list is checked against the CPU renderer on the PC (tools/gpu-ppu-test).
//
// It reproduces what the CPU renderer (PpuDrawWholeLine, mode 1) draws, including
// its quirks: BG3 priority-1 tiles always on top, the first sprite in OAM order wins
// over later ones whatever their priority. Not reproduced: the 32 sprites / 34 tiles
// per line limits. Mode 7 (one 1024x1024 plane, any matrix per line) is drawn as one
// affine quad per line. A layer window that hides part of a line is drawn by cutting the
// layer's quads to the visible spans. Frames it cannot draw (mode 7, a colour window
// that splits a line while clip or prevent-math use it, mode 7 EXTBG, VRAM written
// mid-frame, ...)
// are refused, and the caller draws them with the CPU.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "src/snes/ppu.h"

// RGBA5551 texels in the PICA200 layout: 8x8 blocks, row-major from the top of the
// image, Morton order inside a block. One SNES tile is exactly one block.
typedef struct GpuTex {
  uint16_t *px;
  int w, h;         // powers of two
  void *impl;       // backend data
} GpuTex;

static inline int GpuTexelIndex(int x, int y, int w) {
  static const uint8_t kMorton[64] = {
    0,  1,  4,  5,  16, 17, 20, 21, 2,  3,  6,  7,  18, 19, 22, 23,
    8,  9,  12, 13, 24, 25, 28, 29, 10, 11, 14, 15, 26, 27, 30, 31,
    32, 33, 36, 37, 48, 49, 52, 53, 34, 35, 38, 39, 50, 51, 54, 55,
    40, 41, 44, 45, 56, 57, 60, 61, 42, 43, 46, 47, 58, 59, 62, 63,
  };
  return (((y >> 3) * (w >> 3) + (x >> 3)) << 6) + kMorton[(y & 7) << 3 | (x & 7)];
}

// SNES BGR555 to RGBA5551 (alpha set).
static inline uint16_t GpuRgba5551(uint16_t c) {
  return (uint16_t)((c & 0x1f) << 11 | ((c >> 5) & 0x1f) << 6 | ((c >> 10) & 0x1f) << 1 | 1);
}

enum {
  kGpuQuadFlipX = 1,
  kGpuQuadFlipY = 2,
  kGpuQuadObj = 4,    // sprite: drawn first, the first sprite on a pixel wins
  kGpuQuadMath = 8,   // colour math applies where this quad is the visible pixel
  // Mode 7: the texel comes from ax/ay/adx/ady below instead of sx/sy (one row tall).
  kGpuQuadAffine = 16,
  kGpuQuadBorder = 32,   // affine: transparent outside the 1024x1024 plane (else it wraps)
};

typedef struct {
  int16_t x, y, w, h;   // screen rectangle; may stick out left/right, rows stay in the band
  int16_t sx, sy;       // texel at the top-left corner before flipping; wraps around
  uint8_t tex;          // index into GpuFrame.tex
  uint8_t level;        // priority 1..15, higher wins over lower (the backdrop is 0)
  uint8_t flags;
  // kGpuQuadAffine: plane position of the quad's leftmost pixel and its step per pixel,
  // in 1/256 texel, with the CPU renderer's 32-bit wrapping arithmetic: the pixel at
  // x + i shows texel ((ax + adx*i) >> 8, (ay + ady*i) >> 8), both & 1023.
  int32_t ax, ay, adx, ady;
} GpuQuad;

// Lines that share every register except the BG scrolls. Within a band, a pixel's
// colour is (CPU renderer semantics):
//   main = colour of the highest-level quad, else the backdrop; black if `clip`
//   if `math` and that pixel's math flag (backdrop: backdrop_math):
//     other = add_subscreen && the subscreen has a quad there ? subscreen colour (halved
//             if `half`) : fixed colour (halved if `half` and !add_subscreen)
//     main = subtract ? max(main - other, 0) : min(main + other, 31)  (5-bit, per channel;
//            "halved" halves the result)
//   output = expand to 8 bits, times brightness / 15.
typedef struct {
  int y0, y1;              // output rows [y0, y1)
  bool black;              // forced blank
  uint8_t brightness;      // 0..15
  uint16_t backdrop;       // BGR555
  bool backdrop_math;
  bool clip;
  bool math;
  bool add_subscreen, subtract, half;
  uint16_t fixed;          // BGR555
  int main_first, main_count;   // quads; sprites come first
  int sub_first, sub_count;
} GpuBand;

enum { kGpuMaxQuads = 4096, kGpuMaxBands = 32, kGpuMaxTex = 48, kGpuRows = 224 };

typedef struct {
  GpuTex *tex[kGpuMaxTex];
  int tex_count;
  GpuQuad quads[kGpuMaxQuads];
  int quad_count;
  GpuBand bands[kGpuMaxBands];
  int band_count;
} GpuFrame;

// Implemented by the backend. Texels start out as zero (transparent).
bool GpuBackend_TexCreate(GpuTex *t, int w, int h);
void GpuBackend_TexFree(GpuTex *t);
// The CPU changed texels in rows [y0, y1); flush them to where the GPU reads them.
void GpuBackend_TexWritten(GpuTex *t, int y0, int y1);

// Forget every cached texture. Required whenever VRAM changes other than through the
// PPU's data port (a loaded state, a reset). Cheap; the next frame decodes what it needs.
void GpuPpu_Invalidate(void);

// Builds `out` for the frame in `cap`. Returns false when the frame needs the CPU
// renderer; `*reason` then says why (a static string).
bool GpuPpu_BuildFrame(const Ppu *ppu, const PpuLineCapture *cap, GpuFrame *out, const char **reason);

typedef struct {
  int surfaces, tiles_decoded, sprites, screen_rows_composed, m7_cells_decoded;
  // Time per stage of the last build, in g_gpu_ppu_clock units (0 without a clock):
  // line analysis + bands, VRAM/CGRAM diff, sprites, BG surfaces and quads, shadow copy.
  uint64_t t_lines, t_diff, t_sprites, t_bg, t_shadow;
} GpuPpuStats;

// Optional clock for the stage times above (the 3DS frontend sets svcGetSystemTick).
extern uint64_t (*g_gpu_ppu_clock)(void);
const GpuPpuStats *GpuPpu_LastStats(void);
