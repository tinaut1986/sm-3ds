#include "stereo_depth.h"

// Shift at full slider per plane, in SNES pixels (docs/stereo-design.md). A starting
// point to tune on the console; the order is what tools/stereo-test pins.
static const int8_t kPlanePx[kStereoPlaneCount] = {
  [kStereoHud] = 2, [kStereoFront] = 1, [kStereoPlay] = 0, [kStereoObj] = -1,
  [kStereoMid] = -2, [kStereoFar] = -3, [kStereoScreen] = 0,
};

static bool g_thickness = true;

void StereoDepth_SetThickness(bool on) { g_thickness = on; }
bool StereoDepth_Thickness(void) { return g_thickness; }

StereoPlane StereoDepth_Plane(const StereoFrame *frame, const StereoItem *item) {
  if (item->hud) return kStereoHud;   // text and HUD in front of everything, menus included
  if (!frame->gameplay) return kStereoScreen;
  switch (item->kind) {
  case kStereoKindObj:
    return kStereoObj;   // one plane for all world sprites, whatever their OAM priority
  case kStereoKindMode7:
    return kStereoPlay;  // the elevator shaft's room, Ridley's flight
  case kStereoKindBackdrop:
    return kStereoFar;
  case kStereoKindBg:
    switch (item->layer) {
    case 0: return item->priority ? kStereoFront : kStereoPlay;
    case 1: return kStereoMid;
    default: return item->priority ? kStereoFront : kStereoFar;
    }
  }
  return kStereoScreen;
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
