#include "sm_map.h"

#include <stddef.h>
#include <string.h>

#include "src/types.h"
#include "src/sm_rtl.h"
#include "src/variables.h"

enum { kMaxRooms = 320 };

static SmRoom g_rooms[kMaxRooms];
static int g_room_count;
static bool g_scanned;

// ROM addresses: the pause-menu map tilemap table (3-byte pointers, one per area).
#define kPauseMenuMapTilemaps 0x82964A

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

bool SmMap_HasMapStation(int area) {
  return area >= 0 && area < 6 && map_station_byte_array[area] != 0;
}

const SmRoom *SmMap_RoomAt(int area, int col, int row) {
  SmMap_Init();
  const SmRoom *best = NULL;
  for (int i = 0; i < g_room_count; i++) {
    const SmRoom *r = &g_rooms[i];
    if (r->area != area || col < r->x || col >= r->x + r->w || row < r->y + 1 || row >= r->y + 1 + r->h) continue;
    // Headers can overlap on the map (rooms drawn over one another); prefer the
    // smaller room, which is the more specific one.
    if (!best || r->w * r->h < best->w * best->h) best = r;
  }
  return best;
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
