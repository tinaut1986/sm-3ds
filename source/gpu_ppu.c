#include "gpu_ppu.h"
#include "stereo_depth.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Columns drawn by the frame being built: [0, 256) plus the WIDE margins.
static int g_margin_l, g_margin_r, g_x0 = 0, g_x1 = 256, g_hud_x;

static int g_narrow_bg3_rows;
static bool g_m7_under_hud;

void GpuPpu_SetMode7UnderHud(bool on) { g_m7_under_hud = on; }
static bool g_no_sprite_wrap;
static int g_sprite_cap = 3;   // GpuPpu_SetSpriteCap

void GpuPpu_SetNoSpriteWrap(bool no_wrap) { g_no_sprite_wrap = no_wrap; }
const int16_t *g_gpu_ppu_obj_x, *g_gpu_ppu_obj_y;
const uint8_t *g_gpu_ppu_obj_hud;
static int g_extra_top, g_extra_bottom;

void GpuPpu_SetExtraRows(int top, int bottom) {
  g_extra_top = top < 0 ? 0 : top > kGpuMaxExtraRows ? kGpuMaxExtraRows : top;
  g_extra_bottom = bottom < 0 ? 0 : bottom > kGpuMaxExtraRows ? kGpuMaxExtraRows : bottom;
}

void GpuPpu_SetNarrowBg3Rows(int rows) { g_narrow_bg3_rows = rows; }

static int g_narrow_bg3_map = -1;

void GpuPpu_SetNarrowBg3Map(int tilemap_adr) { g_narrow_bg3_map = tilemap_adr; }

// The HUD's rows are drawn with the first gameplay line's settings (its FX layer, colour
// math) and the HUD itself from the captured lines into the HUD list (BuildFrame).
static bool g_hud_synth;
static bool g_force_narrow, g_no_window_cut;   // while the HUD is emitted from its own lines

// Whether BG3 stays within the 256 px view on lines [l0, l1].
static bool NarrowBg3(const PpuLineCapture *cap, int l0, int l1) {
  if (g_force_narrow) return true;
  if (l1 <= g_narrow_bg3_rows) return !g_hud_synth;
  const BgLayer *bg = &cap->line[l0].bgLayer[2];
  return bg->tilemapAdr == g_narrow_bg3_map && !bg->tilemapWider && !bg->tilemapHigher;
}

void GpuPpu_SetMargins(int left, int right) {
  g_margin_l = left < 0 ? 0 : left > kGpuMaxMargin ? kGpuMaxMargin : left;
  g_margin_r = right < 0 ? 0 : right > kGpuMaxMargin ? kGpuMaxMargin : right;
  if (g_margin_l + g_margin_r > kGpuMaxMargins) g_margin_r = kGpuMaxMargins - g_margin_l;
}

static bool g_crop_to_view;

void GpuPpu_SetCropToView(bool crop) { g_crop_to_view = crop; }

void GpuPpu_SetHudX(int x) { g_hud_x = x; }

static int g_hud_y;

void GpuPpu_SetHudY(int y) { g_hud_y = y; }

static int g_layer_dx[3];
static struct { bool on; int x0, x1, y0, y1; } g_clip[3];

void GpuPpu_SetLayerClip(int layer, int x0, int x1, int y0, int y1) {
  if (layer < 0 || layer >= 3) {   // none: all of them off
    for (int i = 0; i < 3; i++) g_clip[i].on = false;
    return;
  }
  g_clip[layer].on = true;
  g_clip[layer].x0 = x0, g_clip[layer].x1 = x1, g_clip[layer].y0 = y0, g_clip[layer].y1 = y1;
}

void GpuPpu_SetLayerShiftX(int layer, int dx) {
  if (layer >= 0 && layer < 3) g_layer_dx[layer] = dx;
}

bool GpuPpu_AddMask(GpuFrame *f, int x, int y, int w, int h) {
  if (x < f->x0) w -= f->x0 - x, x = f->x0;
  if (x + w > f->x1) w = f->x1 - x;
  if (w <= 0 || h <= 0) return true;
  if (f->mask_count >= kGpuMaxMasks) return false;
  f->mask[f->mask_count++] = (GpuMaskRect){ (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h };
  return true;
}

// ---- Window evaluation (PpuWindows_Calc in ppu.c) ----------------------------------
// The margins are outside both windows, like PpuWindows_Calc's extra border.

typedef struct {
  int16_t edges[6];
  uint8_t nr, bits;
} Win;

enum { kWin1Inversed = 1, kWin1Enabled = 2, kWin2Inversed = 4, kWin2Enabled = 8 };

static const int16_t (*g_win2_ext)[2];

void GpuPpu_SetWindow2Extent(const int16_t (*ext)[2]) { g_win2_ext = ext; }

static const int16_t (*g_cone)[2];
static int g_cone_window;

void GpuPpu_SetWindowCone(int window, const int16_t (*cone)[2]) { g_cone = cone, g_cone_window = window; }

// Window `w` (1 or 2) on `line` with the cone's margins (GpuPpu_SetWindowCone): returns
// whether it applies, with the window as [*l, *r) on the frame's columns.
static bool WinCone(const PpuLineState *st, int w, int line, int *l, int *r) {
  if (!g_cone || w != g_cone_window || line >= kPpuCaptureLines || g_cone[line][0] == kGpuWinNone) return false;
  const int gl = g_cone[line][0], gr = g_cone[line][1];
  const int wl = w == 1 ? st->window1left : st->window2left, wr = w == 1 ? st->window1right : st->window2right;
  if (line > kGpuRows || (g_hud_synth && line <= g_narrow_bg3_rows)) {   // registers that are not the cone's
    *l = gl, *r = gr;
  } else if (wl > wr) {
    // Empty in the view: the cone may still cross a margin.
    if (gl < 0) *l = gl, *r = gr < 0 ? gr : 0;
    else if (gr > 256) *l = gl > 256 ? gl : 256, *r = gr;
    else return false;
  } else {
    *l = wl == 0 && gl < 0 ? gl : wl;
    *r = wr == 255 && gr > 256 ? gr : wr + 1;
  }
  *l = *l < g_x0 ? g_x0 : *l > g_x1 ? g_x1 : *l;
  *r = *r > g_x1 ? g_x1 : *r < *l ? *l : *r;
  return true;
}

// Window 2 on `line` from its real extent, when the registers are that extent cut to the
// screen as SM's power bomb cuts it (wholly off one side: left 255 / right 254, or left 1
// / right 0).
static bool Win2Extent(const PpuLineState *st, int line, int *l, int *r) {
  if (!g_win2_ext || g_win2_ext[line][0] == kGpuWinNone) return false;
  const int x0 = g_win2_ext[line][0], x1 = g_win2_ext[line][1];
  int wl, wr;
  if (x0 > 255) wl = 255, wr = 254;
  else if (x1 <= 0) wl = 1, wr = 0;
  else wl = x0 < 0 ? 0 : x0, wr = x1 > 256 ? 255 : x1 - 1;
  if (st->window2left != wl || st->window2right != wr) return false;
  *l = x0 < g_x0 ? g_x0 : x0 > g_x1 ? g_x1 : x0;
  *r = x1 > g_x1 ? g_x1 : x1 < *l ? *l : x1;
  return true;
}

// A window reaching the 256 px view's edge (left 0, right 255) goes on to the frame's edge:
// SM's shapes (power bomb, X-ray beam) are cut to the screen per line, so touching its
// edge means they would continue past it. Window 2's real extent replaces that guess
// where it is known (GpuPpu_SetWindow2Extent).
static void WinCalc(Win *win, const PpuLineState *st, int layer, int line) {
  const uint32_t winflags = st->windowsel >> (layer * 4);
  unsigned nr = 1, i, j;
  int t;
  // Each window as [l, r) on the frame's columns.
  int l1 = st->window1left == 0 ? g_x0 : st->window1left;
  int r1 = st->window1right == 255 ? g_x1 : st->window1right + 1;
  const bool cone1 = WinCone(st, 1, line, &l1, &r1);
  int l2 = st->window2left == 0 ? g_x0 : st->window2left;
  int r2 = st->window2right == 255 ? g_x1 : st->window2right + 1;
  const bool ext2 = WinCone(st, 2, line, &l2, &r2) || Win2Extent(st, line, &l2, &r2);
  win->edges[0] = (int16_t)g_x0;
  win->edges[1] = (int16_t)g_x1;
  const bool w1 = (winflags & kWin1Enabled) && (cone1 ? l1 < r1 : st->window1left <= st->window1right);
  if (w1) {
    if (l1 > win->edges[0]) {
      win->edges[nr] = (int16_t)l1;
      win->edges[++nr] = (int16_t)g_x1;
    }
    if (r1 < g_x1) {
      win->edges[nr] = (int16_t)r1;
      win->edges[++nr] = (int16_t)g_x1;
    }
  }
  const bool w2 = (winflags & kWin2Enabled) && (ext2 ? l2 < r2 : st->window2left <= st->window2right);
  if (w2) {
    for (i = 0; i <= nr && (t = l2) != win->edges[i]; i++) {
      if (t < win->edges[i]) {
        for (j = nr++; j >= i; j--) win->edges[j + 1] = win->edges[j];
        win->edges[i] = (int16_t)t;
        break;
      }
    }
    for (; i <= nr && (t = r2) != win->edges[i]; i++) {
      if (t < win->edges[i]) {
        for (j = nr++; j >= i; j--) win->edges[j + 1] = win->edges[j];
        win->edges[i] = (int16_t)t;
        break;
      }
    }
  }
  win->nr = nr;
  uint8_t w1_bits = 0, w2_bits = 0;
  if (w1) {
    for (i = 0; win->edges[i] != l1; i++) {}
    for (j = i; win->edges[j] != r1; j++) {}
    w1_bits = ((1 << (j - i)) - 1) << i;
  }
  if ((winflags & (kWin1Enabled | kWin1Inversed)) == (kWin1Enabled | kWin1Inversed)) w1_bits = ~w1_bits;
  if (w2) {
    for (i = 0; win->edges[i] != l2; i++) {}
    for (j = i; win->edges[j] != r2; j++) {}
    w2_bits = ((1 << (j - i)) - 1) << i;
  }
  if ((winflags & (kWin2Enabled | kWin2Inversed)) == (kWin2Enabled | kWin2Inversed)) w2_bits = ~w2_bits;
  win->bits = w1_bits | w2_bits;
}

// ---- Per-line analysis ------------------------------------------------------------

typedef struct {
  uint8_t main, sub;     // layers that draw on this line (TM/TS minus fully windowed)
  uint8_t partial[2];    // of those, layers a window hides on part of the line, per screen
  bool clip, math_ok;    // colour window outcome, uniform over the line
  bool cw_split;         // ... unless the colour window splits it: then see g_cw
} LineInfo;

// A colour window that splits a line: its segments, each clipped to black and/or with
// colour math prevented (kept outside the band key, like g_spans).
typedef struct {
  uint8_t n;
  struct { int16_t x0, x1; bool clip, no_math; } seg[5];
} CwSegs;
static CwSegs g_cw[kPpuCaptureLines];

// Where a partially windowed layer is visible on a line: up to 3 pixel spans [x0, x1).
// Kept apart from LineInfo, which is part of the band key: the spans may change on every
// line of a band (power bomb, the file-select map) without splitting it.
typedef struct {
  uint8_t n;
  int16_t x[3][2];
} WinSpans;
static WinSpans g_spans[kPpuCaptureLines][2][5];   // [line][screen][layer]

// Fills `info` (and g_spans[line]); returns NULL if the line can be drawn, else why not.
static const char *AnalyzeLine(const PpuLineState *st, LineInfo *info, int line) {
  memset(info, 0, sizeof(*info));
  if (st->forcedBlank) return NULL;
  if (st->mode != 1 && st->mode != 7) return "mode is not 1 or 7";
  if (st->mode == 7 && st->m7extBg) return "mode 7 EXTBG";
  if (st->mode == 7 && st->m7largeField && st->m7charFill) return "mode 7 tile 0 fill";
  if (st->objInterlace) return "OBJ interlace";
  for (int sub = 0; sub < 2; sub++) {
    // Mode 7 has one BG; the CPU renderer ignores BG2/BG3 there.
    uint8_t on = st->screenEnabled[sub] & (st->mode == 7 ? 0x11 : 0x17);
    for (int layer = 0; layer < 5; layer++) {
      if (!(on & (1 << layer)) || !(st->screenWindowed[sub] & (1 << layer))) continue;
      Win w;
      WinCalc(&w, st, layer, line);
      // Segment i is [edges[i], edges[i+1]); its bit set means the window hides it.
      const uint8_t all = (uint8_t)((1 << w.nr) - 1);
      if ((w.bits & all) == all) {
        on &= ~(1 << layer);
      } else if (w.bits & all) {
        WinSpans *sp = &g_spans[line][sub][layer];
        sp->n = 0;
        for (int i = 0; i < w.nr; i++) {
          if (w.bits & (1 << i)) continue;
          if (sp->n && sp->x[sp->n - 1][1] == w.edges[i]) {   // joins the previous span
            sp->x[sp->n - 1][1] = w.edges[i + 1];
          } else {
            if (sp->n == 3) return "window splits a line in more than 3";
            sp->x[sp->n][0] = w.edges[i];
            sp->x[sp->n][1] = w.edges[i + 1];
            sp->n++;
          }
        }
        info->partial[sub] |= 1 << layer;
      }
    }
    for (int c = 0; c < 3; c++)
      if (g_clip[c].on && (line - 1 < g_clip[c].y0 || line - 1 >= g_clip[c].y1)) on &= (uint8_t)~(1 << c);
    if (sub) info->sub = on; else info->main = on;
  }
  // The colour window only matters when clip or prevent-math is "inside" or "outside"
  // (modes 1, 2); "never" and "always" do not look at it.
  const bool cw_used = st->clipMode == 1 || st->clipMode == 2 || st->preventMathMode == 1 || st->preventMathMode == 2;
  Win cw;
  WinCalc(&cw, st, 5, line);
  static const uint8_t kCwBitsMod[8] = { 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00 };
  if (cw_used && cw.nr > 1) {
    // Per segment, as PpuDrawWholeLine does with the colour window's bits.
    CwSegs *cs = &g_cw[line];
    cs->n = 0;
    for (int i = 0; i < cw.nr; i++) {
      const uint32_t b = ((cw.bits >> i) & 1) ? 0xff : 0;
      const uint32_t cm = ((b & kCwBitsMod[st->clipMode]) ^ kCwBitsMod[st->clipMode + 4]) |
                          ((b & kCwBitsMod[st->preventMathMode]) ^ kCwBitsMod[st->preventMathMode + 4]) << 8;
      cs->seg[cs->n].x0 = cw.edges[i];
      cs->seg[cs->n].x1 = cw.edges[i + 1];
      cs->seg[cs->n].clip = !(cm & 1);
      cs->seg[cs->n].no_math = !(cm & 0x100);
      cs->n++;
    }
    info->clip = false;
    info->math_ok = true;
    info->cw_split = true;
    return NULL;
  }
  const uint32_t bits = (cw.bits & 1) ? 0xff : 0;
  const uint32_t clip_math = ((bits & kCwBitsMod[st->clipMode]) ^ kCwBitsMod[st->clipMode + 4]) |
                             ((bits & kCwBitsMod[st->preventMathMode]) ^ kCwBitsMod[st->preventMathMode + 4]) << 8;
  info->clip = !(clip_math & 1);
  info->math_ok = (clip_math & 0x100) != 0;
  return NULL;
}

// Two lines are in the same band when they differ at most in the BG scrolls.
typedef struct {
  PpuLineState st;
  LineInfo info;
} LineKey;

static void MakeKey(LineKey *k, const PpuLineState *st, const LineInfo *info) {
  memset(k, 0, sizeof(*k));
  k->st = *st;
  for (int i = 0; i < 4; i++) k->st.bgLayer[i].hScroll = k->st.bgLayer[i].vScroll = 0;
  if (!st->objPriority) k->st.oamAdr = 0;
  // Window bounds: their effect is in `info` (layers on/off, colour window) or, for a
  // layer windowed on part of the line, in g_spans.
  k->st.window1left = k->st.window1right = k->st.window2left = k->st.window2right = 0;
  k->st.evenFrame = false;
  k->st.mosaicSize = 0;
  k->st.fixedColorR = k->st.fixedColorG = k->st.fixedColorB = 0;   // per row: GpuBandFixed
  memset(k->st.m7matrix, 0, sizeof(k->st.m7matrix));
  k->info = *info;
}

// ---- Change tracking -----------------------------------------------------------------
// What changed since the last frame the surfaces were synchronised with. VRAM: the PPU
// marks changed 8-word groups as they are written (g_ppu_vram_dirty), which replaced a
// compare and copy of the whole 64 KB every frame (~2.9 ms on a 2DS). CGRAM is small
// enough to diff against a copy.

static uint16_t g_cgram_shadow[0x100];
static bool g_shadow_valid;             // false: treat everything as changed
static uint8_t g_group_dirty[0x1000];   // per 8 VRAM words
static bool g_pal4_dirty[8], g_pal2_dirty[8];
// Which colours of each palette row changed this frame (bit i = colour i), 4bpp rows of 16 and 2bpp rows of 4.
static uint16_t g_pal4_changed[8], g_pal2_changed[8];
static GpuPpuStats g_stats;
uint64_t (*g_gpu_ppu_clock)(void);
static inline uint64_t Clock(void) { return g_gpu_ppu_clock ? g_gpu_ppu_clock() : 0; }

static uint32_t g_colour_dirty[8];   // per CGRAM entry (256 bits), for mode 7

static void DiffMemories(const Ppu *ppu) {
  g_ppu_vram_dirty = g_group_dirty;   // tracking starts with the first frame built
  if (!g_shadow_valid) {
    memset(g_group_dirty, 1, sizeof(g_group_dirty));
    for (int i = 0; i < 8; i++) g_pal4_dirty[i] = g_pal2_dirty[i] = true, g_pal4_changed[i] = 0xffff, g_pal2_changed[i] = 0xf;
    memset(g_colour_dirty, 0xff, sizeof(g_colour_dirty));
  } else {
    for (int p = 0; p < 8; p++) {
      g_pal4_dirty[p] = memcmp(&ppu->cgram[p * 16], &g_cgram_shadow[p * 16], 32) != 0;
      g_pal2_dirty[p] = memcmp(&ppu->cgram[p * 4], &g_cgram_shadow[p * 4], 8) != 0;
      g_pal4_changed[p] = g_pal2_changed[p] = 0;
      for (int i = 0; i < 16 && g_pal4_dirty[p]; i++)
        if (ppu->cgram[p * 16 + i] != g_cgram_shadow[p * 16 + i]) g_pal4_changed[p] |= (uint16_t)(1u << i);
      for (int i = 0; i < 4 && g_pal2_dirty[p]; i++)
        if (ppu->cgram[p * 4 + i] != g_cgram_shadow[p * 4 + i]) g_pal2_changed[p] |= (uint16_t)(1u << i);
    }
    memset(g_colour_dirty, 0, sizeof(g_colour_dirty));
    for (int i = 0; i < 256; i++)
      if (ppu->cgram[i] != g_cgram_shadow[i]) g_colour_dirty[i >> 5] |= 1u << (i & 31);
  }
}

static void UpdateShadows(const Ppu *ppu) {
  memset(g_group_dirty, 0, sizeof(g_group_dirty));
  memcpy(g_cgram_shadow, ppu->cgram, sizeof(g_cgram_shadow));
  g_shadow_valid = true;
}

// ---- Tile decoding -------------------------------------------------------------------
// Table driven: a bitplane byte spreads to 8 pixels at once, palettes are converted once
// per frame, and the Morton order of a row comes from a table. A full re-decode of a
// 32x32 surface (palette change) took ~15 ms on a 2DS with the per-pixel version.

static uint32_t g_spread[2][256];   // [hflip][byte]: pixel x of the row in bit 4x
static uint8_t g_row_morton[8][8];  // GpuTexelIndex(x, r, 8)
static uint16_t g_lut_bg4[8][16], g_lut_bg2[8][4], g_lut_obj[8][16];   // RGBA5551, [0] = 0
static uint16_t g_lut_m7[256];

static void InitDecodeTables(void) {
  static bool done;
  if (done) return;
  for (int b = 0; b < 256; b++)
    for (int x = 0; x < 8; x++) {
      g_spread[0][b] |= (uint32_t)((b >> (7 - x)) & 1) << (4 * x);
      g_spread[1][b] |= (uint32_t)((b >> x) & 1) << (4 * x);
    }
  for (int r = 0; r < 8; r++)
    for (int x = 0; x < 8; x++) g_row_morton[r][x] = (uint8_t)GpuTexelIndex(x, r, 8);
  done = true;
}

static void ConvertPalettes(const Ppu *ppu) {
  for (int p = 0; p < 8; p++) {
    g_lut_bg4[p][0] = g_lut_bg2[p][0] = g_lut_obj[p][0] = 0;
    for (int i = 1; i < 16; i++) {
      g_lut_bg4[p][i] = GpuRgba5551(ppu->cgram[p * 16 + i]);
      g_lut_obj[p][i] = GpuRgba5551(ppu->cgram[128 + p * 16 + i]);
    }
    for (int i = 1; i < 4; i++) g_lut_bg2[p][i] = GpuRgba5551(ppu->cgram[p * 4 + i]);
  }
  g_lut_m7[0] = 0;
  for (int i = 1; i < 256; i++) g_lut_m7[i] = GpuRgba5551(ppu->cgram[i]);
}

// One 8x8 tile (2 or 4 bpp, chars at VRAM word `base`) into the 64-texel Morton block
// `dst`. Plane 0/1 are the low/high bytes of word `row`, planes 2/3 those of `row + 8`.
// Texels are written in place, in textures the GPU may still be reading for the previous
// frame: wait for it once per frame, before the first write (a statue in WIDE flashed on the
// console, where the parallax fill rewrote BG2 every frame; the scene recorder, which waits
// for the GPU, hid it).
static bool g_tex_waited;
static inline void TexWriteBegin(void) {
  if (g_tex_waited) return;
  g_tex_waited = true;
  GpuBackend_BeforeTexWrite();
}

// Two neighbouring 16-bit texels written at once (they are adjacent in the Morton layout: x
// 0/1, 2/3, ... of a row; may_alias because the texture is read back as 16-bit texels).
typedef uint32_t __attribute__((may_alias)) TexPair;

// Returns the OR of all the texels written: 0 = the tile has nothing visible (index 0 is transparent).
static uint32_t DecodeTile(uint16_t *dst, const Ppu *ppu, int base, int bpp, const uint16_t *lut, bool hflip, bool vflip) {
  TexWriteBegin();
  const uint32_t *spread = g_spread[hflip];
  TexPair *pairs = (TexPair *)dst;   // dst is a 128-byte block: 4-byte aligned
  uint32_t any = 0;
  for (int r = 0; r < 8; r++) {
    const int sr = vflip ? 7 - r : r;
    const uint16_t w0 = ppu->vram[(base + sr) & 0x7fff];
    uint32_t pix = spread[w0 & 0xff] | spread[w0 >> 8] << 1;
    if (bpp == 4) {
      const uint16_t w1 = ppu->vram[(base + sr + 8) & 0x7fff];
      pix |= spread[w1 & 0xff] << 2 | spread[w1 >> 8] << 3;
    }
    const uint8_t *m = g_row_morton[r];
    for (int x = 0; x < 8; x += 2, pix >>= 8) {
      const uint32_t pair = (uint32_t)lut[pix & 15] | (uint32_t)lut[(pix >> 4) & 15] << 16;
      pairs[m[x] >> 1] = pair;
      any |= pair;
    }
  }
  return any;
}

// ---- BG surfaces: a whole tilemap decoded into two textures -------------------------
// One texture holds the priority-0 tiles, the other the priority-1 tiles; each is
// transparent where the other has its tile. Sized like the tilemap (256 or 512 px
// each way), so GPU_REPEAT wrapping is the SNES's own wrap-around.
// Tiles the owner sent to another stereo plane (GpuPpu_SetSlotPlanes) leave those two and
// go to a texture of that plane (and priority), created when a tile first needs it.

enum { kSurfaces = 8, kGpuXPlanes = 8 };
static int (*g_slot_planes)(int layer, int tw, int th, uint8_t *grid);
void GpuPpu_SetSlotPlanes(int (*slot_planes)(int layer, int tw, int th, uint8_t *grid)) { g_slot_planes = slot_planes; }

typedef struct {
  bool used;
  bool fresh;              // contents unknown: decode every tile
  uint32_t key;
  uint32_t last_frame;
  uint16_t tilemap, tiles;
  bool wider, higher;
  int bpp;
  GpuTex tex[2];           // [0] priority 0, [1] priority 1
  GpuTex xtex[kGpuXPlanes][2];   // [StereoPlane + 1 - 1][priority]: tiles sent to that plane; px NULL = not created
  int xcount;              // how many of xtex exist
  uint8_t slot_plane[64 * 64];   // the plane (StereoPlane + 1, 0 = none) each tile was decoded for
  // What each texture holds, so a texture with nothing to draw costs no quad (every pixel of a quad
  // is paid by the GPU, transparent or not): per tile 0 = nothing visible, else 1 + the texture it was
  // decoded into ((plane) * 2 + priority, as TileChoice), and how many tiles each texture has.
  uint8_t ne[64 * 64];
  uint16_t occ[(kGpuXPlanes + 1) * 2];
  uint16_t cuse[1024];       // the colours (bit per index) each char uses, valid when cuse_frame is this frame's
  uint32_t cuse_frame[1024];
  uint8_t pend[64 * 64];   // a tile whose char data changed and whose decode was deferred (GpuPpu_SetDeferTiles)
  int pend_count;
  int cursor;              // the tilemap row the next frame starts the deferred tiles from
  uint16_t map[64 * 64];   // tilemap entries the textures were decoded from
} Surface;

static Surface g_surf[kSurfaces];
static int g_defer_cap, g_defer_left;   // deferred-tile budget per frame, and what is left of this frame's
static bool g_defer_pal;                 // palette changes may wait too, not only char data   // deferred-tile budget per frame, and what is left of this frame's

void GpuPpu_SetDeferTiles(int per_frame, bool palettes) { g_defer_cap = per_frame > 0 ? per_frame : 0, g_defer_pal = palettes; }

int GpuPpu_PendingTiles(void) {
  int n = 0;
  for (int i = 0; i < kSurfaces; i++) n += g_surf[i].used ? g_surf[i].pend_count : 0;
  return n;
}

// Tiles decoded this frame, by what they look like (chars, palette, flips): an animated char used by dozens of
// tilemap entries is decoded once and copied (128 bytes) to the others. Pointers into the textures are only good
// until a texture is freed, so every free, and every new frame, starts a new generation.
typedef struct {
  uint64_t key;
  uint32_t gen;
  const uint16_t *block;
  bool visible;
} TileMemo;
static TileMemo g_tile_memo[2048];
static uint32_t g_memo_gen = 1;

static uint32_t g_frame_no;

static uint32_t SurfaceKey(const BgLayer *bg, int bpp) {
  return (uint32_t)bg->tilemapAdr | (uint32_t)bg->tilemapWider << 16 | (uint32_t)bg->tilemapHigher << 17 |
         (uint32_t)(bg->tileAdr >> 12) << 18 | (uint32_t)(bpp == 2) << 22;
}

static int SurfaceW(const Surface *s) { return s->wider ? 512 : 256; }
static int SurfaceH(const Surface *s) { return s->higher ? 512 : 256; }

static uint16_t MapAddr(const Surface *s, int tx, int ty) {
  int a = s->tilemap + (ty & 31) * 32 + (tx & 31);
  if (tx & 32) a += 0x400;
  if (ty & 32) a += s->wider ? 0x800 : 0x400;
  return (uint16_t)(a & 0x7fff);
}

static void FreeExtras(Surface *s) {
  g_memo_gen++;   // the tile memo points into textures
  for (int p = 0; p < kGpuXPlanes; p++)
    for (int i = 0; i < 2; i++)
      if (s->xtex[p][i].px) GpuBackend_TexFree(&s->xtex[p][i]);
  memset(s->xtex, 0, sizeof(s->xtex));
  memset(s->slot_plane, 0, sizeof(s->slot_plane));
  s->xcount = 0;
}

static void FreeSurface(Surface *s) {
  g_memo_gen++;
  for (int i = 0; i < 2; i++)
    if (s->tex[i].px) GpuBackend_TexFree(&s->tex[i]);
  FreeExtras(s);
  memset(s, 0, sizeof(*s));
}

// The texture of plane `pl` (StereoPlane + 1) and priority, made the first time; NULL if there is no memory.
static GpuTex *ExtraTex(Surface *s, int pl, int prio) {
  GpuTex *t = &s->xtex[pl - 1][prio];
  if (!t->px) {
    if (!GpuBackend_TexCreate(t, SurfaceW(s), SurfaceH(s))) return NULL;
    s->xcount++;
  }
  return t;
}

static Surface *GetSurface(const BgLayer *bg, int bpp) {
  const uint32_t key = SurfaceKey(bg, bpp);
  Surface *lru = NULL;
  for (int i = 0; i < kSurfaces; i++) {
    Surface *s = &g_surf[i];
    if (s->used && s->key == key) return s;
    if (!lru || !s->used || (lru->used && s->last_frame < lru->last_frame)) lru = s;
  }
  FreeSurface(lru);
  lru->used = true;
  lru->fresh = true;
  lru->key = key;
  lru->tilemap = bg->tilemapAdr;
  lru->tiles = bg->tileAdr;
  lru->wider = bg->tilemapWider;
  lru->higher = bg->tilemapHigher;
  lru->bpp = bpp;
  for (int i = 0; i < 2; i++) {
    if (!GpuBackend_TexCreate(&lru->tex[i], SurfaceW(lru), SurfaceH(lru))) {
      FreeSurface(lru);
      return NULL;
    }
  }
  return lru;
}

static bool CharDirty(const Surface *s, uint16_t entry) {
  const int c = entry & 0x3ff;
  if (s->bpp == 4) {
    const int w = (s->tiles + c * 16) & 0x7fff;
    return g_group_dirty[w >> 3] || g_group_dirty[((w + 8) & 0x7fff) >> 3];
  }
  return g_group_dirty[((s->tiles + c * 8) & 0x7fff) >> 3];
}

static bool PalDirty(const Surface *s, uint16_t entry) {
  const int pal = (entry >> 10) & 7;
  return s->bpp == 4 ? g_pal4_dirty[pal] : g_pal2_dirty[pal];
}

// Which of a surface's textures a tile with entry `e` and fix `fix` goes to: the stereo plane
// (0 = the layer's own) and the priority.
static inline int TileChoice(uint16_t e, int fix) {
  const int plane = fix & 15;   // StereoPlane + 1
  const int prio = (fix >> 4) ? (fix >> 4) - 1 : (e & 0x2000) ? 1 : 0;   // a fix may set the priority the tile is drawn with
  return plane * 2 + prio;
}

// The colours (bit per palette index, bit 0 = transparent excluded by the caller) char `c` of the surface uses.
static uint16_t CharColours(const Surface *s, int c, const Ppu *ppu) {
  const int base = s->bpp == 4 ? s->tiles + c * 16 : s->tiles + c * 8;
  uint16_t mask = 0;
  for (int r = 0; r < 8; r++) {
    const uint16_t w0 = ppu->vram[(base + r) & 0x7fff];
    uint32_t pix = g_spread[0][w0 & 0xff] | g_spread[0][w0 >> 8] << 1;
    if (s->bpp == 4) {
      const uint16_t w1 = ppu->vram[(base + r + 8) & 0x7fff];
      pix |= g_spread[0][w1 & 0xff] << 2 | g_spread[0][w1 >> 8] << 3;
    }
    for (int x = 0; x < 8; x++, pix >>= 4) mask |= (uint16_t)(1u << (pix & 15));
  }
  return mask;
}

// Does this frame's palette change touch a colour the tile (entry `e`) draws with? A palette row that
// changed two colours leaves most tiles of that row exactly as they are: their texels hold the same
// colours, so they are not decoded again (the heat rooms' cycling lava, a fade of a few colours).
// A palette change of at most this many colours in a row may be spread over frames (a cycle: the heat rooms' lava turns 5
// colours of each row every few frames). A bigger one (the Landing Site's lightning, a flash, a fade) is a picture-wide
// event that must land in one frame: spread, it reached the screen a band of rows at a time.
enum { kPalSpreadMaxColours = 6 };

static bool PalSmall(const Surface *s, uint16_t e) {
  const int pal = (e >> 10) & 7;
  const uint16_t changed = (uint16_t)((s->bpp == 4 ? g_pal4_changed[pal] : g_pal2_changed[pal]) & ~1u);
  return __builtin_popcount(changed) <= kPalSpreadMaxColours;
}

static bool PalAffects(Surface *s, const Ppu *ppu, uint16_t e) {
  const int pal = (e >> 10) & 7, c = e & 0x3ff;
  const uint16_t changed = (uint16_t)((s->bpp == 4 ? g_pal4_changed[pal] : g_pal2_changed[pal]) & ~1u);   // index 0 is never drawn
  if (!changed) return false;
  if (s->cuse_frame[c] != g_frame_no) s->cuse[c] = CharColours(s, c, ppu), s->cuse_frame[c] = g_frame_no;
  return (s->cuse[c] & changed) != 0;
}

// One 8x8 tile into its block of the priority texture; the same block of the other
// textures is cleared, unless the tile was in this very texture before (`prev` = its choice
// then, -1 when unknown): every tile lives in one texture and is clear in the others.
// The blocks written are marked one by one, so only those are copied to the GPU.
static void DecodeBgTile(Surface *s, const Ppu *ppu, int tx, int ty, uint16_t e, int fix, int prev) {
  TexWriteBegin();   // the clears below write texels too
  const int w = SurfaceW(s);
  const int tile = (ty * (w >> 3)) + tx;
  const int block = tile << 6;
  const int plane = fix & 15;   // StereoPlane + 1, 0 = the layer's own
  const int prio = (fix >> 4) ? (fix >> 4) - 1 : (e & 0x2000) ? 1 : 0;
  GpuTex *to = plane ? ExtraTex(s, plane, prio) : NULL;
  const bool own = to != NULL;   // the plane's texture; if it could not be made the tile falls back to the layer's own
  if (!to) to = &s->tex[prio];
  uint16_t *dst = to->px + block;
  if (!(own || plane == 0) || prev != TileChoice(e, fix)) {
    // The tile is in this texture only: every other one the surface has is clear there.
    for (int i = 0; i < 2; i++)
      if (&s->tex[i] != to) {
        memset(s->tex[i].px + block, 0, 64 * sizeof(uint16_t));
        GpuBackend_TexBlockWritten(&s->tex[i], tile);
      }
    if (s->xcount)
      for (int p = 0; p < kGpuXPlanes; p++)
        for (int i = 0; i < 2; i++)
          if (s->xtex[p][i].px && &s->xtex[p][i] != to) {
            memset(s->xtex[p][i].px + block, 0, 64 * sizeof(uint16_t));
            GpuBackend_TexBlockWritten(&s->xtex[p][i], tile);
          }
  }
  const int pal = (e >> 10) & 7, c = e & 0x3ff;
  const int base = s->bpp == 4 ? s->tiles + c * 16 : s->tiles + c * 8;
  const uint32_t tag = (uint32_t)(e & 0x3ff) | (uint32_t)pal << 10 | (uint32_t)(e >> 14) << 13;
  const uint64_t key = (uint64_t)s->tiles << 17 | (uint64_t)(s->bpp == 4) << 16 | tag;
  TileMemo *memo = &g_tile_memo[(tag * 2654435761u ^ (uint32_t)s->tiles * 40503u) >> 21];
  bool visible;
  if (memo->gen == g_memo_gen && memo->key == key) {
    memcpy(dst, memo->block, 64 * sizeof(uint16_t));
    visible = memo->visible;
    g_stats.tiles_reused++;
  } else {
    visible = DecodeTile(dst, ppu, base, s->bpp, s->bpp == 4 ? g_lut_bg4[pal] : g_lut_bg2[pal], e & 0x4000, e & 0x8000) != 0;
    memo->key = key, memo->gen = g_memo_gen, memo->block = dst, memo->visible = visible;
  }
  GpuBackend_TexBlockWritten(to, tile);
  g_stats.tiles_decoded++;
  // Which texture the tile ended up in (the layer's own when the plane's could not be made).
  const int where = own || plane == 0 ? plane * 2 + prio : prio;
  uint8_t *ne = &s->ne[ty * 64 + tx];
  if (*ne) s->occ[*ne - 1]--;
  *ne = visible ? (uint8_t)(1 + where) : 0;
  if (visible) s->occ[where]++;
}

// Any VRAM change in words [a, a+n) (wrapping), in 8-word groups.
static bool RangeDirty(int a, int n) {
  for (int g = (a & 0x7fff) >> 3, k = 0; k < (n + 7) >> 3; k++, g = (g + 1) & 0xfff)
    if (g_group_dirty[g]) return true;
  return false;
}

// `layer` is the BG (0 or 1 = BG1, BG2) the surface is drawn for: the tile fixes are asked for it.
static void SyncSurface(Surface *s, const Ppu *ppu, int layer) {
  if (s->last_frame == g_frame_no && !s->fresh) return;   // already synced this frame
  const int tw = SurfaceW(s) >> 3, th = SurfaceH(s) >> 3;
  uint8_t grid[64 * 64];
  bool fixes = false;
  if (g_slot_planes && layer < 2) fixes = g_slot_planes(layer + 1, tw, th, grid) > 0;
  if (!fixes && s->xcount) {   // the room has none now: the tiles go back to the layer's own textures
    FreeExtras(s);
    s->fresh = true;
  }
  if (!s->fresh && !(fixes && memcmp(grid, s->slot_plane, (size_t)th * 64))) {
    // Nothing it reads changed: its tilemap, any of its 1024 chars, its palettes.
    bool pal = false;
    for (int i = 0; i < 8; i++) pal |= s->bpp == 4 ? g_pal4_dirty[i] : g_pal2_dirty[i];
    if (!pal && !s->pend_count && !RangeDirty(s->tilemap, tw * th) && !RangeDirty(s->tiles, 1024 * (s->bpp == 4 ? 16 : 8))) {
      s->last_frame = g_frame_no;
      return;
    }
  }
  if (s->fresh) {   // every tile is decoded again
    memset(s->ne, 0, sizeof(s->ne)), memset(s->occ, 0, sizeof(s->occ));
    memset(s->pend, 0, sizeof(s->pend)), s->pend_count = 0, s->cursor = 0;
    memset(s->cuse_frame, 0xff, sizeof(s->cuse_frame));
  }
  int first_deferred = -1;
  for (int k = 0; k < th; k++) {
    const int ty = (s->cursor + k) % th;   // the deferred tiles go on from where the last frame stopped
    for (int tx = 0; tx < tw; tx++) {
      const int ti = ty * 64 + tx;
      const uint16_t e = ppu->vram[MapAddr(s, tx, ty)];
      uint16_t *m = &s->map[ti];
      const uint8_t pl = fixes ? grid[ti] : 0;
      const bool cd = !s->fresh && CharDirty(s, e), pd = !s->fresh && PalDirty(s, e) && PalAffects(s, ppu, e);
      const bool moved = s->fresh || e != *m || pl != s->slot_plane[ti];
      if (!moved && !cd && !pd && !s->pend[ti]) continue;
      // Only the char data (an animated tile) or, if asked for, a small palette cycle changed: it may wait for a later frame.
      if (!moved && (!pd || (g_defer_pal && PalSmall(s, e))) && g_defer_cap > 0) {
        if (g_defer_left <= 0) {
          if (!s->pend[ti]) s->pend[ti] = 1, s->pend_count++;
          g_stats.tiles_deferred++;
          if (first_deferred < 0) first_deferred = ty;
          continue;
        }
        g_defer_left--;
      }
      if (s->pend[ti]) s->pend[ti] = 0, s->pend_count--;
      if (s->fresh) g_stats.tiles_fresh++;
      else if (e != *m) g_stats.tiles_map++;
      else if (pd) g_stats.tiles_pal++;
      else if (pl != s->slot_plane[ti]) g_stats.tiles_plane++;
      else g_stats.tiles_char++;
      const int prev = s->fresh ? -1 : TileChoice(*m, s->slot_plane[ti]);
      *m = e;
      s->slot_plane[ti] = pl;
      DecodeBgTile(s, ppu, tx, ty, e, pl, prev);
    }
  }
  s->cursor = s->pend_count && first_deferred >= 0 ? first_deferred : 0;
  // (the blocks written were marked one by one in DecodeBgTile)
  s->fresh = false;
  s->last_frame = g_frame_no;
}

static bool g_m7_fresh = true;

void GpuPpu_Invalidate(void) {
  for (int i = 0; i < kSurfaces; i++) g_surf[i].fresh = true;
  g_shadow_valid = false;
  g_m7_fresh = true;
}

// ---- Mode 7 plane -------------------------------------------------------------------------
// The whole 1024x1024 plane in one texture: 128x128 cells, the low byte of VRAM word
// y*128+x naming one of 256 8x8 tiles, whose pixels are the high bytes of words
// tile*64 .. tile*64+63, each a CGRAM index (0 = transparent). A cell is decoded when it
// is about to be shown and is stale: its map byte changed, its tile's pixels changed, or
// a colour that tile uses changed (per-tile colour masks, so a sprite palette flash does
// not redo the plane).

static GpuTex g_m7_tex;
static uint8_t g_m7_map[128 * 128];      // tile each cell was decoded with
// Cells set by hand to other stereo planes (GpuPpu_SetMode7Planes) are decoded into a texture per plane instead (and left
// transparent in g_m7_tex and the other planes'), each drawn with the same rows on its plane.
enum { kM7Splits = 2 };
static struct { GpuTex tex; uint8_t plane; } g_m7_split[kM7Splits];   // StereoPlane + 1, 0 = not used in this room
static int (*g_m7_cell_plane)(int cell, int tile);
static int g_m7_planes_key, g_m7_split_key;   // the hook's key now / the one the cells were decoded with
static int g_m7_base_plane = kStereoPlay, g_m7_fill_reach;
static int (*g_m7_fill_mode)(int cell);   // 0 from above (else below), 1 from the row, -1 none
// Per cell and texture (g_m7_tex, then the splits): the tile + 1 drawn there to fill behind a nearer cell, 0 = none.
static uint16_t g_m7_fill[1 + kM7Splits][128 * 128];
void GpuPpu_SetMode7Planes(int (*cell_plane)(int cell, int tile), int key, int base_plane, int fill_reach,
                           int (*fill_mode)(int cell)) {
  g_m7_cell_plane = cell_plane;
  g_m7_fill_mode = fill_mode;
  g_m7_planes_key = cell_plane ? key : 0;
  g_m7_base_plane = base_plane;
  g_m7_fill_reach = cell_plane ? fill_reach : 0;
}
static uint8_t g_m7_stale[128 * 128];
static uint8_t g_m7_row_stale[128];      // stale cells per row of cells: M7Sync skips clean rows

static inline void M7MarkStale(int c) {
  if (!g_m7_stale[c]) g_m7_stale[c] = 1, g_m7_row_stale[c >> 7]++;
}
static uint32_t g_m7_colours[256][8];    // CGRAM entries each tile uses
static bool g_m7_tile_known[256];        // colour mask valid

static void M7TileColours(const Ppu *ppu, int t) {
  uint32_t *m = g_m7_colours[t];
  memset(m, 0, 32);
  for (int i = 0; i < 64; i++) {
    const int c = ppu->vram[t * 64 + i] >> 8;
    m[c >> 5] |= 1u << (c & 31);
  }
  g_m7_tile_known[t] = true;
}

// Marks what this frame's changes made stale.
static void M7Track(const Ppu *ppu) {
  if (g_m7_fresh) {
    memset(g_m7_stale, 1, sizeof(g_m7_stale));
    memset(g_m7_row_stale, 128, sizeof(g_m7_row_stale));
    memset(g_m7_tile_known, 0, sizeof(g_m7_tile_known));
    g_m7_fresh = false;
    return;
  }
  bool tile_dirty[256], any_tile = false;
  bool colours = false;
  for (int i = 0; i < 8; i++) colours |= g_colour_dirty[i] != 0;
  for (int t = 0; t < 256; t++) {
    bool d = false;
    for (int g = 0; g < 8; g++) d |= g_group_dirty[t * 8 + g] != 0;   // words t*64 .. +63
    if (d) g_m7_tile_known[t] = false;
    if (!d && colours && g_m7_tile_known[t])
      for (int i = 0; i < 8 && !d; i++) d = (g_m7_colours[t][i] & g_colour_dirty[i]) != 0;
    if (!d && colours && !g_m7_tile_known[t]) d = true;
    tile_dirty[t] = d;
    any_tile |= d;
  }
  // Map bytes: cells c .. c+7 share a VRAM group.
  for (int g = 0; g < 128 * 128 / 8; g++) {
    if (!g_group_dirty[g]) continue;
    for (int c = g * 8; c < g * 8 + 8; c++) {
      if ((ppu->vram[c] & 0xff) == g_m7_map[c]) continue;
      M7MarkStale(c);
      for (int d = 1; d <= g_m7_fill_reach; d++) {   // cells that may have filled from it
        if ((c & 127) >= d) M7MarkStale(c - d);
        if ((c & 127) + d < 128) M7MarkStale(c + d);
        if (c >= d * 128) M7MarkStale(c - d * 128);
        if (c + d * 128 < 128 * 128) M7MarkStale(c + d * 128);
      }
    }
  }
  if (any_tile)
    for (int c = 0; c < 128 * 128; c++) {
      bool d = tile_dirty[g_m7_map[c]];
      for (int i = 0; i <= kM7Splits && !d && g_m7_fill_reach; i++) d = g_m7_fill[i][c] && tile_dirty[g_m7_fill[i][c] - 1];
      if (d) M7MarkStale(c);
    }
}

// The split texture of `plane` (made when first needed), or NULL when there is no room for another.
static GpuTex *M7SplitTex(int plane) {
  int free_slot = -1;
  for (int i = 0; i < kM7Splits; i++) {
    if (g_m7_split[i].plane == plane + 1) return &g_m7_split[i].tex;
    if (!g_m7_split[i].plane && free_slot < 0) free_slot = i;
  }
  if (free_slot < 0) return NULL;
  GpuTex *t = &g_m7_split[free_slot].tex;
  if (!t->px && !GpuBackend_TexCreate(t, 1024, 1024)) return NULL;
  g_m7_split[free_slot].plane = (uint8_t)(plane + 1);
  return t;
}

static bool M7IsSplit(const GpuTex *t) {
  for (int i = 0; i < kM7Splits; i++)
    if (t == &g_m7_split[i].tex) return true;
  return false;
}

// A new room or new rules: the planes are given out again and every cell is decoded again (M7Track).
static void M7SplitUpdate(void) {
  if (g_m7_planes_key == g_m7_split_key) return;
  g_m7_split_key = g_m7_planes_key;
  for (int i = 0; i < kM7Splits; i++) g_m7_split[i].plane = 0;
  g_m7_fresh = true;
}

// Tile `t` into cell `c` of `tex`; with `cover` (a fill) only where tile `cover` has a pixel, so the fill shows only
// past that pixel's edge with the 3D on, never through it.
static void M7DecodeTile(const Ppu *ppu, GpuTex *tex, int c, int t, int cover) {
  if (!g_m7_tile_known[t]) M7TileColours(ppu, t);
  uint16_t *dst = tex->px + (c << 6);   // cells are the texture's 8x8 blocks, row-major
  const uint16_t *src = &ppu->vram[t * 64], *mask = cover >= 0 ? &ppu->vram[cover * 64] : NULL;
  for (int r = 0; r < 8; r++) {
    const uint8_t *m = g_row_morton[r];
    for (int x = 0; x < 8; x++)
      dst[m[x]] = mask && !(mask[r * 8 + x] >> 8) ? 0 : g_lut_m7[src[r * 8 + x] >> 8];
  }
  // Marked block by block, as the BG tiles: the Ceres elevator decodes 16-36 cells a frame spread over ~90 rows of
  // cells, and marking the rows between them uploaded 1.4 MB a frame (13.7 ms on the 2DS) for ~4 KB of texels.
  GpuBackend_TexBlockWritten(tex, c);
}

// The plane cell `c` goes to (the hook's, or g_m7_base_plane), and the texture that holds it.
static int M7CellPlane(const Ppu *ppu, int c, GpuTex **tex) {
  const int p = g_m7_cell_plane ? g_m7_cell_plane(c, ppu->vram[c] & 0xff) : -1;
  GpuTex *split = p >= 0 ? M7SplitTex(p) : NULL;
  if (tex) *tex = split ? split : &g_m7_tex;
  return split ? p : g_m7_base_plane;
}

static void M7DecodeCell(const Ppu *ppu, int c) {
  TexWriteBegin();
  const int t = ppu->vram[c] & 0xff;
  g_m7_map[c] = (uint8_t)t;
  if (g_m7_stale[c]) g_m7_stale[c] = 0, g_m7_row_stale[c >> 7]--;
  GpuTex *tex;
  const int plane = M7CellPlane(ppu, c, &tex);
  // In every other texture that exists (one made for a previous room included: it may get a plane later) the cell is
  // transparent, or, in a texture of a farther plane, the nearest cell on that plane (within the reach) above it, else
  // below it, or in its row where the hook says so: what the other eye sees beside the nearer cell, which has no picture
  // of its own behind it.
  const int mode = g_m7_fill_mode ? g_m7_fill_mode(c) : 0;
  const bool across = mode == 1;
  const int reach = mode < 0 ? 0 : g_m7_fill_reach;
  for (int i = -1; i < kM7Splits; i++) {
    GpuTex *o = i < 0 ? &g_m7_tex : &g_m7_split[i].tex;
    if (o == tex || !o->px) continue;
    const int oplane = i < 0 ? g_m7_base_plane : g_m7_split[i].plane - 1;
    int fill = -1;
    if (oplane > plane && (i < 0 || g_m7_split[i].plane))
      for (int d = 1; d <= reach && fill < 0; d++)
        for (int s = -1; s <= 1 && fill < 0; s += 2) {
          const int x = (c & 127) + (across ? s * d : 0), y = (c >> 7) + (across ? 0 : s * d);
          const int n = y * 128 + x;
          GpuTex *xt;
          if (x >= 0 && x < 128 && y >= 0 && y < 128 && M7CellPlane(ppu, n, &xt) == oplane && xt == o) fill = ppu->vram[n] & 0xff;
        }
    g_m7_fill[i + 1][c] = (uint16_t)(fill + 1);
    if (fill >= 0) {
      M7DecodeTile(ppu, o, c, fill, t);
    } else {
      memset(o->px + (c << 6), 0, 64 * sizeof(uint16_t));
      GpuBackend_TexBlockWritten(o, c);
    }
  }
  for (int i = 0; i < kM7Splits; i++)
    if (tex == &g_m7_split[i].tex) g_m7_fill[i + 1][c] = 0;
  if (tex == &g_m7_tex) g_m7_fill[0][c] = 0;
  M7DecodeTile(ppu, tex, c, t, -1);
  g_stats.m7_cells_decoded++;
}

// Decodes the stale cells in texel rectangle [x0, x1] x [y0, y1] (may lie outside the
// plane: it wraps, as the plane does without the large field).
static void M7Sync(const Ppu *ppu, int x0, int x1, int y0, int y1) {
  int cx0 = x0 >> 3, cx1 = x1 >> 3, cy0 = y0 >> 3, cy1 = y1 >> 3;
  if (cx1 - cx0 >= 127) cx0 = 0, cx1 = 127;
  if (cy1 - cy0 >= 127) cy0 = 0, cy1 = 127;
  for (int cy = cy0; cy <= cy1; cy++) {
    if (!g_m7_row_stale[cy & 127]) continue;
    for (int cx = cx0; cx <= cx1; cx++) {
      const int c = (cy & 127) * 128 + (cx & 127);
      if (g_m7_stale[c]) M7DecodeCell(ppu, c);
    }
  }
}

// The CPU renderer's per-line setup (PpuDrawBackground_mode7): plane position of the
// line's leftmost pixel and the step per pixel, in 1/256 texel.
static void M7LineStart(const PpuLineState *st, int y, uint32_t *xpos, uint32_t *ypos, uint32_t *dx, uint32_t *dy) {
  const int16_t *m = st->m7matrix;
  const int hScroll = ((int16_t)(m[6] << 3)) >> 3, vScroll = ((int16_t)(m[7] << 3)) >> 3;
  const int xCenter = ((int16_t)(m[4] << 3)) >> 3, yCenter = ((int16_t)(m[5] << 3)) >> 3;
  int clippedH = hScroll - xCenter, clippedV = vScroll - yCenter;
  clippedH = (clippedH & 0x2000) ? (clippedH | ~1023) : (clippedH & 1023);
  clippedV = (clippedV & 0x2000) ? (clippedV | ~1023) : (clippedV & 1023);
  const uint32_t ry = st->m7yFlip ? 255 - y : y;
  const uint32_t startX = (m[0] * clippedH & ~63) + (m[1] * ry & ~63) + (m[1] * clippedV & ~63) + (xCenter << 8);
  const uint32_t startY = (m[2] * clippedH & ~63) + (m[3] * ry & ~63) + (m[3] * clippedV & ~63) + (yCenter << 8);
  const uint32_t rx = st->m7xFlip ? 255 : 0;
  *xpos = startX + m[0] * rx;
  *ypos = startY + m[2] * rx;
  *dx = st->m7xFlip ? -m[0] : m[0];
  *dy = st->m7xFlip ? -m[2] : m[2];
}

// ---- Screen-space layers ---------------------------------------------------------------
// Fallback for a layer whose scroll runs would not fit in the quad list (see EmitBg):
// its rows are copied from the surface into a screen texture on the CPU and drawn as
// one quad. 512 wide for the WIDE margins (never resized: the GPU may still be reading
// the previous frame's texture while the next one is built).

static GpuTex g_screen_tex[3][2];

static GpuTex *ScreenTex(int layer, int prio) {
  GpuTex *t = &g_screen_tex[layer][prio];
  if (!t->px && !GpuBackend_TexCreate(t, 512, 256)) return NULL;
  return t;
}

// ---- Sprites ---------------------------------------------------------------------------

static const int kSpriteSizes[8][2] = {
  { 8, 16 }, { 8, 32 }, { 8, 64 }, { 16, 32 }, { 16, 64 }, { 32, 64 }, { 16, 32 }, { 16, 32 },
};

enum { kAtlasW = 512, kAtlasH = 512 };
static GpuTex g_atlas;

typedef struct {
  int size, x, y;         // screen position (x signed, may be in a margin; y 0..255, wraps)
  bool no_wrap;           // y is the full row (g_gpu_ppu_obj_y): no wrap at 256
  int ax, ay;             // in the atlas
  uint8_t level;
  bool hflip, vflip, math;
  bool hud;               // g_gpu_ppu_obj_hud: drawn with the HUD's BG3, over the masks
} Sprite;

typedef struct {
  uint16_t name_attr;     // what the pixels depend on: name, table, palette
  uint8_t size;
  int16_t ax, ay;
} AtlasEntry;

static Sprite g_sprites[128];
static int g_sprite_count;
static AtlasEntry g_atlas_entries[128];
static int g_atlas_entry_count, g_shelf_x, g_shelf_y, g_shelf_h;

static void DecodeSpriteTile(const Ppu *ppu, int obj_adr, int tile, int pal, int dx, int dy) {
  uint16_t *dst = g_atlas.px + (((dy >> 3) * (kAtlasW >> 3) + (dx >> 3)) << 6);
  DecodeTile(dst, ppu, obj_adr + tile * 16, 4, g_lut_obj[pal], false, false);
}

// Places a sprite's pixels in the atlas (once per distinct look per frame).
static bool AtlasPlace(const Ppu *ppu, const PpuLineState *st, uint16_t attr, int size, int *ax, int *ay) {
  const uint16_t key = attr & 0x1ff;   // name + table; the palette is below
  const int pal = (attr >> 9) & 7;
  const uint16_t full = key | pal << 9;
  for (int i = 0; i < g_atlas_entry_count; i++) {
    const AtlasEntry *e = &g_atlas_entries[i];
    if (e->name_attr == full && e->size == size) {
      *ax = e->ax;
      *ay = e->ay;
      return true;
    }
  }
  if (g_shelf_x + size > kAtlasW) {
    g_shelf_x = 0;
    g_shelf_y += g_shelf_h;
    g_shelf_h = 0;
  }
  if (g_shelf_y + size > kAtlasH || g_atlas_entry_count >= 128) return false;
  *ax = g_shelf_x;
  *ay = g_shelf_y;
  g_shelf_x += size;
  if (size > g_shelf_h) g_shelf_h = size;
  g_atlas_entries[g_atlas_entry_count++] = (AtlasEntry){ full, (uint8_t)size, (int16_t)*ax, (int16_t)*ay };
  const int obj_adr = (attr & 0x100) ? st->objTileAdr2 : st->objTileAdr1;
  const int name = attr & 0xff;
  for (int ty = 0; ty < size / 8; ty++)
    for (int tx = 0; tx < size / 8; tx++)
      DecodeSpriteTile(ppu, obj_adr, ((((name >> 4) + ty) << 4) | (((name & 0xf) + tx) & 0xf)), pal, *ax + tx * 8,
                       *ay + ty * 8);
  return true;
}

// The sprite list for one band's OBJ settings, in OAM order (first = on top).
static const char *BuildSprites(const Ppu *ppu, const PpuLineState *st) {
  g_sprite_count = 0;
  uint8_t index = st->objPriority ? (st->oamAdr & 0xfe) : 0;
  for (int i = 0; i < 128; i++, index += 2) {
    const int size = kSpriteSizes[st->objSize][(ppu->highOam[index >> 3] >> ((index & 7) + 1)) & 1];
    int x = ppu->oam[index] & 0xff;
    x |= ((ppu->highOam[index >> 3] >> (index & 7)) & 1) << 8;
    // X is 9 bits: 256..511 is off-screen left. With a right margin, 256..256+margin is
    // there instead; a sprite seen in the left margin (x >= -margin-64) never reads as
    // that while the margin is under 96.
    int y = ppu->oam[index] >> 8;
    // SM parks unused sprites at x 0x180, y 0xE0: off-screen on the SNES, but in reach of
    // the extra rows and a wide left margin.
    if (x == 0x180 && y == 0xe0 && (g_margin_l || g_margin_r || g_extra_bottom)) continue;
    // The full position when the game recorded it (and it matches these bits).
    const int16_t fx = g_gpu_ppu_obj_x ? g_gpu_ppu_obj_x[index >> 1] : INT16_MIN;
    const int16_t fy = g_gpu_ppu_obj_y ? g_gpu_ppu_obj_y[index >> 1] : INT16_MIN;
    const bool full = fx != INT16_MIN && (fx & 0x1ff) == x;
    if (full) x = fx;
    else if (x >= g_x1) x -= 512;
    const bool hud = g_gpu_ppu_obj_hud && g_gpu_ppu_obj_hud[index >> 1];
    if (hud) x += g_hud_x;   // moves with the HUD
    if (x <= g_x0 - size || x >= g_x1) continue;
    bool no_wrap = false;
    if (full && fy != INT16_MIN) {
      if (fy >= 0x4000) continue;   // parked off-screen on purpose
      if ((fy & 0xff) == y) y = fy, no_wrap = true;
    }
    if (hud) y += g_hud_y;
    // SM's WIDE view shows sprites on the HUD rows, where the SNES had them off: a piece
    // below the screen or parked at y 0xF0 wrapped to the top (Ceres: stray pieces there).
    // The price: a piece above the top edge shows only once it is fully on screen.
    if (g_no_sprite_wrap) no_wrap = true;
    // Visible on rows y .. y+size-1, wrapping at 256 (unless no_wrap); rows 224+ (plus the
    // extra rows) are not shown.
    if (no_wrap ? (y + size <= -g_extra_top || y >= kGpuRows + g_extra_bottom)
                : (y + size <= 256 && y >= kGpuRows + g_extra_bottom))
      continue;
    const uint16_t attr = ppu->oam[index + 1];
    Sprite *sp = &g_sprites[g_sprite_count];
    if (!AtlasPlace(ppu, st, attr, size, &sp->ax, &sp->ay)) return "sprite atlas full";
    sp->size = size;
    sp->x = x;
    sp->y = y;
    const int prio = (attr >> 12) & 3;
    sp->level = (uint8_t)((!hud && prio > g_sprite_cap ? g_sprite_cap : prio) * 4 + 2);
    sp->hflip = attr & 0x4000;
    sp->vflip = attr & 0x8000;
    sp->no_wrap = no_wrap;
    sp->math = (attr & 0x800) && st->mathEnabled[4];
    sp->hud = hud;
    g_sprite_count++;
  }
  g_stats.sprites = g_sprite_count;
  return NULL;
}

// ---- Frame building ----------------------------------------------------------------------

static GpuFrame *g_out;

static int TexIndex(GpuTex *t) {
  for (int i = 0; i < g_out->tex_count; i++)
    if (g_out->tex[i] == t) return i;
  if (g_out->tex_count >= kGpuMaxTex) return -1;
  g_out->tex[g_out->tex_count] = t;
  return g_out->tex_count++;
}

static int (*g_plane_rule)(int layer, int prio);
void GpuPpu_SetPlaneRule(int (*rule)(int layer, int prio)) { g_plane_rule = rule; }
static uint8_t g_quad_plane;   // GpuQuad.plane of the quads being added
static int g_msgbox_map = -1;
static bool g_msgbox;          // the BG3 being added is a message box
void GpuPpu_SetMessageBoxMap(int tilemap_adr) { g_msgbox_map = tilemap_adr; }

// Sets g_quad_plane from the rule of (layer, prio): the quads added next carry it.
static void UsePlaneRule(int layer, int prio) {
  const int p = g_plane_rule ? g_plane_rule(layer, prio) : -1;
  g_quad_plane = p >= 0 ? (uint8_t)(p + 1) : 0;
  if (g_msgbox && layer == 3) g_quad_plane = kStereoHud + 1;   // text in front of everything, whatever the room's rules
}

static bool AddQuad(GpuTex *t, int x, int y, int w, int h, int sx, int sy, int level, int flags) {
  const int ti = TexIndex(t);
  if (ti < 0 || g_out->quad_count >= kGpuMaxQuads || h <= 0) return ti >= 0 && h <= 0;
  g_out->quads[g_out->quad_count++] = (GpuQuad){ (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (int16_t)sx, (int16_t)sy,
                                                 (uint8_t)ti, (uint8_t)level, (uint8_t)flags, g_quad_plane, 0, 0, 0, 0, 0, 0 };
  return true;
}

// Mode 7 rows: see GpuQuad.ax.
static bool AddAffine(GpuTex *t, int x, int y, int w, int h, uint32_t ax, uint32_t ay, uint32_t adx, uint32_t ady,
                      uint32_t ardx, uint32_t ardy, int level, int flags) {
  if (!AddQuad(t, x, y, w, h, 0, 0, level, flags | kGpuQuadAffine)) return false;
  GpuQuad *q = &g_out->quads[g_out->quad_count - 1];
  q->ax = (int32_t)ax, q->ay = (int32_t)ay, q->adx = (int32_t)adx, q->ady = (int32_t)ady;
  q->ardx = (int32_t)ardx, q->ardy = (int32_t)ardy;
  return true;
}

static const LineInfo *g_info;   // per line, for the frame being built
static struct { uint32_t ax, ay, adx, ady; } g_affine;   // the affine row AddQuadWin is cutting

static bool SameSpans(const WinSpans *a, const WinSpans *b) {
  return a->n == b->n && !memcmp(a->x, b->x, a->n * sizeof(a->x[0]));
}

// AddQuad for `layer` on screen `scr`: where a window hides that layer on part of the
// lines, the quad is cut into the visible rectangles (rows with the same spans grouped),
// each keeping the texels it showed, flips included.
// The captured line a row of the picture takes its window settings from: the extra rows above the 224
// (negative rows) are the first line's; the ones below are lines of their own (copies of the last, but a
// window cone moves along them), up to the last one built.
static inline int WinLine(int row) {
  const int rows = kGpuRows + g_extra_bottom;
  return (row < 0 ? 0 : row >= rows ? rows - 1 : row) + 1;
}

static bool AddQuadWin(GpuTex *t, int x, int y, int w, int h, int sx, int sy, int level, int flags, int scr,
                       int layer) {
  // Bands share LineInfo, so the band's first row tells whether the layer is partial.
  if (g_no_window_cut || !(g_info[WinLine(y)].partial[scr] & (1 << layer))) return AddQuad(t, x, y, w, h, sx, sy, level, flags);
  for (int r0 = y; r0 < y + h;) {
    const WinSpans *sp = &g_spans[WinLine(r0)][scr][layer];
    int r1 = r0 + 1;
    while (r1 < y + h && SameSpans(sp, &g_spans[WinLine(r1)][scr][layer])) r1++;
    for (int i = 0; i < sp->n; i++) {
      const int x0 = sp->x[i][0] > x ? sp->x[i][0] : x;
      const int x1 = sp->x[i][1] < x + w ? sp->x[i][1] : x + w;
      if (x0 >= x1) continue;
      if (flags & kGpuQuadAffine) {   // one row; the cut moves the start along the step
        const uint32_t k = (uint32_t)(x0 - x);
        if (!AddAffine(t, x0, r0, x1 - x0, 1, g_affine.ax + g_affine.adx * k, g_affine.ay + g_affine.ady * k, g_affine.adx,
                       g_affine.ady, 0, 0, level, flags & ~kGpuQuadAffine))
          return false;
        continue;
      }
      const int qsx = (flags & kGpuQuadFlipX) ? sx + (x + w) - x1 : sx + (x0 - x);
      const int qsy = (flags & kGpuQuadFlipY) ? sy + (y + h) - r1 : sy + (r0 - y);
      if (!AddQuad(t, x0, r0, x1 - x0, r1 - r0, qsx, qsy, level, flags)) return false;
    }
    r0 = r1;
  }
  return true;
}

// The HUD's quads (its BG3, and its sprites: EmitSprites), taken out of their bands (GpuFrame.hud_first).
enum { kMaxHudQuads = 256 };
static GpuQuad g_hud_quads[kMaxHudQuads];
static int g_hud_quad_count;

// A HUD sprite's rows [y0, y1) of the main screen, onto the HUD list instead of the band:
// the HUD keeps its place while the view leans, also over the masked margins (the Ceres
// timer in a door transition, issue #15). No window, no math, like the HUD's BG3.
static bool EmitHudSprite(const Sprite *sp, int y0, int y1) {
  const int flags = kGpuQuadObj | (sp->hflip ? kGpuQuadFlipX : 0) | (sp->vflip ? kGpuQuadFlipY : 0);
  for (int top = sp->y; top >= (sp->no_wrap ? sp->y : sp->y - 256); top -= 256) {
    const int r0 = top > y0 ? top : y0, r1 = top + sp->size < y1 ? top + sp->size : y1;
    if (r0 >= r1) continue;
    const int h = r1 - r0, skip = r0 - top;
    const int sy = sp->vflip ? sp->ay + sp->size - h - skip : sp->ay + skip;
    const int q = g_out->quad_count;
    if (!AddQuad(&g_atlas, sp->x, r0, sp->size, h, sp->ax, sy, sp->level, flags)) return false;
    if (g_out->quad_count == q) continue;
    if (g_hud_quad_count >= kMaxHudQuads) return false;
    g_hud_quads[g_hud_quad_count++] = g_out->quads[--g_out->quad_count];
  }
  return true;
}

static bool g_sprite_priority_order;
// The 3D is on (gameplay): sprites are emitted by priority first, OAM order within one (EmitSprites).
void GpuPpu_SetSpritePriorityOrder(bool on) { g_sprite_priority_order = on; }
void GpuPpu_SetSpriteCap(int cap) { g_sprite_cap = cap < 0 ? 0 : cap > 3 ? 3 : cap; }

static bool EmitSprites(int y0, int y1, bool main) {
  // The HUD list is drawn in order, the last quad on top; among sprites the first wins.
  g_quad_plane = 0;
  for (int i = g_sprite_count - 1; i >= 0; i--)
    if (main && g_sprites[i].hud && !EmitHudSprite(&g_sprites[i], y0, y1)) return false;
  // The first sprite on a pixel wins, so the emission order is the priority order. The SNES's is the OAM order alone
  // (the priority bits are for the BGs); with the 3D on, a sprite of priority 3 is on a nearer plane than one of 2
  // (StereoDepth_Plane), so it goes first too: an enemy then covers Samus as its depth says, not the other way round.
  // A sprite the room sends to a plane by hand keeps its OAM place among the priority 2's (9DC7's stalk, under Samus).
  const bool by_level = g_sprite_priority_order;
  for (int pass = 0; pass < (by_level ? 4 : 1); pass++) {
    const int pass_level = 14 - pass * 4;
  for (int i = 0; i < g_sprite_count; i++) {
    const Sprite *sp = &g_sprites[i];
    if (sp->hud) continue;
    if (by_level) {
      const int prio = (sp->level - 2) / 4;
      const bool by_hand = g_plane_rule && g_plane_rule(4, prio) >= 0;
      if ((by_hand ? 10 : sp->level) != pass_level) continue;
    }
    const int flags = kGpuQuadObj | (sp->hflip ? kGpuQuadFlipX : 0) | (sp->vflip ? kGpuQuadFlipY : 0) |
                      (main && sp->math ? kGpuQuadMath : 0);
    UsePlaneRule(4, (sp->level - 2) / 4);
    for (int top = sp->y; top >= (sp->no_wrap ? sp->y : sp->y - 256); top -= 256) {
      const int r0 = top > y0 ? top : y0, r1 = top + sp->size < y1 ? top + sp->size : y1;
      if (r0 >= r1) continue;
      const int h = r1 - r0, skip = r0 - top;
      // With a vertical flip the quad's last row shows texel row ay + size-1-skip-(h-1).
      const int sy = sp->vflip ? sp->ay + sp->size - h - skip : sp->ay + skip;
      if (!AddQuadWin(&g_atlas, sp->x, r0, sp->size, h, sp->ax, sy, sp->level, flags, main ? 0 : 1, 4)) return false;
    }
  }
  }
  g_quad_plane = 0;
  return true;
}

// A layer whose scroll changes on many lines (Norfair heat: BG2/BG3 vertical scroll on
// every line; Maridia water: horizontal) is drawn as one quad per run of lines with the
// same scroll, even one per line: the GPU is far from busy (2DS: never waited for),
// while composing those rows on the CPU took ~15 ms per frame at 268 MHz. Composing is
// kept for when the quads would not fit.
enum { kQuadReserve = 1024 };   // left for sprites and the other layers

static const uint8_t kBgLevel[3][2] = { { 8, 12 }, { 7, 11 }, { 1, 15 } };

static uint32_t g_composed[3][kPpuCaptureLines];   // frame number each screen-texture row was composed in

// One BG layer over the band's lines [l0, l1] (output rows l0-1 .. l1-1).
static const char *EmitBgQuads(const Ppu *ppu, const PpuLineCapture *cap, int layer, int l0, int l1, bool math, int scr) {
  Surface *s = GetSurface(&cap->line[l0].bgLayer[layer], layer < 2 ? 4 : 2);
  if (!s) return "out of texture memory";
  SyncSurface(s, ppu, layer);
  // Columns this layer covers (the band's last output row is l1-1).
  const bool narrow = layer == 2 && NarrowBg3(cap, l0, l1);
  const BgLayer *bgl = &cap->line[l0].bgLayer[layer];
  g_msgbox = layer == 2 && g_msgbox_map >= 0 && l0 > g_narrow_bg3_rows && bgl->tilemapAdr == g_msgbox_map &&
             !bgl->tilemapWider && !bgl->tilemapHigher;
  int vx0 = narrow ? g_hud_x : g_x0, vx1 = narrow ? g_hud_x + 256 : g_x1;
  if (layer < 3 && g_clip[layer].on && !narrow) {   // GpuPpu_SetLayerClip: one copy of a picture the tilemap repeats
    if (vx0 < g_clip[layer].x0) vx0 = g_clip[layer].x0;
    if (vx1 > g_clip[layer].x1) vx1 = g_clip[layer].x1;
    if (vx1 <= vx0) return NULL;
  }
  // Texel column of screen column vx0 (the narrow layer is moved, not scrolled), with the
  // layer's extra shift.
  const int sx0 = (narrow ? 0 : vx0) + g_layer_dx[layer];
  const int flags = math ? kGpuQuadMath : 0;
  int runs = 1;
  for (int l = l0 + 1; l <= l1; l++) {
    const BgLayer *a = &cap->line[l - 1].bgLayer[layer], *b = &cap->line[l].bgLayer[layer];
    runs += a->hScroll != b->hScroll || a->vScroll != b->vScroll;
  }
  if (g_out->quad_count + runs * (2 + s->xcount) <= kGpuMaxQuads - kQuadReserve) {
    // All priority-0 runs, then all priority-1 runs: each priority is its own texture,
    // and alternating them made the backend switch textures (and end a draw batch) on
    // every quad (Maridia water: ~7 ms of submit on a 2DS). Quads of one layer never
    // overlap and depth orders the levels, so the order does not matter otherwise.
    // Then, per priority, the layer's own texture (its plane rule) and those of the tiles sent to
    // another plane (that plane): the same quads over another texture, at the same level.
    for (int prio = 0; prio < 2; prio++) {
      for (int v = -1; v < kGpuXPlanes; v++) {
        GpuTex *tex = &s->tex[prio];
        if (!s->occ[(v + 1) * 2 + prio]) continue;   // nothing visible in that texture: no quad
        if (v < 0) {
          UsePlaneRule(layer + 1, prio);
        } else {
          if (!s->xtex[v][prio].px) continue;
          tex = &s->xtex[v][prio];
          g_quad_plane = (uint8_t)(v + 1);
        }
        for (int a = l0; a <= l1;) {
          const BgLayer *bg = &cap->line[a].bgLayer[layer];
          int b = a;
          while (b + 1 <= l1 && cap->line[b + 1].bgLayer[layer].hScroll == bg->hScroll &&
                 cap->line[b + 1].bgLayer[layer].vScroll == bg->vScroll)
            b++;
          // Output row a-1 shows tilemap row a + vScroll (the PPU draws line a there).
          if (!AddQuadWin(tex, vx0, a - 1, vx1 - vx0, b - a + 1, bg->hScroll + sx0, a + bg->vScroll,
                          kBgLevel[layer][prio], flags, scr, layer))
            return "too many quads";
          a = b + 1;
        }
      }
    }
    g_quad_plane = 0;
    return NULL;
  }
  if (s->xcount) return "too many quads for tile fixes";   // the composed rows below only know the layer's own textures
  const int w = SurfaceW(s) - 1, h = SurfaceH(s) - 1;
  // Screen column x (from vx0) is texture column x - vx0.
  const int view_w = vx1 - vx0;
  GpuTex *t[2] = { ScreenTex(layer, 0), ScreenTex(layer, 1) };
  if (!t[0] || !t[1]) return "out of texture memory";
  // GpuTexelIndex(x, y, w) = row part (y) + column part (x): precompute the columns.
  static uint16_t dst_col[512], src_col[512];
  static int src_col_w;
  if (!dst_col[1]) for (int x = 0; x < 512; x++) dst_col[x] = (uint16_t)GpuTexelIndex(x, 0, 512);
  if (src_col_w != w + 1) {
    for (int x = 0; x <= w; x++) src_col[x] = (uint16_t)GpuTexelIndex(x, 0, w + 1);
    src_col_w = w + 1;
  }
  TexWriteBegin();
  for (int l = l0; l <= l1; l++) {
    const int row = l - 1;
    if (g_composed[layer][row] == g_frame_no) continue;   // main and sub share it
    const BgLayer *bg = &cap->line[l].bgLayer[layer];
    const int ty = (l + bg->vScroll) & h;
    const int src_row = GpuTexelIndex(0, ty, w + 1), dst_row = GpuTexelIndex(0, row, t[0]->w);
    const int col0 = bg->hScroll + sx0;
    for (int p = 0; p < 2; p++) {
      const uint16_t *src = s->tex[p].px + src_row;
      uint16_t *dst = t[p]->px + dst_row;
      for (int x = 0; x < view_w; x++) dst[dst_col[x]] = src[src_col[(col0 + x) & w]];
    }
    g_composed[layer][row] = g_frame_no;
    g_stats.screen_rows_composed++;
  }
  GpuBackend_TexWritten(t[0], l0 - 1, l1);
  GpuBackend_TexWritten(t[1], l0 - 1, l1);
  for (int prio = 0; prio < 2; prio++) {
    UsePlaneRule(layer + 1, prio);
    if (!AddQuadWin(&g_screen_tex[layer][prio], vx0, l0 - 1, view_w, l1 - l0 + 1, 0, l0 - 1, kBgLevel[layer][prio],
                    flags, scr, layer))
      return "too many quads";
  }
  g_quad_plane = 0;
  return NULL;
}

// EmitBgQuads, then a message box's quads (this BG3 is one: g_msgbox) to the HUD list: it is moved
// to the HUD's place, and the masks over a room's outer margins (a room narrower than the view)
// would cut it where they sit, as they cut the room.
static const char *EmitBg(const Ppu *ppu, const PpuLineCapture *cap, int layer, int l0, int l1, bool math, int scr) {
  const int first = g_out->quad_count;
  const char *err = EmitBgQuads(ppu, cap, layer, l0, l1, math, scr);
  const int n = g_out->quad_count - first;
  if (err || !g_msgbox || g_force_narrow || !NarrowBg3(cap, l0, l1) || g_hud_quad_count + n > kMaxHudQuads) return err;
  for (int q = first; q < g_out->quad_count; q++) {
    GpuQuad qd = g_out->quads[q];
    qd.flags &= ~kGpuQuadMath;
    g_hud_quads[g_hud_quad_count++] = qd;
  }
  g_out->quad_count = first;
  return NULL;
}

static uint32_t g_m7_tracked_frame, g_m7_used_frame;

static inline int64_t FloorDiv64(int64_t a, int64_t b) {   // b > 0
  return a >= 0 ? a / b : -((-a + b - 1) / b);
}

// The mode 7 plane over the band's lines [l0, l1]: one affine row per line, so the
// matrix may change on every line (perspective) as well as between frames.
// One pass over the mode 7 plane's rows: the plane itself, or the generated art in plane coordinates (`tex` the art's,
// its texel (x + 128, y + 128) is plane (x, y); `shift` 128, `level` below the plane's).
static const char *EmitMode7Pass(const Ppu *ppu, const PpuLineCapture *cap, int l0, int l1, bool math, int scr, GpuTex *tex,
                                 int shift, int level) {
  const bool plane = tex == &g_m7_tex, art = !plane && !M7IsSplit(tex);   // a split texture is synced with the plane
  if (plane && !g_m7_tex.px) {
    if (!GpuBackend_TexCreate(&g_m7_tex, 1024, 1024)) return "out of texture memory";
    g_m7_fresh = true;
  }
  if (plane && g_m7_tracked_frame != g_frame_no) {   // first use: GpuPpu_BuildFrame tracks from now on
    M7SplitUpdate();
    M7Track(ppu);
    g_m7_tracked_frame = g_frame_no;
  }
  if (plane) g_m7_used_frame = g_frame_no;
  // Rows: the band's lines, plus the extra rows above or below when the band touches that
  // edge (the first or last line's matrix, carried on: the plane goes on there too).
  // Columns: the whole frame, margins included.
  const int la = l0 == 1 ? 1 - g_extra_top : l0, lb = l1 == kGpuRows ? kGpuRows + g_extra_bottom : l1;
  // Texels the band can show: the ends of each row, since a row is a straight line.
  const bool border = art || cap->line[l0].m7largeField;
  int x0 = INT32_MAX, x1 = INT32_MIN, y0 = INT32_MAX, y1 = INT32_MIN;
  for (int l = la; l <= lb; l++) {
    uint32_t ax, ay, adx, ady;
    M7LineStart(&cap->line[l < l0 ? l0 : l > l1 ? l1 : l], l, &ax, &ay, &adx, &ady);
    ax += (uint32_t)shift << 8, ay += (uint32_t)shift << 8;
    const int32_t ex[2] = { (int32_t)(ax + adx * (uint32_t)g_x0) >> 8, (int32_t)(ax + adx * (uint32_t)(g_x1 - 1)) >> 8 };
    const int32_t ey[2] = { (int32_t)(ay + ady * (uint32_t)g_x0) >> 8, (int32_t)(ay + ady * (uint32_t)(g_x1 - 1)) >> 8 };
    for (int i = 0; i < 2; i++) {
      if (ex[i] < x0) x0 = ex[i];
      if (ex[i] > x1) x1 = ex[i];
      if (ey[i] < y0) y0 = ey[i];
      if (ey[i] > y1) y1 = ey[i];
    }
  }
  if (border) {   // outside the plane is transparent: nothing to decode there
    x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > 1023 ? 1023 : x1, y1 = y1 > 1023 ? 1023 : y1;
  } else if (x1 - x0 > 1023 || y1 - y0 > 1023) {   // it wraps: the whole plane
    x0 = y0 = 0, x1 = y1 = 1023;
  } else {
    x0 &= 1023, x1 &= 1023, y0 &= 1023, y1 &= 1023;
    if (x0 > x1 || y0 > y1) x0 = y0 = 0, x1 = y1 = 1023;
  }
  if (plane && x0 <= x1 && y0 <= y1) M7Sync(ppu, x0, x1, y0, y1);
  const int flags = (math ? kGpuQuadMath : 0) | (border ? kGpuQuadBorder : 0);
  // Each line's row, cut to the plane with the large field. Consecutive whole rows whose
  // starts advance by the same amount every line (and with the same per-pixel step) are
  // one quad: exact, and the usual case (the title's identity matrix is one quad).
  struct { uint32_t ax, ay, adx, ady; int i0, i1; } run = { 0 };
  int run_l = -1, run_n = 0;
  uint32_t run_rdx = 0, run_rdy = 0;
  for (int l = la; l <= lb + 1; l++) {
    uint32_t ax = 0, ay = 0, adx = 0, ady = 0;
    int i0 = g_x0, i1 = g_x1;
    const int ls = l < l0 ? l0 : l > l1 ? l1 : l;   // the line whose registers apply
    const bool partial = l <= lb && (g_info[ls].partial[scr] & 1);
    // A windowed line's spans exist only for the captured lines: no extra rows then.
    if (partial && ls != l) continue;
    if (l <= lb) {
      M7LineStart(&cap->line[ls], l, &ax, &ay, &adx, &ady);
      ax += (uint32_t)shift << 8, ay += (uint32_t)shift << 8;
      if (border) {
        // Outside the plane is transparent: keep the pixels whose position is inside, so
        // the GPU never sees far-out coordinates (its 24-bit floats would lose the
        // texel). Exact as long as a row does not wrap 32 bits, which no sane matrix does.
        const int64_t p[2] = { (int32_t)ax, (int32_t)ay }, d[2] = { (int32_t)adx, (int32_t)ady };
        for (int k = 0; k < 2 && i0 < i1; k++) {
          // a <= p + d*i < b
          const int64_t a = 0, b = 0x40000;
          if (d[k] == 0) {
            if (p[k] < a || p[k] >= b) i1 = i0;
            continue;
          }
          int64_t lo, hi;   // i in [lo, hi)
          if (d[k] > 0) {
            lo = FloorDiv64(a - p[k] + d[k] - 1, d[k]);
            hi = FloorDiv64(b - p[k] + d[k] - 1, d[k]);
          } else {
            lo = FloorDiv64(p[k] - b, -d[k]) + 1;
            hi = FloorDiv64(p[k] - a, -d[k]) + 1;
          }
          if (lo > i0) i0 = lo > i1 ? i1 : (int)lo;
          if (hi < i1) i1 = hi < i0 ? i0 : (int)hi;
        }
      }
      if (i0 < i1) ax += adx * (uint32_t)i0, ay += ady * (uint32_t)i0;
    }
    // Does line l continue the run?
    if (run_n > 0 && l <= lb && !partial && i0 == run.i0 && i1 == run.i1 && adx == run.adx && ady == run.ady) {
      const uint32_t rdx = ax - (run.ax + run_rdx * (uint32_t)(run_n - 1)), rdy = ay - (run.ay + run_rdy * (uint32_t)(run_n - 1));
      if (run_n == 1) run_rdx = rdx, run_rdy = rdy;
      if (rdx == run_rdx && rdy == run_rdy) {
        run_n++;
        continue;
      }
    }
    if (run_n > 0 && run.i0 < run.i1 &&
        !AddAffine(tex, run.i0, run_l - 1, run.i1 - run.i0, run_n, run.ax, run.ay, run.adx, run.ady, run_rdx,
                   run_rdy, level, flags))
      return "too many quads";
    run_n = 0;
    if (l > lb) break;
    // The CPU renderer draws the plane at level 5 (z 0x5000): above sprites of
    // priority 0, below the others.
    if (partial) {
      if (i0 >= i1) continue;
      g_affine.ax = ax, g_affine.ay = ay, g_affine.adx = adx, g_affine.ady = ady;
      if (!AddQuadWin(tex, i0, l - 1, i1 - i0, 1, 0, 0, level, flags | kGpuQuadAffine, scr, 0)) return "too many quads";
      continue;
    }
    run.ax = ax, run.ay = ay, run.adx = adx, run.ady = ady, run.i0 = i0, run.i1 = i1;
    run_l = l, run_n = 1, run_rdx = run_rdy = 0;
  }
  return NULL;
}

// Generated art around the title's mode 7 picture (docs/wide-scene-extension.md): a 1024x1024 texture where texel
// (x + 128, y + 128) is plane (x, y), so the art is under the plane wherever the plane is empty, zoomed or not, with the
// plane's own matrix and fades. Kept as CGRAM indices and decoded with the plane's colour table, so a palette fade
// or animation changes it as it changes the plane.
static GpuTex g_art_tex;
// The art's pixels that are not empty, as texel indices in g_art_tex and their values (1..255 a CGRAM index, 0x8000 |
// BGR555 a fixed colour), the CGRAM ones first: only those change with the palette. A few thousand of the 96000 (the
// title's: 4104 and 2658), so a palette change rewrites those and the 8x8 blocks that hold them, not the whole canvas.
static uint32_t *g_art_texel;
static uint16_t *g_art_val;
static int g_art_n, g_art_n_pal;
static uint16_t g_art_block[(400 / 8 + 1) * (240 / 8 + 1)];   // the blocks of the CGRAM pixels, each once
static int g_art_blocks;
static uint8_t g_art_used[256];   // the CGRAM indices the art uses ...
static uint16_t g_art_lut[256];   // ... and their colours when it was last decoded
static bool g_art_have, g_art_show, g_art_written, g_art_lut_ok;
enum { kArtShift = 128, kArtX = 72, kArtY = 7 };   // the canvas' (0, 0) is plane (-72, -7): the picture's row 1 at 8

bool GpuPpu_SetSceneArt(const uint16_t *idx) {   // 400x240 CGRAM indices, rows from the top; 0 = nothing
  if (!idx) return g_art_have;
  int n = 0;
  for (int i = 0; i < 400 * 240; i++) n += idx[i] != 0;
  uint32_t *texel = (uint32_t *)realloc(g_art_texel, (size_t)(n ? n : 1) * sizeof(*texel));
  if (texel) g_art_texel = texel;
  uint16_t *val = (uint16_t *)realloc(g_art_val, (size_t)(n ? n : 1) * sizeof(*val));
  if (val) g_art_val = val;
  if (!texel || !val) return g_art_have = false;
  static uint8_t seen[1024 * 1024 / 64 / 8];   // blocks already listed
  memset(seen, 0, sizeof(seen));
  memset(g_art_used, 0, sizeof(g_art_used));
  g_art_blocks = 0;
  int k = 0;
  for (int pass = 0; pass < 2; pass++) {   // the CGRAM pixels, then the fixed ones
    for (int y = 0; y < 240; y++)
      for (int x = 0; x < 400; x++) {
        const uint16_t v = idx[y * 400 + x];
        if (!v || (pass == 0) != !(v & 0x8000)) continue;
        const int t = GpuTexelIndex(x - kArtX + kArtShift, y - kArtY + kArtShift, 1024);
        g_art_texel[k] = (uint32_t)t, g_art_val[k++] = v;
        if (pass) continue;
        g_art_used[v & 255] = 1;
        const int b = t >> 6;
        if (!(seen[b >> 3] & 1u << (b & 7))) seen[b >> 3] |= (uint8_t)(1u << (b & 7)), g_art_block[g_art_blocks++] = (uint16_t)b;
      }
    if (pass == 0) g_art_n_pal = k;
  }
  g_art_n = k;
  g_art_have = true;
  g_art_written = g_art_lut_ok = false;
  return true;
}
void GpuPpu_ShowSceneArt(bool on) { g_art_show = on && g_art_have; }

// Decoded again whenever a colour the art uses in the plane's colour table (g_lut_m7, rebuilt each frame) is not the
// one it was decoded with. The title cycles two other colours (46, 47) every frame.
static bool ArtRefresh(void) {
  if (!g_art_tex.px) {
    if (!GpuBackend_TexCreate(&g_art_tex, 1024, 1024)) return false;
    g_art_written = g_art_lut_ok = false;
  }
  if (g_art_lut_ok) {
    bool same = true;
    for (int i = 1; i < 256 && same; i++) same = !g_art_used[i] || g_art_lut[i] == g_lut_m7[i];
    if (same) return true;
  }
  memcpy(g_art_lut, g_lut_m7, sizeof(g_art_lut));
  g_art_lut_ok = true;
  g_stats.art_decodes++;
  GpuBackend_BeforeTexWrite();
  uint16_t *px = g_art_tex.px;
  for (int k = 0; k < g_art_n_pal; k++) px[g_art_texel[k]] = g_lut_m7[g_art_val[k] & 255];
  if (!g_art_written) {   // the fixed colours once (the rest of the texture starts out transparent)
    for (int k = g_art_n_pal; k < g_art_n; k++) px[g_art_texel[k]] = GpuRgba5551(g_art_val[k] & 0x7fff);
    g_art_written = true;
    GpuBackend_TexWritten(&g_art_tex, kArtShift - kArtY, kArtShift - kArtY + 240);
  } else {
    for (int b = 0; b < g_art_blocks; b++) GpuBackend_TexBlockWritten(&g_art_tex, g_art_block[b]);
  }
  return true;
}

static const char *EmitMode7Quads(const Ppu *ppu, const PpuLineCapture *cap, int l0, int l1, bool math, int scr) {
  if (g_art_show && ArtRefresh()) {
    const char *err = EmitMode7Pass(ppu, cap, l0, l1, math, scr, &g_art_tex, kArtShift, 4);
    if (err) return err;
  }
  // The plane's texture and the splits share a level, and a pixel keeps the first quad drawn there: they go nearest
  // plane first, so a fill behind a nearer cell never covers it. The plane's pass goes first anyway (it decodes the cells
  // of all, and gives the splits their planes), then the passes' quads are put in that order.
  typedef struct { int first, end, plane; } M7Pass;
  M7Pass pass[1 + kM7Splits];
  const uint8_t own = g_quad_plane;
  int n = 0;
  pass[n] = (M7Pass){ g_out->quad_count, 0, g_m7_base_plane };
  const char *err = EmitMode7Pass(ppu, cap, l0, l1, math, scr, &g_m7_tex, 0, 5);
  pass[n++].end = g_out->quad_count;
  for (int i = 0; i < kM7Splits && !err; i++) {
    if (!g_m7_split[i].plane) continue;
    g_quad_plane = g_m7_split[i].plane;
    pass[n] = (M7Pass){ g_out->quad_count, 0, g_m7_split[i].plane - 1 };
    err = EmitMode7Pass(ppu, cap, l0, l1, math, scr, &g_m7_split[i].tex, 0, 5);
    pass[n++].end = g_out->quad_count;
  }
  if (!err && n > 1) {
    static GpuQuad held[kGpuMaxQuads];
    const int first = pass[0].first, count = g_out->quad_count - first;
    memcpy(held, &g_out->quads[first], (size_t)count * sizeof(GpuQuad));
    int at = first;
    for (int k = 0; k < n; k++) {   // the nearest pass left, each time
      int best = -1;
      for (int i = 0; i < n; i++)
        if (pass[i].end >= 0 && (best < 0 || pass[i].plane < pass[best].plane)) best = i;
      const int len = pass[best].end - pass[best].first;
      memcpy(&g_out->quads[at], &held[pass[best].first - first], (size_t)len * sizeof(GpuQuad));
      at += len;
      pass[best].end = -1;
    }
  }
  g_quad_plane = own;
  return err;
}

static const char *EmitMode7(const Ppu *ppu, const PpuLineCapture *cap, int l0, int l1, bool math, int scr) {
  UsePlaneRule(5, 0);
  const char *err = EmitMode7Quads(ppu, cap, l0, l1, math, scr);
  g_quad_plane = 0;
  return err;
}

static const char *EmitScreen(const Ppu *ppu, const PpuLineCapture *cap, uint8_t layers, int l0, int l1, bool main,
                              bool *sprites_built) {
  const PpuLineState *st = &cap->line[l0];
  const char *err = NULL;
  if (layers & 0x10) {
    if (!*sprites_built) {
      const uint64_t t0 = Clock();
      err = BuildSprites(ppu, st);
      g_stats.t_sprites += Clock() - t0;
      if (err) return err;
      *sprites_built = true;
    }
    // The first and last bands also cover the extra rows.
    const int r0 = l0 == 1 ? -g_extra_top : l0 - 1, r1 = l1 == kGpuRows ? kGpuRows + g_extra_bottom : l1;
    if (!EmitSprites(r0, r1, main)) return "too many quads";
  }
  const uint64_t t0 = Clock();
  if (st->mode == 7) {
    if (layers & 1) err = EmitMode7(ppu, cap, l0, l1, main && st->mathEnabled[0], main ? 0 : 1);
    g_stats.t_bg += Clock() - t0;
    return err;
  }
  if (main && g_m7_under_hud && l1 <= g_narrow_bg3_rows) {
    if ((err = EmitMode7(ppu, cap, l0, l1, st->mathEnabled[0], 0))) return err;
    layers &= ~3;   // mode 1's BG1 and BG2 there would read the mode 7 VRAM
  }
  for (int layer = 0; layer < 3; layer++)
    if ((layers & (1 << layer)) && (err = EmitBg(ppu, cap, layer, l0, l1, main && st->mathEnabled[layer], main ? 0 : 1)))
      break;
  g_stats.t_bg += Clock() - t0;
  return err;
}

// The colour window's clip / no-math segments over lines [l0, l1] as rectangles, lines
// with the same segments grouped.
static bool EmitCwRects(GpuFrame *out, int l0, int l1) {
  for (int a = l0; a <= l1;) {
    const CwSegs *cs = &g_cw[a];
    int b = a;
    while (b + 1 <= l1 && g_cw[b + 1].n == cs->n && !memcmp(g_cw[b + 1].seg, cs->seg, cs->n * sizeof(cs->seg[0]))) b++;
    for (int i = 0; i < cs->n; i++) {
      if (!cs->seg[i].clip && !cs->seg[i].no_math) continue;
      if (out->cw_count >= kGpuMaxCwRects) return false;
      out->cw[out->cw_count++] = (GpuCwRect){ cs->seg[i].x0, (int16_t)(a - 1), (int16_t)(cs->seg[i].x1 - cs->seg[i].x0),
                                              (int16_t)(b - a + 1), cs->seg[i].clip, cs->seg[i].no_math };
    }
    a = b + 1;
  }
  return true;
}

// The part of SM's BG3 map a tile row is in: the HUD (rows 0-3), the FX layer's first page (4-31: in a
// liquid room the air, then its surface) or the second page of a 64-row map (32-63: the liquid's body).
static int Bg3Part(int row) { return row < 4 ? 0 : row < 32 ? 1 : 2; }

// The extra rows above (`up`) or below the 224: BG quads and colour window rectangles
// touching that edge grow by `n` rows with the same scroll (their texel rows go on).
// Sprites were emitted over the extra rows already; mode 7 rows and composed layers are
// left as they are (backdrop there). `fx_rows` (up only, the band's last quads): BG3 is the FX layer
// in the HUD's map, that many rows (FxSharesHudMap); where the rows above its first line belong to
// another part of the map (Bg3Part), the quad gets one more over the extra rows that repeats its own
// first tile row instead (the FX's body is one tile repeated, issue #50).
static void ExtendBand(GpuFrame *out, GpuBand *b, bool up, int n, bool keep_bg3, int fx_rows) {
  if (up && fx_rows && !keep_bg3) {
    for (int q = b->main_first, end = b->main_first + b->main_count; q < end; q++) {
      GpuQuad *qd0 = &out->quads[q];
      if ((qd0->flags & (kGpuQuadObj | kGpuQuadAffine)) || qd0->y != 0 ||
          (qd0->level != kBgLevel[2][0] && qd0->level != kBgLevel[2][1]) ||
          (out->tex[qd0->tex] >= &g_screen_tex[0][0] && out->tex[qd0->tex] <= &g_screen_tex[2][1]))
        continue;
      const int edge = Bg3Part((qd0->sy >> 3) & (fx_rows - 1));
      bool crosses = false;
      for (int k = 1; k <= n; k++) crosses |= Bg3Part(((qd0->sy - k) >> 3) & (fx_rows - 1)) != edge;
      if (!crosses) {   // goes on as it is
        qd0->y -= n, qd0->h += n, qd0->sy -= n;
        continue;
      }
      if (out->quad_count >= kGpuMaxQuads) break;
      const GpuQuad qd = *qd0;
      // In the main list, after its quads: the sub screen's move up one.
      const int at = b->main_first + b->main_count;
      memmove(&out->quads[at + 1], &out->quads[at], (size_t)(out->quad_count - at) * sizeof(GpuQuad));
      out->quad_count++, b->main_count++, b->sub_first++;
      GpuQuad *extra = &out->quads[at];
      *extra = qd;
      extra->y = (int16_t)-n, extra->h = (int16_t)n, extra->sy = (int16_t)(qd.sy + 8 - n);
    }
    keep_bg3 = true;   // BG3's quads are done
  }
  const int firsts[2] = { b->main_first, b->sub_first }, counts[2] = { b->main_count, b->sub_count };
  for (int s = 0; s < 2; s++)
    for (int q = firsts[s]; q < firsts[s] + counts[s]; q++) {
      GpuQuad *qd = &out->quads[q];
      if (qd->flags & (kGpuQuadObj | kGpuQuadAffine)) continue;
      // A composed screen texture has only rows 0..223 (EmitBg's fallback).
      const GpuTex *t = out->tex[qd->tex];
      if (t >= &g_screen_tex[0][0] && t <= &g_screen_tex[2][1]) continue;
      // BG3 (levels 1 and 15) in a band that keeps it narrow: the HUD, nothing above it.
      if (keep_bg3 && (qd->level == kBgLevel[2][0] || qd->level == kBgLevel[2][1])) continue;
      if (up && qd->y == 0) qd->y -= n, qd->h += n, qd->sy -= n;
      else if (!up && qd->y + qd->h == kGpuRows) qd->h += n;
    }
  for (int i = b->cw_first; i < b->cw_first + b->cw_count; i++) {
    GpuCwRect *c = &out->cw[i];
    if (up && c->y == 0) c->y -= n, c->h += n;
    else if (!up && c->y + c->h == kGpuRows) c->h += n;
  }
}


// Moves the BG3 quads of band `b`'s main screen, the last quads emitted, to the HUD list.
static bool TakeHudQuads(GpuFrame *out, GpuBand *b) {
  int keep = b->main_first;
  for (int q = b->main_first; q < b->main_first + b->main_count; q++) {
    GpuQuad qd = out->quads[q];
    const bool bg3 = !(qd.flags & (kGpuQuadObj | kGpuQuadAffine)) &&
                     (qd.level == kBgLevel[2][0] || qd.level == kBgLevel[2][1]);
    if (!bg3) {
      out->quads[keep++] = qd;
      continue;
    }
    if (g_hud_quad_count >= kMaxHudQuads) return false;
    qd.y += g_hud_y;
    qd.flags &= ~kGpuQuadMath;
    g_hud_quads[g_hud_quad_count++] = qd;
  }
  out->quad_count = keep;
  b->main_count = keep - b->main_first;
  return true;
}

static bool SameBand(const PpuLineState *a, const LineInfo *ia, const PpuLineState *b, const LineInfo *ib) {
  LineKey ka, kb;
  MakeKey(&ka, a, ia);
  MakeKey(&kb, b, ib);
  return memcmp(&ka, &kb, sizeof(ka)) == 0;
}

static bool SameBgConfig(const BgLayer *a, const BgLayer *b) {
  return a->tilemapWider == b->tilemapWider && a->tilemapHigher == b->tilemapHigher && a->tilemapAdr == b->tilemapAdr &&
         a->tileAdr == b->tileAdr && a->bigTiles == b->bigTiles;
}

// The HUD's rows as a gameplay line `g` draws, keeping what HDMA changes per line
// (BG1/BG2, the windows' bounds): the room under the HUD gets the FX layer and the colour
// math the rows below have (fog, rain, water, lava), as it would if the screen went on up there.
// Not where the FX layer would read the HUD's own tilemap rows (SM keeps both in one BG3
// map, the HUD in its first 4 rows): the layer's scroll puts its blank rows there (a lava
// surface below the HUD), so the line keeps its own settings without BG3 (the HUD is drawn
// from the HUD list). `g` is the line after the first one below the HUD: the first one is
// still before the layer's HDMA (a stale scroll), so it is built from `g` too.
static void SynthHudLine(PpuLineState *dst, const PpuLineState *hud, const PpuLineState *g, int line) {
  const BgLayer *fx = &g->bgLayer[2], *hb = &hud->bgLayer[2];
  const int map_rows = fx->tilemapHigher ? 64 : 32, row = ((line + fx->vScroll) >> 3) & (map_rows - 1);
  const bool fx_on = ((g->screenEnabled[0] | g->screenEnabled[1]) & 4) != 0;
  if (fx_on && fx->tilemapAdr == hb->tilemapAdr && row < 4) {
    *dst = *hud;
    dst->screenEnabled[0] &= ~4, dst->screenEnabled[1] &= ~4;
    return;
  }
  *dst = *g;
  dst->bgLayer[0] = hud->bgLayer[0];
  dst->bgLayer[1] = hud->bgLayer[1];
  dst->window1left = hud->window1left, dst->window1right = hud->window1right;
  dst->window2left = hud->window2left, dst->window2right = hud->window2right;
  dst->forcedBlank = hud->forcedBlank;
  dst->brightness = hud->brightness;
}

// The extra rows above the HUD's band carry the FX layer on (ExtendBand), but SM keeps the HUD in the first 4 rows
// of the FX layer's own tilemap: when the layer's scroll puts those rows just above the first line, the extension
// would draw the HUD again at the top (energy, ammo and map repeated in the first row).
// BG3 above the view in gameplay: the FX layer in the HUD's map (SM keeps both in one), its rows (32 or
// 64), else 0.
static int FxSharesHudMap(const PpuLineCapture *cap, const PpuLineCapture *orig) {
  if (!g_hud_synth) return 0;
  const BgLayer *fx = &cap->line[1].bgLayer[2], *hb = &orig->line[1].bgLayer[2];
  if (fx->tilemapAdr != hb->tilemapAdr) return 0;
  return fx->tilemapHigher ? 64 : 32;
}

// The extra rows below the 224 are copies of line 224, its BG3 scroll carried on. SM keeps the FX layer
// (lava, water, fog) and the HUD in one BG3 map, the HUD in its first 4 rows: carried on, the FX layer
// can reach those rows and show the HUD's blank cells (see-through over the room) instead of lava, row by
// row as the camera moves (issue #50). Those lines repeat line 224's tile row instead: the FX's body is
// one tile repeated, so it goes on seamlessly.
static void BottomRowsSkipHud(PpuLineCapture *work, const PpuLineState *hud) {
  const BgLayer *last = &work->line[kGpuRows].bgLayer[2];
  if (last->tilemapAdr != hud->bgLayer[2].tilemapAdr) return;
  const int map_rows = last->tilemapHigher ? 64 : 32, row224 = ((kGpuRows + last->vScroll) >> 3) & (map_rows - 1);
  if (row224 < 4) return;   // line 224 itself shows the HUD's rows: nothing to repeat
  for (int l = kGpuRows + 1; l <= kGpuRows + g_extra_bottom; l++) {
    BgLayer *fx = &work->line[l].bgLayer[2];
    const int row = ((l + fx->vScroll) >> 3) & (map_rows - 1);
    if (row < 4) fx->vScroll = (uint16_t)(fx->vScroll - 8 * (((l + fx->vScroll) >> 3) - ((kGpuRows + fx->vScroll) >> 3)));
  }
}

bool GpuPpu_BuildFrame(const Ppu *ppu, const PpuLineCapture *cap, GpuFrame *out, const char **reason) {
  static LineInfo info[kPpuCaptureLines];
  // The lines drawn: the capture's, or a copy changed where the HUD and extra rows need it.
  static PpuLineCapture work;
  const PpuLineCapture *orig = cap;
  memset(&g_stats, 0, sizeof(g_stats));
  g_memo_gen++;
  g_defer_left = g_defer_cap;
  g_quad_plane = 0;
  out->tex_count = out->quad_count = out->band_count = out->cw_count = out->mask_count = 0;
  out->hud_first = out->hud_count = g_hud_quad_count = 0;
  g_tex_waited = false;
  g_x0 = -g_margin_l;
  g_x1 = 256 + g_margin_r;
  out->x0 = g_x0;
  out->x1 = g_x1;
  out->show_x0 = g_crop_to_view ? 0 : g_x0;
  out->show_x1 = g_crop_to_view ? 256 : g_x1;
  out->y0 = -g_extra_top;
  out->y1 = kGpuRows + g_extra_bottom;
  g_out = out;
  *reason = NULL;
  uint64_t t0 = Clock();
  if (cap->last_line < kGpuRows) { *reason = "frame shorter than 224 lines"; return false; }
  if (cap->midframe_data_writes) { *reason = "VRAM/CGRAM/OAM written mid-frame"; return false; }
  // The extra rows below are lines of their own, copies of the last one: what depends on
  // the line (a window cone, GpuPpu_SetWindowCone) goes on there instead of standing still.
  const int last = kGpuRows + g_extra_bottom;
  int g = g_narrow_bg3_rows + 1;
  while (g <= kGpuRows && orig->line[g].forcedBlank) g++;
  g_hud_synth = g_narrow_bg3_rows > 0 && !g_m7_under_hud && g <= kGpuRows && orig->line[g].mode != 7;
  if (g_hud_synth || last > kGpuRows) {
    memcpy(&work.line[1], &orig->line[1], kGpuRows * sizeof(PpuLineState));
    work.last_line = orig->last_line;
    work.midframe_data_writes = orig->midframe_data_writes;
    for (int l = kGpuRows + 1; l <= last; l++) work.line[l] = orig->line[kGpuRows];
    if (last > kGpuRows) BottomRowsSkipHud(&work, &orig->line[1]);
    const int src = g < kGpuRows ? g + 1 : g;   // the line whose BG3 scroll and colour math the HUD's rows copy
    for (int l = 1; g_hud_synth && l <= g_narrow_bg3_rows; l++) SynthHudLine(&work.line[l], &orig->line[l], &orig->line[src], l);
    if (g_hud_synth && src != g && orig->line[g].bgLayer[2].vScroll != orig->line[src].bgLayer[2].vScroll) SynthHudLine(&work.line[g], &orig->line[g], &orig->line[src], g);
    cap = &work;
  }
  for (int l = 1; l <= last; l++)
    if ((*reason = AnalyzeLine(&cap->line[l], &info[l], l))) return false;
  g_info = info;
  if (!g_atlas.px && !GpuBackend_TexCreate(&g_atlas, kAtlasW, kAtlasH)) { *reason = "out of texture memory"; return false; }

  g_frame_no++;
  uint64_t t1 = Clock();
  g_stats.t_lines += t1 - t0;
  InitDecodeTables();
  DiffMemories(ppu);
  ConvertPalettes(ppu);
  // The change marks are cleared after every frame built, so once the mode 7 plane
  // exists it must take note of them on every frame, mode 7 or not.
  // Mode 7 is rare (title, intro, Ceres, ending): after 2 s without it the plane is freed
  // (2 MB) and tracking stops; the next use decodes it again.
  if (g_m7_tex.px && g_frame_no - g_m7_used_frame > 120) {
    GpuBackend_TexFree(&g_m7_tex);
    for (int i = 0; i < kM7Splits; i++) {
      if (g_m7_split[i].tex.px) GpuBackend_TexFree(&g_m7_split[i].tex);
      g_m7_split[i].plane = 0;
    }
  }
  if (g_m7_tex.px) {
    M7SplitUpdate();
    M7Track(ppu);
    g_m7_tracked_frame = g_frame_no;
  }
  g_stats.t_diff = Clock() - t1;
  t0 = Clock();
  g_atlas_entry_count = g_shelf_x = g_shelf_y = g_shelf_h = 0;
  bool ok = true;
  for (int l0 = 1; l0 <= last && ok;) {
    int l1 = l0;
    while (l1 + 1 <= last && SameBand(&cap->line[l0], &info[l0], &cap->line[l1 + 1], &info[l1 + 1])) l1++;
    if (out->band_count >= kGpuMaxBands) { *reason = "too many bands"; ok = false; break; }
    const PpuLineState *st = &cap->line[l0];
    const LineInfo *inf = &info[l0];
    GpuBand *b = &out->bands[out->band_count++];
    memset(b, 0, sizeof(*b));
    b->y0 = l0 - 1;
    b->y1 = l1;
    b->black = st->forcedBlank;
    b->brightness = st->brightness;
    if (!b->black) {
      bool any_math = false;
      for (int i = 0; i < 6; i++) any_math |= st->mathEnabled[i];
      b->backdrop = ppu->cgram[0];
      b->backdrop_math = st->mathEnabled[5];
      b->clip = inf->clip;
      b->math = inf->math_ok && any_math;
      b->cw_first = out->cw_count;
      if (inf->cw_split && !EmitCwRects(out, l0, l1)) { *reason = "too many colour window rectangles"; ok = false; break; }
      b->cw_count = out->cw_count - b->cw_first;
      b->add_subscreen = st->addSubscreen;
      b->subtract = st->subtractColor;
      b->half = st->halfColor;
      b->fixed = (uint16_t)(st->fixedColorR | st->fixedColorG << 5 | st->fixedColorB << 10);
      for (int l = l0; l <= l1; l++) {
        const PpuLineState *ls = &cap->line[l];
        const uint16_t c = (uint16_t)(ls->fixedColorR | ls->fixedColorG << 5 | ls->fixedColorB << 10);
        out->row_fixed[l - 1 + kGpuMaxExtraRows] = c;
        b->fixed_rows |= c != b->fixed;
      }
      for (int y = -g_extra_top; l0 == 1 && y < 0; y++) out->row_fixed[y + kGpuMaxExtraRows] = b->fixed;   // line 1's
      bool sprites_built = false;
      b->main_first = out->quad_count;
      if ((*reason = EmitScreen(ppu, cap, inf->main, l0, l1, true, &sprites_built))) { ok = false; break; }
      b->main_count = out->quad_count - b->main_first;
      if (g_narrow_bg3_rows && !g_hud_synth && l1 <= g_narrow_bg3_rows && !TakeHudQuads(out, b)) {
        *reason = "too many HUD quads";
        ok = false;
        break;
      }
      b->sub_first = out->quad_count;
      if (b->math && b->add_subscreen && inf->sub &&
          (*reason = EmitScreen(ppu, cap, inf->sub, l0, l1, false, &sprites_built))) { ok = false; break; }
      b->sub_count = out->quad_count - b->sub_first;
      const bool hud = NarrowBg3(cap, l0, l1);
      if (l0 == 1 && g_extra_top) ExtendBand(out, b, true, g_extra_top, hud, FxSharesHudMap(cap, orig));
    }
    if (l0 == 1) b->y0 = -g_extra_top;
    l0 = l1 + 1;
  }
  // The HUD from its own lines: BG3 within the view at the HUD's place, no window, no math.
  if (ok && g_hud_synth) {
    g_force_narrow = g_no_window_cut = true;
    for (int l0 = 1; l0 <= g_narrow_bg3_rows && ok;) {
      const PpuLineState *st = &orig->line[l0];
      int l1 = l0;
      while (l1 + 1 <= g_narrow_bg3_rows && SameBgConfig(&orig->line[l1 + 1].bgLayer[2], &st->bgLayer[2]) &&
             orig->line[l1 + 1].forcedBlank == st->forcedBlank && orig->line[l1 + 1].screenEnabled[0] == st->screenEnabled[0])
        l1++;
      if (!st->forcedBlank && (st->screenEnabled[0] & 4)) {
        const int first = out->quad_count;
        if ((*reason = EmitBg(ppu, orig, 2, l0, l1, false, 0))) ok = false;
        GpuBand fake = { 0 };
        fake.main_first = first;
        fake.main_count = out->quad_count - first;
        if (ok && !TakeHudQuads(out, &fake)) { *reason = "too many HUD quads"; ok = false; }
      }
      l0 = l1 + 1;
    }
    g_force_narrow = g_no_window_cut = false;
  }
  if (ok && g_hud_quad_count) {
    if (out->quad_count + g_hud_quad_count > kGpuMaxQuads) {
      *reason = "too many quads";
      ok = false;
    } else {
      out->hud_first = out->quad_count;
      memcpy(&out->quads[out->quad_count], g_hud_quads, g_hud_quad_count * sizeof(GpuQuad));
      out->quad_count += out->hud_count = g_hud_quad_count;
    }
  }
  // band detection time = loop time minus what sprites and BGs took inside it
  g_stats.t_lines += Clock() - t0 - g_stats.t_sprites - g_stats.t_bg;
  if (g_atlas_entry_count) GpuBackend_TexWritten(&g_atlas, 0, g_shelf_y + g_shelf_h);
  // Surfaces this frame did not sync have missed this frame's changes.
  for (int i = 0; i < kSurfaces; i++) {
    if (!g_surf[i].used) continue;
    g_stats.surfaces += g_surf[i].last_frame == g_frame_no;
    if (g_surf[i].last_frame != g_frame_no) g_surf[i].fresh = true;
  }
  t0 = Clock();
  UpdateShadows(ppu);
  g_stats.t_shadow = Clock() - t0;
  return ok;
}

const GpuPpuStats *GpuPpu_LastStats(void) { return &g_stats; }
