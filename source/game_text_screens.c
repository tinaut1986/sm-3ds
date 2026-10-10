// The game's screens in the UI language (docs/PLAN.md P4.8): menus, intro, pause screen...
// Same rule as the message boxes (game_text.c): only VRAM changes, never the game's RAM.
//
// How: the PPU keeps a copy of VRAM as the game wrote it (g_ppu_vram_shadow, ppu.c). Each
// frame, after the game and before the PPU draws (g_rtl_game_text_hook), the visible
// tilemaps are read from that copy, the English phrases of the current screen are found
// by their letters (a font is the char, or pair of chars for 16 px letters, of each
// letter), and the translation is written over them in VRAM. Letters the game's font
// lacks (accents, Ñ, a missing capital) are drawn into chars nothing on screen uses,
// derived from the font's own letter when there is one. What was changed last frame and
// not this one is put back from the copy, so leaving a screen or switching to English
// restores the game's VRAM exactly.

#include "game_text.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "src/types.h"
#include "src/variables.h"
#include "src/sm_cpu_infra.h"
#include "src/sm_rtl.h"
#include "src/snes/ppu.h"
#include "src/snes/snes.h"
#include "ui_lang.h"

enum { kVramWords = 0x8000 };

static uint16_t g_shadow[kVramWords];

static Ppu *ThePpu(void) { return g_snes ? g_snes->ppu : NULL; }

// ---- VRAM changes, put back when no longer wanted -----------------------------------------

enum { kMaxMods = 6144 };
static uint16_t g_mod_adr[kMaxMods], g_mod_val[kMaxMods];
static int g_mod_count;
static uint16_t g_prev_adr[kMaxMods];
static int g_prev_count;
static uint8_t g_marked[kVramWords / 8];

static bool Marked(uint16_t adr) { return g_marked[adr >> 3] >> (adr & 7) & 1; }

static void Want(uint16_t adr, uint16_t v) {
  adr &= 0x7fff;
  if (Marked(adr)) {
    for (int i = g_mod_count - 1; i >= 0; i--)
      if (g_mod_adr[i] == adr) { g_mod_val[i] = v; return; }
  }
  if (g_mod_count >= kMaxMods) return;
  g_marked[adr >> 3] |= (uint8_t)(1 << (adr & 7));
  g_mod_adr[g_mod_count] = adr;
  g_mod_val[g_mod_count++] = v;
}

static void Put(Ppu *ppu, uint16_t adr, uint16_t v) {
  if (ppu->vram[adr] == v) return;
  ppu->vram[adr] = v;
  if (g_ppu_vram_dirty) g_ppu_vram_dirty[adr >> 3] = 1;
}

static void Commit(void) {
  Ppu *ppu = ThePpu();
  if (!ppu) return;
  for (int i = 0; i < g_prev_count; i++)
    if (!Marked(g_prev_adr[i])) Put(ppu, g_prev_adr[i], g_shadow[g_prev_adr[i]]);
  for (int i = 0; i < g_mod_count; i++) {
    Put(ppu, g_mod_adr[i], g_mod_val[i]);
    g_prev_adr[i] = g_mod_adr[i];
    g_marked[g_mod_adr[i] >> 3] = 0;
  }
  g_prev_count = g_mod_count;
  g_mod_count = 0;
}

// ---- Tiles --------------------------------------------------------------------------------

typedef struct {
  uint8_t px[8][8];
} Tile;

static void ReadTile(uint16_t base, int c, int bpp, Tile *t) {
  const uint16_t a = (uint16_t)(base + c * 4 * bpp);
  for (int y = 0; y < 8; y++) {
    const uint16_t p0 = g_shadow[(a + y) & 0x7fff], p1 = bpp == 4 ? g_shadow[(a + 8 + y) & 0x7fff] : 0;
    for (int x = 0; x < 8; x++) {
      const int k = 7 - x;
      t->px[y][x] = (uint8_t)((p0 >> k & 1) | (p0 >> (k + 8) & 1) << 1 | (p1 >> k & 1) << 2 | (p1 >> (k + 8) & 1) << 3);
    }
  }
}

static void WriteTile(uint16_t base, int c, int bpp, const Tile *t) {
  const uint16_t a = (uint16_t)(base + c * 4 * bpp);
  for (int y = 0; y < 8; y++) {
    uint16_t p0 = 0, p1 = 0;
    for (int x = 0; x < 8; x++) {
      const int k = 7 - x, v = t->px[y][x];
      p0 |= (uint16_t)((v & 1) << k | (v >> 1 & 1) << (k + 8));
      p1 |= (uint16_t)((v >> 2 & 1) << k | (v >> 3 & 1) << (k + 8));
    }
    Want((uint16_t)(a + y), p0);
    if (bpp == 4) Want((uint16_t)(a + 8 + y), p1);
  }
}

// Pixels from strings: '.' is 0, a hex digit that colour.
static void TileFromRows(Tile *t, const char *const *rows) {
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) {
      const char ch = rows[y][x];
      t->px[y][x] = (uint8_t)(ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : 0);
    }
}

// ---- Fonts --------------------------------------------------------------------------------

typedef struct {
  uint16_t code;            // Latin-1 code point
  uint16_t top, bottom;     // the chars (bottom only for 16 px fonts)
} FontChar;

typedef struct {
  uint16_t code;
  const char *rows[16];     // 8 or 16 rows of 8 pixels; a 16 px glyph whose first row is NULL
                            // takes its top from the font's char (`top_from`)
  uint16_t top_from;
} CustomGlyph;

typedef struct {
  int h, bpp;               // cells per letter (1 or 2), bits per pixel
  uint16_t fill;            // the blank cell
  const FontChar *chars;
  int char_count;
  const CustomGlyph *custom;
  int custom_count;
  uint8_t pen, shade;       // the accent marks' colour and their shadow (0: none)
  uint16_t pool_lo, pool_hi;   // chars that may be borrowed when nothing on screen uses them
} Font;

typedef enum { kMarkNone, kMarkAcute, kMarkGrave, kMarkCirc, kMarkTilde, kMarkDiaer, kMarkCedilla } Mark;

// The plain capital and the mark of a Latin-1 letter (lowercase taken as capitals).
static Mark MarkOf(unsigned code, unsigned *base) {
  static const struct { uint8_t code; char base; uint8_t mark; } kMarks[] = {
    { 0xC1, 'A', kMarkAcute }, { 0xC0, 'A', kMarkGrave }, { 0xC2, 'A', kMarkCirc }, { 0xC3, 'A', kMarkTilde },
    { 0xC4, 'A', kMarkDiaer }, { 0xC9, 'E', kMarkAcute }, { 0xC8, 'E', kMarkGrave }, { 0xCA, 'E', kMarkCirc },
    { 0xCB, 'E', kMarkDiaer }, { 0xCD, 'I', kMarkAcute }, { 0xCC, 'I', kMarkGrave }, { 0xCE, 'I', kMarkCirc },
    { 0xCF, 'I', kMarkDiaer }, { 0xD3, 'O', kMarkAcute }, { 0xD2, 'O', kMarkGrave }, { 0xD4, 'O', kMarkCirc },
    { 0xD5, 'O', kMarkTilde }, { 0xD6, 'O', kMarkDiaer }, { 0xDA, 'U', kMarkAcute }, { 0xD9, 'U', kMarkGrave },
    { 0xDB, 'U', kMarkCirc }, { 0xDC, 'U', kMarkDiaer }, { 0xD1, 'N', kMarkTilde }, { 0xC7, 'C', kMarkCedilla },
  };
  if (code >= 'a' && code <= 'z') code -= 32;
  if (code >= 0xE0 && code <= 0xFE && code != 0xF7) code -= 0x20;
  for (size_t i = 0; i < sizeof(kMarks) / sizeof(kMarks[0]); i++)
    if (kMarks[i].code == code) {
      *base = (unsigned char)kMarks[i].base;
      return (Mark)kMarks[i].mark;
    }
  *base = code;
  return kMarkNone;
}

static const FontChar *FontFind(const Font *f, unsigned code) {
  for (int i = 0; i < f->char_count; i++)
    if (f->chars[i].code == code) return &f->chars[i];
  return NULL;
}

static const CustomGlyph *CustomFind(const Font *f, unsigned code) {
  for (int i = 0; i < f->custom_count; i++)
    if (f->custom[i].code == code) return &f->custom[i];
  return NULL;
}

// The pixels of a plain letter (h tiles), from the game's font or ours. False: none.
static bool LetterTiles(const Font *f, uint16_t base, unsigned code, Tile out[2]) {
  const CustomGlyph *g = CustomFind(f, code);
  if (g) {
    for (int k = 0; k < f->h; k++) {
      if (!g->rows[k * 8]) ReadTile(base, g->top_from, f->bpp, &out[k]);
      else TileFromRows(&out[k], &g->rows[k * 8]);
    }
    return true;
  }
  const FontChar *c = FontFind(f, code);
  if (!c) return false;
  ReadTile(base, c->top, f->bpp, &out[0]);
  if (f->h == 2) ReadTile(base, c->bottom, f->bpp, &out[1]);
  return true;
}

// Two rows of mark, 8 pixels each ('#' = pen).
static const char *const kMark2[][2] = {
  [kMarkAcute] = { "....##..", "...##..." },
  [kMarkGrave] = { "..##....", "...##..." },
  [kMarkCirc] = { "...##...", "..#..#.." },
  [kMarkTilde] = { "..##..#.", ".#..##.." },
  [kMarkDiaer] = { "........", ".##..##." },
  [kMarkCedilla] = { "...##...", "..##...." },
};
// One row, for the 8 px fonts: the letter is squeezed into the 7 rows under it.
static const char *const kMark1[] = {
  [kMarkAcute] = "....##..", [kMarkGrave] = "..##....", [kMarkCirc] = "..####..",
  [kMarkTilde] = ".##.##..", [kMarkDiaer] = ".#...#..", [kMarkCedilla] = "...##...",
};

static void PaintRow(Tile *t, int y, const char *row, uint8_t pen, uint8_t shade) {
  for (int x = 0; x < 8; x++)
    if (row[x] == '#') {
      t->px[y][x] = pen;
      if (shade && x + 1 < 8 && row[x + 1] != '#' && !t->px[y][x + 1]) t->px[y][x + 1] = shade;
    }
}

static void AddMark(const Font *f, Tile t[2], Mark m) {
  if (m == kMarkNone) return;
  if (f->h == 2) {
    if (m == kMarkCedilla) {
      for (int x = 0; x < 8; x++) t[1].px[6][x] = t[1].px[7][x] = 0;
      PaintRow(&t[1], 6, kMark2[m][0], f->pen, 0);
      PaintRow(&t[1], 7, kMark2[m][1], f->pen, 0);
    } else {
      for (int y = 0; y < 2; y++)
        for (int x = 0; x < 8; x++) t[0].px[y][x] = 0;
      PaintRow(&t[0], 0, kMark2[m][0], f->pen, f->shade);
      PaintRow(&t[0], 1, kMark2[m][1], f->pen, f->shade);
    }
    return;
  }
  if (m == kMarkCedilla) {
    for (int x = 0; x < 8; x++) t[0].px[7][x] = 0;
    PaintRow(&t[0], 7, kMark1[m], f->pen, 0);
    return;
  }
  // The letter loses one row (rows 1-7 then), the mark goes on row 0: a row repeated by its
  // neighbour, nearest the middle (E keeps its middle bar), else the middle one.
  static const int kOrder[6] = { 4, 3, 5, 2, 6, 1 };
  int drop = 4;
  for (int i = 0; i < 6; i++) {
    const int y = kOrder[i];
    if (!memcmp(t[0].px[y], t[0].px[y - 1], 8) || !memcmp(t[0].px[y], t[0].px[y + 1], 8)) { drop = y; break; }
  }
  Tile s;
  memset(&s, 0, sizeof(s));
  for (int y = 0, o = 1; y < 8; y++)
    if (y != drop) memcpy(s.px[o++], t[0].px[y], 8);
  PaintRow(&s, 0, kMark1[m], f->pen, 0);
  t[0] = s;
}

// ---- Chars handed out this frame ------------------------------------------------------------

enum { kMaxNewTiles = 160 };
typedef struct {
  uint16_t base;
  int bpp;
  bool used[0x400];         // chars the screen refers to (they are never borrowed)
  int next;
  Tile tiles[kMaxNewTiles];
  int chr[kMaxNewTiles];
  int count;
} CharPool;

static void MarkMapChars(bool used[0x400], uint16_t map, int pages) {
  for (int i = 0; i < 0x400 * pages; i++) used[g_shadow[(map + i) & 0x7fff] & 0x3ff] = true;
}

static void PoolInit(CharPool *p, uint16_t base, int bpp) {
  memset(p, 0, sizeof(*p));
  p->base = base;
  p->bpp = bpp;
  const Ppu *ppu = ThePpu();
  for (int k = 0; k < 4; k++) {
    const BgLayer *l = &ppu->bgLayer[k];
    if (l->tileAdr != base) continue;
    MarkMapChars(p->used, l->tilemapAdr, 1 + l->tilemapWider + l->tilemapHigher + (l->tilemapWider && l->tilemapHigher));
  }
  // Never a char whose bytes are a tilemap or the sprites' chars.
  const int words = 4 * bpp;
  for (int c = 0; c < 0x400; c++) {
    const unsigned a0 = (unsigned)base + (unsigned)c * words, a1 = a0 + words;
    bool clash = a1 > 0x8000;
    for (int k = 0; k < 4 && !clash; k++) {
      const BgLayer *l = &ppu->bgLayer[k];
      const unsigned m0 = l->tilemapAdr, m1 = m0 + 0x400u * (1 + l->tilemapWider + l->tilemapHigher +
                                                             (l->tilemapWider && l->tilemapHigher));
      clash = a0 < m1 && a1 > m0;
    }
    const unsigned o[2] = { ppu->objTileAdr1, ppu->objTileAdr2 };
    for (int k = 0; k < 2 && !clash; k++) clash = a0 < o[k] + 0x1000u && a1 > o[k];
    if (clash) p->used[c] = true;
  }
}

// The font's own letters are never borrowed: a translation may need one the English lacks.
static void PoolReserve(CharPool *p, const Font *f) {
  for (int i = 0; i < f->char_count; i++) {
    p->used[f->chars[i].top & 0x3ff] = true;
    if (f->h == 2) p->used[f->chars[i].bottom & 0x3ff] = true;
  }
}

// A char showing `t` (one already handed out if the same), or -1 when the pool is used up.
static int PoolChar(CharPool *p, int lo, int hi, const Tile *t) {
  for (int i = 0; i < p->count; i++)
    if (!memcmp(&p->tiles[i], t, sizeof(*t))) return p->chr[i];
  if (p->count >= kMaxNewTiles) return -1;
  if (p->next < lo) p->next = lo;
  while (p->next <= hi && p->used[p->next]) p->next++;
  if (p->next > hi) return -1;
  const int c = p->next++;
  p->tiles[p->count] = *t;
  p->chr[p->count++] = c;
  WriteTile(p->base, c, p->bpp, t);
  return c;
}

// ---- Phrases ------------------------------------------------------------------------------

enum {
  kCentre = 1,        // centred on the English one when on the top rows (a screen's title)
  kClearRight = 2,    // the English row's cells after it are cleared too (a picture of the
                      // Japanese words after JAPANESE TEXT)
  kMiddle = 4,        // centred on the English one wherever it is
};

typedef struct {
  const char *en;                   // capitals, single spaces; '#' is any letter, put back
                                    // where the translation has '#' ("SAMUS #")
  uint8_t flags;
} Phrase;

static unsigned NextCode(const char **s) {
  const unsigned char *p = (const unsigned char *)*s;
  if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
    *s += 2;
    return (p[0] & 0x1F) << 6 | (p[1] & 0x3F);
  }
  if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {   // 日本語
    *s += 3;
    return (p[0] & 0x0F) << 12 | (p[1] & 0x3F) << 6 | (p[2] & 0x3F);
  }
  *s += 1;
  return p[0];
}

static int Utf8Len(const char *s) {
  int n = 0;
  while (*s) NextCode(&s), n++;
  return n;
}

typedef struct {
  const Font *font;
  uint16_t map, base;       // the 32x32 page and the chars
  CharPool *pool;
  int rows;                 // 0: 32; 64 for a tilemap two pages high
} TextLayer;

static int LayerRows(const TextLayer *L) { return L->rows ? L->rows : 32; }

static uint16_t Cell(const TextLayer *L, int row, int col) { return g_shadow[(L->map + row * 32 + col) & 0x7fff]; }

static bool BlankCell(const TextLayer *L, int row, int col) {
  for (int k = 0; k < L->font->h; k++)
    if (row + k >= LayerRows(L) || (Cell(L, row + k, col) & 0x3ff) != L->font->fill) return false;
  return true;
}

// The letter shown at (row, col), 0 for a blank cell, 1 for anything else.
static unsigned DecodeCell(const TextLayer *L, int row, int col) {
  if (BlankCell(L, row, col)) return ' ';
  const Font *f = L->font;
  const uint16_t t = Cell(L, row, col);
  if (t & 0xC000) return 1;
  const uint16_t b = f->h == 2 && row < LayerRows(L) - 1 ? Cell(L, row + 1, col) : 0;
  for (int i = 0; i < f->char_count; i++)
    if (f->chars[i].top == (t & 0x3ff) && (f->h == 1 || (f->chars[i].bottom == (b & 0x3ff) && !(b & 0xC000))))
      return f->chars[i].code;
  return 1;
}

// Writes letter `code` at (row, col) with attribute `attr`. False if the font has no way.
static bool PutLetter(const TextLayer *L, int row, int col, unsigned code, uint16_t attr) {
  const Font *f = L->font;
  if (code == ' ') {
    for (int k = 0; k < f->h; k++) Want((uint16_t)(L->map + (row + k) * 32 + col), (uint16_t)(attr | f->fill));
    return true;
  }
  // ¿ and ¡: ? and ! turned round.
  if (code == 0xBF || code == 0xA1) {
    const FontChar *c = FontFind(f, code == 0xBF ? '?' : '!');
    if (!c) return false;
    if (f->h == 2) {
      Want((uint16_t)(L->map + row * 32 + col), (uint16_t)(attr | 0xC000 | c->bottom));
      Want((uint16_t)(L->map + (row + 1) * 32 + col), (uint16_t)(attr | 0xC000 | c->top));
    } else {
      Want((uint16_t)(L->map + row * 32 + col), (uint16_t)(attr | 0xC000 | c->top));
    }
    return true;
  }
  unsigned base;
  const Mark m = MarkOf(code, &base);
  const FontChar *own = m == kMarkNone && !CustomFind(f, base) ? FontFind(f, base) : NULL;
  if (own) {
    Want((uint16_t)(L->map + row * 32 + col), (uint16_t)(attr | own->top));
    if (f->h == 2) Want((uint16_t)(L->map + (row + 1) * 32 + col), (uint16_t)(attr | own->bottom));
    return true;
  }
  Tile t[2];
  if (!LetterTiles(f, L->base, base, t)) return false;
  AddMark(f, t, m);
  int chr[2] = { -1, -1 };
  for (int k = 0; k < f->h; k++) chr[k] = PoolChar(L->pool, f->pool_lo, f->pool_hi, &t[k]);
  if (chr[0] < 0 || (f->h == 2 && chr[1] < 0)) {
    // No char free: the plain letter, without its mark.
    return m != kMarkNone && FontFind(f, base) && !CustomFind(f, base) && PutLetter(L, row, col, base, attr);
  }
  for (int k = 0; k < f->h; k++) Want((uint16_t)(L->map + (row + k) * 32 + col), (uint16_t)(attr | chr[k]));
  return true;
}

// `wild`: the letters the English one's '#' matched, put where the translation has '#'.
static void PutPhrase(const TextLayer *L, int row, int col, int en_len, const Phrase *p, const char *tr,
                      const unsigned *wild) {
  const Font *f = L->font;
  const uint16_t attr = Cell(L, row, col) & 0x3c00;
  // Clear the English letters (and the rest of the row for kClearRight).
  int end = col + en_len;
  if (p->flags & kClearRight)
    for (int x = end; x < 32; x++)
      if (!BlankCell(L, row, x)) end = x + 1;
  for (int x = col; x < end; x++)
    for (int k = 0; k < f->h; k++)
      Want((uint16_t)(L->map + (row + k) * 32 + x), (uint16_t)((Cell(L, row + k, x) & 0xfc00) | f->fill));
  const int n = Utf8Len(tr);
  // A title (the top rows) stays centred; the same words in a list keep their column.
  const bool centred = ((p->flags & kCentre) && row <= 2) || (p->flags & kMiddle);
  int x = centred ? col + (en_len - n) / 2 : col;
  while (x < col && (x < 0 || !BlankCell(L, row, x))) x++;   // never over something else
  for (const char *s = tr; *s && x < 32; x++) {
    unsigned code = NextCode(&s);
    if (code == '#') code = *wild ? *wild++ : ' ';
    if ((x < col || x >= end) && !BlankCell(L, row, x)) break;
    if (!PutLetter(L, row, x, code, attr)) PutLetter(L, row, x, ' ', attr);
  }
}

static bool IsLetter(unsigned c) { return c > ' '; }
// A phrase is found only between non-letters ("END" is not in "LEGEND"; "(SHOT" has SHOT).
static bool IsAlpha(unsigned c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c >= 0xC0; }

static void TranslateLayer(const TextLayer *L, const char *section, const Phrase *phrases, int count) {
  if (!UiLang_Translated()) return;
  if (count > 64) count = 64;
  const char *trs[64];   // looked up once, not per row
  for (int i = 0; i < count; i++) trs[i] = UiLang_Get(section, phrases[i].en);
  const int rows = LayerRows(L) - (L->font->h - 1);
  for (int r = 0; r < rows; r++) {
    unsigned line[32];
    bool any = false;
    for (int x = 0; x < 32; x++) any |= IsLetter(line[x] = DecodeCell(L, r, x));
    if (!any) continue;
    bool done[64] = { false };
    for (int pass = 0; pass < count; pass++) {
      int i = -1;   // the longest phrase not tried yet: "SHOT HOLD" before "SHOT"
      for (int j = 0; j < count; j++)
        if (!done[j] && (i < 0 || strlen(phrases[j].en) > strlen(phrases[i].en))) i = j;
      done[i] = true;
      const Phrase *p = &phrases[i];
      const char *tr = trs[i];
      if (!tr) continue;
      const int n = (int)strlen(p->en);
      for (int x = 0; x + n <= 32; x++) {
        if (x > 0 && IsAlpha(line[x - 1])) continue;
        if (x + n < 32 && IsAlpha(line[x + n])) continue;
        unsigned wild[8];
        int k = 0, w = 0;
        for (; k < n; k++) {
          if (p->en[k] == '#' && IsAlpha(line[x + k]) && w < 7) wild[w++] = line[x + k];
          else if (line[x + k] != (unsigned char)p->en[k]) break;
        }
        if (k < n) continue;
        wild[w] = 0;
        PutPhrase(L, r, x, n, p, tr, wild);
        for (k = 0; k < n; k++) line[x + k] = 1;   // not matched again by a shorter phrase
      }
    }
  }
}

// ---- Labels drawn as pictures ------------------------------------------------------------------
// Some words are a picture over a few chars (ENERGY and TIME on the file-select screen), in a
// small font of their own. They are redrawn into those same chars with a 6 px tall font like
// theirs: letters on rows 1-6, a shadow under each pixel, an accent on row 0.

typedef struct {
  uint16_t code;
  const char *rows[6];
} TinyGlyph;

static const TinyGlyph kTiny[] = {
  { 'A', { "###", "#.#", "###", "#.#", "#.#", "#.#" } }, { 'B', { "##.", "#.#", "##.", "#.#", "#.#", "##." } },
  { 'C', { "###", "#..", "#..", "#..", "#..", "###" } }, { 'D', { "##.", "#.#", "#.#", "#.#", "#.#", "##." } },
  { 'E', { "###", "#..", "###", "#..", "#..", "###" } }, { 'F', { "###", "#..", "###", "#..", "#..", "#.." } },
  { 'G', { "###", "#..", "#..", "#.#", "#.#", "###" } }, { 'H', { "#.#", "#.#", "###", "#.#", "#.#", "#.#" } },
  { 'I', { "#", "#", "#", "#", "#", "#" } },               { 'J', { "..#", "..#", "..#", "..#", "#.#", "###" } },
  { 'K', { "#.#", "#.#", "##.", "#.#", "#.#", "#.#" } }, { 'L', { "#..", "#..", "#..", "#..", "#..", "###" } },
  { 'M', { "#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#" } },
  { 'N', { "#..#", "##.#", "#.##", "#.##", "#..#", "#..#" } },
  { 'O', { "###", "#.#", "#.#", "#.#", "#.#", "###" } }, { 'P', { "###", "#.#", "###", "#..", "#..", "#.." } },
  { 'Q', { "###", "#.#", "#.#", "#.#", "###", "..#" } }, { 'R', { "###", "#.#", "#.#", "##.", "#.#", "#.#" } },
  { 'S', { "###", "#..", "###", "..#", "..#", "###" } }, { 'T', { "###", ".#.", ".#.", ".#.", ".#.", ".#." } },
  { 'U', { "#.#", "#.#", "#.#", "#.#", "#.#", "###" } }, { 'V', { "#.#", "#.#", "#.#", "#.#", "#.#", ".#." } },
  { 'W', { "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#" } },
  { 'X', { "#.#", "#.#", ".#.", "#.#", "#.#", "#.#" } }, { 'Y', { "#.#", "#.#", "#.#", ".#.", ".#.", ".#." } },
  { 'Z', { "###", "..#", ".#.", "#..", "#..", "###" } }, { '.', { ".", ".", ".", ".", ".", "#" } },
};

static const TinyGlyph *TinyFind(unsigned code) {
  for (size_t i = 0; i < sizeof(kTiny) / sizeof(kTiny[0]); i++)
    if (kTiny[i].code == code) return &kTiny[i];
  return NULL;
}

// Draws `text` centred over `n` chars (n * 8 px) with `pen`, its shadow `shade`, on 0.
static void PutLabel(uint16_t base, int bpp, const uint16_t *chars, int n, const char *text, uint8_t pen,
                     uint8_t shade) {
  enum { kMaxW = 64 };
  uint8_t px[8][kMaxW];
  memset(px, 0, sizeof(px));
  int w = 0;
  for (const char *s = text; *s;) {
    unsigned base_code;
    const Mark m = MarkOf(NextCode(&s), &base_code);
    const TinyGlyph *g = base_code == ' ' ? NULL : TinyFind(base_code);
    const int gw = g ? (int)strlen(g->rows[0]) : 2;
    if (w + gw > kMaxW) break;
    if (!g) { w += 2; continue; }
    for (int y = 0; y < 6; y++)
      for (int x = 0; x < gw; x++)
        if (g->rows[y][x] == '#') px[1 + y][w + x] = pen;
    if (m != kMarkNone && m != kMarkCedilla) {
      const int c = gw / 2;
      if (m == kMarkAcute) px[0][w + gw - 1] = pen;
      else if (m == kMarkGrave) px[0][w] = pen;
      else if (m == kMarkCirc) px[0][w + c] = pen;
      else if (m == kMarkDiaer) px[0][w] = px[0][w + gw - 1] = pen;
      else for (int x = 0; x < gw; x++) px[0][w + x] = pen;   // tilde: a bar
    } else if (m == kMarkCedilla) {
      px[7][w + gw / 2] = pen;
    }
    w += gw + 1;
  }
  if (w > 0) w--;
  for (int y = 7; y > 0; y--)
    for (int x = 0; x < kMaxW; x++)
      if (!px[y][x] && px[y - 1][x] == pen) px[y][x] = shade;
  const int x0 = (n * 8 - w) / 2 > 0 ? (n * 8 - w) / 2 : 0;
  for (int i = 0; i < n; i++) {
    Tile t;
    memset(&t, 0, sizeof(t));
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        const int sx = i * 8 + x - x0;
        if (sx >= 0 && sx < kMaxW) t.px[y][x] = px[y][sx];
      }
    WriteTile(base, chars[i], bpp, &t);
  }
}

typedef struct {
  const char *en;           // the key of its translation (the English of the picture)
  uint16_t chars[4];
  int n;
} Label;

// ---- Words as pictures, redrawn into chars of their own -----------------------------------------
// The pause screen's names are pictures over a run of chars, some shared between names (the SUIT
// of VARIA SUIT and GRAVITY SUIT). The run is found by its chars on the tilemap, the translation
// drawn with the tiny font into chars nothing on screen uses, and the run's cells (plus the blank
// ones after it) pointed at them.

typedef struct {
  const char *en;           // the key of its translation (the English of the picture)
  uint16_t chars[10];       // the English run
  int n;
} WordRun;

typedef struct {
  uint16_t map;             // the tilemap (both pages when `pages` is 2)
  int pages;
  uint16_t base;
  int bpp;
  uint16_t filler;          // a blank cell after a run that may take letters too (-1: none)
  int rows;                 // 5 or 6: the font's height
  int y0;                   // its first row in the cell
  int x0;                   // its first column (left-aligned), -1: centred
  int lo, hi;               // the chars that may be used
  CharPool *pool;
} WordStyle;

// The pixels of `text` in the tiny font, `rows` tall (5: the 6-row letters without their 5th row).
static int TinyText(const char *text, int rows, uint8_t px[8][128], int y0, uint8_t pen) {
  int w = 0;
  for (const char *s = text; *s;) {
    unsigned base_code;
    const Mark m = MarkOf(NextCode(&s), &base_code);
    const TinyGlyph *g = base_code == ' ' ? NULL : TinyFind(base_code);
    const int gw = g ? (int)strlen(g->rows[0]) : 2;
    if (w + gw > 128) break;   // the width returned stays within px
    if (!g) { w += 2; continue; }
    for (int y = 0, oy = 0; y < 6; y++) {
      if (rows == 5 && y == 4) continue;
      for (int x = 0; x < gw; x++)
        if (g->rows[y][x] == '#' && y0 + oy < 8) px[y0 + oy][w + x] = pen;
      oy++;
    }
    if (m != kMarkNone && y0 > 0) {
      const int ym = m == kMarkCedilla ? (y0 + rows < 8 ? y0 + rows : 7) : y0 - 1;
      if (m == kMarkAcute) px[ym][w + gw - 1] = pen;
      else if (m == kMarkGrave) px[ym][w] = pen;
      else if (m == kMarkCirc || m == kMarkCedilla) px[ym][w + gw / 2] = pen;
      else if (m == kMarkDiaer) px[ym][w] = px[ym][w + gw - 1] = pen;
      else for (int x = 0; x < gw; x++) px[ym][w + x] = pen;
    }
    w += gw + 1;
  }
  return w > 0 ? w - 1 : 0;
}

static void TranslateWordRuns(const WordStyle *st, const char *section, const WordRun *runs, int count, uint8_t pen) {
  if (!UiLang_Translated()) return;
  const int cells_total = 0x400 * st->pages;
  for (int i = 0; i < count; i++) {
    const WordRun *wr = &runs[i];
    const char *tr = UiLang_Get(section, wr->en);
    if (!tr) continue;
    Tile first;
    ReadTile(st->base, wr->chars[0], st->bpp, &first);
    const uint8_t bg = first.px[0][0];
    for (int c = 0; c + wr->n <= cells_total; c++) {
      int k = 0;
      while (k < wr->n && (g_shadow[(st->map + c + k) & 0x7fff] & 0x3ff) == wr->chars[k]) k++;
      if (k < wr->n) continue;
      // The run, and the blank cells after it on the same row.
      int n = wr->n;
      while ((c % 32) + n < 32 && (g_shadow[(st->map + c + n) & 0x7fff] & 0x3ff) == st->filler) n++;
      const int cells = n < 16 ? n : 16;   // what px holds (128 px)
      uint8_t px[8][128];
      memset(px, bg, sizeof(px));
      uint8_t ink[8][128];
      memset(ink, 0, sizeof(ink));
      const int w = TinyText(tr, st->rows, ink, st->y0, 1);
      const int x0 = st->x0 >= 0 ? st->x0 : (cells * 8 - w) / 2 > 0 ? (cells * 8 - w) / 2 : 0;
      for (int y = 0; y < 8; y++)
        for (int x = 0; x < 128 && x0 + x < cells * 8; x++)
          if (ink[y][x]) px[y][x0 + x] = pen;
      for (int j = 0; j < cells; j++) {
        Tile t;
        for (int y = 0; y < 8; y++) memcpy(t.px[y], &px[y][j * 8], 8);
        const int ch = PoolChar(st->pool, st->lo, st->hi, &t);
        if (ch < 0) return;
        const uint16_t adr = (uint16_t)(st->map + c + j);
        Want(adr, (uint16_t)((g_shadow[adr & 0x7fff] & 0xfc00) | ch));
      }
      c += n - 1;
    }
  }
}

// The labels found on the layer's page (their chars side by side) are redrawn.
static void TranslateLabels(uint16_t map, uint16_t base, int bpp, const char *section, const Label *labels, int count, uint8_t pen,
                            uint8_t shade) {
  if (!UiLang_Translated()) return;
  for (int i = 0; i < count; i++) {
    const Label *l = &labels[i];
    const char *tr = UiLang_Get(section, l->en);
    if (!tr) continue;
    bool found = false;
    for (int c = 0; c + l->n <= 1024 && !found; c++) {
      int k = 0;
      while (k < l->n && (g_shadow[(map + c + k) & 0x7fff] & 0x3ff) == l->chars[k]) k++;
      found = k == l->n;
    }
    if (found) PutLabel(base, bpp, l->chars, l->n, tr, pen, shade);
  }
}

// ---- The menus' fonts (BG1, 4 bpp, chars at 0) -----------------------------------------------
// 16 px capitals: a top and a bottom char, halves shared between letters (P, R and D have
// one top; B, V, Y and Z are made that way too). Q has O's top but no bottom with a tail: that
// half is drawn. Accents go in the two blank rows on top of the letter.

static const FontChar kMenuBigChars[] = {
  { 'A', 0x0a, 0x1a }, { 'B', 0x0b, 0x1b }, { 'C', 0x0c, 0x1c }, { 'D', 0x0d, 0x1d }, { 'E', 0x0e, 0x1e },
  { 'F', 0x0e, 0x1f }, { 'G', 0x0c, 0x30 }, { 'H', 0x21, 0x31 }, { 'I', 0x22, 0x11 }, { 'J', 0x23, 0x33 },
  { 'K', 0x24, 0x34 }, { 'L', 0x25, 0x35 }, { 'M', 0x26, 0x36 }, { 'N', 0x27, 0x37 }, { 'O', 0x00, 0x10 },
  { 'P', 0x0d, 0x38 }, { 'R', 0x0d, 0x3a }, { 'S', 0x2b, 0x3b }, { 'T', 0x2c, 0x11 }, { 'U', 0x2d, 0x10 },
  { 'V', 0x2d, 0x3e }, { 'W', 0x2f, 0x3f }, { 'X', 0x40, 0x50 }, { 'Y', 0x41, 0x17 }, { 'Z', 0x42, 0x52 },
  { '?', 0x44, 0x54 }, { '+', 0x45, 0x55 }, { '-', 0x46, 0x56 }, { '(', 0x47, 0x57 }, { ')', 0x48, 0x58 },
  { '1', 0x01, 0x11 }, { '2', 0x02, 0x12 }, { '3', 0x03, 0x13 }, { '4', 0x04, 0x14 }, { '5', 0x05, 0x15 },
  { '6', 0x06, 0x16 }, { '7', 0x07, 0x17 }, { '8', 0x08, 0x18 }, { '9', 0x09, 0x19 },
  // 日本語, from OPTION MODE's "(日本語字幕スーパー)" (the language list, P4.9).
  { 0x65E5, 0x49, 0x59 }, { 0x672C, 0x4a, 0x5a }, { 0x8A9E, 0x4b, 0x5b },
};

static const CustomGlyph kMenuBigCustom[] = {
  // Q: the top of O, a bottom with a tail.
  { 'Q', { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
           ".eed.eed", ".eed.eed", ".eed.eed", ".eedeeed", ".eed.eee", "..eeeeee", "...dddee", "......ed" }, 0x00 },
  // /: the game's font has none (CONTROLLER SETTING MODE's modern rows).
  { '/', { "........", ".....eed", ".....eed", "....eed.", "....eed.", "...eed..", "...eed..", "..eed...",
           "..eed...", ".eed....", ".eed....", "eed.....", "eed.....", "........", "........", "........" }, 0x00 },
};

static const Font kMenuBig = {
  .h = 2, .bpp = 4, .fill = 0x0f,
  .chars = kMenuBigChars, .char_count = sizeof(kMenuBigChars) / sizeof(kMenuBigChars[0]),
  .custom = kMenuBigCustom, .custom_count = sizeof(kMenuBigCustom) / sizeof(kMenuBigCustom[0]),
  .pen = 0xe, .shade = 0xd, .pool_lo = 0x0f0, .pool_hi = 0x2af,
};

// 8 px capitals: 0-9 at 0x60, A-Z at 0x6a, then ! ? + - . , ( ) :
static FontChar g_menu_small_chars[10 + 26 + 9];
static const Font *MenuSmall(void) {
  static Font f;
  if (!f.h) {
    int n = 0;
    for (int i = 0; i < 10; i++) g_menu_small_chars[n++] = (FontChar){ (uint16_t)('0' + i), (uint16_t)(0x60 + i), 0 };
    for (int i = 0; i < 26; i++) g_menu_small_chars[n++] = (FontChar){ (uint16_t)('A' + i), (uint16_t)(0x6a + i), 0 };
    static const char kPunct[] = "!?+-.,():";
    for (int i = 0; i < 9; i++) g_menu_small_chars[n++] = (FontChar){ (uint16_t)kPunct[i], (uint16_t)(0x84 + i), 0 };
    f = (Font){ .h = 1, .bpp = 4, .fill = 0x0f, .chars = g_menu_small_chars, .char_count = n,
                .pen = 0xe, .shade = 0xd, .pool_lo = 0x0f0, .pool_hi = 0x2af };
  }
  return &f;
}

// ---- Option menus (game state 2) ------------------------------------------------------------

static const Phrase kOptionsBig[] = {
  { "OPTION MODE", kCentre },
  { "START GAME" },
  { "CONTROLLER SETTING MODE", kCentre },
  { "SPECIAL SETTING MODE", kCentre },
  // CONTROLLER SETTING MODE
  { "SHOT" },
  { "JUMP" },
  { "DASH" },
  { "ITEM SELECT" },
  { "ITEM CANCEL" },
  { "ANGLE UP" },
  { "ANGLE DOWN" },
  { "RESET TO DEFAULT" },
  { "END" },
  // SPECIAL SETTING MODE
  { "ICON CANCEL" },
  { "AUTO" },
  { "MANUAL" },
  { "MOON WALK" },
  { "ON" },
  { "OFF" },
  { "SHOT HOLD" },
};


static const Phrase kOptionsSmall[] = {
  { "SELECT" },
  { "CANCEL" },
  { "CURSOR" },
  { "OR" },
  { "MODE" },
  { "CHANGE" },
};

// ---- One language for the game and the port (docs/PLAN.md P4.9, sm_rtl.h) ----------------------
// With the port's LANGUAGE entry, in every language (English too: the game's rows are gone):
// on OPTION MODE, ENGLISH TEXT becomes LANGUAGE and the JAPANESE TEXT row the language chosen;
// the list screen (OPTION MODE's tilemap with blank rows 4-24, sm_82.c) gets LANGUAGE as its
// title and the languages, the chosen one bright and the others dim as the game's own rows were.

enum { kPalBright = 0x0000, kPalDim = 0x0400, kPalMask = 0x1c00 };

// The title of the list and of OPTION MODE's entry: [options] LANGUAGE in the language files.
static const char *LanguageTitle(void) {
  const char *t = UiLang_Get("options", "LANGUAGE");
  return t ? t : "LANGUAGE";
}

// A language in itself; Japanese with the game's own letters.
static const char *LanguageName(int lang) { return UiLang_IsJapanese(lang) ? "日本語" : UiLang_Name(lang); }

static void ClearCells(const TextLayer *L, int row, int col0, int col1) {
  for (int x = col0; x < col1; x++)
    for (int k = 0; k < L->font->h; k++)
      Want((uint16_t)(L->map + (row + k) * 32 + x), (uint16_t)((Cell(L, row + k, x) & 0xfc00) | L->font->fill));
}

// Writes `text` from (row, col) on, in palette `pal` (kPalBright / kPalDim).
static void PutText(const TextLayer *L, int row, int col, const char *text, uint16_t pal) {
  const uint16_t attr = (uint16_t)((Cell(L, row, col) & 0x3c00 & ~kPalMask) | pal);
  for (const char *s = text; *s && col < 32; col++) {
    const unsigned code = NextCode(&s);
    if (!PutLetter(L, row, col, code, attr)) PutLetter(L, row, col, ' ', attr);
  }
}

// True if (row, col) on shows the English `en` (capitals and spaces).
static bool ShowsText(const TextLayer *L, int row, int col, const char *en) {
  for (; *en; en++, col++)
    if (col >= 32 || DecodeCell(L, row, col) != (unsigned char)*en) return false;
  return true;
}

static void LanguageScreens(const TextLayer *big) {
  const RtlLanguageMenu *m = g_rtl_language_menu;
  // OPTION MODE: the rows of ENGLISH TEXT (10) and JAPANESE TEXT (13), at column 4.
  if (ShowsText(big, 10, 4, "ENGLISH TEXT")) {
    ClearCells(big, 10, 4, 32);
    ClearCells(big, 13, 4, 32);
    PutText(big, 10, 4, LanguageTitle(), kPalBright);
    PutText(big, 13, 6, LanguageName(m->current()), kPalBright);
    return;
  }
  // The list: the title still OPTION MODE, nothing under it until the footer.
  if (!ShowsText(big, 1, 10, "OPTION MODE")) return;
  for (int r = 4; r < 25; r++)
    for (int x = 0; x < 32; x++)
      if ((Cell(big, r, x) & 0x3ff) != big->font->fill) return;
  static const Phrase kTitle = { "OPTION MODE", kCentre };
  PutPhrase(big, 1, 10, 11, &kTitle, LanguageTitle(), (const unsigned[]){ 0 });
  int n = m->count();
  if (n > kRtlLanguageMaxShown) n = kRtlLanguageMaxShown;
  const int cur = m->current();
  for (int i = 0; i < n; i++)
    PutText(big, RtlLanguageMenuRow(i, n), 4, LanguageName(i), i == cur ? kPalBright : kPalDim);
}

// CONTROLLER SETTING MODE with the control scheme (sm_rtl.h, RtlControlsMenu), drawn over the game's
// own list every frame: a fixed row with the game's left-right D-pad (as SPECIAL SETTING MODE shows
// "change") and CLASSIC / MODERN, the chosen one bright; under it the rows g_rtl_ctl_top on: the
// original's button rows with the icon of the button each is set to, END and RESET TO DEFAULT, or the
// modern scheme's buttons, dim (they cannot be changed), and END.
static const char *OptionsText(const char *en) {
  const char *t = UiLang_Get("options", en);
  return t ? t : en;
}

enum { kCtlLabelCol = 6, kCtlIconCol = 23 };
enum { kBtnX, kBtnA, kBtnB, kBtnSelect, kBtnY, kBtnL, kBtnR, kBtnLeftRight = 10, kBtnUpDown };

// A 3x2 button icon (RtlCtlButtonIcon), or one of the font's 2x2 D-pads: its left and right arms
// marked (SPECIAL SETTING MODE's "change"), or its up and down ones (the footer's "select"); at
// (row, col). Returns its width.
static int CtlIcon(const TextLayer *L, int row, int col, int button, bool dim) {
  if (button >= kBtnLeftRight) {
    const uint16_t first = button == kBtnUpDown ? 0xb2 : 0xb4;
    const uint16_t attr = (uint16_t)((Cell(L, 1, 16) & 0x2000) | (dim ? kPalDim : kPalBright));
    for (int k = 0; k < 4; k++)
      Want((uint16_t)(L->map + (row + k / 2) * 32 + col + k % 2), (uint16_t)(attr | (first + (k / 2) * 0x10 + k % 2)));
    return 2;
  }
  const uint16 *w = RtlCtlButtonIcon(button);
  for (int k = 0; k < 6; k++) {
    uint16_t v = w[k];
    if (dim) v = (uint16_t)((v & ~kPalMask) | kPalDim);
    Want((uint16_t)(L->map + (row + k / 3) * 32 + col + k % 3), v);
  }
  return 3;
}

static void CtlPlus(const TextLayer *L, int row, int col, bool dim) {
  const uint16_t attr = (uint16_t)((Cell(L, 1, 16) & 0x2000) | (dim ? kPalDim : kPalBright));
  Want((uint16_t)(L->map + row * 32 + col), (uint16_t)(attr | 0x45));
  Want((uint16_t)(L->map + (row + 1) * 32 + col), (uint16_t)(attr | 0x55));
}

static void ControllerList(const TextLayer *big) {
  const bool modern = g_rtl_controls_menu->modern();
  const int n = RtlCtlCount(modern);
  // The area under the title, blank.
  for (int r = 3; r < 28; r++)
    for (int x = 0; x < 32; x++)
      Want((uint16_t)(big->map + r * 32 + x), (uint16_t)((Cell(big, r, x) & 0xfc00 & ~kPalMask) | big->font->fill));
  static const char *const kClassic[7] = { "SHOT", "JUMP", "DASH", "ITEM SELECT", "ITEM CANCEL", "ANGLE UP", "ANGLE DOWN" };
  // The modern scheme: the label (an [options] key) and its buttons, a second one after a "+".
  static const struct { const char *en; int8_t b1, b2; } kModern[11] = {
    { "DASH", kBtnLeftRight, -1 }, { "JUMP", kBtnA, -1 }, { "WALK", kBtnB, -1 }, { "SHOT / BOMB", kBtnX, -1 },
    { "MORPH BALL", kBtnY, -1 }, { "AIM", kBtnL, kBtnUpDown }, { "MISSILE", kBtnR, kBtnX },
    { "POWER BOMB", kBtnR, kBtnX }, { "GRAPPLING BEAM", kBtnR, kBtnY }, { "X-RAY SCOPE", kBtnR, kBtnB },
    { "MISSILE / SUPER", kBtnSelect, -1 },
  };
  // The scheme: the left-right D-pad, then both names, the chosen one bright.
  CtlIcon(big, kRtlCtlHeadRow, kCtlLabelCol, kBtnLeftRight, false);
  PutText(big, kRtlCtlHeadRow, kCtlLabelCol + 3, OptionsText("CLASSIC"), modern ? kPalDim : kPalBright);
  PutText(big, kRtlCtlHeadRow, 20, OptionsText("MODERN"), modern ? kPalBright : kPalDim);
  for (int i = g_rtl_ctl_top; i < n && i < g_rtl_ctl_top + kRtlCtlShown; i++) {
    const int row = kRtlCtlRow0 + kRtlCtlPitch * (i - g_rtl_ctl_top);
    if (!modern && i < 7) {
      PutText(big, row, kCtlLabelCol, OptionsText(kClassic[i]), kPalBright);
      CtlIcon(big, row, kCtlIconCol, RtlCtlBinding(i), false);
    } else if (modern && i < 11) {
      const int k = i;
      PutText(big, row, kCtlLabelCol, OptionsText(kModern[k].en), kPalDim);
      int col = kCtlIconCol;
      col += CtlIcon(big, row, col, kModern[k].b1, true);
      if (kModern[k].b2 >= 0) {
        CtlPlus(big, row, col++, true);
        CtlIcon(big, row, col, kModern[k].b2, true);
      }
    } else {
      const bool reset = !modern && i == n - 1;
      PutText(big, row, kCtlLabelCol, OptionsText(reset ? "RESET TO DEFAULT" : "END"), kPalBright);
    }
  }
}

static int LanguageMenuCount(void) { return UiLang_Count(); }
static int LanguageMenuCurrent(void) { return g_ui_lang; }
static void LanguageMenuChoose(int i) {
  if (i < 0 || i >= UiLang_Count() || i == g_ui_lang) return;
  UiLang_Set(i);
  if (g_ui_lang_on_change) g_ui_lang_on_change();
}
static bool LanguageMenuIsJapanese(int i) { return UiLang_IsJapanese(i); }

static const RtlLanguageMenu kLanguageMenu = {
  LanguageMenuCount, LanguageMenuCurrent, LanguageMenuChoose, LanguageMenuIsJapanese,
};

static void OptionsScreen(void) {
  const Ppu *ppu = ThePpu();
  static CharPool pool;
  PoolInit(&pool, ppu->bgLayer[0].tileAdr, 4);
  TextLayer big = { &kMenuBig, ppu->bgLayer[0].tilemapAdr, ppu->bgLayer[0].tileAdr, &pool };
  PoolReserve(&pool, &kMenuBig);
  TextLayer small = { MenuSmall(), ppu->bgLayer[0].tilemapAdr, ppu->bgLayer[0].tileAdr, &pool };
  PoolReserve(&pool, MenuSmall());
  if (UiLang_Translated()) {
    TranslateLayer(&big, "options", kOptionsBig, sizeof(kOptionsBig) / sizeof(kOptionsBig[0]));
    TranslateLayer(&small, "options", kOptionsSmall, sizeof(kOptionsSmall) / sizeof(kOptionsSmall[0]));
  }
  if (g_rtl_language_menu) LanguageScreens(&big);
  if (g_rtl_controls_menu && g_rtl_controls_row_shown && game_options_screen_index >= 5 && game_options_screen_index <= 7)
    ControllerList(&big);   // also while it dissolves in and out
}

// ---- File select (game state 4) ----------------------------------------------------------------

static const Phrase kFileSelectBig[] = {
  { "SAMUS DATA", kCentre },
  { "DATA COPY MODE", kCentre },
  { "DATA CLEAR MODE", kCentre },
  { "YES" },
  { "NO" },
};

static const Phrase kFileSelectSmall[] = {
  { "NO DATA" },
  { "DATA COPY" },
  { "DATA CLEAR" },
  { "EXIT" },
  { "COPY WHICH DATA?", kMiddle },
  { "COPY (SAMUS #) TO WHERE?", kMiddle },
  { "COPY (SAMUS #) TO (SAMUS #).", kMiddle },
  { "IS THIS OK?", kMiddle },
  { "COPY COMPLETED.", kMiddle },
  { "CLEAR WHICH DATA?", kMiddle },
  { "CLEAR (SAMUS #).", kMiddle },
  { "DATA CLEARED...", kMiddle },
};

static const Label kFileSelectLabels[] = {
  { "ENERGY", { 0x9d, 0x9e, 0x9f, 0xcc }, 4 },
  { "TIME", { 0xad, 0xae, 0xaf }, 3 },
};

static void FileSelectScreen(void) {
  const Ppu *ppu = ThePpu();
  static CharPool pool;
  PoolInit(&pool, ppu->bgLayer[0].tileAdr, 4);
  TextLayer big = { &kMenuBig, ppu->bgLayer[0].tilemapAdr, ppu->bgLayer[0].tileAdr, &pool };
  PoolReserve(&pool, &kMenuBig);
  TextLayer small = { MenuSmall(), ppu->bgLayer[0].tilemapAdr, ppu->bgLayer[0].tileAdr, &pool };
  PoolReserve(&pool, MenuSmall());
  TranslateLayer(&big, "file select", kFileSelectBig, sizeof(kFileSelectBig) / sizeof(kFileSelectBig[0]));
  TranslateLayer(&small, "file select", kFileSelectSmall, sizeof(kFileSelectSmall) / sizeof(kFileSelectSmall[0]));
  TranslateLabels(ppu->bgLayer[0].tilemapAdr, ppu->bgLayer[0].tileAdr, 4, "file select", kFileSelectLabels,
                  sizeof(kFileSelectLabels) / sizeof(kFileSelectLabels[0]), 0xe, 0xd);
}

// ---- Pages of text (the intro) ------------------------------------------------------------------
// A page is typed letter by letter: the letters on screen are read back in order and, while they
// are the start of a known page, the same share of the translation is shown, word-wrapped from
// the page's first line on (same column, same rows apart). Each letter takes the colour of the
// English letter at the same point (the newest ones fade in).

typedef struct {
  const char *en;                   // the page's words, single spaces
  int width;                        // letters per line
} Page;

enum { kMaxPageCells = 400 };

static bool PageMatch(const char *typed, const char *en) {
  // typed: what is on screen (spaces collapsed); a non-empty start of en
  const size_t n = strlen(typed);
  return n >= 3 && strncmp(en, typed, n) == 0;
}

// Splits `tr` (its first `shown` letters) into lines of `width`: line starts in starts[].
static int WrapLines(const char *tr, int width, const char *starts[16], int lens[16]) {
  int lines = 0;
  const char *s = tr;
  while (*s && lines < 16) {
    while (*s == ' ') s++;
    if (!*s) break;
    const char *line = s, *last_break = NULL;
    int n = 0;
    const char *p = s;
    while (*p && n < width) {
      const char *q = p;
      const unsigned c = NextCode(&q);
      if (c == ' ') {
        // a break, unless what follows is punctuation that belongs to the word before ("PAIX !")
        const char *r = q;
        const unsigned next = *r ? NextCode(&r) : 0;
        if (next != '!' && next != '?' && next != ':' && next != ';') last_break = p;
      }
      p = q;
      n++;
    }
    const char *end = (*p && last_break) ? last_break : p;
    starts[lines] = line;
    int len = 0;
    for (const char *q = line; q < end;) NextCode(&q), len++;
    lens[lines++] = len;
    s = end;
  }
  return lines;
}

static bool RestIsSpaces(const char *q) {
  while (*q == ' ') q++;
  return !*q;
}

// ---- The typing cursor ----------------------------------------------------------------------------

// The game rewrites OAM every frame (before this hook runs), so a moved sprite needs no putting back.

// The cursor sprite: the game keeps it in the page's cells (after its last letter, a gap behind the
// letter it has already typed, or at the start of the next line), so any sprite over the rows of the
// text is it. Put at (to_row, to_col).
static void MoveCursorSprite(const TextLayer *L, int row_lo, int row_hi, int to_row, int to_col) {
  Ppu *ppu = ThePpu();
  if (!ppu || to_col >= 32 || to_row >= 32) return;
  const BgLayer *bg = NULL;
  for (int i = 0; i < 4; i++)
    if (ppu->bgLayer[i].tilemapAdr == L->map) bg = &ppu->bgLayer[i];
  if (!bg) return;
  const int top = row_lo * 8 - bg->vScroll - 1, bottom = row_hi * 8 - bg->vScroll - 1;
  for (int i = 0; i < 128; i++) {
    const uint16_t w = ppu->oam[i * 2];
    const int hi = ppu->highOam[i >> 2] >> ((i & 3) * 2) & 3;
    const int x = (w & 0xff) | (hi & 1) << 8, y = w >> 8;
    if (x >= 256 || y < top - 8 || y > bottom + 8) continue;
    const int nx = (to_col * 8 - bg->hScroll) & 0x1ff;
    const int ny = (to_row * 8 - bg->vScroll - 1) & 0xff;
    ppu->oam[i * 2] = (uint16_t)(ny << 8 | (nx & 0xff));
    ppu->highOam[i >> 2] = (uint8_t)((ppu->highOam[i >> 2] & ~(1 << ((i & 3) * 2))) | (nx >> 8) << ((i & 3) * 2));
    return;
  }
}

static void TranslatePages(const TextLayer *L, int row0, int row1, const char *section, const Page *pages, int count) {
  if (!UiLang_Translated()) return;
  const Font *f = L->font;
  // The English letters, in reading order.
  static int16_t cell_row[kMaxPageCells], cell_col[kMaxPageCells];
  static uint16_t cell_attr[kMaxPageCells];
  static bool cell_alpha[kMaxPageCells];
  char typed[kMaxPageCells + 32];
  int cells = 0, tn = 0, first_row = -1, first_col = 0, second_row = -1;
  for (int r = row0; r <= row1 && r < 32 - (f->h - 1); r++) {
    int x0 = -1, x1 = -1;
    for (int x = 0; x < 32; x++) {
      const unsigned c = DecodeCell(L, r, x);
      if (c > ' ' && c != 1) { if (x0 < 0) x0 = x; x1 = x; }
    }
    if (x0 < 0) continue;
    if (first_row < 0) first_row = r, first_col = x0;
    else if (second_row < 0) second_row = r;
    if (tn > 0 && tn < (int)sizeof(typed) - 1) typed[tn++] = ' ';
    for (int x = x0; x <= x1; x++) {
      const unsigned c = DecodeCell(L, r, x);
      if (c == 1) continue;
      if (c == ' ') {
        if (tn > 0 && typed[tn - 1] != ' ' && tn < (int)sizeof(typed) - 1) typed[tn++] = ' ';
        continue;
      }
      if (tn < (int)sizeof(typed) - 1) typed[tn++] = (char)c;
      if (cells < kMaxPageCells) {
        cell_row[cells] = (int16_t)r, cell_col[cells] = (int16_t)x;
        cell_alpha[cells] = IsAlpha(c);
        // A tall letter's colour is on its top cell, but the dots have only a bottom one.
        const bool top_blank = (Cell(L, r, x) & 0x3ff) == f->fill;
        cell_attr[cells++] = Cell(L, r + (f->h == 2 && top_blank), x) & 0x3c00;
      }
    }
    if (f->h == 2) r++;
  }
  typed[tn] = 0;
  if (!cells) return;
  const Page *page = NULL;
  for (int i = 0; i < count && !page; i++)
    if (PageMatch(typed, pages[i].en)) page = &pages[i];
  const char *tr = page ? UiLang_Get(section, page->en) : NULL;
  if (!tr) return;
  // Clear the English letters.
  for (int i = 0; i < cells; i++)
    for (int k = 0; k < f->h; k++) {
      const int r = cell_row[i] + k, x = cell_col[i];
      Want((uint16_t)(L->map + r * 32 + x), (uint16_t)((Cell(L, r, x) & 0xfc00) | f->fill));
    }
  // How much of the translation: the share of the English page typed.
  int en_letters = 0;
  for (const char *e = page->en; *e; e++) en_letters += *e != ' ';
  int tr_letters = 0;
  for (const char *q = tr; *q;) tr_letters += NextCode(&q) != ' ';
  const int shown = cells >= en_letters ? tr_letters : cells * tr_letters / en_letters;
  const int step = second_row > first_row ? second_row - first_row : f->h + 1;
  const char *starts[16];
  int lens[16];
  const int lines = WrapLines(tr, page->width, starts, lens);
  // The fade-in tail: the English letters at the end whose colour differs from the page's steady one
  // (the first letter's, long since faded in).
  const int steady = 0;
  int tail = 0;
  while (tail < cells && cell_attr[cells - 1 - tail] != cell_attr[steady]) tail++;
  int k = 0, last_row = -1, last_col = 0, last_full = 0;
  for (int l = 0; l < lines && k < shown; l++) {
    const int r = first_row + l * step;
    if (r + f->h - 1 > row1) break;
    const char *q = starts[l];
    for (int i = 0; i < lens[l] && k < shown; i++) {
      const unsigned c = NextCode(&q);
      if (c == ' ') continue;
      const int x = first_col + i;
      if (x >= 32) break;
      // The colour: the newest letters fade in, so the last ones shown take the colours of the
      // English page's last ones, in order; every other letter has the colour the rest of the page has.
      const int from_end = shown - 1 - k;
      const uint16_t attr = from_end < tail ? cell_attr[cells - 1 - from_end] : cell_attr[steady];
      if (!PutLetter(L, r, x, c, attr)) PutLetter(L, r, x, ' ', attr);
      last_row = r, last_col = x;
      k++;
      last_full = i + 1 >= lens[l] || k >= shown && RestIsSpaces(q);
    }
  }
  // The typing cursor is a sprite the game keeps after its last English letter, or at the start of the
  // next line when that line is full or the page done: the same for the translation, wherever its
  // lines break (so it works for every language).
  const int row_lo = first_row, row_hi = cell_row[cells - 1] + step;
  if (last_row < 0) MoveCursorSprite(L, row_lo, row_hi, first_row, first_col);
  else if (last_full) MoveCursorSprite(L, row_lo, row_hi, last_row + step, first_col);
  else MoveCursorSprite(L, row_lo, row_hi, last_row, last_col + 1);
}

// The story font: 8 px letters outlined in colour 3 (A at 0, 0-9 at 0x1a, then . , ' : !).
static FontChar g_story_chars[26 + 10 + 5];
static const CustomGlyph kStoryCustom[] = {
  { '?', { ".333333.", "33111133", "31133113", "33331133", "...3113.", "...3333.", "...3113.", "...3333." } },
  { '-', { "........", "........", "........", "3333333.", "3111113.", "3333333.", "........", "........" } },
};
static const Font *StoryFont(void) {
  static Font f;
  if (!f.h) {
    int n = 0;
    for (int i = 0; i < 26; i++) g_story_chars[n++] = (FontChar){ (uint16_t)('A' + i), (uint16_t)i, 0 };
    for (int i = 0; i < 10; i++) g_story_chars[n++] = (FontChar){ (uint16_t)('0' + i), (uint16_t)(0x1a + i), 0 };
    g_story_chars[n++] = (FontChar){ '.', 0x24, 0 };
    g_story_chars[n++] = (FontChar){ ',', 0x25, 0 };
    g_story_chars[n++] = (FontChar){ '\'', 0x27, 0 };
    g_story_chars[n++] = (FontChar){ ':', 0x28, 0 };
    g_story_chars[n++] = (FontChar){ '!', 0x2a, 0 };
    f = (Font){ .h = 1, .bpp = 2, .fill = 0x2f, .chars = g_story_chars, .char_count = n,
                .custom = kStoryCustom, .custom_count = 2, .pen = 1, .shade = 0, .pool_lo = 0x90, .pool_hi = 0xff };
  }
  return &f;
}

// The intro's 16 px letters (THE LAST METROID...): A-P tops at 0x30, Q-Z at 0x50, the bottoms
// 0x10 further; '.' and ',' are bottoms only.
static FontChar g_intro_big_chars[26 + 2];
static const Font *IntroBigFont(void) {
  static Font f;
  if (!f.h) {
    int n = 0;
    for (int i = 0; i < 26; i++) {
      const uint16_t top = (uint16_t)(i < 16 ? 0x30 + i : 0x50 + i - 16);
      g_intro_big_chars[n++] = (FontChar){ (uint16_t)('A' + i), top, (uint16_t)(top + 0x10) };
    }
    g_intro_big_chars[n++] = (FontChar){ '.', 0x2f, 0x26 };
    g_intro_big_chars[n++] = (FontChar){ ',', 0x2f, 0x25 };
    f = (Font){ .h = 2, .bpp = 2, .fill = 0x2f, .chars = g_intro_big_chars, .char_count = n,
                .pen = 1, .shade = 3, .pool_lo = 0x90, .pool_hi = 0xff };
  }
  return &f;
}

static const Page kIntroBigPages[] = {
  { "THE LAST METROID IS IN CAPTIVITY. THE GALAXY IS AT PEACE...", 22 },
};

static const Page kStoryPages[] = {
  { "I FIRST BATTLED THE METROIDS ON PLANET ZEBES. IT WAS THERE THAT I FOILED THE PLANS OF THE SPACE "
    "PIRATE LEADER MOTHER BRAIN TO USE THE CREATURES TO ATTACK GALACTIC CIVILIZATION...", 30 },
  { "I NEXT FOUGHT THE METROIDS ON THEIR HOMEWORLD, SR388. I COMPLETELY ERADICATED THEM EXCEPT FOR A LARVA, "
    "WHICH AFTER HATCHING FOLLOWED ME LIKE A CONFUSED CHILD...", 30 },
  { "I PERSONALLY DELIVERED IT TO THE GALACTIC RESEARCH STATION AT CERES SO SCIENTISTS COULD STUDY ITS "
    "ENERGY PRODUCING QUALITIES...", 30 },
  { "THE SCIENTISTS' FINDINGS WERE ASTOUNDING! THEY DISCOVERED THAT THE POWERS OF THE METROID MIGHT BE "
    "HARNESSED FOR THE GOOD OF CIVILIZATION!", 30 },
  { "SATISFIED THAT ALL WAS WELL, I LEFT THE STATION TO SEEK A NEW BOUNTY TO HUNT. BUT, I HAD HARDLY GONE "
    "BEYOND THE ASTEROID BELT WHEN I PICKED UP A DISTRESS SIGNAL!", 30 },
  { "CERES STATION WAS UNDER ATTACK!!", 30 },
};

static void IntroScreen(void) {
  const Ppu *ppu = ThePpu();
  const BgLayer *bg3 = &ppu->bgLayer[2];
  static CharPool pool;
  PoolInit(&pool, bg3->tileAdr, 2);
  TextLayer big = { IntroBigFont(), bg3->tilemapAdr, bg3->tileAdr, &pool };
  PoolReserve(&pool, IntroBigFont());
  TextLayer story = { StoryFont(), bg3->tilemapAdr, bg3->tileAdr, &pool };
  PoolReserve(&pool, StoryFont());
  TranslatePages(&big, 0, 27, "intro", kIntroBigPages, sizeof(kIntroBigPages) / sizeof(kIntroBigPages[0]));
  TranslatePages(&story, 4, 23, "intro", kStoryPages, sizeof(kStoryPages) / sizeof(kStoryPages[0]));
}

// ---- Pause screen (game states 12-18) -----------------------------------------------------------

static const WordRun kPauseItems[] = {
  { "VARIA SUIT", { 0x100, 0x101, 0x102, 0x103, 0x104, 0x105 }, 6 },
  { "GRAVITY SUIT", { 0x0d0, 0x0d1, 0x0d2, 0x0d3, 0x103, 0x104, 0x105 }, 7 },
  { "MORPHING BALL", { 0x120, 0x121, 0x122, 0x123, 0x117, 0x118, 0x10f, 0x11f }, 8 },
  { "BOMBS", { 0x0d5, 0x0d6, 0x0d7 }, 3 },
  { "SPRING BALL", { 0x110, 0x111, 0x112, 0x113, 0x114, 0x115, 0x116 }, 7 },
  { "SCREW ATTACK", { 0x0e0, 0x0e1, 0x0e2, 0x0e3, 0x0e4, 0x0e5, 0x0e6 }, 7 },
  { "HI-JUMP BOOTS", { 0x130, 0x131, 0x132, 0x133, 0x134, 0x135, 0x136 }, 7 },
  { "SPACE JUMP", { 0x0f0, 0x0f1, 0x0f2, 0x0f3, 0x0f4, 0x0f5 }, 6 },
  { "SPEED BOOSTER", { 0x124, 0x125, 0x126, 0x127, 0x128, 0x129, 0x12a, 0x12b }, 8 },
  // Beams: 4 cells each.
  { "CHARGE", { 0x0d8, 0x0d9, 0x0da, 0x0e7 }, 4 },
  { "ICE", { 0x0db, 0x0dc }, 2 },
  { "WAVE", { 0x0dd, 0x0de, 0x0df }, 3 },
  { "SPAZER", { 0x0e8, 0x0e9, 0x0ea, 0x0eb }, 4 },
  { "PLASMA", { 0x0ec, 0x0ed, 0x0ee, 0x0ef }, 4 },
  { "HYPER", { 0x137, 0x138, 0x139, 0x12f }, 4 },
};

static const WordRun kPauseHeaders[] = {
  { "RESERVE TANK", { 0x107, 0x108, 0x109, 0x10a }, 4 },
  { "BEAM", { 0x0f9, 0x0fa, 0x0fb }, 3 },
  { "SUIT", { 0x0f6, 0x0f7, 0x0f8 }, 3 },
  { "MISC.", { 0x1b0, 0x1b1, 0x1b2 }, 3 },
  { "BOOTS", { 0x0a0, 0x0a1, 0x0a2 }, 3 },
};

// The buttons under the pause screen: a word inside a drawn box, two rows of chars. The box is
// kept, its inside cleared and the translation drawn there (MAP: bold, twice as wide; EXIT: thin).
typedef struct {
  const char *en;           // the key of its translation (the English in the box)
  uint16_t top[6], bottom[6];
  int n;
  int x0, y0, x1, y1;       // the inside, in pixels of the n x 2 cells
  uint8_t pen, bg;
  bool bold;
} BoxWord;

static const BoxWord kPauseButtons[] = {
  { "MAP", { 0x99, 0x9a, 0x9b, 0x9c, 0x9d }, { 0xa9, 0xaa, 0xab, 0xac, 0xad }, 5, 1, 2, 37, 12, 0x4, 0xb, true },
  { "EXIT", { 0xb8, 0xb9, 0xba, 0xbb }, { 0xc8, 0xc9, 0xca, 0xcb }, 4, 4, 5, 28, 11, 0x4, 0x7, false },
};

static void TranslateBoxWords(uint16_t map, int pages, uint16_t base, CharPool *pool, const char *section, const BoxWord *words,
                              int count) {
  if (!UiLang_Translated()) return;
  for (int i = 0; i < count; i++) {
    const BoxWord *bw = &words[i];
    const char *tr = UiLang_Get(section, bw->en);
    if (!tr) continue;
    for (int c = 0; c + bw->n <= 0x400 * pages - 32; c++) {
      int k = 0;
      while (k < bw->n && (g_shadow[(map + c + k) & 0x7fff] & 0x3ff) == bw->top[k] &&
             (g_shadow[(map + c + 32 + k) & 0x7fff] & 0x3ff) == bw->bottom[k]) k++;
      if (k < bw->n) continue;
      uint8_t px[16][48];
      for (int j = 0; j < bw->n; j++) {
        Tile t;
        ReadTile(base, bw->top[j], 4, &t);
        for (int y = 0; y < 8; y++) memcpy(&px[y][j * 8], t.px[y], 8);
        ReadTile(base, bw->bottom[j], 4, &t);
        for (int y = 0; y < 8; y++) memcpy(&px[8 + y][j * 8], t.px[y], 8);
      }
      for (int y = bw->y0; y <= bw->y1; y++)
        for (int x = bw->x0; x <= bw->x1; x++) px[y][x] = bw->bg;
      uint8_t ink[8][128];
      memset(ink, 0, sizeof(ink));
      const int rows = bw->bold ? 6 : 5;
      int w = TinyText(tr, rows, ink, 1, 1);
      const int h_in = bw->y1 - bw->y0 + 1, w_in = bw->x1 - bw->x0 + 1;
      const int sx = bw->bold && w * 2 <= w_in ? 2 : 1;   // bold: each column twice
      const int th = bw->bold ? 8 : rows;
      const int ox = bw->x0 + (w_in - w * sx) / 2, oy = bw->y0 + (h_in - th) / 2;
      for (int y = 0; y < th; y++) {
        const int src_y = 1 + (bw->bold ? y * 6 / 8 : y);
        for (int x = 0; x < w * sx; x++) {
          const int dx = ox + x, dy = oy + y;
          if (dx < bw->x0 || dx > bw->x1 || dy < bw->y0 || dy > bw->y1) continue;
          if (ink[src_y][x / sx]) px[dy][dx] = bw->pen;
        }
      }
      for (int j = 0; j < bw->n; j++)
        for (int half = 0; half < 2; half++) {
          Tile t;
          for (int y = 0; y < 8; y++) memcpy(t.px[y], &px[half * 8 + y][j * 8], 8);
          const int ch = PoolChar(pool, 0x000, 0x3ff, &t);
          if (ch < 0) return;
          const uint16_t adr = (uint16_t)(map + c + half * 32 + j);
          Want(adr, (uint16_t)((g_shadow[adr & 0x7fff] & 0xfc00) | ch));
        }
      c += bw->n - 1;
    }
  }
}

// The map screen's title (BG2): 8 px capitals at 0x30 (A-Z), 0x01 blank. Area names stay,
// but for the Wrecked Ship (12 letters at most: the title's frame is drawn around them).
static FontChar g_pause_title_chars[26];
static const Font *PauseTitleFont(void) {
  static Font f;
  if (!f.h) {
    for (int i = 0; i < 26; i++) g_pause_title_chars[i] = (FontChar){ (uint16_t)('A' + i), (uint16_t)(0x30 + i), 0 };
    f = (Font){ .h = 1, .bpp = 4, .fill = 0x01, .chars = g_pause_title_chars, .char_count = 26, .pen = 0xe,
                .shade = 0, .pool_lo = 0x000, .pool_hi = 0x3ff };
  }
  return &f;
}

static const Phrase kPauseTitles[] = {
  { "WRECKED SHIP", kMiddle },
};

static void PauseScreen(void) {
  const Ppu *ppu = ThePpu();
  const BgLayer *bg1 = &ppu->bgLayer[0];
  static CharPool pool;
  PoolInit(&pool, bg1->tileAdr, 4);
  PoolReserve(&pool, PauseTitleFont());
  const int pages = 1 + bg1->tilemapWider;
  // The names' chars carry palette 2 in the game's tables (0x900 is char 0x100).
  WordStyle items = { bg1->tilemapAdr, pages, bg1->tileAdr, 4, 0x0d4, 5, 2, 1, 0x000, 0x3ff, &pool };
  TranslateWordRuns(&items, "pause", kPauseItems, sizeof(kPauseItems) / sizeof(kPauseItems[0]), 0xa);
  WordStyle headers = { bg1->tilemapAdr, pages, bg1->tileAdr, 4, 0xffff, 6, 1, -1, 0x000, 0x3ff, &pool };
  TranslateWordRuns(&headers, "pause", kPauseHeaders, sizeof(kPauseHeaders) / sizeof(kPauseHeaders[0]), 0xd);
  const BgLayer *bg2 = &ppu->bgLayer[1];
  TextLayer title = { PauseTitleFont(), bg2->tilemapAdr, bg2->tileAdr, &pool };
  TranslateLayer(&title, "pause", kPauseTitles, sizeof(kPauseTitles) / sizeof(kPauseTitles[0]));
  TranslateBoxWords(bg2->tilemapAdr, 1, bg2->tileAdr, &pool, "pause", kPauseButtons, sizeof(kPauseButtons) / sizeof(kPauseButtons[0]));
}

// ---- Game over (game state 26) -------------------------------------------------------------------

static const Phrase kGameOverBig[] = {
  { "YES" },
  { "N O" },
};

static const Phrase kGameOverSmall[] = {
  { "FIND THE METROID LARVA!", kMiddle },
  { "TRY AGAIN ?", kMiddle },
  { "(RETURN TO GAME)" },
  { "(GO TO TITLE)" },
};

static void GameOverScreen(void) {
  const Ppu *ppu = ThePpu();
  static CharPool pool;
  PoolInit(&pool, ppu->bgLayer[0].tileAdr, 4);
  TextLayer big = { &kMenuBig, ppu->bgLayer[0].tilemapAdr, ppu->bgLayer[0].tileAdr, &pool };
  PoolReserve(&pool, &kMenuBig);
  TextLayer small = { MenuSmall(), ppu->bgLayer[0].tilemapAdr, ppu->bgLayer[0].tileAdr, &pool };
  PoolReserve(&pool, MenuSmall());
  TranslateLayer(&big, "game over", kGameOverBig, sizeof(kGameOverBig) / sizeof(kGameOverBig[0]));
  TranslateLayer(&small, "game over", kGameOverSmall, sizeof(kGameOverSmall) / sizeof(kGameOverSmall[0]));
}

// ---- Ending and credits (game state 39) ----------------------------------------------------------
// BG1, 4 bpp: headings in 8 px capitals (A at 0), names and SEE YOU NEXT MISSION in 16 px ones
// (A-P tops at 0x20, Q-Z at 0x40, bottoms 0x10 further); 0x7f is blank. The names stay.

// The 8 px letters are drawn from a copy (taken from the credits): on the last screen the game
// reuses the chars of the letters its own text does not need (B and D among them).
static const CustomGlyph kCreditsSmallGlyphs[26] = {
  { 'A', { ".222222.", "22111122", "21122112", "21111112", "21122112", "21122112", "22222222", "........" } },
  { 'B', { "2222222.", "21111122", "21122112", "21111122", "21122112", "21111122", "2222222.", "........" } },
  { 'C', { ".222222.", "22111122", "21122112", "21122222", "21122112", "22111122", ".222222.", "........" } },
  { 'D', { "2222222.", "21111122", "21122112", "21122112", "21122112", "21111122", "2222222.", "........" } },
  { 'E', { "22222222", "21111112", "21122222", "2111112.", "21122222", "21111112", "22222222", "........" } },
  { 'F', { "22222222", "21111112", "21122222", "2111112.", "2112222.", "2112....", "2222....", "........" } },
  { 'G', { ".222222.", "2211112.", "21122222", "21121112", "21122112", "22111112", ".2222222", "........" } },
  { 'H', { "22222222", "21122112", "21122112", "21111112", "21122112", "21122112", "22222222", "........" } },
  { 'I', { ".222222.", ".211112.", ".221122.", "..2112..", ".221122.", ".211112.", ".222222.", "........" } },
  { 'J', { "....2222", "....2112", "....2112", "22222112", "21122112", "22111122", ".222222.", "........" } },
  { 'K', { "222.2222", "21122112", "21121122", "2111122.", "2111112.", "21122112", "22222222", "........" } },
  { 'L', { "2222....", "2112....", "2112....", "2112....", "21122222", "21111112", "22222222", "........" } },
  { 'M', { "2222.222", "2112.211", "21112111", "21121211", "21121211", "21122211", "2222.222", "........" } },
  { 'N', { "222.2222", "21222112", "21122112", "21112112", "21121112", "21122212", "2222.222", "........" } },
  { 'O', { ".222222.", "22111122", "21122112", "21122112", "21122112", "22111122", ".222222.", "........" } },
  { 'P', { "2222222.", "21111122", "21122112", "21111122", "2112222.", "2112....", "2222....", "........" } },
  { 'Q', { ".222222.", "22111122", "21122112", "21122112", "21122112", "22111122", ".2221112", "...22222" } },
  { 'R', { "2222222.", "21111122", "21122112", "21111122", "21122112", "21122112", "22222222", "........" } },
  { 'S', { ".2222222", "22111112", "21122222", "22111222", "22221112", "21111122", "2222222.", "........" } },
  { 'T', { "22222222", "21111112", "22211222", "..2112..", "..2112..", "..2112..", "..2222..", "........" } },
  { 'U', { "22222222", "21122112", "21122112", "21122112", "21122112", "22111122", ".222222.", "........" } },
  { 'V', { "22222222", "21122112", "21122112", "21122112", "21121122", "2111122.", "222222..", "........" } },
  { 'W', { "22222222", "11211211", "11211211", "11211211", "11211211", "11111112", "22222222", "........" } },
  { 'X', { "22222222", "21122112", "21122112", "22111122", "21122112", "21122112", "22222222", "........" } },
  { 'Y', { "22222222", "21122112", "21122112", "22111122", ".221122.", "..2112..", "..2222..", "........" } },
  { 'Z', { "22222222", "21111112", "22222112", ".221122.", "22112222", "21111112", "22222222", "........" } },
};
static FontChar g_credits_small_chars[26];
static FontChar g_credits_big_chars[26];
static const Font *CreditsSmall(void) {
  static Font f;
  if (!f.h) {
    for (int i = 0; i < 26; i++) g_credits_small_chars[i] = (FontChar){ (uint16_t)('A' + i), (uint16_t)i, 0 };
    f = (Font){ .h = 1, .bpp = 4, .fill = 0x7f, .chars = g_credits_small_chars, .char_count = 26,
                .custom = kCreditsSmallGlyphs, .custom_count = 26, .pen = 1, .shade = 0, .pool_lo = 0x000,
                .pool_hi = 0x3ff };
  }
  return &f;
}
static const Font *CreditsBig(void) {
  static Font f;
  if (!f.h) {
    for (int i = 0; i < 26; i++) {
      const uint16_t top = (uint16_t)(i < 16 ? 0x20 + i : 0x40 + i - 16);
      g_credits_big_chars[i] = (FontChar){ (uint16_t)('A' + i), top, (uint16_t)(top + 0x10) };
    }
    f = (Font){ .h = 2, .bpp = 4, .fill = 0x7f, .chars = g_credits_big_chars, .char_count = 26,
                .pen = 1, .shade = 2, .pool_lo = 0x000, .pool_hi = 0x3ff };
  }
  return &f;
}

static const Phrase kCreditsSmall[] = {
  { "SUPER METROID STAFF", kMiddle },
  { "EXECUTIVE PRODUCER", kMiddle },
  { "PRODUCER", kMiddle },
  { "DIRECTOR", kMiddle },
  { "BACK GROUND DESIGNERS", kMiddle },
  { "OBJECT DESIGNERS", kMiddle },
  { "SAMUS ORIGINAL DESIGNER", kMiddle },
  { "SAMUS DESIGNER", kMiddle },
  { "PROGRAM DIRECTOR", kMiddle },
  { "SPECIAL THANKS TO", kMiddle },
  { "SYSTEM COORDINATOR", kMiddle },
  { "SYSTEM PROGRAMMER", kMiddle },
  { "SAMUS PROGRAMMER", kMiddle },
  { "EVENT PROGRAMMER", kMiddle },
  { "ENEMY PROGRAMMER", kMiddle },
  { "MAP PROGRAMMER", kMiddle },
  { "ASSISTANT PROGRAMMER", kMiddle },
  { "COORDINATORS", kMiddle },
  { "PRINTED ART WORK", kMiddle },
  { "SOUND PROGRAM" },
  { "AND SOUND EFFECTS" },
  { "MUSIC COMPOSERS", kMiddle },
  { "GENERAL MANAGER", kMiddle },
  { "YOUR RATE FOR", kMiddle },
  { "COLLECTING ITEMS IS", kMiddle },
};

static const Phrase kCreditsBig[] = {
  { "SEE YOU NEXT MISSION", kMiddle },
};

static void EndingScreen(void) {
  const Ppu *ppu = ThePpu();
  const BgLayer *bg1 = &ppu->bgLayer[0];
  if (ppu->mode == 7) return;   // the mode 7 scenes: their texts are sprites (not done)
  static CharPool pool;
  PoolInit(&pool, bg1->tileAdr, 4);
  const int rows = bg1->tilemapHigher ? 64 : 32;
  TextLayer small = { CreditsSmall(), bg1->tilemapAdr, bg1->tileAdr, &pool, rows };
  PoolReserve(&pool, CreditsSmall());
  TextLayer big = { CreditsBig(), bg1->tilemapAdr, bg1->tileAdr, &pool, rows };
  PoolReserve(&pool, CreditsBig());
  TranslateLayer(&small, "ending", kCreditsSmall, sizeof(kCreditsSmall) / sizeof(kCreditsSmall[0]));
  TranslateLayer(&big, "ending", kCreditsBig, sizeof(kCreditsBig) / sizeof(kCreditsBig[0]));
}

// ---- HUD (BG3 tilemap at 0x5800, chars at BG3's tile address) ------------------------------------------------
// ENERGY is a picture over chars 0x0b-0x0d and 0x32: colour 2 letters outlined in 1 on 3.

static void HudScreen(void) {
  static const uint16_t kChars[4] = { 0x0b, 0x0c, 0x0d, 0x32 };
  enum { kHudMap = 0x5800 };
  // BG3's chars are where the room says: 0x4000 in most, 0x2000 in Kraid's, whose BG2 tilemap is at 0x4000 (writing the
  // letters there left a strip of garbage tiles in his body).
  const Ppu *hud_ppu = ThePpu();
  if (!hud_ppu) return;
  const uint16_t kHudChars = hud_ppu->bgLayer[2].tileAdr;
  const char *energy = UiLang_Get("hud", "ENERGY");
  if (!energy) return;
  bool found = false;
  for (int c = 0; c + 4 <= 0x80 && !found; c++) {
    int k = 0;
    while (k < 4 && (g_shadow[kHudMap + c + k] & 0x3ff) == kChars[k]) k++;
    found = k == 4;
  }
  if (!found) return;
  uint8_t ink[8][128];
  memset(ink, 0, sizeof(ink));
  const int w = TinyText(energy, 5, ink, 1, 1);
  const int x0 = (32 - w) / 2 > 1 ? (32 - w) / 2 : 1;
  uint8_t px[8][32];
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 32; x++) {
      const int sx = x - x0;
      const bool on = sx >= 0 && sx < 128 && ink[y][sx];
      bool near = false;
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
          const int yy = y + dy, xx = sx + dx;
          if (yy >= 0 && yy < 8 && xx >= 0 && xx < 128 && ink[yy][xx]) near = true;
        }
      px[y][x] = on ? 2 : near && y < 7 ? 1 : 3;
    }
  for (int i = 0; i < 4; i++) {
    Tile t;
    for (int y = 0; y < 8; y++) memcpy(t.px[y], &px[y][i * 8], 8);
    WriteTile(kHudChars, kChars[i], 2, &t);
  }
}

// ---- The hook ---------------------------------------------------------------------------------

static void Hook(void) {
  if (!ThePpu()) return;
  // The map after a chosen file (state 5) starts by clearing only the menu's BG2 and fading its own
  // palettes in (FileSelectMap_0, _1): BG1 still holds the menu that was left (options, state 2, or the
  // file select, state 4) until the map's tilemap arrives (_2), and it showed in English for those
  // frames (issue #27, sm-rec-0003).
  static int menu_state;
  if (game_state == 2 || game_state == 4) menu_state = game_state;
  else if (game_state != 5) menu_state = 0;
  // The option menus also in English when the port's LANGUAGE entry is on (LanguageScreens).
  const bool options = game_state == 2 || (game_state == 5 && menu_index < 2 && menu_state == 2);
  if (options && (g_rtl_language_menu || UiLang_Translated())) OptionsScreen();
  if (UiLang_Translated()) {
    switch (game_state) {
    case 4: FileSelectScreen(); break;
    case 5:
      if (menu_index < 2 && menu_state == 4) FileSelectScreen();
      break;
    case 0x1e: IntroScreen(); break;
    case 12: case 13: case 14: case 15: case 16: case 17: case 18: PauseScreen(); HudScreen(); break;
    case 7: case 8: case 9: case 10: case 11: case 19: case 27: HudScreen(); break;
    case 26: GameOverScreen(); break;
    case 39: EndingScreen(); break;
    default: break;
    }
  }
  Commit();
}

#define FOR_KEYS(fn, section, table, note) \
  for (size_t i_ = 0; i_ < sizeof(table) / sizeof(table[0]); i_++) fn(section, table[i_].en, note)

void GameTextScreens_ForEachKey(LangKeyFn *fn) {
  static const char kRoom[] = "in the game's capitals, in about the English one's room";
  FOR_KEYS(fn, "options", kOptionsBig, kRoom);
  FOR_KEYS(fn, "options", kOptionsSmall, kRoom);
  fn("options", "LANGUAGE", "the title of the language list and its entry in OPTION MODE");
  fn("options", "CLASSIC", "CONTROLLER SETTING MODE, the scheme under the title: its first choice, 10 letters at most");
  fn("options", "MODERN", "its second choice, 12 letters at most");
  static const char *const kModernRows[] = { "WALK", "SHOT / BOMB", "MORPH BALL", "AIM", "MISSILE", "POWER BOMB",
                                             "GRAPPLING BEAM", "X-RAY SCOPE", "MISSILE / SUPER" };
  for (size_t i = 0; i < sizeof(kModernRows) / sizeof(kModernRows[0]); i++)
    fn("options", kModernRows[i], "CONTROLLER SETTING MODE, the modern scheme's rows: 16 letters at most");
  FOR_KEYS(fn, "file select", kFileSelectBig, kRoom);
  FOR_KEYS(fn, "file select", kFileSelectSmall, kRoom);
  FOR_KEYS(fn, "file select", kFileSelectLabels, "a picture redrawn with a small font in the English one's room");
  FOR_KEYS(fn, "intro", kIntroBigPages, "the whole page, wrapped by the port");
  FOR_KEYS(fn, "intro", kStoryPages, "the whole page, wrapped by the port");
  FOR_KEYS(fn, "pause", kPauseItems, "a picture redrawn with a small font in the English one's room");
  FOR_KEYS(fn, "pause", kPauseHeaders, "a picture redrawn with a small font in the English one's room");
  FOR_KEYS(fn, "pause", kPauseButtons, "inside the button's box");
  FOR_KEYS(fn, "pause", kPauseTitles, kRoom);
  FOR_KEYS(fn, "game over", kGameOverBig, kRoom);
  FOR_KEYS(fn, "game over", kGameOverSmall, kRoom);
  FOR_KEYS(fn, "ending", kCreditsSmall, kRoom);
  FOR_KEYS(fn, "ending", kCreditsBig, kRoom);
  fn("hud", "ENERGY", "a picture redrawn with a small font, about 6 letters");
}

void GameTextScreens_Init(void) {
  Ppu *ppu = ThePpu();
  if (ppu) memcpy(g_shadow, ppu->vram, sizeof(g_shadow));
  g_ppu_vram_shadow = g_shadow;
  g_rtl_game_text_hook = Hook;
  g_rtl_language_menu = &kLanguageMenu;
}

void GameTextScreens_PutBack(void) {
  Ppu *ppu = ThePpu();
  if (!ppu) return;
  for (int i = 0; i < g_prev_count; i++) Put(ppu, g_prev_adr[i], g_shadow[g_prev_adr[i]]);
  g_prev_count = 0;
}

void GameTextScreens_Forget(void) {
  Ppu *ppu = ThePpu();
  if (ppu) memcpy(g_shadow, ppu->vram, sizeof(g_shadow));
  g_prev_count = 0;
}
