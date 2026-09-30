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
void SmMap_Init(void);
const SmRoom *SmMap_Rooms(int *count);

// Cell state. `exists` means the area's map draws something there; `explored` is
// the player's own bit. Returns false for out-of-range coordinates.
bool SmMap_Cell(int area, int col, int row, bool *exists, bool *explored);

// True if the area's map station has been used, so every existing cell shows.
bool SmMap_HasMapStation(int area);

// Where Samus is, in map cells of the current area. False outside of gameplay.
bool SmMap_SamusCell(int *area, int *col, int *row);

// The room covering a cell, or NULL.
const SmRoom *SmMap_RoomAt(int area, int col, int row);

// The room Samus is in now, or NULL.
const SmRoom *SmMap_CurrentRoom(void);
