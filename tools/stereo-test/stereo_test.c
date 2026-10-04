// Host test of source/stereo_depth.c (docs/stereo-design.md): every input of the depth
// mapping against an oracle of the SNES compositor written here on its own, so agreeing
// with it means something.
//
// The two failures it looks for (mzm's lesson): a layer placed nearer than something that
// visibly draws over it, or farther than something it draws over. SM puts its walls and
// floors on BG1 priority 1 and Samus at OAM priority 2, under them: following the
// compositor gives the platform thickness by itself. Each case where the
// mapping does that on purpose is listed in kAccepted; a new one fails, and so does an
// accepted one that no longer happens (the list must stay exact).
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "stereo_depth.h"

static int g_fail, g_checks;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_fail++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

// Mode 1 with BG3 priority (SM's gameplay): the order the PPU draws, higher in front.
// Written from the SNES documentation, not taken from gpu_ppu.c's kBgLevel.
typedef struct { const char *name; StereoItem item; int level; } Layer;
static const Layer kLayers[] = {
  { "BG3 prio 1", { kStereoKindBg, 2, 1, false }, 15 },
  { "OBJ prio 3", { kStereoKindObj, 0, 3, false }, 14 },
  { "BG1 prio 1", { kStereoKindBg, 0, 1, false }, 12 },
  { "BG2 prio 1", { kStereoKindBg, 1, 1, false }, 11 },
  { "OBJ prio 2", { kStereoKindObj, 0, 2, false }, 10 },
  { "BG1 prio 0", { kStereoKindBg, 0, 0, false }, 8 },
  { "BG2 prio 0", { kStereoKindBg, 1, 0, false }, 7 },
  { "OBJ prio 1", { kStereoKindObj, 0, 1, false }, 6 },
  { "OBJ prio 0", { kStereoKindObj, 0, 0, false }, 2 },
  { "BG3 prio 0", { kStereoKindBg, 2, 0, false }, 1 },
  { "backdrop", { kStereoKindBackdrop, 0, 0, false }, 0 },
};
enum { kLayerCount = sizeof(kLayers) / sizeof(kLayers[0]) };

// "front" draws over "back" yet sits farther. Why each is accepted:
static const struct { const char *front, *back; bool needs_thickness; const char *why; } kAccepted[] = {
  // Sprites of OAM priority 0/1 share Samus's plane though the level's back tiles and BG2
  // are drawn over them: rare in SM (things hidden behind the background).
  { "BG1 prio 0", "OBJ prio 1", false, "low sprites on Samus's plane" },
  { "BG1 prio 0", "OBJ prio 0", false, "low sprites on Samus's plane" },
  { "BG2 prio 0", "OBJ prio 1", false, "low sprites on Samus's plane" },
  { "BG2 prio 0", "OBJ prio 0", false, "low sprites on Samus's plane" },
  // BG2 priority 1 over the level or over Samus: rare (Spore Spawn's body); P3.3 checks it.
  { "BG2 prio 1", "OBJ prio 2", false, "BG2 always mid" },
  { "BG2 prio 1", "BG1 prio 0", false, "BG2 always mid" },
  { "BG2 prio 1", "OBJ prio 1", false, "BG2 always mid" },
  { "BG2 prio 1", "OBJ prio 0", false, "BG2 always mid" },
};
enum { kAcceptedCount = sizeof(kAccepted) / sizeof(kAccepted[0]) };

static int Accepted(const char *front, const char *back, bool thickness) {
  for (int i = 0; i < kAcceptedCount; i++)
    if (!strcmp(kAccepted[i].front, front) && !strcmp(kAccepted[i].back, back) &&
        (!kAccepted[i].needs_thickness || thickness))
      return i;
  return -1;
}

static void TestGameplayOrder(bool thickness) {
  StereoDepth_SetThickness(thickness);
  const StereoFrame fr = { true };
  int used[kAcceptedCount] = { 0 };
  for (int a = 0; a < kLayerCount; a++) {
    const int pa = StereoDepth_PlanePx(StereoDepth_Plane(&fr, &kLayers[a].item));
    for (int b = 0; b < kLayerCount; b++) {
      if (kLayers[a].level <= kLayers[b].level) continue;   // a draws over b
      const int pb = StereoDepth_PlanePx(StereoDepth_Plane(&fr, &kLayers[b].item));
      if (pa >= pb) continue;
      const int k = Accepted(kLayers[a].name, kLayers[b].name, thickness);
      if (k >= 0) used[k]++;
      CHECK(k >= 0, "thickness %d: %s draws over %s but sits farther (%+d vs %+d px)", thickness, kLayers[a].name,
            kLayers[b].name, pa, pb);
    }
  }
  for (int k = 0; k < kAcceptedCount; k++)
    if (!kAccepted[k].needs_thickness || thickness)
      CHECK(used[k], "thickness %d: accepted case %s over %s (%s) no longer happens: drop it", thickness,
            kAccepted[k].front, kAccepted[k].back, kAccepted[k].why);
}

static void TestPlanes(void) {
  const StereoFrame play = { true }, menu = { false };
  for (int t = 0; t < 2; t++) {
    StereoDepth_SetThickness(t);
    // Sprites over the walls (OAM priority 3) on the walls' plane, the rest on Samus's.
    for (int p = 0; p < 4; p++) {
      const StereoItem obj = { kStereoKindObj, 0, (uint8_t)p, false };
      const StereoPlane want = p == 3 ? kStereoPlay : kStereoObj;
      CHECK(StereoDepth_Plane(&play, &obj) == want, "OBJ prio %d on plane %d, want %d", p,
            StereoDepth_Plane(&play, &obj), want);
    }
    const int obj = StereoDepth_PlanePx(kStereoObj), lvl = StereoDepth_PlanePx(kStereoPlay);
    CHECK(t ? obj < lvl : obj == lvl, "thickness %d: sprites %+d px against the level %+d px", t, obj, lvl);
    // The HUD nearest of all, in every frame.
    for (int p = 0; p < kStereoPlaneCount; p++)
      if (p != kStereoHud)
        CHECK(StereoDepth_PlanePx(kStereoHud) > StereoDepth_PlanePx((StereoPlane)p), "HUD not nearer than plane %d", p);
    for (int a = 0; a < kLayerCount; a++) {
      StereoItem it = kLayers[a].item;
      it.hud = true;
      CHECK(StereoDepth_Plane(&play, &it) == kStereoHud && StereoDepth_Plane(&menu, &it) == kStereoHud,
            "%s in the HUD list off the HUD plane", kLayers[a].name);
      // Outside gameplay everything else is flat.
      CHECK(StereoDepth_Plane(&menu, &kLayers[a].item) == kStereoScreen, "%s not flat outside gameplay",
            kLayers[a].name);
    }
    CHECK(StereoDepth_PlanePx(kStereoScreen) == 0, "flat screens shifted");
    const StereoItem m7 = { kStereoKindMode7, 0, 0, false };
    CHECK(StereoDepth_Plane(&play, &m7) == kStereoPlay, "mode 7 plane off the level plane");
    int max = 0;
    for (int p = 0; p < kStereoPlaneCount; p++) {
      const int v = abs(StereoDepth_PlanePx((StereoPlane)p));
      if (v > max) max = v;
    }
    CHECK(max <= kStereoMaxPx, "a plane shifts %d px, more than kStereoMaxPx %d", max, kStereoMaxPx);
  }
  StereoDepth_SetThickness(true);
}

// Whole pixels, symmetric eyes, never past the full-slider value, never shrinking as the
// slider goes up, and the HUD never behind another plane at any slider position.
static void TestOffsets(void) {
  for (int t = 0; t < 2; t++) {
    StereoDepth_SetThickness(t);
    for (int p = 0; p < kStereoPlaneCount; p++) {
      const int full = StereoDepth_PlanePx((StereoPlane)p);
      int prev = 0;
      for (int i = 0; i <= 1024; i++) {
        const float s = i / 1024.0f;
        const int l = StereoDepth_EyeOffset((StereoPlane)p, s, 1), r = StereoDepth_EyeOffset((StereoPlane)p, s, -1);
        CHECK(l == -r, "plane %d slider %.4f: eyes %d / %d not symmetric", p, s, l, r);
        CHECK(abs(l) <= abs(full), "plane %d slider %.4f: %d px past full %d", p, s, l, full);
        CHECK(abs(l) >= abs(prev), "plane %d slider %.4f: shrinks %d -> %d", p, s, prev, l);
        prev = l;
        for (int q = 0; q < kStereoPlaneCount; q++)
          CHECK(StereoDepth_EyeOffset(kStereoHud, s, 1) >= StereoDepth_EyeOffset((StereoPlane)q, s, 1),
                "slider %.4f: HUD %d px behind plane %d (%d px)", s, StereoDepth_EyeOffset(kStereoHud, s, 1), q,
                StereoDepth_EyeOffset((StereoPlane)q, s, 1));
      }
      CHECK(StereoDepth_EyeOffset((StereoPlane)p, 0, 1) == 0, "plane %d shifted at slider 0", p);
      CHECK(StereoDepth_EyeOffset((StereoPlane)p, 1, 1) == full, "plane %d at full slider %d, want %d", p,
            StereoDepth_EyeOffset((StereoPlane)p, 1, 1), full);
    }
  }
  StereoDepth_SetThickness(true);
}

// The debug tint needs every plane to read apart: a name and a colour of its own.
static void TestDebugView(void) {
  for (int a = 0; a < kStereoPlaneCount; a++) {
    CHECK(StereoDepth_PlaneName((StereoPlane)a)[0] && StereoDepth_PlaneName((StereoPlane)a)[0] != '?',
          "plane %d has no name", a);
    for (int b = a + 1; b < kStereoPlaneCount; b++) {
      CHECK(strcmp(StereoDepth_PlaneName((StereoPlane)a), StereoDepth_PlaneName((StereoPlane)b)) != 0,
            "planes %d and %d share a name", a, b);
      CHECK(StereoDepth_PlaneColor((StereoPlane)a) != StereoDepth_PlaneColor((StereoPlane)b),
            "planes %d and %d share a colour", a, b);
    }
  }
}

// The ramps of the DRAW ORDER and STEREO DEPTH views: monotonic, and the depth follows the planes' shifts.
static void TestRamps(void) {
  for (int ramp = 0; ramp < 2; ramp++) {
    const int shift = ramp == kRampDepth ? 0 : 8;   // the strong channel: blue for depth, green for the order
    uint32_t prev = StereoDepth_RampColor(0, ramp);
    for (int i = 1; i <= 100; i++) {
      const uint32_t c = StereoDepth_RampColor(i / 100.0f, ramp);
      CHECK((c >> shift & 255) >= (prev >> shift & 255) && (c >> 16 & 255) >= (prev >> 16 & 255),
            "ramp %d not brightening at %d", ramp, i);
      prev = c;
    }
    CHECK(StereoDepth_RampColor(-1, ramp) == StereoDepth_RampColor(0, ramp) &&
              StereoDepth_RampColor(2, ramp) == StereoDepth_RampColor(1, ramp),
          "ramp %d not clamped", ramp);
  }
  CHECK(StereoDepth_RampColor(1, kRampOrder) != StereoDepth_RampColor(1, kRampDepth), "both ramps share a colour");
  for (int t = 0; t < 2; t++) {
    StereoDepth_SetThickness(t);
    for (int a = 0; a < kStereoPlaneCount; a++) {
      const float da = StereoDepth_PlaneDepth((StereoPlane)a);
      CHECK(da >= 0 && da <= 1, "plane %d depth %f out of 0..1", a, da);
      for (int b = 0; b < kStereoPlaneCount; b++)
        CHECK((StereoDepth_PlanePx((StereoPlane)a) < StereoDepth_PlanePx((StereoPlane)b)) ==
                  (da < StereoDepth_PlaneDepth((StereoPlane)b)),
              "thickness %d: depth of planes %d and %d out of step with their shifts", t, a, b);
    }
    CHECK(StereoDepth_PlaneDepth(kStereoHud) == 1.0f && StereoDepth_PlaneDepth(kStereoFar) == 0.0f,
          "thickness %d: HUD is not the nearest or FAR the farthest", t);
  }
  StereoDepth_SetThickness(true);
}

int main(void) {
  TestRamps();
  TestDebugView();
  TestGameplayOrder(true);
  TestGameplayOrder(false);
  TestPlanes();
  TestOffsets();
  printf("stereo-test: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
