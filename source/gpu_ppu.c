#include "gpu_ppu.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ---- Window evaluation (PpuWindows_Calc in ppu.c, without the extra border) --------

typedef struct {
  int16_t edges[6];
  uint8_t nr, bits;
} Win;

enum { kWin1Inversed = 1, kWin1Enabled = 2, kWin2Inversed = 4, kWin2Enabled = 8 };

static void WinCalc(Win *win, const PpuLineState *st, int layer) {
  const uint32_t winflags = st->windowsel >> (layer * 4);
  unsigned nr = 1, i, j;
  int t;
  win->edges[0] = 0;
  win->edges[1] = 256;
  const bool w1 = (winflags & kWin1Enabled) && st->window1left <= st->window1right;
  if (w1) {
    if (st->window1left > win->edges[0]) {
      win->edges[nr] = st->window1left;
      win->edges[++nr] = 256;
    }
    if (st->window1right + 1 < 256) {
      win->edges[nr] = st->window1right + 1;
      win->edges[++nr] = 256;
    }
  }
  const bool w2 = (winflags & kWin2Enabled) && st->window2left <= st->window2right;
  if (w2) {
    for (i = 0; i <= nr && (t = st->window2left) != win->edges[i]; i++) {
      if (t < win->edges[i]) {
        for (j = nr++; j >= i; j--) win->edges[j + 1] = win->edges[j];
        win->edges[i] = t;
        break;
      }
    }
    for (; i <= nr && (t = st->window2right + 1) != win->edges[i]; i++) {
      if (t < win->edges[i]) {
        for (j = nr++; j >= i; j--) win->edges[j + 1] = win->edges[j];
        win->edges[i] = t;
        break;
      }
    }
  }
  win->nr = nr;
  uint8_t w1_bits = 0, w2_bits = 0;
  if (w1) {
    for (i = 0; win->edges[i] != st->window1left; i++) {}
    for (j = i; win->edges[j] != st->window1right + 1; j++) {}
    w1_bits = ((1 << (j - i)) - 1) << i;
  }
  if ((winflags & (kWin1Enabled | kWin1Inversed)) == (kWin1Enabled | kWin1Inversed)) w1_bits = ~w1_bits;
  if (w2) {
    for (i = 0; win->edges[i] != st->window2left; i++) {}
    for (j = i; win->edges[j] != st->window2right + 1; j++) {}
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
} LineInfo;

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
      WinCalc(&w, st, layer);
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
    if (sub) info->sub = on; else info->main = on;
  }
  // The colour window only matters when clip or prevent-math is "inside" or "outside"
  // (modes 1, 2); "never" and "always" do not look at it.
  const bool cw_used = st->clipMode == 1 || st->clipMode == 2 || st->preventMathMode == 1 || st->preventMathMode == 2;
  Win cw;
  WinCalc(&cw, st, 5);
  if (cw_used && cw.nr > 1) return "colour window splits a line";
  static const uint8_t kCwBitsMod[8] = { 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00 };
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
static GpuPpuStats g_stats;
uint64_t (*g_gpu_ppu_clock)(void);
static inline uint64_t Clock(void) { return g_gpu_ppu_clock ? g_gpu_ppu_clock() : 0; }

static uint32_t g_colour_dirty[8];   // per CGRAM entry (256 bits), for mode 7

static void DiffMemories(const Ppu *ppu) {
  g_ppu_vram_dirty = g_group_dirty;   // tracking starts with the first frame built
  if (!g_shadow_valid) {
    memset(g_group_dirty, 1, sizeof(g_group_dirty));
    for (int i = 0; i < 8; i++) g_pal4_dirty[i] = g_pal2_dirty[i] = true;
    memset(g_colour_dirty, 0xff, sizeof(g_colour_dirty));
  } else {
    for (int p = 0; p < 8; p++) {
      g_pal4_dirty[p] = memcmp(&ppu->cgram[p * 16], &g_cgram_shadow[p * 16], 32) != 0;
      g_pal2_dirty[p] = memcmp(&ppu->cgram[p * 4], &g_cgram_shadow[p * 4], 8) != 0;
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
static void DecodeTile(uint16_t *dst, const Ppu *ppu, int base, int bpp, const uint16_t *lut, bool hflip, bool vflip) {
  const uint32_t *spread = g_spread[hflip];
  for (int r = 0; r < 8; r++) {
    const int sr = vflip ? 7 - r : r;
    const uint16_t w0 = ppu->vram[(base + sr) & 0x7fff];
    uint32_t pix = spread[w0 & 0xff] | spread[w0 >> 8] << 1;
    if (bpp == 4) {
      const uint16_t w1 = ppu->vram[(base + sr + 8) & 0x7fff];
      pix |= spread[w1 & 0xff] << 2 | spread[w1 >> 8] << 3;
    }
    const uint8_t *m = g_row_morton[r];
    for (int x = 0; x < 8; x++, pix >>= 4) dst[m[x]] = lut[pix & 15];
  }
}

// ---- BG surfaces: a whole tilemap decoded into two textures -------------------------
// One texture holds the priority-0 tiles, the other the priority-1 tiles; each is
// transparent where the other has its tile. Sized like the tilemap (256 or 512 px
// each way), so GPU_REPEAT wrapping is the SNES's own wrap-around.

enum { kSurfaces = 8 };

typedef struct {
  bool used;
  bool fresh;              // contents unknown: decode every tile
  uint32_t key;
  uint32_t last_frame;
  uint16_t tilemap, tiles;
  bool wider, higher;
  int bpp;
  GpuTex tex[2];           // [0] priority 0, [1] priority 1
  uint16_t map[64 * 64];   // tilemap entries the textures were decoded from
} Surface;

static Surface g_surf[kSurfaces];
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

static void FreeSurface(Surface *s) {
  for (int i = 0; i < 2; i++)
    if (s->tex[i].px) GpuBackend_TexFree(&s->tex[i]);
  memset(s, 0, sizeof(*s));
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

// One 8x8 tile into its block of the priority texture; the same block of the other
// texture is cleared.
static void DecodeBgTile(Surface *s, const Ppu *ppu, int tx, int ty, uint16_t e) {
  const int w = SurfaceW(s);
  const int block = ((ty * (w >> 3)) + tx) << 6;
  uint16_t *dst = s->tex[(e & 0x2000) ? 1 : 0].px + block;
  memset(s->tex[(e & 0x2000) ? 0 : 1].px + block, 0, 64 * sizeof(uint16_t));
  const int pal = (e >> 10) & 7, c = e & 0x3ff;
  const int base = s->bpp == 4 ? s->tiles + c * 16 : s->tiles + c * 8;
  DecodeTile(dst, ppu, base, s->bpp, s->bpp == 4 ? g_lut_bg4[pal] : g_lut_bg2[pal], e & 0x4000, e & 0x8000);
  g_stats.tiles_decoded++;
}

// Any VRAM change in words [a, a+n) (wrapping), in 8-word groups.
static bool RangeDirty(int a, int n) {
  for (int g = (a & 0x7fff) >> 3, k = 0; k < (n + 7) >> 3; k++, g = (g + 1) & 0xfff)
    if (g_group_dirty[g]) return true;
  return false;
}

static void SyncSurface(Surface *s, const Ppu *ppu) {
  if (s->last_frame == g_frame_no && !s->fresh) return;   // already synced this frame
  const int tw = SurfaceW(s) >> 3, th = SurfaceH(s) >> 3;
  if (!s->fresh) {
    // Nothing it reads changed: its tilemap, any of its 1024 chars, its palettes.
    bool pal = false;
    for (int i = 0; i < 8; i++) pal |= s->bpp == 4 ? g_pal4_dirty[i] : g_pal2_dirty[i];
    if (!pal && !RangeDirty(s->tilemap, tw * th) && !RangeDirty(s->tiles, 1024 * (s->bpp == 4 ? 16 : 8))) {
      s->last_frame = g_frame_no;
      return;
    }
  }
  int y0 = th, y1 = -1;
  for (int ty = 0; ty < th; ty++) {
    for (int tx = 0; tx < tw; tx++) {
      const uint16_t e = ppu->vram[MapAddr(s, tx, ty)];
      uint16_t *m = &s->map[ty * 64 + tx];
      if (!s->fresh && e == *m && !CharDirty(s, e) && !PalDirty(s, e)) continue;
      *m = e;
      DecodeBgTile(s, ppu, tx, ty, e);
      if (ty < y0) y0 = ty;
      y1 = ty;
    }
  }
  if (y1 >= 0) {
    GpuBackend_TexWritten(&s->tex[0], y0 * 8, (y1 + 1) * 8);
    GpuBackend_TexWritten(&s->tex[1], y0 * 8, (y1 + 1) * 8);
  }
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
static uint8_t g_m7_stale[128 * 128];
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
    for (int c = g * 8; c < g * 8 + 8; c++)
      if ((ppu->vram[c] & 0xff) != g_m7_map[c]) g_m7_stale[c] = 1;
  }
  if (any_tile)
    for (int c = 0; c < 128 * 128; c++)
      if (tile_dirty[g_m7_map[c]]) g_m7_stale[c] = 1;
}

static void M7DecodeCell(const Ppu *ppu, int c) {
  const int t = ppu->vram[c] & 0xff;
  g_m7_map[c] = (uint8_t)t;
  g_m7_stale[c] = 0;
  if (!g_m7_tile_known[t]) M7TileColours(ppu, t);
  uint16_t *dst = g_m7_tex.px + (c << 6);   // cells are the texture's 8x8 blocks, row-major
  const uint16_t *src = &ppu->vram[t * 64];
  for (int r = 0; r < 8; r++) {
    const uint8_t *m = g_row_morton[r];
    for (int x = 0; x < 8; x++) dst[m[x]] = g_lut_m7[src[r * 8 + x] >> 8];
  }
  g_stats.m7_cells_decoded++;
}

// Decodes the stale cells in texel rectangle [x0, x1] x [y0, y1] (may lie outside the
// plane: it wraps, as the plane does without the large field).
static void M7Sync(const Ppu *ppu, int x0, int x1, int y0, int y1) {
  int cx0 = x0 >> 3, cx1 = x1 >> 3, cy0 = y0 >> 3, cy1 = y1 >> 3;
  if (cx1 - cx0 >= 127) cx0 = 0, cx1 = 127;
  if (cy1 - cy0 >= 127) cy0 = 0, cy1 = 127;
  int rows0 = 128, rows1 = -1;
  for (int cy = cy0; cy <= cy1; cy++)
    for (int cx = cx0; cx <= cx1; cx++) {
      const int c = (cy & 127) * 128 + (cx & 127);
      if (!g_m7_stale[c]) continue;
      M7DecodeCell(ppu, c);
      if ((cy & 127) < rows0) rows0 = cy & 127;
      if ((cy & 127) > rows1) rows1 = cy & 127;
    }
  if (rows1 >= rows0) GpuBackend_TexWritten(&g_m7_tex, rows0 * 8, rows1 * 8 + 8);
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
// A layer whose scroll changes on many lines (Norfair heat, Maridia water) would need
// one quad per line. Instead its rows are copied from the surface into a 256x256
// screen texture on the CPU and drawn as one quad.

static GpuTex g_screen_tex[3][2];

static GpuTex *ScreenTex(int layer, int prio) {
  GpuTex *t = &g_screen_tex[layer][prio];
  if (!t->px && !GpuBackend_TexCreate(t, 256, 256)) return NULL;
  return t;
}

// ---- Sprites ---------------------------------------------------------------------------

static const int kSpriteSizes[8][2] = {
  { 8, 16 }, { 8, 32 }, { 8, 64 }, { 16, 32 }, { 16, 64 }, { 32, 64 }, { 16, 32 }, { 16, 32 },
};

enum { kAtlasW = 512, kAtlasH = 512 };
static GpuTex g_atlas;

typedef struct {
  int size, x, y;         // screen position (x signed, y 0..255)
  int ax, ay;             // in the atlas
  uint8_t level;
  bool hflip, vflip, math;
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
    if (x > 255) x -= 512;
    if (x <= -size) continue;
    const int y = ppu->oam[index] >> 8;
    // Visible on rows y .. y+size-1, wrapping at 256; rows 224+ are not shown.
    if (y + size <= 256 && y >= kGpuRows) continue;
    const uint16_t attr = ppu->oam[index + 1];
    Sprite *sp = &g_sprites[g_sprite_count];
    if (!AtlasPlace(ppu, st, attr, size, &sp->ax, &sp->ay)) return "sprite atlas full";
    sp->size = size;
    sp->x = x;
    sp->y = y;
    sp->level = (uint8_t)(((attr >> 12) & 3) * 4 + 2);
    sp->hflip = attr & 0x4000;
    sp->vflip = attr & 0x8000;
    sp->math = (attr & 0x800) && st->mathEnabled[4];
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

static bool AddQuad(GpuTex *t, int x, int y, int w, int h, int sx, int sy, int level, int flags) {
  const int ti = TexIndex(t);
  if (ti < 0 || g_out->quad_count >= kGpuMaxQuads || h <= 0) return ti >= 0 && h <= 0;
  g_out->quads[g_out->quad_count++] = (GpuQuad){ (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (int16_t)sx, (int16_t)sy,
                                                 (uint8_t)ti, (uint8_t)level, (uint8_t)flags, 0, 0, 0, 0 };
  return true;
}

// Mode 7 row: see GpuQuad.ax.
static bool AddAffine(GpuTex *t, int x, int y, int w, uint32_t ax, uint32_t ay, uint32_t adx, uint32_t ady, int level,
                      int flags) {
  if (!AddQuad(t, x, y, w, 1, 0, 0, level, flags | kGpuQuadAffine)) return false;
  GpuQuad *q = &g_out->quads[g_out->quad_count - 1];
  q->ax = (int32_t)ax, q->ay = (int32_t)ay, q->adx = (int32_t)adx, q->ady = (int32_t)ady;
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
static bool AddQuadWin(GpuTex *t, int x, int y, int w, int h, int sx, int sy, int level, int flags, int scr,
                       int layer) {
  // Bands share LineInfo, so the band's first row tells whether the layer is partial.
  if (!(g_info[y + 1].partial[scr] & (1 << layer))) return AddQuad(t, x, y, w, h, sx, sy, level, flags);
  for (int r0 = y; r0 < y + h;) {
    const WinSpans *sp = &g_spans[r0 + 1][scr][layer];
    int r1 = r0 + 1;
    while (r1 < y + h && SameSpans(sp, &g_spans[r1 + 1][scr][layer])) r1++;
    for (int i = 0; i < sp->n; i++) {
      const int x0 = sp->x[i][0] > x ? sp->x[i][0] : x;
      const int x1 = sp->x[i][1] < x + w ? sp->x[i][1] : x + w;
      if (x0 >= x1) continue;
      if (flags & kGpuQuadAffine) {   // one row; the cut moves the start along the step
        const uint32_t k = (uint32_t)(x0 - x);
        if (!AddAffine(t, x0, r0, x1 - x0, g_affine.ax + g_affine.adx * k, g_affine.ay + g_affine.ady * k, g_affine.adx,
                       g_affine.ady, level, flags & ~kGpuQuadAffine))
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

static bool EmitSprites(int y0, int y1, bool main) {
  for (int i = 0; i < g_sprite_count; i++) {
    const Sprite *sp = &g_sprites[i];
    const int flags = kGpuQuadObj | (sp->hflip ? kGpuQuadFlipX : 0) | (sp->vflip ? kGpuQuadFlipY : 0) |
                      (main && sp->math ? kGpuQuadMath : 0);
    for (int top = sp->y; top >= sp->y - 256; top -= 256) {
      const int r0 = top > y0 ? top : y0, r1 = top + sp->size < y1 ? top + sp->size : y1;
      if (r0 >= r1) continue;
      const int h = r1 - r0, skip = r0 - top;
      // With a vertical flip the quad's last row shows texel row ay + size-1-skip-(h-1).
      const int sy = sp->vflip ? sp->ay + sp->size - h - skip : sp->ay + skip;
      if (!AddQuadWin(&g_atlas, sp->x, r0, sp->size, h, sp->ax, sy, sp->level, flags, main ? 0 : 1, 4)) return false;
    }
  }
  return true;
}

enum { kMaxScrollRuns = 8 };

static const uint8_t kBgLevel[3][2] = { { 8, 12 }, { 7, 11 }, { 1, 15 } };

static uint32_t g_composed[3][kGpuRows];   // frame number each screen-texture row was composed in

// One BG layer over the band's lines [l0, l1] (output rows l0-1 .. l1-1).
static const char *EmitBg(const Ppu *ppu, const PpuLineCapture *cap, int layer, int l0, int l1, bool math, int scr) {
  Surface *s = GetSurface(&cap->line[l0].bgLayer[layer], layer < 2 ? 4 : 2);
  if (!s) return "out of texture memory";
  SyncSurface(s, ppu);
  const int flags = math ? kGpuQuadMath : 0;
  int runs = 1;
  for (int l = l0 + 1; l <= l1; l++) {
    const BgLayer *a = &cap->line[l - 1].bgLayer[layer], *b = &cap->line[l].bgLayer[layer];
    runs += a->hScroll != b->hScroll || a->vScroll != b->vScroll;
  }
  if (runs <= kMaxScrollRuns) {
    for (int a = l0; a <= l1;) {
      const BgLayer *bg = &cap->line[a].bgLayer[layer];
      int b = a;
      while (b + 1 <= l1 && cap->line[b + 1].bgLayer[layer].hScroll == bg->hScroll &&
             cap->line[b + 1].bgLayer[layer].vScroll == bg->vScroll)
        b++;
      // Output row a-1 shows tilemap row a + vScroll (the PPU draws line a there).
      for (int prio = 0; prio < 2; prio++)
        if (!AddQuadWin(&s->tex[prio], 0, a - 1, 256, b - a + 1, bg->hScroll, a + bg->vScroll, kBgLevel[layer][prio],
                        flags, scr, layer))
          return "too many quads";
      a = b + 1;
    }
    return NULL;
  }
  const int w = SurfaceW(s) - 1, h = SurfaceH(s) - 1;
  GpuTex *t[2] = { ScreenTex(layer, 0), ScreenTex(layer, 1) };
  if (!t[0] || !t[1]) return "out of texture memory";
  // GpuTexelIndex(x, y, w) = row part (y) + column part (x): precompute the columns.
  static uint16_t dst_col[256], src_col[512];
  static int src_col_w;
  if (!dst_col[1]) for (int x = 0; x < 256; x++) dst_col[x] = (uint16_t)GpuTexelIndex(x, 0, 256);
  if (src_col_w != w + 1) {
    for (int x = 0; x <= w; x++) src_col[x] = (uint16_t)GpuTexelIndex(x, 0, w + 1);
    src_col_w = w + 1;
  }
  for (int l = l0; l <= l1; l++) {
    const int row = l - 1;
    if (g_composed[layer][row] == g_frame_no) continue;   // main and sub share it
    const BgLayer *bg = &cap->line[l].bgLayer[layer];
    const int ty = (l + bg->vScroll) & h;
    const int src_row = GpuTexelIndex(0, ty, w + 1), dst_row = GpuTexelIndex(0, row, 256);
    for (int p = 0; p < 2; p++) {
      const uint16_t *src = s->tex[p].px + src_row;
      uint16_t *dst = t[p]->px + dst_row;
      for (int x = 0; x < 256; x++) dst[dst_col[x]] = src[src_col[(bg->hScroll + x) & w]];
    }
    g_composed[layer][row] = g_frame_no;
    g_stats.screen_rows_composed++;
  }
  GpuBackend_TexWritten(t[0], l0 - 1, l1);
  GpuBackend_TexWritten(t[1], l0 - 1, l1);
  for (int prio = 0; prio < 2; prio++)
    if (!AddQuadWin(&g_screen_tex[layer][prio], 0, l0 - 1, 256, l1 - l0 + 1, 0, l0 - 1, kBgLevel[layer][prio], flags,
                    scr, layer))
      return "too many quads";
  return NULL;
}

static uint32_t g_m7_tracked_frame, g_m7_used_frame;

// The mode 7 plane over the band's lines [l0, l1]: one affine row per line, so the
// matrix may change on every line (perspective) as well as between frames.
static const char *EmitMode7(const Ppu *ppu, const PpuLineCapture *cap, int l0, int l1, bool math, int scr) {
  if (!g_m7_tex.px) {
    if (!GpuBackend_TexCreate(&g_m7_tex, 1024, 1024)) return "out of texture memory";
    g_m7_fresh = true;
  }
  if (g_m7_tracked_frame != g_frame_no) {   // first use: GpuPpu_BuildFrame tracks from now on
    M7Track(ppu);
    g_m7_tracked_frame = g_frame_no;
  }
  g_m7_used_frame = g_frame_no;
  // Texels the band can show: the ends of each row, since a row is a straight line.
  const bool border = cap->line[l0].m7largeField;
  int x0 = INT32_MAX, x1 = INT32_MIN, y0 = INT32_MAX, y1 = INT32_MIN;
  for (int l = l0; l <= l1; l++) {
    uint32_t ax, ay, adx, ady;
    M7LineStart(&cap->line[l], l, &ax, &ay, &adx, &ady);
    const int32_t ex[2] = { (int32_t)ax >> 8, (int32_t)(ax + adx * 255) >> 8 };
    const int32_t ey[2] = { (int32_t)ay >> 8, (int32_t)(ay + ady * 255) >> 8 };
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
  }
  if (x0 <= x1 && y0 <= y1) M7Sync(ppu, x0, x1, y0, y1);
  const int flags = (math ? kGpuQuadMath : 0) | (border ? kGpuQuadBorder : 0);
  for (int l = l0; l <= l1; l++) {
    uint32_t ax, ay, adx, ady;
    M7LineStart(&cap->line[l], l, &ax, &ay, &adx, &ady);
    int i0 = 0, i1 = 256;   // pixels of the row to draw
    if (border) {
      // Outside the plane is transparent: keep the pixels whose position is inside, so
      // the GPU never sees far-out coordinates (its 24-bit floats would lose the texel).
      // Exact as long as a row does not wrap 32 bits, which no sane matrix does.
      const int64_t p[2] = { (int32_t)ax, (int32_t)ay }, d[2] = { (int32_t)adx, (int32_t)ady };
      for (int k = 0; k < 2 && i0 < i1; k++) {
        // 0 <= p + d*i < 0x40000
        if (d[k] == 0) {
          if (p[k] < 0 || p[k] >= 0x40000) i1 = i0;
          continue;
        }
        int64_t lo, hi;   // i in [lo, hi)
        if (d[k] > 0) {
          lo = p[k] >= 0 ? 0 : (-p[k] + d[k] - 1) / d[k];
          hi = 0x40000 - p[k] <= 0 ? 0 : (0x40000 - p[k] + d[k] - 1) / d[k];
        } else {
          lo = p[k] < 0x40000 ? 0 : (p[k] - 0x40000) / -d[k] + 1;
          hi = p[k] < 0 ? 0 : p[k] / -d[k] + 1;
        }
        if (lo > i0) i0 = lo > 256 ? 256 : (int)lo;
        if (hi < i1) i1 = hi < 0 ? 0 : (int)hi;
      }
      if (i0 >= i1) continue;
      ax += adx * (uint32_t)i0, ay += ady * (uint32_t)i0;
    }
    g_affine.ax = ax, g_affine.ay = ay, g_affine.adx = adx, g_affine.ady = ady;
    // The CPU renderer draws the plane at level 5 (z 0x5000): above sprites of
    // priority 0, below the others.
    if (g_info[l].partial[scr] & 1) {
      if (!AddQuadWin(&g_m7_tex, i0, l - 1, i1 - i0, 1, 0, 0, 5, flags | kGpuQuadAffine, scr, 0)) return "too many quads";
    } else if (!AddAffine(&g_m7_tex, i0, l - 1, i1 - i0, ax, ay, adx, ady, 5, flags)) {
      return "too many quads";
    }
  }
  return NULL;
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
    if (!EmitSprites(l0 - 1, l1, main)) return "too many quads";
  }
  const uint64_t t0 = Clock();
  if (st->mode == 7) {
    if (layers & 1) err = EmitMode7(ppu, cap, l0, l1, main && st->mathEnabled[0], main ? 0 : 1);
    g_stats.t_bg += Clock() - t0;
    return err;
  }
  for (int layer = 0; layer < 3; layer++)
    if ((layers & (1 << layer)) && (err = EmitBg(ppu, cap, layer, l0, l1, main && st->mathEnabled[layer], main ? 0 : 1)))
      break;
  g_stats.t_bg += Clock() - t0;
  return err;
}

static bool SameBand(const PpuLineState *a, const LineInfo *ia, const PpuLineState *b, const LineInfo *ib) {
  LineKey ka, kb;
  MakeKey(&ka, a, ia);
  MakeKey(&kb, b, ib);
  return memcmp(&ka, &kb, sizeof(ka)) == 0;
}

bool GpuPpu_BuildFrame(const Ppu *ppu, const PpuLineCapture *cap, GpuFrame *out, const char **reason) {
  static LineInfo info[kPpuCaptureLines];
  memset(&g_stats, 0, sizeof(g_stats));
  out->tex_count = out->quad_count = out->band_count = 0;
  g_out = out;
  *reason = NULL;
  uint64_t t0 = Clock();
  if (cap->last_line < kGpuRows) { *reason = "frame shorter than 224 lines"; return false; }
  if (cap->midframe_data_writes) { *reason = "VRAM/CGRAM/OAM written mid-frame"; return false; }
  for (int l = 1; l <= kGpuRows; l++)
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
  if (g_m7_tex.px && g_frame_no - g_m7_used_frame > 120) GpuBackend_TexFree(&g_m7_tex);
  if (g_m7_tex.px) {
    M7Track(ppu);
    g_m7_tracked_frame = g_frame_no;
  }
  g_stats.t_diff = Clock() - t1;
  t0 = Clock();
  g_atlas_entry_count = g_shelf_x = g_shelf_y = g_shelf_h = 0;
  bool ok = true;
  for (int l0 = 1; l0 <= kGpuRows && ok;) {
    int l1 = l0;
    while (l1 + 1 <= kGpuRows && SameBand(&cap->line[l0], &info[l0], &cap->line[l1 + 1], &info[l1 + 1])) l1++;
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
      b->add_subscreen = st->addSubscreen;
      b->subtract = st->subtractColor;
      b->half = st->halfColor;
      b->fixed = (uint16_t)(st->fixedColorR | st->fixedColorG << 5 | st->fixedColorB << 10);
      bool sprites_built = false;
      b->main_first = out->quad_count;
      if ((*reason = EmitScreen(ppu, cap, inf->main, l0, l1, true, &sprites_built))) { ok = false; break; }
      b->main_count = out->quad_count - b->main_first;
      b->sub_first = out->quad_count;
      if (b->math && b->add_subscreen && inf->sub &&
          (*reason = EmitScreen(ppu, cap, inf->sub, l0, l1, false, &sprites_built))) { ok = false; break; }
      b->sub_count = out->quad_count - b->sub_first;
    }
    l0 = l1 + 1;
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
