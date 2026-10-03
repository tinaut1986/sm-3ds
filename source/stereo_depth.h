// Stereo depth: which plane each quad of a frame goes on, and how far each plane is
// shifted per eye (docs/stereo-design.md).
//
// A pure function of what the frame builder already knows about a quad (its compositor
// level, layer kind, whether it is in the HUD list) and a few facts about the frame. No
// 3DS headers and no globals besides the settings below, so tools/stereo-test can
// enumerate every input on the host. Anything that changes how depth is picked belongs
// here, not in the renderer, or it escapes that test.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Nearest first. The renderer keeps a byte per quad with one of these.
typedef enum {
  kStereoHud,      // the HUD list (HUD rows, escape timer), message boxes, the port's own text
  kStereoFront,    // BG3 priority 1 FX that is not the HUD (water, lava, fog over the room)
  kStereoPlay,     // BG1 or BG2 tile priority 1: SM's walls and floors (Samus passes behind them);
                   // sprites of OAM priority 3 (drawn over them: wall faces); the mode 7 plane
  kStereoObj,      // the other world sprites: Samus, enemies (OAM priority 0-2)
  kStereoBack,     // BG1 tile priority 0: the level's parts Samus passes in front of (the
                   // save station's glass, background pipes)
  kStereoMid,      // BG2 priority 0 (the room's background)
  kStereoFar,      // BG3 priority 0 FX, the backdrop
  kStereoScreen,   // a non-gameplay screen: flat until P3.4 gives it depth
  kStereoPlaneCount
} StereoPlane;

// What a quad is, as the frame builder sees it.
typedef enum {
  kStereoKindBg,         // a BG layer of mode 1 (`layer` 0..2 = BG1..BG3)
  kStereoKindObj,        // a sprite
  kStereoKindMode7,      // the mode 7 plane
  kStereoKindBackdrop,   // the backdrop colour
} StereoKind;

typedef struct {
  StereoKind kind;
  uint8_t layer;      // kStereoKindBg: 0..2
  uint8_t priority;   // kStereoKindBg: tile priority 0/1; kStereoKindObj: OAM priority 0..3
  bool hud;           // in the frame's HUD list (GpuFrame.hud_first), or the port's own text
} StereoItem;

typedef struct {
  bool gameplay;      // the room is on screen (game states 7, 8, 12, 18, ... as SmWide's)
} StereoFrame;

StereoPlane StereoDepth_Plane(const StereoFrame *frame, const StereoItem *item);

// The item a GPU renderer quad stands for, from its compositor level (gpu_ppu.c: BG1 8/12,
// BG2 7/11, BG3 1/15, sprites 4 * priority + 2) and kind. `hud`: in the HUD list.
StereoItem StereoDepth_ItemOfLevel(int level, bool obj, bool mode7, bool hud);

// Whether Samus and enemies sit one step behind the walls and floors (platform thickness,
// as mzm; in SM the compositor agrees: they are drawn under BG1 priority 1) or on them.
// On by default; an option can turn it off where it reads wrong.
void StereoDepth_SetThickness(bool on);
bool StereoDepth_Thickness(void);

// Shift of a plane at full slider, in SNES pixels, positive = nearer. Whole pixels.
int StereoDepth_PlanePx(StereoPlane plane);
enum { kStereoMaxPx = 4 };   // the largest |StereoDepth_PlanePx|: columns the edges need

// Shift for one eye (left = +1, right = -1) at slider 0..1: whole pixels, rounded once per
// plane, so a plane moves rigidly and text never lands between pixels.
int StereoDepth_EyeOffset(StereoPlane plane, float slider, int eye);
