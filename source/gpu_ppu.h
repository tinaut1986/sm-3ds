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
// per line limits. A colour window that splits a line becomes rectangles of clip /
// no-math per band. Mode 7 (one 1024x1024 plane, any matrix per line) is drawn as one
// affine quad per line. A layer window that hides part of a line is drawn by cutting the
// layer's quads to the visible spans. Frames it cannot draw (mode 7 EXTBG, VRAM written
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
  // Mode 7: the texel comes from ax/ay/adx/ady/ardx/ardy below instead of sx/sy.
  kGpuQuadAffine = 16,
  kGpuQuadBorder = 32,   // affine: transparent outside the 1024x1024 plane (else it wraps)
};

typedef struct {
  int16_t x, y, w, h;   // screen rectangle; may stick out left/right, rows stay in the band
  int16_t sx, sy;       // texel at the top-left corner before flipping; wraps around
  uint8_t tex;          // index into GpuFrame.tex
  uint8_t level;        // priority 1..15, higher wins over lower (the backdrop is 0)
  uint8_t flags;
  // kGpuQuadAffine: plane position of the quad's top-left pixel, its step per pixel and
  // per row, in 1/256 texel, with the CPU renderer's 32-bit wrapping arithmetic: pixel
  // (x + i, y + r) shows texel ((ax + adx*i + ardx*r) >> 8, (ay + ady*i + ardy*r) >> 8),
  // both & 1023.
  int32_t ax, ay, adx, ady, ardx, ardy;
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
  // A colour window that splits lines: rectangles where, on top of `clip` and `math`,
  // the main colour is clipped to black and/or colour math is prevented.
  int cw_first, cw_count;
} GpuBand;

typedef struct {
  int16_t x, y, w, h;
  bool clip, no_math;
} GpuCwRect;

enum {
  kGpuMaxQuads = 4096, kGpuMaxBands = 32, kGpuMaxTex = 48, kGpuRows = 224, kGpuMaxCwRects = 1024,
  // Margins: up to 160 px a side and 256 in all (the citro3d targets are 512 wide).
  kGpuMaxMargin = 160, kGpuMaxMargins = 256,
  kGpuMaxMasks = 16, kGpuMaxExtraRows = 16,
};

// Drawn black over the finished image (e.g. the WIDE margins next to the HUD).
typedef struct {
  int16_t x, y, w, h;
} GpuMaskRect;

typedef struct {
  // Columns the frame covers: [0, 256), or wider for the WIDE view (GpuPpu_SetMargin).
  // Outside 0..255 the windows count as "outside" both windows, as the zelda3/snesrev
  // extra side space does. Rows: [0, 224), or with extra rows above and below
  // (GpuPpu_SetExtraRows), which repeat the first and last line's registers.
  int x0, x1, y0, y1;
  GpuTex *tex[kGpuMaxTex];
  int tex_count;
  GpuQuad quads[kGpuMaxQuads];
  int quad_count;
  GpuBand bands[kGpuMaxBands];
  int band_count;
  GpuCwRect cw[kGpuMaxCwRects];
  int cw_count;
  // Cleared by GpuPpu_BuildFrame; the caller may add masks before drawing the frame.
  GpuMaskRect mask[kGpuMaxMasks];
  int mask_count;
} GpuFrame;

// Implemented by the backend. Texels start out as zero (transparent).
bool GpuBackend_TexCreate(GpuTex *t, int w, int h);
void GpuBackend_TexFree(GpuTex *t);
// The CPU changed texels in rows [y0, y1); flush them to where the GPU reads them.
void GpuBackend_TexWritten(GpuTex *t, int y0, int y1);

// Forget every cached texture. Required whenever VRAM changes other than through the
// PPU's data port (a loaded state, a reset). Cheap; the next frame decodes what it needs.
void GpuPpu_Invalidate(void);

// Pixels to add on the left and right of the next frames built: the WIDE view. BG layers
// and sprites are drawn there as the PPU would if the screen were wider; mode 7 rows stay
// 256 wide. Not necessarily even: next to a room edge the view leans away from it.
void GpuPpu_SetMargins(int left, int right);
static inline void GpuPpu_SetMargin(int margin) { GpuPpu_SetMargins(margin, margin); }

// Where BG3 is drawn on the narrow (HUD) rows of GpuPpu_SetNarrowBg3Rows: columns
// [x, x + 256). 0 = where the PPU puts it; with uneven margins, the HUD keeps its place on
// the screen by moving by the difference.
void GpuPpu_SetHudX(int x);

// Added to a BG layer's horizontal scroll on every line of the next frames (0..2 = BG1..3).
void GpuPpu_SetLayerShiftX(int layer, int dx);

// Full position of each of the 128 OAM entries (INT16_MIN = unknown, a Y of 0x4000 or more
// = hidden), used instead of the 9-bit X and the wrapping 8-bit Y when set: with margins
// a sprite far off one side would read as one in the other side's margin, and one below
// the screen as one at the top. Set them only then (SM's WIDE view: g_rtl_oam_x/y); without
// margins they would hide what the SNES shows (a piece at x 500 appears at -12). NULL = none.
extern const int16_t *g_gpu_ppu_obj_x, *g_gpu_ppu_obj_y;
// Per OAM entry, non-zero: the sprite is part of the HUD and is drawn moved by the HUD's
// offset (GpuPpu_SetHudX), as the HUD keeps its place when the view leans. NULL = none.
extern const uint8_t *g_gpu_ppu_obj_hud;

// Sprites do not wrap from the bottom to the top (SM's WIDE view: its HUD rows show
// sprites, which on the SNES never showed there).
void GpuPpu_SetNoSpriteWrap(bool no_wrap);

// Rows to add above and below the 224 of the next frames built (0..kGpuMaxExtraRows):
// the first band grows up and the last one down, BG layers keep their scroll, sprites
// show there (an SM sprite parked off-screen at x 0x180, y 0xE0 is left out).
void GpuPpu_SetExtraRows(int top, int bottom);

// Bands entirely within output rows [0, rows) keep BG3 within 0..255 when margins are
// on: SM's HUD is a 256 px BG3 tilemap that would repeat into them.
void GpuPpu_SetNarrowBg3Rows(int rows);

// Also narrow (and drawn at the HUD's place): any band whose BG3 uses this 32x32 tilemap
// (VRAM word address; -1 = none). SM's message boxes: BG3SC 0x58.
void GpuPpu_SetNarrowBg3Map(int tilemap_adr);

// Bands entirely within the narrow BG3 rows (the HUD, GpuPpu_SetNarrowBg3Rows) that are
// not mode 7 get the mode 7 plane on the main screen under them, from their own lines'
// matrix registers: SM switches to mode 7 below the HUD, so with WIDE the plane goes on
// under it (the HUD lines keep only BG3 and sprites there).
void GpuPpu_SetMode7UnderHud(bool on);

// Window 2's real extent per captured line, [x0, x1) in view columns, for shapes the game
// cuts to 0..255 (the power bomb): used instead of the registers on lines where cutting it
// as SM does gives exactly the captured WH2/WH3. x0 = kGpuWinNone: none. NULL = off.
enum { kGpuWinNone = -32768 };
void GpuPpu_SetWindow2Extent(const int16_t (*ext)[2]);

// Adds a black mask rectangle to `f`, clipped to its columns; false if the list is full.
bool GpuPpu_AddMask(GpuFrame *f, int x, int y, int w, int h);

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
