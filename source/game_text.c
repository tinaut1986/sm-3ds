#include "game_text.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "src/types.h"
#include "src/variables.h"
#include "src/ida_types.h"
#include "src/sm_cpu_infra.h"
#include "src/sm_rtl.h"
#include "src/snes/ppu.h"
#include "src/snes/snes.h"
#include "ui_lang.h"

extern uint16 message_box_das0l_value;   // sm_85.c: bytes of the box's tilemap DMA

// ---- The game's message box font (BG3, chars at its tile address, usually VRAM word 0x4000, 2bpp) --------------
// Capitals A-Z are chars 0xE0-0xF9 in colour 1 on colour 3, 6 rows tall from the top;
// '.' 0xFA, '?' 0xFE, '-' 0xCF; 0x4E (and 0x0F) is a cell of plain colour 3, the box's
// background. The instruction lines are pre-drawn lowercase words in colour 2 on chars
// 0xB0-0xDF, mixed with item icons and the button (a capital in its own colours, placed
// by the game from the controller settings). Outside the box: 0x0E.
enum {
  kHudMap = 0x5800, kHudCells = 4 * 32,
  kTileA = 0xE0, kTileDot = 0xFA, kTileQuestion = 0xFE, kTileDash = 0xCF, kTileFill = 0x4E, kTileOutside = 0x0E,
  kPoolFirst = 0xB0, kPoolLast = 0xDF,
  kFlipXY = 0xC000,
};

// Chars in the pool range that are pictures, not words: kept where they are.
static bool FixedChar(int c) {
  return (c >= 0x34 && c <= 0x3B) || (c >= 0x49 && c <= 0x4C) ||   // item icons
         c == 0xC9 || c == 0xCC || c == 0xCD ||                      // bomb, arrow (also the save cursor)
         c == 0xCF ||                                                // '-'
         (c >= 0xDC && c <= 0xDF);                                   // Samus (the bomb box)
}

// ---- Translations ---------------------------------------------------------------------
// Per message box (1..28, see sm_85.c) the rows that change, row 0 being the first line of
// text. Capitals rows use the game's own letters; accents become marks above (below for
// Ç). An instruction line (`instr`, under an item's name) is lowercase, with "{}" where
// each picture of the English line goes (icon, button), in the English order.
// NULL keeps the English text.

typedef struct {
  const char *row[3];
  const char *instr;
  const char *yes, *no;   // the save prompt's choices (3 letters at most)
} MsgText;

enum { kMsgCount = 29 };

// The English of each box, the key of its translation in the [message boxes] section of the
// language files: "KEY = ROW | ROW | ROW" (an empty row keeps the English one), "KEY (line)" the
// instruction line, "KEY (yes)" and "KEY (no)" the save prompt's choices. 23 and 28 are the
// same prompt (a save station, the ship).
static const char *const kMsgKeys[kMsgCount] = {
  [1] = "ENERGY TANK", [2] = "MISSILE", [3] = "SUPER MISSILE", [4] = "POWER BOMB", [5] = "GRAPPLING BEAM",
  [6] = "X-RAY SCOPE", [7] = "VARIA SUIT", [8] = "SPRING BALL", [9] = "MORPHING BALL", [10] = "SCREW ATTACK",
  [11] = "HI-JUMP BOOTS", [12] = "SPACE JUMP", [13] = "SPEED BOOSTER", [14] = "CHARGE BEAM", [15] = "ICE BEAM",
  [16] = "WAVE BEAM", [17] = "SPAZER", [18] = "PLASMA BEAM", [19] = "BOMB", [20] = "MAP DATA ACCESS COMPLETED.",
  [21] = "ENERGY RECHARGE COMPLETED.", [22] = "MISSILE RELOAD COMPLETED.", [23] = "WOULD YOU LIKE TO SAVE?",
  [24] = "SAVE COMPLETED.", [25] = "RESERVE TANK", [26] = "GRAVITY SUIT", [28] = "WOULD YOU LIKE TO SAVE?",
};

static const char *BoxText(int index, const char *what) {
  char key[64];
  snprintf(key, sizeof(key), "%s (%s)", kMsgKeys[index], what);
  return UiLang_Get("message boxes", key);
}

// Box `index` in the current language. False when nothing of it is translated.
static bool MessageText(int index, MsgText *m) {
  static char rows[3][96];
  memset(m, 0, sizeof(*m));
  if (index <= 0 || index >= kMsgCount || !kMsgKeys[index]) return false;
  const char *t = UiLang_Get("message boxes", kMsgKeys[index]);
  for (int r = 0; r < 3 && t; r++) {
    const char *bar = strchr(t, '|');
    const char *end = bar ? bar : t + strlen(t);
    while (t < end && *t == ' ') t++;
    while (end > t && end[-1] == ' ') end--;
    const size_t n = (size_t)(end - t) < sizeof(rows[r]) - 1 ? (size_t)(end - t) : sizeof(rows[r]) - 1;
    if (n) {
      memcpy(rows[r], t, n);
      rows[r][n] = 0;
      m->row[r] = rows[r];
    }
    t = bar ? bar + 1 : NULL;
  }
  m->instr = BoxText(index, "line");
  m->yes = BoxText(index, "yes");
  m->no = BoxText(index, "no");
  if (m->yes && !m->no) m->no = "NO";
  if (m->no && !m->yes) m->yes = "YES";
  return m->row[0] || m->row[1] || m->row[2] || m->instr || m->yes;
}

void GameText_ForEachKey(LangKeyFn *fn) {
  for (int i = 1; i < kMsgCount; i++) {
    if (!kMsgKeys[i] || (i == 28 && !strcmp(kMsgKeys[i], kMsgKeys[23]))) continue;
    char key[64];
    fn("message boxes", kMsgKeys[i], "up to three rows split by |");
    if ((i >= 2 && i <= 6) || i == 13 || i == 19) {
      snprintf(key, sizeof(key), "%s (line)", kMsgKeys[i]);
      fn("message boxes", key, "lowercase, a {} for each picture of the English line");
    }
    if (i == 23) {
      snprintf(key, sizeof(key), "%s (yes)", kMsgKeys[i]);
      fn("message boxes", key, "3 letters at most");
      snprintf(key, sizeof(key), "%s (no)", kMsgKeys[i]);
      fn("message boxes", key, "3 letters at most");
    }
  }
}

// ---- Pixels -----------------------------------------------------------------------------

typedef struct {
  uint8_t px[8][8];   // colour indices 0..3
} Tile;

static void TileFill(Tile *t, int c) { memset(t->px, c, sizeof(t->px)); }

static void TileToChar(const Tile *t, uint16_t out[8]) {
  for (int y = 0; y < 8; y++) {
    uint16_t lo = 0, hi = 0;
    for (int x = 0; x < 8; x++) {
      lo |= (uint16_t)((t->px[y][x] & 1) << (7 - x));
      hi |= (uint16_t)(((t->px[y][x] >> 1) & 1) << (7 - x));
    }
    out[y] = (uint16_t)(lo | hi << 8);
  }
}

// Accent marks for the capitals, 2 rows at the bottom of the cell above (rows 5-6, a free
// row between them and the letter), or for the cedilla 2 rows at the top of the cell below.
typedef enum { kMarkNone, kMarkAcute, kMarkGrave, kMarkCirc, kMarkTilde, kMarkDiaer, kMarkCedilla } Mark;
static const char *const kMarkRows[][2] = {
  [kMarkAcute] = { "....##..", "...##..." },
  [kMarkGrave] = { "..##....", "...##..." },
  [kMarkCirc] = { "...##...", "..#..#.." },
  [kMarkTilde] = { "..##..#.", ".#..##.." },
  [kMarkDiaer] = { "........", ".##..##." },
  [kMarkCedilla] = { "...##...", "..##...." },
};

static void MarkTile(Tile *t, Mark m) {
  TileFill(t, 3);
  const int y0 = m == kMarkCedilla ? 0 : 5;
  for (int r = 0; r < 2; r++)
    for (int x = 0; x < 8; x++)
      if (kMarkRows[m][r][x] == '#') t->px[y0 + r][x] = 1;
}

static void ApostropheTile(Tile *t) {
  static const char *const rows[4] = { "..##....", "..##....", "...#....", "..#....." };
  TileFill(t, 3);
  for (int r = 0; r < 4; r++)
    for (int x = 0; x < 8; x++)
      if (rows[r][x] == '#') t->px[r][x] = 1;
}

// ---- The lowercase font of the instruction lines ------------------------------------------
// Colour 2 on 3 like the game's words: x-height rows 1-5, ascenders from row 0, descenders
// down to row 7, accents on row 0. One column between letters, 3 for a space.
typedef struct {
  uint16_t code;
  uint8_t w;
  const char *rows[8];
} Glyph;

static const Glyph kGlyphs[] = {
  { 'a', 4, { "....", ".##.", "...#", ".###", "#..#", ".###", "....", "...." } },
  { 'b', 4, { "#...", "#...", "###.", "#..#", "#..#", "###.", "....", "...." } },
  { 'c', 3, { "...", ".##", "#..", "#..", "#..", ".##", "...", "..." } },
  { 'd', 4, { "...#", "...#", ".###", "#..#", "#..#", ".###", "....", "...." } },
  { 'e', 4, { "....", ".##.", "#..#", "####", "#...", ".###", "....", "...." } },
  { 'f', 3, { ".##", "#..", "###", "#..", "#..", "#..", "...", "..." } },
  { 'g', 4, { "....", ".###", "#..#", "#..#", "#..#", ".###", "...#", ".##." } },
  { 'h', 4, { "#...", "#...", "###.", "#..#", "#..#", "#..#", "....", "...." } },
  { 'i', 1, { "#", ".", "#", "#", "#", "#", ".", "." } },
  { 'j', 2, { ".#", "..", ".#", ".#", ".#", ".#", ".#", "#." } },
  { 'k', 4, { "#...", "#..#", "#.#.", "##..", "#.#.", "#..#", "....", "...." } },
  { 'l', 1, { "#", "#", "#", "#", "#", "#", ".", "." } },
  { 'm', 5, { ".....", "##.#.", "#.#.#", "#.#.#", "#.#.#", "#.#.#", ".....", "....." } },
  { 'n', 4, { "....", "###.", "#..#", "#..#", "#..#", "#..#", "....", "...." } },
  { 'o', 4, { "....", ".##.", "#..#", "#..#", "#..#", ".##.", "....", "...." } },
  { 'p', 4, { "....", "###.", "#..#", "#..#", "#..#", "###.", "#...", "#..." } },
  { 'q', 4, { "....", ".###", "#..#", "#..#", "#..#", ".###", "...#", "...#" } },
  { 'r', 3, { "...", "#.#", "##.", "#..", "#..", "#..", "...", "..." } },
  { 's', 4, { "....", ".###", "#...", ".##.", "...#", "###.", "....", "...." } },
  { 't', 3, { "#..", "###", "#..", "#..", "#..", ".##", "...", "..." } },
  { 'u', 4, { "....", "#..#", "#..#", "#..#", "#..#", ".###", "....", "...." } },
  { 'v', 5, { ".....", "#...#", "#...#", ".#.#.", ".#.#.", "..#..", ".....", "....." } },
  { 'w', 5, { ".....", "#...#", "#...#", "#.#.#", "#.#.#", ".#.#.", ".....", "....." } },
  { 'x', 5, { ".....", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", ".....", "....." } },
  { 'y', 4, { "....", "#..#", "#..#", "#..#", "#..#", ".###", "...#", ".##." } },
  { 'z', 4, { "....", "####", "...#", ".##.", "#...", "####", "....", "...." } },
  { '.', 1, { ".", ".", ".", ".", ".", "#", ".", "." } },
  { ',', 1, { ".", ".", ".", ".", ".", "#", "#", "." } },
  { '\'', 1, { "#", "#", ".", ".", ".", ".", ".", "." } },
  { '-', 3, { "...", "...", "...", "###", "...", "...", "...", "..." } },
  { '?', 4, { ".##.", "#..#", "..#.", ".#..", "....", ".#..", "....", "...." } },
  { '!', 1, { "#", "#", "#", "#", ".", "#", ".", "." } },
  { ':', 1, { ".", ".", "#", ".", ".", "#", ".", "." } },
  { 0xE7, 3, { "...", ".##", "#..", "#..", "#..", ".##", ".#.", "#.." } },          // ç
  { 0xF1, 4, { "####", "###.", "#..#", "#..#", "#..#", "#..#", "....", "...." } },  // ñ
  { 0xED, 2, { ".#", "..", "#.", "#.", "#.", "#.", "..", ".." } },                  // í
  { 0xEC, 2, { "#.", "..", ".#", ".#", ".#", ".#", "..", ".." } },                  // ì
  { 0xEF, 3, { "#.#", "...", ".#.", ".#.", ".#.", ".#.", "...", "..." } },          // ï
};

// Accented vowels: the plain letter with a mark on row 0.
static const struct { uint16_t code; char base; const char *mark; } kAccented[] = {
  { 0xE1, 'a', "..#." }, { 0xE0, 'a', ".#.." }, { 0xE2, 'a', ".##." }, { 0xE3, 'a', "#.##" }, { 0xE4, 'a', "#..#" },
  { 0xE9, 'e', "..#." }, { 0xE8, 'e', ".#.." }, { 0xEA, 'e', ".##." }, { 0xEB, 'e', "#..#" },
  { 0xF3, 'o', "..#." }, { 0xF2, 'o', ".#.." }, { 0xF4, 'o', ".##." }, { 0xF5, 'o', "#.##" }, { 0xF6, 'o', "#..#" },
  { 0xFA, 'u', "..#." }, { 0xF9, 'u', ".#.." }, { 0xFB, 'u', ".##." }, { 0xFC, 'u', "#..#" },
};

static const Glyph *FindGlyph(unsigned code) {
  for (size_t i = 0; i < sizeof(kGlyphs) / sizeof(kGlyphs[0]); i++)
    if (kGlyphs[i].code == code) return &kGlyphs[i];
  return NULL;
}

// The glyph for `code` (with its accent, if any) in `rows`; its width, or -1 for a space,
// 0 for a character the font does not have.
static int GlyphRows(unsigned code, char rows[8][6]) {
  if (code >= 'A' && code <= 'Z') code += 32;
  if (code == ' ') return -1;
  const char *mark = NULL;
  for (size_t i = 0; i < sizeof(kAccented) / sizeof(kAccented[0]); i++)
    if (kAccented[i].code == code || kAccented[i].code == code + 32) code = (unsigned char)kAccented[i].base, mark = kAccented[i].mark;
  const Glyph *g = FindGlyph(code);
  if (!g) return 0;
  for (int r = 0; r < 8; r++) snprintf(rows[r], 6, "%s", r == 0 && mark ? mark : g->rows[r]);
  return g->w;
}

static unsigned NextCode(const char **s) {
  const unsigned char *p = (const unsigned char *)*s;
  if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
    *s += 2;
    return (p[0] & 0x1F) << 6 | (p[1] & 0x3F);
  }
  *s += 1;
  return p[0];
}

static int TextWidth(const char *s, int n) {
  const char *end = s + n;
  int w = 0, chars = 0;
  while (s < end) {
    char rows[8][6];
    const int gw = GlyphRows(NextCode(&s), rows);
    if (gw < 0) w += 3;
    else if (gw > 0) w += (chars++ ? 1 : 0) + gw;
  }
  return w;
}

// ---- VRAM and the characters borrowed ------------------------------------------------------

static Ppu *VramPpu(void) { return g_snes ? g_snes->ppu : NULL; }

static void VramPut(uint16_t adr, uint16_t v) {
  Ppu *ppu = VramPpu();
  adr &= 0x7fff;
  if (ppu->vram[adr] == v) return;
  ppu->vram[adr] = v;
  if (g_ppu_vram_dirty) g_ppu_vram_dirty[adr >> 3] = 1;
}

// BG3's chars as the room has them (0x4000 in most rooms; Kraid's has them at 0x2000 and his BG2 tilemap at 0x4000).
static uint16_t CharBase(void) {
  const Ppu *ppu = VramPpu();
  return ppu ? ppu->bgLayer[2].tileAdr : 0x4000;
}

enum { kMaxBorrowed = kPoolLast - kPoolFirst + 1 };
static uint16_t g_saved[kMaxBorrowed][8];
static uint8_t g_saved_char[kMaxBorrowed];
static int g_saved_count;
static uint16_t g_saved_base;   // where the borrowed chars were taken from (the room may change before they go back)

static void RestoreChars(void) {
  for (int i = 0; i < g_saved_count; i++)
    for (int y = 0; y < 8; y++) VramPut((uint16_t)(g_saved_base + g_saved_char[i] * 8 + y), g_saved[i][y]);
  g_saved_count = 0;
}

void GameText_Forget(void) {
  g_saved_count = 0;
  GameTextScreens_Forget();
}

// ---- Building a box -------------------------------------------------------------------------

enum { kMaxRows = 6, kMaxNew = 48 };
typedef struct {
  uint16_t map[kMaxRows][32];      // the box's rows (0 = top border), as they will be in VRAM
  int16_t new_tile[kMaxRows][32];  // index into tiles, or -1: the cell gets a new char
  Tile tiles[kMaxNew];
  int tile_count;
  int rows;                        // message rows (1..4), the border rows not counted
  int x0, x1;                      // the box's columns: [x0, x1)
} Box;

static int NewTile(Box *b, const Tile *t) {
  for (int i = 0; i < b->tile_count; i++)
    if (!memcmp(&b->tiles[i], t, sizeof(*t))) return i;
  if (b->tile_count >= kMaxNew) return -1;
  b->tiles[b->tile_count] = *t;
  return b->tile_count++;
}

static void PutNewTile(Box *b, int row, int col, const Tile *t, uint16_t attr) {
  const int i = NewTile(b, t);
  if (i < 0) return;
  b->new_tile[row][col] = (int16_t)i;
  b->map[row][col] = attr;   // the char is filled in when chars are handed out
}

static int CellChar(uint16_t e) { return e & 0x3ff; }

static bool IsLetterCell(uint16_t e) {
  const int c = CellChar(e);
  return c >= kTileA && c <= 0xFE;
}

// The attribute (palette, priority) of the first letter on box row `row`, else of its fill.
static uint16_t RowAttr(const Box *b, int row) {
  for (int x = b->x0; x < b->x1; x++)
    if (IsLetterCell(b->map[row][x])) return b->map[row][x] & 0xfc00;
  return b->map[row][b->x0] & 0xfc00;
}

static Mark MarkOf(unsigned code, char *base) {
  static const struct { uint16_t code; char base; Mark mark; } kMarks[] = {
    { 0xC1, 'A', kMarkAcute }, { 0xC0, 'A', kMarkGrave }, { 0xC2, 'A', kMarkCirc }, { 0xC3, 'A', kMarkTilde },
    { 0xC4, 'A', kMarkDiaer }, { 0xC9, 'E', kMarkAcute }, { 0xC8, 'E', kMarkGrave }, { 0xCA, 'E', kMarkCirc },
    { 0xCB, 'E', kMarkDiaer }, { 0xCD, 'I', kMarkAcute }, { 0xCC, 'I', kMarkGrave }, { 0xCE, 'I', kMarkCirc },
    { 0xCF, 'I', kMarkDiaer }, { 0xD3, 'O', kMarkAcute }, { 0xD2, 'O', kMarkGrave }, { 0xD4, 'O', kMarkCirc },
    { 0xD5, 'O', kMarkTilde }, { 0xD6, 'O', kMarkDiaer }, { 0xDA, 'U', kMarkAcute }, { 0xD9, 'U', kMarkGrave },
    { 0xDB, 'U', kMarkCirc }, { 0xDC, 'U', kMarkDiaer }, { 0xD1, 'N', kMarkTilde }, { 0xC7, 'C', kMarkCedilla },
  };
  if (code >= 0xE0 && code <= 0xFE && code != 0xF7) code -= 0x20;
  for (size_t i = 0; i < sizeof(kMarks) / sizeof(kMarks[0]); i++)
    if (kMarks[i].code == code) {
      *base = kMarks[i].base;
      return kMarks[i].mark;
    }
  *base = (char)(code >= 'a' && code <= 'z' ? code - 32 : code);
  return kMarkNone;
}

static int Utf8Len(const char *s) {
  int n = 0;
  while (*s) NextCode(&s), n++;
  return n;
}

// Writes capitals `text` on box row `row` from column `x`, with `attr`.
static void PutCapitals(Box *b, int row, int x, const char *text, uint16_t attr) {
  for (const char *s = text; *s && x < b->x1; x++) {
    char base;
    const Mark mark = MarkOf(NextCode(&s), &base);
    uint16_t e = attr | kTileFill;
    if (base >= 'A' && base <= 'Z') e = attr | (kTileA + base - 'A');
    else if (base == '.') e = attr | kTileDot;
    else if (base == '?') e = attr | kTileQuestion;
    else if (base == '-') e = attr | kTileDash;
    else if ((unsigned char)base == 0xBF) e = (attr ^ kFlipXY) | kTileQuestion;   // ¿: ? turned round
    else if (base == '\'') {
      Tile t;
      ApostropheTile(&t);
      PutNewTile(b, row, x, &t, attr);
      continue;
    }
    b->map[row][x] = e;
    if (mark != kMarkNone) {
      const int r = mark == kMarkCedilla ? row + 1 : row - 1;
      Tile t;
      MarkTile(&t, mark);
      PutNewTile(b, r, x, &t, attr);
    }
  }
}

static void ClearRow(Box *b, int row, uint16_t attr) {
  for (int x = b->x0; x < b->x1; x++) b->map[row][x] = attr | kTileFill, b->new_tile[row][x] = -1;
}

// A capitals row: centred like the game's, or from where the English text started.
static void TitleRow(Box *b, int row, const char *text, bool left) {
  int start = b->x0;
  while (start < b->x1 && !IsLetterCell(b->map[row][start])) start++;
  const uint16_t attr = RowAttr(b, row);
  ClearRow(b, row, b->map[row][b->x0] & 0xfc00);
  const int n = Utf8Len(text), w = b->x1 - b->x0;
  const int x = left && start < b->x1 ? start : b->x0 + (w - n > 0 ? (w - n + 1) / 2 : 0);
  PutCapitals(b, row, x, text, attr);
}

// The save prompt's choices: the English YES and NO are replaced where they are (the
// cursor stays put), up to 3 letters each.
static void ChoicesRow(Box *b, int row, const char *yes, const char *no) {
  int words[2][2], n = 0;   // [start, end) of the two words
  for (int x = b->x0; x < b->x1 && n < 2;) {
    if (!IsLetterCell(b->map[row][x])) { x++; continue; }
    int e = x;
    while (e < b->x1 && IsLetterCell(b->map[row][e])) e++;
    words[n][0] = x, words[n][1] = e, n++;
    x = e;
  }
  if (n < 2) return;
  for (int k = 0; k < 2; k++) {
    const uint16_t attr = b->map[row][words[k][0]] & 0xfc00;
    for (int x = words[k][0]; x < words[k][1]; x++) b->map[row][x] = (b->map[row][x] & 0xfc00) | kTileFill;
    PutCapitals(b, row, words[k][0], k ? no : yes, attr);
  }
}

// The instruction line: the English pictures (icon, button...) are columns kept whole over
// the rows above too; the words around them are drawn anew, everything centred.
static void InstructionRow(Box *b, int row, const char *format) {
  typedef struct { int x0, x1; } Block;
  Block blocks[6];
  int nb = 0;
  const int first = 2;   // the pictures may reach up to the row under the title (Samus)
  for (int x = b->x0; x < b->x1;) {
    bool fixed = false;
    for (int r = first; r <= row; r++) fixed |= FixedChar(CellChar(b->map[r][x])) && CellChar(b->map[r][x]) != kTileDash;
    // The button: the one capital on the line.
    if (!fixed && IsLetterCell(b->map[row][x])) fixed = true;
    if (!fixed) { x++; continue; }
    int e = x;
    for (;;) {
      bool f = false;
      if (e < b->x1) {
        for (int r = first; r <= row; r++) f |= FixedChar(CellChar(b->map[r][e])) && CellChar(b->map[r][e]) != kTileDash;
        f |= IsLetterCell(b->map[row][e]);
      }
      if (!f) break;
      e++;
    }
    if (nb < 6) blocks[nb].x0 = x, blocks[nb].x1 = e, nb++;
    x = e;
  }
  // The English rows, to copy the pictures from once the rows are cleared.
  uint16_t wide[kMaxRows][32];
  memcpy(wide, b->map, sizeof(wide));
  const uint16_t text_attr = 0x2800;   // palette 2: colour 2 is the white of the game's words
  for (int r = first; r <= row; r++) ClearRow(b, r, b->map[r][b->x0] & 0xfc00);
  // Items: text, block, text, block, ..., text.
  const char *parts[8];
  int part_len[8], np = 0;
  for (const char *s = format;;) {
    const char *h = strstr(s, "{}");
    parts[np] = s;
    part_len[np] = h ? (int)(h - s) : (int)strlen(s);
    np++;
    if (!h || np >= 7) break;
    s = h + 2;
  }
  int cells = 0;
  for (int i = 0; i < np; i++) {
    cells += (TextWidth(parts[i], part_len[i]) + 7) / 8;
    if (i < nb && i < np - 1) cells += blocks[i].x1 - blocks[i].x0;
  }
  int x = b->x0 + ((b->x1 - b->x0) - cells > 0 ? ((b->x1 - b->x0) - cells) / 2 : 0);
  for (int i = 0; i < np; i++) {
    // The text, drawn into its own run of cells.
    const int w = TextWidth(parts[i], part_len[i]), n = (w + 7) / 8;
    if (n > 0 && x + n <= b->x1) {
      Tile run[32];
      for (int k = 0; k < n; k++) TileFill(&run[k], 3);
      // Snug against the pictures: a text before one ends at its right edge, one after it
      // starts at its left, one between two is centred in its cells.
      const bool before = i < nb && i < np - 1, after = i > 0;
      int px = before && after ? (n * 8 - w) / 2 : before ? n * 8 - w : 0, chars = 0;
      for (const char *s = parts[i]; s < parts[i] + part_len[i];) {
        char rows[8][6];
        const int gw = GlyphRows(NextCode(&s), rows);
        if (gw < 0) { px += 3; continue; }
        if (!gw) continue;
        px += chars++ ? 1 : 0;
        for (int yy = 0; yy < 8; yy++)
          for (int xx = 0; xx < gw; xx++)
            if (rows[yy][xx] == '#' && (px + xx) / 8 < n) run[(px + xx) / 8].px[yy][(px + xx) % 8] = 2;
        px += gw;
      }
      for (int k = 0; k < n; k++) PutNewTile(b, row, x + k, &run[k], text_attr);
    }
    x += n;
    if (i < nb && i < np - 1) {
      const Block *k = &blocks[i];
      for (int c = k->x0; c < k->x1 && x < b->x1; c++, x++)
        for (int r = first; r <= row; r++) b->map[r][x] = wide[r][c];
    }
  }
}

// Columns a text of `n` capitals needs with a cell of margin on each side.
static void Widen(Box *b, int need) {
  while (b->x1 - b->x0 < need && (b->x0 > 1 || b->x1 < 31)) {
    const bool left = (b->x1 - b->x0) % 2 ? b->x1 >= 31 : b->x0 > 1;
    const int from = left ? b->x0 : b->x1 - 1, to = left ? b->x0 - 1 : b->x1;
    for (int r = 0; r < b->rows + 2; r++) b->map[r][to] = b->map[r][from], b->new_tile[r][to] = -1;
    if (left) b->x0--;
    else b->x1++;
  }
}

static void Build(Box *b, const MsgText *m, int index) {
  // A longer translation widens the box (its background is tiles like any other).
  for (int r = 0; r < 3 && r < b->rows; r++)
    if (m->row[r]) Widen(b, Utf8Len(m->row[r]) + 2);
  for (int r = 0; r < 3 && r < b->rows; r++) {
    if (!m->row[r]) continue;
    TitleRow(b, 1 + r, m->row[r], index == 23 || index == 28);
  }
  if (m->yes && b->rows >= 4) ChoicesRow(b, 4, m->yes, m->no);
  if (m->instr && b->rows >= 4) InstructionRow(b, 4, m->instr);
}

// The box is in VRAM in English (from the game's own buffer): translate it.
static void Translate(void) {
  RestoreChars();
  const int index = message_box_index;
  MsgText text;
  if (!MessageText(index, &text)) return;
  const MsgText *m = &text;
  Ppu *ppu = VramPpu();
  if (!ppu) return;
  static Box b;
  memset(&b, 0, sizeof(b));
  memset(b.new_tile, 0xff, sizeof(b.new_tile));
  b.rows = (message_box_das0l_value - 128) / 64;
  if (b.rows < 1 || b.rows > kMaxRows - 2) return;
  const uint16_t vram = g_rtl_message_box_vram;
  for (int r = 0; r < b.rows + 2; r++)
    for (int x = 0; x < 32; x++) b.map[r][x] = ram3000.pause_menu_map_tilemap[256 + r * 32 + x];
  b.x0 = 0;
  while (b.x0 < 32 && CellChar(b.map[1][b.x0]) == kTileOutside) b.x0++;
  b.x1 = 32;
  while (b.x1 > b.x0 && CellChar(b.map[1][b.x1 - 1]) == kTileOutside) b.x1--;
  if (b.x1 - b.x0 < 4) return;
  Build(&b, m, index);
  // Hand out characters for the new tiles: pool chars nothing else shown refers to.
  bool used[0x400] = { false };
  for (int i = 0; i < kHudCells; i++) used[CellChar(ppu->vram[kHudMap + i])] = true;
  for (int r = 0; r < b.rows + 2; r++)
    for (int x = 0; x < 32; x++)
      if (b.new_tile[r][x] < 0) used[CellChar(b.map[r][x])] = true;
  int chr_of[kMaxNew], next = kPoolFirst;
  const uint16_t char_base = CharBase();
  if (g_saved_count && g_saved_base != char_base) RestoreChars();
  g_saved_base = char_base;
  for (int i = 0; i < b.tile_count; i++) {
    while (next <= kPoolLast && (used[next] || FixedChar(next))) next++;
    chr_of[i] = next <= kPoolLast ? next++ : -1;
    if (chr_of[i] < 0) continue;
    uint16_t data[8];
    TileToChar(&b.tiles[i], data);
    for (int y = 0; y < 8; y++) g_saved[g_saved_count][y] = ppu->vram[(char_base + chr_of[i] * 8 + y) & 0x7fff];
    g_saved_char[g_saved_count++] = (uint8_t)chr_of[i];
    for (int y = 0; y < 8; y++) VramPut((uint16_t)(char_base + chr_of[i] * 8 + y), data[y]);
  }
  for (int r = 0; r < b.rows + 2; r++)
    for (int x = 0; x < 32; x++) {
      uint16_t e = b.map[r][x];
      if (b.new_tile[r][x] >= 0) {
        const int c = chr_of[b.new_tile[r][x]];
        e = c < 0 ? (uint16_t)((e & 0xfc00) | kTileFill) : (uint16_t)((e & 0xfc00) | c);
      }
      VramPut((uint16_t)(vram + r * 32 + x), e);
    }
}

static void Hook(int shown) {
  if (shown) Translate();
  else RestoreChars();
}

void GameText_Init(void) {
  g_rtl_message_box_hook = Hook;
  GameTextScreens_Init();
}
