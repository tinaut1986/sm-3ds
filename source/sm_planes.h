// Per-room stereo depth set by hand: the plane a layer of a room goes to, from source/sm_plane_fixes.inc
// (written by tools/layer-workbench). The renderer asks for the plane of a layer (GpuPpu_SetPlaneRule) and
// tags its quads with it; StereoDepth_Plane leaves those quads alone.
#pragma once

#include <stdint.h>

#include "stereo_depth.h"

// The plane (a StereoPlane) the current room sends the layer to, or -1 for the depth function's own choice.
// `layer`: 1..3 = BG1..BG3, 4 = sprites (`prio` = OAM priority 0..3), 5 = Mode 7. `prio` for the BGs: the tiles'
// priority bit. Suits GpuPpu_SetPlaneRule.
int SmPlanes_LayerRule(int layer, int prio);

// Whether the current room has any rule or tile fix (to leave the renderer's hooks off elsewhere).
int SmPlanes_RoomHasRules(void);

// The tile fixes (SM_PLANE_FIX) of the current room for the renderer's tilemap surface of `layer` (1 = BG1, 2 = BG2),
// which is `tw` x `th` tiles (32 or 64). Fills `grid` (rows of 64, th rows): per tile, bits 0-3 are 0 when it keeps the
// layer's plane or StereoPlane + 1 when a fix sends it elsewhere, bits 4-7 are 0 when it keeps its tile priority or
// priority + 1 when a fix sets the priority it is drawn with (SM_TILE_PRIO); returns how many tiles are set. A block of the level sits in
// the tilemap at tile (2*(vx0 + bx - lx0) + i, 2*(vy0 + by - ly0) + j) modulo the tilemap's size (lx0 the block of the layer's
// position, vx0 that of the position plus the scroll offset); of the blocks that share a slot, the one nearest
// the camera is on screen. A fix whose block word no longer matches the level data (another room state) is ignored.
// Suits GpuPpu_SetSlotPlanes.
int SmPlanes_SlotPlanes(int layer, int tw, int th, uint8_t *grid);

// Rules in the file, for the log and the tests.
int SmPlanes_RuleCount(void);

// Which non-gameplay screen the game state is, for the 3D (stereo_depth.h): its text and interface in front.
StereoScreen SmPlanes_Screen(void);
