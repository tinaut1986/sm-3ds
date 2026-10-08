// Super Metroid map and room data, read from the ROM and the game's RAM.
//
// Map cells use the pause-screen tilemap coordinates: 64 columns by 32 rows per
// area, where row 0 is an empty margin. A room header's map y is the row minus 1.
// The explored bitmap uses the same cell order (two 32-column screens, row by row).
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum { kSmMapCols = 64, kSmMapRows = 32, kSmAreaCount = 7 /* 0-5 plus Ceres (6) */ };

typedef struct {
  uint16_t header;      // pointer of the room header in bank $8F
  uint16_t door_list;   // pointer of its list of door definitions in bank $8F
  uint8_t area;
  uint8_t x, y;         // on the map, as stored in the header (map row = y + 1)
  uint8_t w, h;         // in screens / map cells
} SmRoom;

// Scans the room headers in the ROM once. Call after the ROM is loaded.
// The most a room's decompressed level data takes (the game's own buffer: WRAM bank $7F).
enum { kSmMapLevelBytes = 0x10000 };

void SmMap_Init(void);
const SmRoom *SmMap_Rooms(int *count);

// Cell state. `exists` means the area's map draws something there; `explored` is
// the player's own bit. Returns false for out-of-range coordinates.
bool SmMap_Cell(int area, int col, int row, bool *exists, bool *explored);

// The pause-map tilemap entry of a cell, as the game shows it (explored cells use
// the explored palette). kSmMapBlankTile means nothing is drawn there.
enum { kSmMapBlankTile = 0x1F };
uint16_t SmMap_CellTile(int area, int col, int row, bool explored);

// Decodes a tilemap entry's 8x8 tile (flips applied) to BGR555 with bit 15 set;
// 0 = transparent.
void SmMap_TilePixels(uint16_t tile, uint16_t bgr555[64]);

// What the game's pause map draws as sprites over the tiles: refills, the map station,
// bosses (a defeated one crossed out), the ship and the names beside the arrows that
// leave the area. Save stations and items are tiles already. Positions are in the map's
// own pixels (8 per cell, row 0 the empty margin); `sprite` is a menu spritemap and
// `chr` the attribute bits the game ORs into its tiles (palette). Listed in the game's
// order: an earlier one is drawn on top of a later one. Returns how many were filled in.
typedef struct { uint16_t x, y; uint16_t sprite, chr; } SmMapIcon;
enum { kSmMapMaxIcons = 32 };
int SmMap_Icons(int area, SmMapIcon *out, int max);

// A spritemap rendered to BGR555 with bit 15 set (0 = transparent). `x0, y0` is where the
// image's top-left corner lies relative to the icon's position. False if it does not fit.
enum { kSmSpriteMaxW = 96, kSmSpriteMaxH = 32 };
typedef struct { int x0, y0, w, h; uint16_t px[kSmSpriteMaxW * kSmSpriteMaxH]; } SmMapSprite;
bool SmMap_RenderSprite(int sprite, uint16_t chr, SmMapSprite *out);

// Whether a cell belongs to `room` as drawn: inside its box, a cell the map draws, and
// not one of a smaller room whose box overlaps. The box alone also covers blank cells.
bool SmMap_RoomOwnsCell(const SmRoom *room, int col, int row);

// True if the area's map station has been used, so every existing cell shows.
bool SmMap_HasMapStation(int area);

// Where Samus is, in map cells of the current area. False outside of gameplay.
bool SmMap_SamusCell(int *area, int *col, int *row);

// The room covering a cell, or NULL.
const SmRoom *SmMap_RoomAt(int area, int col, int row);

// The room Samus is in now, or NULL.
const SmRoom *SmMap_CurrentRoom(void);

// --- Debug: unlock an area's map -------------------------------------------------
// Areas 0-5 (Ceres has no map station). Each tap on the Status tab cycles
//   real -> map station used (every cell shows) -> every cell explored -> real.
// Leaving "real" remembers the area's station byte and explored bits; coming back
// restores them. These are the game's own RAM values, so the pause map, the HUD
// minimap and a save at a station all see them.
typedef enum { kSmMapDebug_Real, kSmMapDebug_Station, kSmMapDebug_Explored } SmMapDebugState;
SmMapDebugState SmMap_DebugState(int area);
void SmMap_DebugCycle(int area);
// Drops the remembered real states, e.g. after loading a save state or a reset,
// which bring their own.
void SmMap_DebugForget(void);
