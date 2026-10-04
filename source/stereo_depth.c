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

int StereoDepth_EyeOffsetPx(int px, float slider, int eye) {
  if (slider <= 0) return 0;
  if (slider > 1) slider = 1;
  const float v = (float)eye * slider * (float)px;
  return (int)(v < 0 ? v - 0.5f : v + 0.5f);   // round half away from zero: symmetric eyes
}

int StereoDepth_EyeOffset(StereoPlane plane, float slider, int eye) {
  return StereoDepth_EyeOffsetPx(StereoDepth_PlanePx(plane), slider, eye);
}

static const char *const kPlaneName[kStereoPlaneCount] = {
  [kStereoHud] = "HUD", [kStereoFront] = "FRONT", [kStereoPlay] = "PLAY", [kStereoObj] = "OBJ",
  [kStereoBack] = "BACK", [kStereoMid] = "MID", [kStereoFar] = "FAR", [kStereoScreen] = "FLAT",
};

// Warm colours for the near planes, cool ones for the far ones, white for the sprites.
static const uint32_t kPlaneColor[kStereoPlaneCount] = {
  [kStereoHud] = 0xFF00FF, [kStereoFront] = 0x00E5FF, [kStereoPlay] = 0xFF9600, [kStereoObj] = 0xFFFFFF,
  [kStereoBack] = 0xFF2828, [kStereoMid] = 0x3CC83C, [kStereoFar] = 0x3C5AFF, [kStereoScreen] = 0xA0A0A0,
};

const char *StereoDepth_PlaneName(StereoPlane plane) {
  return plane >= 0 && plane < kStereoPlaneCount ? kPlaneName[plane] : "?";
}

uint32_t StereoDepth_PlaneColor(StereoPlane plane) {
  return plane >= 0 && plane < kStereoPlaneCount ? kPlaneColor[plane] : 0;
}

uint32_t StereoDepth_RampColor(float t, int ramp) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  const int main = (int)(40 + 215 * t + 0.5f), side = (int)(24 + 56 * t + 0.5f), low = (int)(24 + 80 * t + 0.5f);
  if (ramp == kRampDepth) return (uint32_t)(side << 16 | low << 8 | main);   // blue
  return (uint32_t)(side << 16 | main << 8 | low);                           // green
}

float StereoDepth_PlaneDepth(StereoPlane plane) {
  int lo = StereoDepth_PlanePx(kStereoFar), hi = lo;
  for (int p = 0; p < kStereoPlaneCount; p++) {
    const int v = StereoDepth_PlanePx((StereoPlane)p);
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  return hi == lo ? 0.5f : (float)(StereoDepth_PlanePx(plane) - lo) / (float)(hi - lo);
}
