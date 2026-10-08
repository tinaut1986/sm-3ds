#include "sm_map.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "src/types.h"
#include "src/sm_rtl.h"
#include "src/funcs.h"
#include "src/variables.h"

enum { kMaxRooms = 320 };

static SmRoom g_rooms[kMaxRooms];
static int g_room_count;
static bool g_scanned;

// ROM addresses: the pause-menu map tilemap table (3-byte pointers, one per area).
#define kPauseMenuMapTilemaps 0x82964A
// Pause-screen map tile graphics (4bpp, loaded to BG1's character base) and palettes.
#define kPauseScreenTiles 0xB68000
#define kPauseScreenPalettes 0xB6F000

static uint16_t Word(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

// The first word after a room header is the first room-state condition; these are
// the condition routines the game uses, which tells a real header from noise.
static bool IsStateCondition(uint16_t w) {
  switch (w) {
  case 0xE5E6: case 0xE5EB: case 0xE5FF: case 0xE612: case 0xE629:
  case 0xE640: case 0xE652: case 0xE669: case 0xE676:
    return true;
  }
  return false;
}

void SmMap_Init(void) {
  if (g_scanned) return;
  g_scanned = true;
  g_room_count = 0;
  // Room headers are 11 bytes (RoomDefHeader) followed by the state list. Scan the
  // whole bank for that shape; 262 headers come out, matching the game's rooms.
  for (uint32_t a = 0x8000; a + 13 <= 0x10000 && g_room_count < kMaxRooms; a++) {
    const uint8_t *h = RomPtr(0x8F0000 | a);
    const uint8_t area = h[1], x = h[2], y = h[3], w = h[4], hh = h[5];
    const uint16_t doors = Word(h + 9);
    if (!IsStateCondition(Word(h + 11)) || area >= kSmAreaCount || w < 1 || hh < 1 || w > 25 || hh > 25 ||
        x + w > kSmMapCols || y + hh > kSmMapRows || doors < 0x8000)
      continue;
    g_rooms[g_room_count++] = (SmRoom){ (uint16_t)a, doors, area, x, y, w, hh };
  }
}

const SmRoom *SmMap_Rooms(int *count) {
  SmMap_Init();
  if (count) *count = g_room_count;
  return g_rooms;
}

static const uint16_t *Tilemap(int area) {
  // RomFixedPtr does not parenthesise its argument, so pass it a plain variable.
  const uint32_t entry = kPauseMenuMapTilemaps + 3 * area;
  const uint8_t *p = RomFixedPtr(entry);
  return (const uint16_t *)RomPtr((uint32_t)(p[2] << 16 | p[1] << 8 | p[0]));
}

// Cell order in both the tilemap and the explored bitmap: two 32-column screens.
static int CellIndex(int col, int row) { return (col >= 32 ? 1024 : 0) + row * 32 + (col & 31); }

// The explored bitmap of an area: live for the current one, the saved copy otherwise.
static uint8_t *ExploredBits(int area) {
  if (area == (int)area_index) return map_tiles_explored;
  if (area >= 0 && area < 6) return (uint8_t *)explored_map_tiles_saved + area * 256;
  return NULL;
}

uint32_t SmMap_AreaKey(int area) {
  uint32_t h = 2166136261u;
  const uint8_t *bits = ExploredBits(area);
  for (int i = 0; bits && i < 256; i++) h = (h ^ bits[i]) * 16777619u;
  if (area >= 0 && area < 6) h = (h ^ boss_bits_for_area[area]) * 16777619u;
  return (h ^ (SmMap_HasMapStation(area) ? 1u : 0u)) * 16777619u;
}

bool SmMap_Cell(int area, int col, int row, bool *exists, bool *explored) {
  if (area < 0 || area >= kSmAreaCount || col < 0 || col >= kSmMapCols || row < 0 || row >= kSmMapRows) return false;
  const int i = CellIndex(col, row);
  // Tile 0x1F is the blank tile the game uses for "no map here".
  if (exists) *exists = (Tilemap(area)[i] & 0x3FF) != 0x1F;
  if (explored) {
    // Ceres has no saved copy.
    const uint8_t *bits = ExploredBits(area);
    *explored = bits && (bits[i >> 3] & (0x80 >> (i & 7))) != 0;
  }
  return true;
}

uint16_t SmMap_CellTile(int area, int col, int row, bool explored) {
  if (area < 0 || area >= kSmAreaCount || col < 0 || col >= kSmMapCols || row < 0 || row >= kSmMapRows) return kSmMapBlankTile;
  const uint16_t t = Tilemap(area)[CellIndex(col, row)];
  // The game clears the palette's low bit on explored cells (LoadPauseMenuMapTilemap).
  return explored ? (uint16_t)(t & ~0x400) : t;
}

void SmMap_TilePixels(uint16_t tile, uint16_t bgr555[64]) {
  const uint8_t *gfx = RomPtr(kPauseScreenTiles + (tile & 0x3FF) * 32);
  const uint16_t *pal = (const uint16_t *)RomPtr(kPauseScreenPalettes + ((tile >> 10) & 7) * 32);
  for (int y = 0; y < 8; y++) {
    const uint8_t *r = gfx + y * 2;  // bitplanes 0/1 at +0/+1, planes 2/3 at +16/+17
    for (int x = 0; x < 8; x++) {
      const int bit = 7 - x;
      const int c = (r[0] >> bit & 1) | (r[1] >> bit & 1) << 1 | (r[16] >> bit & 1) << 2 | (r[17] >> bit & 1) << 3;
      const int dx = (tile & 0x4000) ? 7 - x : x, dy = (tile & 0x8000) ? 7 - y : y;
      bgr555[dy * 8 + dx] = c ? (uint16_t)(pal[c] | 0x8000) : 0;  // bit 15 = opaque
    }
  }
}

// ---- Sprites over the map ----------------------------------------------------------
// The pause map draws its icons (refills, map station, bosses, the ship) and the labels
// beside the elevator arrows as menu sprites: a table of spritemaps in bank $82 whose
// tiles are the pause screen's object tiles ($B6C000, 4bpp, 16 tiles a row) and whose
// colours are the object palettes (colour 128 on) of the pause palette block.

#define kMenuSpritemaps 0x82C569
#define kMenuSpriteTiles 0xB6C000
#define kMapIconTables 0x82C7CB     // 8 groups of 8 area pointers, see SmMap_Icons
#define kMapElevatorDests 0x82C74D  // per area: (x, y, spritemap) words, ended by 0xFFFF
#define kMapShipX 0xD8              // the ship's spot on Crateria's map (kMap_Criteria_SavePoints)
#define kMapShipY 0x28

static int Pixel4bpp(const uint8_t *tile, int x, int y) {
  const uint8_t *r = tile + y * 2;
  const int bit = 7 - x;
  return (r[0] >> bit & 1) | (r[1] >> bit & 1) << 1 | (r[16] >> bit & 1) << 2 | (r[17] >> bit & 1) << 3;
}

bool SmMap_RenderSprite(int id, uint16_t chr, SmMapSprite *out) {
  const uint32_t table_entry = kMenuSpritemaps + 2 * id, bank = 0x820000;
  const uint8_t *pp = RomPtr(bank | Word(RomPtr(table_entry)));
  const int n = Word(pp);
  pp += 2;
  enum { kMaxEntries = 32 };
  struct { int x, y, size, chr; } e[kMaxEntries];
  if (n > kMaxEntries) return false;
  int x0 = 1 << 20, y0 = 1 << 20, x1 = -(1 << 20), y1 = -(1 << 20);
  for (int i = 0; i < n; i++, pp += 5) {
    const int w = Word(pp);
    e[i].x = (w & 0x1FF) - ((w & 0x100) ? 0x200 : 0);   // 9-bit signed
    e[i].size = (w & 0x8000) ? 16 : 8;
    e[i].y = (int8_t)pp[2];
    e[i].chr = Word(pp + 3);
    if (e[i].x < x0) x0 = e[i].x;
    if (e[i].y < y0) y0 = e[i].y;
    if (e[i].x + e[i].size > x1) x1 = e[i].x + e[i].size;
    if (e[i].y + e[i].size > y1) y1 = e[i].y + e[i].size;
  }
  if (n == 0 || x1 - x0 > kSmSpriteMaxW || y1 - y0 > kSmSpriteMaxH) return false;
  out->x0 = x0;
  out->y0 = y0;
  out->w = x1 - x0;
  out->h = y1 - y0;
  memset(out->px, 0, sizeof(out->px));
  // OAM entry 0 is the one on top, so draw from the last one back.
  for (int i = n - 1; i >= 0; i--) {
    const int c = e[i].chr | chr, tile = c & 0x1FF, pal = (c >> 9) & 7, size = e[i].size;
    if (tile >= 0x100) continue;   // second name table: not loaded in the pause screen
    for (int y = 0; y < size; y++) {
      for (int x = 0; x < size; x++) {
        const int sx = (c & 0x4000) ? size - 1 - x : x, sy = (c & 0x8000) ? size - 1 - y : y;
        const uint8_t *gfx = RomPtr(kMenuSpriteTiles + (tile + (sy >> 3) * 16 + (sx >> 3)) * 32);
        const int ci = Pixel4bpp(gfx, sx & 7, sy & 7);
        if (!ci) continue;
        const uint16_t col = Word(RomPtr(kPauseScreenPalettes + (128 + pal * 16 + ci) * 2));
        out->px[(e[i].y - y0 + y) * kSmSpriteMaxW + (e[i].x - x0 + x)] = col | 0x8000;
      }
    }
  }
  return true;
}

int SmMap_Icons(int area, SmMapIcon *out, int max) {
  int n = 0;
  if (area < 0 || area >= 6) return 0;   // Ceres has none
  const uint32_t bank = 0x820000;
#define ADD(px, py, spr, bits) do { if (n < max) out[n++] = (SmMapIcon){ (px), (py), (spr), (bits) }; } while (0)

  // Bosses: group 0, one bit of boss_bits_for_area per entry (a 0xFFFE entry only uses up
  // its bit). A defeated one is crossed out and shows always; the others only once the
  // area's map station was used.
  {
    const uint32_t entry = kMapIconTables + 2 * area;
    const uint16_t p = Word(RomPtr(entry));
    unsigned bits = boss_bits_for_area[area];
    for (const uint8_t *l = p ? RomPtr(bank | p) : NULL; l && Word(l) != 0xFFFF; l += 4, bits >>= 1) {
      if (Word(l) == 0xFFFE) continue;
      if (bits & 1) {
        ADD(Word(l), Word(l + 2), 0x62, 3584);
        ADD(Word(l), Word(l + 2), 9, 3072);
      } else if (SmMap_HasMapStation(area)) {
        ADD(Word(l), Word(l + 2), 9, 3584);
      }
    }
  }
  // Refills (groups 1 and 2) and the map station (group 3): once the cell under them is explored.
  static const struct { int group, sprite; } kPlain[] = { { 1, 0xB }, { 2, 0xA }, { 3, 0x4E } };
  for (unsigned g = 0; g < sizeof(kPlain) / sizeof(kPlain[0]); g++) {
    const uint32_t entry = kMapIconTables + kPlain[g].group * 0x10 + 2 * area;
    const uint16_t p = Word(RomPtr(entry));
    for (const uint8_t *l = p ? RomPtr(bank | p) : NULL; l && !(l[1] & 0x80); l += 4) {
      bool explored = false;
      SmMap_Cell(area, Word(l) / 8, Word(l + 2) / 8, NULL, &explored);
      if (explored) ADD(Word(l), Word(l + 2), kPlain[g].sprite, 3584);
    }
  }
  if (area == 0) ADD(kMapShipX, kMapShipY, 0x63, 3584);   // Samus' ship
  // The names beside the arrows that leave the area, with the map station only.
  if (SmMap_HasMapStation(area)) {
    const uint32_t entry = kMapElevatorDests + 2 * area;
    for (const uint8_t *l = RomPtr(bank | Word(RomPtr(entry))); Word(l) != 0xFFFF; l += 6)
      ADD(Word(l), Word(l + 2), Word(l + 4), 0);
  }
#undef ADD
  return n;
}

bool SmMap_HasMapStation(int area) {
  return area >= 0 && area < 6 && map_station_byte_array[area] != 0;
}

// A room's header gives a rectangle of screens, but the room is often not a rectangle:
// the screens it does not use are full of solid blocks (and the map leaves them blank),
// so the rectangles of neighbouring rooms overlap a lot. Which screens a room really has
// is read from its level data, once, the first time it is asked.
static uint8_t g_real[kMaxRooms][80];       // bit per screen (w * h <= 625): not all solid
static uint8_t g_real_state[kMaxRooms];     // 0 not read yet, 1 read, 2 could not be read

static bool ScreenIsReal(const SmRoom *r, int sx, int sy) {
  const int idx = (int)(r - g_rooms);
  if (g_real_state[idx] == 0) {
    g_real_state[idx] = 2;
    // The default room state (condition 0xE5E6) follows the other conditions of the
    // header; its first field is the level data, compressed.
    const uint8_t *h = RomPtr(0x8F0000 | r->header) + 11;
    int k = 0;
    while (k < 80 && Word(h + k) != 0xE5E6) k++;
    const int blocks = r->w * r->h * 256;
    // The level data comes out of the decompressor with no size limit: a room's layer 1, its BTS and
    // its layer 2 (a custom background) go to WRAM bank $7F, 64 KB, so that is the buffer. A size
    // worked out from the room's width and height (5 bytes a block) was too small for Norfair's ADAD
    // and the Wrecked Ship's C98E (their data is 1.5 times that) and corrupted the heap: the console
    // crashed in free() when the map was tapped in those areas. tools/map-test checks every room.
    static uint8_t level_buf[kSmMapLevelBytes];
    uint8_t *d = k < 80 ? level_buf : NULL;
    if (d) {
      const uint32_t src = h[k + 2] | h[k + 3] << 8 | h[k + 4] << 16;
      DecompressToMem(src, d);
      // Layer 1: a size word, then one word a block (type in the top nibble, 8 = solid).
      const int bw = r->w * 16;
      for (int s = 0; s < r->w * r->h; s++) {
        bool solid = true;
        for (int by = 0; by < 16 && solid; by++)
          for (int bx = 0; bx < 16 && solid; bx++) {
            const int b = ((s / r->w) * 16 + by) * bw + (s % r->w) * 16 + bx;
            if (b < blocks && (d[2 + 2 * b + 1] >> 4) != 8) solid = false;
          }
        if (!solid) g_real[idx][s >> 3] |= 1 << (s & 7);
      }
      g_real_state[idx] = 1;
    }
  }
  if (g_real_state[idx] != 1) return true;   // unknown: do not rule it out
  const int s = sy * r->w + sx;
  return (g_real[idx][s >> 3] >> (s & 7)) & 1;
}

const SmRoom *SmMap_RoomAt(int area, int col, int row) {
  SmMap_Init();
  // Of the rooms whose rectangle holds the cell, one that really has that screen wins; the
  // smaller rectangle breaks ties (a room drawn inside another one). Only if none has it
  // does a plain rectangle count.
  const SmRoom *real = NULL, *boxed = NULL;
  for (int i = 0; i < g_room_count; i++) {
    const SmRoom *r = &g_rooms[i];
    if (r->area != area || col < r->x || col >= r->x + r->w || row < r->y + 1 || row >= r->y + 1 + r->h) continue;
    const SmRoom **slot = ScreenIsReal(r, col - r->x, row - r->y - 1) ? &real : &boxed;
    if (!*slot || r->w * r->h < (*slot)->w * (*slot)->h) *slot = r;
  }
  return real ? real : boxed;
}

bool SmMap_RoomOwnsCell(const SmRoom *room, int col, int row) {
  if (!room || col < room->x || col >= room->x + room->w || row < room->y + 1 || row >= room->y + 1 + room->h) return false;
  bool exists = false;
  if (!SmMap_Cell(room->area, col, row, &exists, NULL) || !exists) return false;
  return SmMap_RoomAt(room->area, col, row) == room;
}

const SmRoom *SmMap_CurrentRoom(void) {
  SmMap_Init();
  for (int i = 0; i < g_room_count; i++)
    if (g_rooms[i].header == room_ptr && g_rooms[i].area == area_index) return &g_rooms[i];
  return NULL;
}

bool SmMap_SamusCell(int *area, int *col, int *row) {
  if (game_state < 0x08 || game_state > 0x12) return false;
  const SmRoom *r = SmMap_CurrentRoom();
  if (!r) return false;
  int c = r->x + (samus_x_pos >> 8), rr = r->y + 1 + (samus_y_pos >> 8);
  if (c >= r->x + r->w) c = r->x + r->w - 1;
  if (rr >= r->y + 1 + r->h) rr = r->y + r->h;
  if (area) *area = area_index;
  if (col) *col = c;
  if (row) *row = rr;
  return true;
}

// ---- Debug map unlock ------------------------------------------------------------

enum { kStationAreas = 6 };
static SmMapDebugState g_debug_state[kStationAreas];
static uint8_t g_real_station[kStationAreas];
static uint8_t g_real_bits[kStationAreas][256];

SmMapDebugState SmMap_DebugState(int area) {
  return area >= 0 && area < kStationAreas ? g_debug_state[area] : kSmMapDebug_Real;
}

void SmMap_DebugCycle(int area) {
  uint8_t *bits = ExploredBits(area);
  if (area < 0 || area >= kStationAreas || !bits) return;
  switch (g_debug_state[area]) {
  case kSmMapDebug_Real:
    g_real_station[area] = map_station_byte_array[area];
    memcpy(g_real_bits[area], bits, 256);
    map_station_byte_array[area] = 1;
    g_debug_state[area] = kSmMapDebug_Station;
    break;
  case kSmMapDebug_Station:
    for (int row = 0; row < kSmMapRows; row++) {
      for (int col = 0; col < kSmMapCols; col++) {
        bool exists;
        SmMap_Cell(area, col, row, &exists, NULL);
        const int i = CellIndex(col, row);
        if (exists) bits[i >> 3] |= 0x80 >> (i & 7);
      }
    }
    g_debug_state[area] = kSmMapDebug_Explored;
    break;
  default:
    map_station_byte_array[area] = g_real_station[area];
    memcpy(bits, g_real_bits[area], 256);
    g_debug_state[area] = kSmMapDebug_Real;
    break;
  }
}

void SmMap_DebugForget(void) {
  memset(g_debug_state, 0, sizeof(g_debug_state));
}
