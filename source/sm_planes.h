// Per-room stereo depth set by hand: the plane a layer of a room goes to, from source/sm_plane_fixes.inc
// (written by tools/layer-workbench). The renderer asks for the plane of a layer (GpuPpu_SetPlaneRule) and
// tags its quads with it; StereoDepth_Plane leaves those quads alone.
#pragma once

// The plane (a StereoPlane) the current room sends the layer to, or -1 for the depth function's own choice.
// `layer`: 1..3 = BG1..BG3, 4 = sprites (`prio` = OAM priority 0..3), 5 = Mode 7. `prio` for the BGs: the tiles'
// priority bit. Suits GpuPpu_SetPlaneRule.
int SmPlanes_LayerRule(int layer, int prio);

// Whether the current room has any rule (to leave the renderer's hook off elsewhere).
int SmPlanes_RoomHasRules(void);

// Rules in the file, for the log and the tests.
int SmPlanes_RuleCount(void);
