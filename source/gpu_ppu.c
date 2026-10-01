#include "gpu_ppu.h"

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
  bool clip, math_ok;    // colour window outcome, uniform over the line
} LineInfo;

// Fills `info`; returns NULL if the line can be drawn, else why not.
static const char *AnalyzeLine(const PpuLineState *st, LineInfo *info) {
  memset(info, 0, sizeof(*info));
  if (st->forcedBlank) return NULL;
  if (st->mode != 1) return "mode is not 1";
  if (st->objInterlace) return "OBJ interlace";
  for (int sub = 0; sub < 2; sub++) {
    uint8_t on = st->screenEnabled[sub] & 0x17;
    for (int layer = 0; layer < 5; layer++) {
      if (!(on & (1 << layer)) || !(st->screenWindowed[sub] & (1 << layer))) continue;
      Win w;
      WinCalc(&w, st, layer);
      if (w.nr > 1) return "window splits a line";
      if (w.bits & 1) on &= ~(1 << layer);
    }
    if (sub) info->sub = on; else info->main = on;
  }
  Win cw;
  WinCalc(&cw, st, 5);
  if (cw.nr > 1) return "colour window splits a line";
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
  k->st.evenFrame = false;
  k->st.mosaicSize = 0;
  memset(k->st.m7matrix, 0, sizeof(k->st.m7matrix));
  k->info = *info;
}

// ---- Change tracking -----------------------------------------------------------------
// VRAM and CGRAM as of the last frame the surfaces were synchronised with.

static uint16_t g_vram_shadow[0x8000];
static uint16_t g_cgram_shadow[0x100];
static bool g_shadow_valid;
static uint8_t g_group_dirty[0x1000];   // per 8 VRAM words
static bool g_pal4_dirty[8], g_pal2_dirty[8];
static GpuPpuStats g_stats;

static void DiffMemories(const Ppu *ppu) {
  if (!g_shadow_valid) {
    memset(g_group_dirty, 1, sizeof(g_group_dirty));
    for (int i = 0; i < 8; i++) g_pal4_dirty[i] = g_pal2_dirty[i] = true;
  } else {
    for (int g = 0; g < 0x1000; g++)
      g_group_dirty[g] = memcmp(&ppu->vram[g * 8], &g_vram_shadow[g * 8], 16) != 0;
    for (int p = 0; p < 8; p++) {
      g_pal4_dirty[p] = memcmp(&ppu->cgram[p * 16], &g_cgram_shadow[p * 16], 32) != 0;
      g_pal2_dirty[p] = memcmp(&ppu->cgram[p * 4], &g_cgram_shadow[p * 4], 8) != 0;
    }
  }
}

static void UpdateShadows(const Ppu *ppu) {
  memcpy(g_vram_shadow, ppu->vram, sizeof(g_vram_shadow));
  memcpy(g_cgram_shadow, ppu->cgram, sizeof(g_cgram_shadow));
  g_shadow_valid = true;
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
  const bool hflip = e & 0x4000, vflip = e & 0x8000;
  uint16_t lut[16];
  lut[0] = 0;
  if (s->bpp == 4) {
    for (int i = 1; i < 16; i++) lut[i] = GpuRgba5551(ppu->cgram[pal * 16 + i]);
  } else {
    for (int i = 1; i < 4; i++) lut[i] = GpuRgba5551(ppu->cgram[pal * 4 + i]);
  }
  const int base = s->bpp == 4 ? s->tiles + c * 16 : s->tiles + c * 8;
  for (int r = 0; r < 8; r++) {
    const int sr = vflip ? 7 - r : r;
    const uint16_t w0 = ppu->vram[(base + sr) & 0x7fff];
    const uint16_t w1 = s->bpp == 4 ? ppu->vram[(base + sr + 8) & 0x7fff] : 0;
    for (int col = 0; col < 8; col++) {
      const int b = hflip ? col : 7 - col;
      const int p = (w0 >> b & 1) | (w0 >> (b + 8) & 1) << 1 | (w1 >> b & 1) << 2 | (w1 >> (b + 8) & 1) << 3;
      dst[GpuTexelIndex(col, r, 8)] = lut[p];
    }
  }
  g_stats.tiles_decoded++;
}

static void SyncSurface(Surface *s, const Ppu *ppu) {
  if (s->last_frame == g_frame_no && !s->fresh) return;   // already synced this frame
  const int tw = SurfaceW(s) >> 3, th = SurfaceH(s) >> 3;
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

void GpuPpu_Invalidate(void) {
  for (int i = 0; i < kSurfaces; i++) g_surf[i].fresh = true;
  g_shadow_valid = false;
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
  uint16_t lut[16];
  lut[0] = 0;
  for (int i = 1; i < 16; i++) lut[i] = GpuRgba5551(ppu->cgram[128 + pal * 16 + i]);
  uint16_t *dst = g_atlas.px + (((dy >> 3) * (kAtlasW >> 3) + (dx >> 3)) << 6);
  for (int r = 0; r < 8; r++) {
    const uint16_t *addr = &ppu->vram[(obj_adr + tile * 16 + r) & 0x7fff];
    const uint32_t plane = addr[0] | (uint32_t)ppu->vram[(obj_adr + tile * 16 + r + 8) & 0x7fff] << 16;
    for (int col = 0; col < 8; col++) {
      const uint32_t bits = plane >> (7 - col);
      const int p = (bits & 1) | ((bits >> 7) & 2) | ((bits >> 14) & 4) | ((bits >> 21) & 8);
      dst[GpuTexelIndex(col, r, 8)] = lut[p];
    }
  }
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
                                                 (uint8_t)ti, (uint8_t)level, (uint8_t)flags };
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
      if (!AddQuad(&g_atlas, sp->x, r0, sp->size, h, sp->ax, sy, sp->level, flags)) return false;
    }
  }
  return true;
}

enum { kMaxScrollRuns = 8 };

static const uint8_t kBgLevel[3][2] = { { 8, 12 }, { 7, 11 }, { 1, 15 } };

static uint32_t g_composed[3][kGpuRows];   // frame number each screen-texture row was composed in

// One BG layer over the band's lines [l0, l1] (output rows l0-1 .. l1-1).
static const char *EmitBg(const Ppu *ppu, const PpuLineCapture *cap, int layer, int l0, int l1, bool math) {
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
        if (!AddQuad(&s->tex[prio], 0, a - 1, 256, b - a + 1, bg->hScroll, a + bg->vScroll, kBgLevel[layer][prio], flags))
          return "too many quads";
      a = b + 1;
    }
    return NULL;
  }
  const int w = SurfaceW(s) - 1, h = SurfaceH(s) - 1;
  GpuTex *t[2] = { ScreenTex(layer, 0), ScreenTex(layer, 1) };
  if (!t[0] || !t[1]) return "out of texture memory";
  for (int l = l0; l <= l1; l++) {
    const int row = l - 1;
    if (g_composed[layer][row] == g_frame_no) continue;   // main and sub share it
    const BgLayer *bg = &cap->line[l].bgLayer[layer];
    const int ty = (l + bg->vScroll) & h;
    for (int p = 0; p < 2; p++) {
      const uint16_t *src = s->tex[p].px;
      uint16_t *dst = t[p]->px;
      for (int x = 0; x < 256; x++)
        dst[GpuTexelIndex(x, row, 256)] = src[GpuTexelIndex((bg->hScroll + x) & w, ty, w + 1)];
    }
    g_composed[layer][row] = g_frame_no;
    g_stats.screen_rows_composed++;
  }
  GpuBackend_TexWritten(t[0], l0 - 1, l1);
  GpuBackend_TexWritten(t[1], l0 - 1, l1);
  for (int prio = 0; prio < 2; prio++)
    if (!AddQuad(&g_screen_tex[layer][prio], 0, l0 - 1, 256, l1 - l0 + 1, 0, l0 - 1, kBgLevel[layer][prio], flags))
      return "too many quads";
  return NULL;
}

static const char *EmitScreen(const Ppu *ppu, const PpuLineCapture *cap, uint8_t layers, int l0, int l1, bool main,
                              bool *sprites_built) {
  const PpuLineState *st = &cap->line[l0];
  const char *err;
  if (layers & 0x10) {
    if (!*sprites_built) {
      if ((err = BuildSprites(ppu, st))) return err;
      *sprites_built = true;
    }
    if (!EmitSprites(l0 - 1, l1, main)) return "too many quads";
  }
  for (int layer = 0; layer < 3; layer++)
    if ((layers & (1 << layer)) && (err = EmitBg(ppu, cap, layer, l0, l1, main && st->mathEnabled[layer])))
      return err;
  return NULL;
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
  if (cap->last_line < kGpuRows) { *reason = "frame shorter than 224 lines"; return false; }
  if (cap->midframe_data_writes) { *reason = "VRAM/CGRAM/OAM written mid-frame"; return false; }
  for (int l = 1; l <= kGpuRows; l++)
    if ((*reason = AnalyzeLine(&cap->line[l], &info[l]))) return false;
  if (!g_atlas.px && !GpuBackend_TexCreate(&g_atlas, kAtlasW, kAtlasH)) { *reason = "out of texture memory"; return false; }

  g_frame_no++;
  DiffMemories(ppu);
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
  if (g_atlas_entry_count) GpuBackend_TexWritten(&g_atlas, 0, g_shelf_y + g_shelf_h);
  // Surfaces this frame did not sync have missed this frame's changes.
  for (int i = 0; i < kSurfaces; i++) {
    if (!g_surf[i].used) continue;
    g_stats.surfaces += g_surf[i].last_frame == g_frame_no;
    if (g_surf[i].last_frame != g_frame_no) g_surf[i].fresh = true;
  }
  UpdateShadows(ppu);
  return ok;
}

const GpuPpuStats *GpuPpu_LastStats(void) { return &g_stats; }
