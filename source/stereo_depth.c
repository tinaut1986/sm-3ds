#include "stereo_depth.h"

// Shift at full slider per plane, in SNES pixels (docs/stereo-design.md). A starting
// point to tune on the console; the order is what tools/stereo-test pins.
static const int8_t kPlanePx[kStereoPlaneCount] = {
  [kStereoHud] = 2, [kStereoFront] = 1, [kStereoPlay] = 0, [kStereoObj] = -1,
  [kStereoBack] = -2, [kStereoMid] = -3, [kStereoFar] = -4, [kStereoScreen] = 0,
};

static bool g_thickness = true;

void StereoDepth_SetThickness(bool on) { g_thickness = on; }
bool StereoDepth_Thickness(void) { return g_thickness; }

StereoPlane StereoDepth_Plane(const StereoFrame *frame, const StereoItem *item) {
  if (item->hud) return kStereoHud;   // text and HUD in front of everything, menus included
  if (!frame->gameplay) return kStereoScreen;
  switch (item->kind) {
  case kStereoKindObj:
    return item->priority == 3 ? kStereoPlay : kStereoObj;   // priority 3: drawn over the walls
  case kStereoKindMode7:
    return kStereoPlay;  // the elevator shaft's room, Ridley's flight
  case kStereoKindBackdrop:
    return kStereoFar;
  case kStereoKindBg:
    switch (item->layer) {
    case 0: return item->priority ? kStereoPlay : kStereoBack;
    case 1: return item->priority ? kStereoPlay : kStereoFar;   // priority 1 draws over Samus: a wall (9C5E)
    default: return item->priority ? kStereoFront : kStereoMid;   // priority 0: ash, fog, in front of BG2
    }
  }
  return kStereoScreen;
}

StereoItem StereoDepth_ItemOfLevel(int level, bool obj, bool mode7, bool hud) {
  StereoItem it = { kStereoKindBg, 0, 0, hud };
  if (mode7) it.kind = kStereoKindMode7;
  else if (obj) it.kind = kStereoKindObj, it.priority = (uint8_t)((level - 2) / 4);
  else if (level == 0) it.kind = kStereoKindBackdrop;
  else if (level == 8 || level == 12) it.layer = 0, it.priority = level == 12;
  else if (level == 7 || level == 11) it.layer = 1, it.priority = level == 11;
  else it.layer = 2, it.priority = level == 15;
  return it;
}

int StereoDepth_PlanePx(StereoPlane plane) {
  if (plane < 0 || plane >= kStereoPlaneCount) return 0;
  if (plane == kStereoObj && !g_thickness) return kPlanePx[kStereoPlay];
  return kPlanePx[plane];
}

int StereoDepth_EyeOffset(StereoPlane plane, float slider, int eye) {
  if (slider <= 0) return 0;
  if (slider > 1) slider = 1;
  const float v = (float)eye * slider * (float)StereoDepth_PlanePx(plane);
  return (int)(v < 0 ? v - 0.5f : v + 0.5f);   // round half away from zero: symmetric eyes
}
