// Debug teleport: go to any room, arriving at one of the doors that lead into it.
//
// Like mzm's warp, this targets a *door*, not a room: a door definition (bank $83) says
// which screen the camera goes to and where the door is in the destination room. The room
// is then loaded the way "Continue" loads a saved game (game_state 6, through a hook in
// LoadFromLoadStation), not with a door transition: see the comment in SmWarp_ToRoom.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sm_map.h"

typedef enum {
  kWarp_Ok = 0,
  kWarp_NotReady,   // not in normal gameplay (paused, in a transition, at a menu)
  kWarp_NoDoor,     // no door in the game leads into that room
} SmWarpResult;

// Indexes the doors of every room. Needs SmMap_Init() first.
void SmWarp_Init(void);

// Number of doors in the game that lead into `room`.
int SmWarp_DoorCount(const SmRoom *room);

// The map cell of door number `which` into `room` (as SmWarp_ToRoom picks it), and the
// side of the cell its door cap is on: 0 left, 1 right, 2 top, 3 bottom (the door
// definition's orientation says which way Samus goes through: 0 right, 1 left, 2 down,
// 3 up, so the cap is on the left, right, top, bottom). False for a scripted transition
// that has no door in the room.
typedef struct { int col, row, side; } SmWarpDoorMark;
bool SmWarp_DoorOnMap(const SmRoom *room, int which, SmWarpDoorMark *out);

// Whether a warp could start right now.
bool SmWarp_Ready(void);

// Starts loading `room`. `which` picks among the doors that lead there (0 = first);
// it wraps around.
SmWarpResult SmWarp_ToRoom(const SmRoom *room, int which);

const char *SmWarp_ResultText(SmWarpResult r);

// Call once after every RtlRunFrame. When a warp has just finished arriving, checks
// that Samus is inside the room and not inside a solid block, and if not moves her
// to the nearest free floor. See the comment in sm_warp.c for why this is needed.
void SmWarp_AfterFrame(void);

// The standing spot the last warp aimed for, in pixels; false if it had none (a scripted
// transition with no real door). For the host test.
bool SmWarp_LastTarget(int *x, int *y, bool *vertical);

// How many warps needed that correction (for the host test).
int SmWarp_FixupCount(void);
