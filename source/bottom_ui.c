#include "bottom_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <3ds.h>

#include "build_config.h"
#include "src/types.h"
#include "src/sm_rtl.h"
#include "src/variables.h"
#include "cheats.h"
#include "debug_tools.h"
#include "sm_map.h"
#include "sm_warp.h"
#include "ui_draw.h"

#define SCREEN_W 320
#define SCREEN_H 240
#define REFRESH_FRAMES 15   // periodic redraw so live numbers (and the clock) keep moving
#define TAP_FLASH_MS 150    // how long a tapped button shows pressed
#define ARM_MS 2000         // a save-state button waits this long for its second tap
#define STATE_SLOTS 10

UiOptions g_ui = {
  .audio_on = true,
  .render_on = true,
  .frameskip = true,
  .new3ds_speedup = true,
  // On in every build: it is what makes Old 3DS playable (2DS: ~60 fps against ~25 with
  // the CPU renderer), and any frame it cannot draw goes to the CPU renderer anyway.
  // DEBUG_TOOLS builds can switch it off for the session (Debug tab, RENDERER).
  .gpu_render = true,
};

// Tab order as drawn, like mzm. DEBUG exists only in DEBUG_TOOLS builds.
typedef enum { TAB_MAP, TAB_STATUS, TAB_DEBUG, TAB_STATES, TAB_OPTIONS, TAB_COUNT } Tab;

typedef enum { MODAL_NONE, MODAL_RESET, MODAL_TOOLS } Modal;

static UiRomInfo g_rom_info;
static Tab g_tab = TAB_STATUS;
static Modal g_modal;
static int g_dirty = 2;             // frames left to redraw (bottom is double buffered)
static uint32_t g_last_redraw;
static bool g_is_new3ds;
static char g_toast[48];
static u64 g_toast_until;
static int g_tap_x = -1000, g_tap_y;
static u64 g_tap_ms;
static bool g_tap_flash_pending;    // redraw once more when the flash ends
static bool g_ptmu;
static u8 g_battery = 5, g_charging;

void BottomUi_Toast(const char *msg) {
  snprintf(g_toast, sizeof(g_toast), "%s", msg);
  g_toast_until = osGetTime() + 1500;
  g_dirty = 2;
}
#define Toast BottomUi_Toast

static bool Pressed(Rect r) { return osGetTime() - g_tap_ms < TAP_FLASH_MS && UiIn(r, g_tap_x, g_tap_y); }

static uint32_t FpsColor(float fps) { return fps >= 58.0f ? COL_GOOD : fps >= 45.0f ? COL_WARN : COL_BAD; }

#if DEBUG_TOOLS
static void ReportGameplay(bool ok) {
  if (!ok) Toast("Only works inside a room");
}
#endif

// ---- Tab bar ------------------------------------------------------------------
// 30 px icon tabs from the left, clock, wifi and battery on the right (mzm's layout).

static int VisibleTabs(Tab out[TAB_COUNT]) {
  int n = 0;
  out[n++] = TAB_MAP;
  out[n++] = TAB_STATUS;
#if DEBUG_TOOLS
  out[n++] = TAB_DEBUG;
#endif
  out[n++] = TAB_STATES;
  out[n++] = TAB_OPTIONS;
  return n;
}

static Rect TabRect(int slot) { return (Rect){ 4 + slot * 34, 2, 30, 21 }; }

// 14x14 glyphs made of rectangles, centred on (cx, cy). Shapes copied from mzm.
static void DrawTabIcon(Surface s, Tab tab, int x, int y, uint32_t c) {
#define R(dx, dy, w, h) UiFillRect(s, x + (dx), y + (dy), w, h, c)
  switch (tab) {
  case TAB_MAP:   // floor plan: 3x3 rooms, hollow centre
    R(-7, -7, 4, 4); R(-2, -7, 4, 4); R(3, -7, 4, 4);
    R(-7, -2, 4, 4);                  R(3, -2, 4, 4);
    R(-7, 3, 4, 4);  R(-2, 3, 4, 4);  R(3, 3, 4, 4);
    break;
  case TAB_STATUS:   // stat bars
    R(-6, 1, 3, 4); R(-2, -4, 3, 9); R(2, -1, 3, 6);
    break;
  case TAB_DEBUG:   // bug
    R(-1, -7, 1, 2); R(1, -7, 1, 2); R(-2, -5, 4, 2);
    R(-3, -3, 3, 8); R(1, -3, 3, 8); R(-2, -4, 4, 1); R(-2, 5, 4, 1);
    R(-6, -2, 3, 1); R(-6, 1, 3, 1); R(-6, 4, 3, 1);
    R(3, -2, 3, 1);  R(3, 1, 3, 1);  R(3, 4, 3, 1);
    break;
  case TAB_STATES:   // floppy disk
    R(-6, -6, 12, 1); R(-6, 5, 12, 1); R(-6, -6, 1, 12); R(5, -5, 1, 11);
    R(3, -6, 3, 3); R(-3, -6, 5, 4); R(-4, 1, 8, 4);
    break;
  default:   // sliders
    R(-6, -4, 12, 1); R(-6, 0, 12, 1); R(-6, 4, 12, 1);
    R(-2, -5, 3, 3);  R(2, -1, 3, 3);  R(-5, 3, 3, 3);
    break;
  }
#undef R
}

static void DrawSystemStatus(Surface s) {
  const uint32_t frame = RGB(120, 132, 158), dim = RGB(58, 66, 84);
  const int cy = 13;
  // Battery: 15x8 shell and a 2x4 nub, far right.
  const int bx = 299, by = cy - 4;
  UiFrameRect(s, bx, by, 15, 8, frame);
  UiFillRect(s, bx + 15, by + 2, 2, 4, frame);
  if (g_ptmu) {
    const int w = g_battery * 11 / 5;
    const uint32_t fill = g_charging ? RGB(90, 210, 120) : g_battery <= 1 ? RGB(230, 80, 60)
                        : g_battery == 2 ? RGB(235, 190, 70) : RGB(110, 205, 130);
    if (w > 0) UiFillRect(s, bx + 2, by + 2, w, 4, fill);
  } else {
    UiFillRect(s, bx + 5, by + 3, 4, 2, dim);
  }
  // Wifi: three rising bars on the battery's baseline.
  const int wifi = osGetWifiStrength(), wx = bx - 16;
  for (int i = 0; i < 3; i++) {
    const int h = 3 + i * 2;
    UiFillRect(s, wx + i * 4, cy + 4 - h, 2, h, i < wifi ? frame : dim);
  }
  // Clock, left of the wifi bars. The console clock is local time.
  char buf[8] = "--:--";
  time_t t = time(NULL);
  struct tm *tm = gmtime(&t);
  if (tm) snprintf(buf, sizeof(buf), "%02d:%02d", tm->tm_hour, tm->tm_min);
  UiDrawText(s, wx - 4 - UiTextWidth(buf, 1), cy - 3, 1, RGB(210, 220, 235), buf);
}

static void DrawTabBar(Surface s) {
  Tab tabs[TAB_COUNT];
  const int n = VisibleTabs(tabs);
  for (int i = 0; i < n; i++) {
    const bool on = tabs[i] == g_tab;
    const Rect r = TabRect(i);
    UiFillRect(s, r.x, r.y, r.w, r.h, on ? RGB(45, 150, 240) : RGB(50, 56, 75));
    UiFillRect(s, r.x + 1, r.y + 1, r.w - 2, r.h - 2, on ? RGB(18, 70, 130) : RGB(26, 30, 42));
    DrawTabIcon(s, tabs[i], r.x + r.w / 2, r.y + r.h / 2, on ? RGB(255, 255, 255) : RGB(140, 150, 175));
  }
  // Things that are on whatever tab is shown.
  int x = TabRect(n).x;
  if (Debug_PerfRecording()) { UiDrawText(s, x, 9, 1, COL_BAD, "REC"); x += 24; }
  if (g_cheats.invincible || g_cheats.max_mode) UiDrawText(s, x, 9, 1, COL_WARN, "CHT");
  DrawSystemStatus(s);
}

// ---- Map tab ------------------------------------------------------------------
// One area at a time: 64x31 cells of 5 px fill the screen width, so no zoom or
// scrolling is needed. Row 0 of the tilemap is an empty margin and is not drawn.
// Picking a room and warping into it exists in DEBUG_TOOLS builds only.

#define MAP_CELL 5
#define MAP_Y0 26
#define MAP_COL_EXPLORED  RGB(214, 96, 150)
#define MAP_COL_KNOWN     RGB(56, 64, 104)
#define MAP_COL_ROOM      RGB(250, 220, 90)
#define MAP_COL_SELECT    RGB(90, 220, 240)

static int g_map_area;              // area being shown
static bool g_map_follow = true;    // follow the area Samus is in
static int g_sel_col = -1, g_sel_row = -1;

static const char *const kAreaShort[kSmAreaCount] = { "CRA", "BRI", "NOR", "WRE", "MAR", "TOU", "CER" };

static Rect AreaButtonRect(int i) { return (Rect){ 2 + i * 45, 183, 43, 13 }; }
static Rect FollowRect(void) { return (Rect){ 226, 198, 92, 13 }; }

#if DEBUG_TOOLS
static int g_warp_door;             // which of the doors into the selected room to use

static Rect WarpRect(void) { return (Rect){ 4, 222, 150, 14 }; }
static Rect DoorRect(void) { return (Rect){ 160, 222, 96, 14 }; }

static const SmRoom *SelectedRoom(int area) {
  return g_sel_col >= 0 ? SmMap_RoomAt(area, g_sel_col, g_sel_row) : NULL;
}
#endif

static int ShownMapArea(void) {
  if (g_map_follow && area_index < kSmAreaCount && Cheats_InGameplay()) g_map_area = (int)area_index;
  return g_map_area;
}

static void MapTouch(int x, int y) {
  const int area = ShownMapArea();
  for (int i = 0; i < kSmAreaCount; i++) {
    if (UiIn(AreaButtonRect(i), x, y)) {
      g_map_area = i;
      g_map_follow = false;
      g_sel_col = g_sel_row = -1;
      return;
    }
  }
  if (UiIn(FollowRect(), x, y)) {
    g_map_follow = !g_map_follow;
    return;
  }
#if DEBUG_TOOLS
  const SmRoom *room = SelectedRoom(area);
  if (room && UiIn(WarpRect(), x, y)) {
    Toast(SmWarp_ResultText(SmWarp_ToRoom(room, g_warp_door)));
  } else if (room && UiIn(DoorRect(), x, y)) {
    const int n = SmWarp_DoorCount(room);
    if (n > 1) g_warp_door = (g_warp_door + 1) % n;
  } else if (y >= MAP_Y0 && y < MAP_Y0 + (kSmMapRows - 1) * MAP_CELL) {
    g_sel_col = x / MAP_CELL;
    g_sel_row = (y - MAP_Y0) / MAP_CELL + 1;
    g_warp_door = 0;
  }
#else
  (void)area;
#endif
}

static void DrawMap(Surface s, const UiPerf *p) {
  const int area = ShownMapArea();
  const bool station = SmMap_HasMapStation(area);
  int total = 0, seen = 0;
  for (int row = 1; row < kSmMapRows; row++) {
    for (int col = 0; col < kSmMapCols; col++) {
      bool exists, explored;
      SmMap_Cell(area, col, row, &exists, &explored);
      if (!exists) continue;
      total++;
      if (explored) seen++;
      if (!explored && !station) continue;
      UiFillRect(s, col * MAP_CELL, MAP_Y0 + (row - 1) * MAP_CELL, MAP_CELL - 1, MAP_CELL - 1,
                 explored ? MAP_COL_EXPLORED : MAP_COL_KNOWN);
    }
  }

  // Outline the room Samus is in, and mark Samus (blinking).
  int sa, sc, sr;
  if (SmMap_SamusCell(&sa, &sc, &sr) && sa == area) {
    const SmRoom *r = SmMap_CurrentRoom();
    if (r) UiFrameRect(s, r->x * MAP_CELL - 1, MAP_Y0 + r->y * MAP_CELL - 1, r->w * MAP_CELL + 1, r->h * MAP_CELL + 1, MAP_COL_ROOM);
    if ((p->frames / 15) & 1)
      UiFillRect(s, sc * MAP_CELL + 1, MAP_Y0 + (sr - 1) * MAP_CELL + 1, MAP_CELL - 2, MAP_CELL - 2, RGB(255, 255, 255));
  }

  for (int i = 0; i < kSmAreaCount; i++)
    UiDrawButton(s, AreaButtonRect(i), i == area ? COL_TAB_ON : COL_TAB, kAreaShort[i]);
  UiDrawTextf(s, 4, 201, COL_TEXT, "%s  %d/%d CELLS%s", kSmAreaNames[area], seen, total, station ? "  MAP" : "");
  UiDrawButton(s, FollowRect(), g_map_follow ? COL_ON : COL_OFF, g_map_follow ? "FOLLOW: ON" : "FOLLOW: OFF");

#if DEBUG_TOOLS
  if (g_sel_col >= 0)
    UiFrameRect(s, g_sel_col * MAP_CELL - 1, MAP_Y0 + (g_sel_row - 1) * MAP_CELL - 1, MAP_CELL + 1, MAP_CELL + 1, MAP_COL_SELECT);
  const SmRoom *room = SelectedRoom(area);
  if (room) {
    const int doors = SmWarp_DoorCount(room);
    UiDrawTextf(s, 4, 212, MAP_COL_SELECT, "ROOM %04X  %dX%d AT %d,%d  DOORS %d", room->header, room->w, room->h, room->x, room->y, doors);
    if (doors > 0) {
      UiDrawBoxLabel(s, WarpRect(), COL_TAB_ON, COL_BOX_EDGE, COL_TEXT, Pressed(WarpRect()), "WARP HERE");
      char buf[32];
      snprintf(buf, sizeof(buf), "DOOR %d/%d", g_warp_door + 1, doors);
      UiDrawBoxLabel(s, DoorRect(), doors > 1 ? COL_BTN : COL_FAINT, COL_BORDER, COL_TEXT, Pressed(DoorRect()), buf);
    } else {
      UiDrawText(s, 4, 226, 1, COL_WARN, "NO DOOR LEADS TO THIS ROOM");
    }
  } else if (g_sel_col >= 0) {
    UiDrawTextf(s, 4, 212, COL_DIM, "NO ROOM AT %d,%d", g_sel_col, g_sel_row - 1);
  } else {
    UiDrawText(s, 4, 212, 1, COL_DIM, "TAP THE MAP TO PICK A ROOM TO WARP TO");
  }
#endif
}

// ---- Status tab -----------------------------------------------------------------
// In DEBUG_TOOLS builds it is also where the cheats live, as in mzm: tap an item or
// beam to add or remove it, GOD and MAX next to the energy, and the map-station boxes
// unlock an area's map (so any room can be picked for a warp).

static Rect ItemRect(int i) { return (Rect){ 8 + (i % 4) * 77, 111 + (i / 4) * 14, 74, 13 }; }
static Rect BeamRect(int i) { return (Rect){ 8 + i * 61, 165, 58, 13 }; }
static Rect StationRect(int i) { return (Rect){ 8 + i * 51, 191, 49, 14 }; }

#if DEBUG_TOOLS
static Rect GodRect(void) { return (Rect){ 254, 48, 26, 16 }; }
static Rect MaxRect(void) { return (Rect){ 282, 48, 26, 16 }; }

static void DrawCheatButton(Surface s, Rect r, bool on, const char *label) {
  UiDrawBoxLabel(s, r, on ? RGB(110, 85, 20) : RGB(24, 34, 52), on ? RGB(255, 220, 90) : RGB(60, 90, 140),
                 on ? RGB(255, 220, 90) : RGB(150, 190, 230), Pressed(r), label);
}
#endif

static void DrawStatus(Surface s) {
  const unsigned health = samus_health, max_health = samus_max_health;

  // Energy panel: number, tanks, current-tank bar, reserve.
  UiFillRect(s, 8, 26, 304, 44, COL_PANEL);
  UiFrameRect(s, 8, 26, 304, 44, COL_BORDER);
  UiDrawText(s, 14, 30, 1, COL_DIM, "ENERGY");
  {
    char big[8];
    snprintf(big, sizeof(big), "%u", health);
    UiDrawText(s, 14, 40, 2, COL_ENERGY, big);
  }
  const unsigned tanks = max_health >= 199 ? (max_health - 99) / 100 : 0;
  const unsigned full = health >= 100 ? health / 100 : 0;
  for (unsigned i = 0; i < tanks && i < 14; i++) {
    const int x = 70 + (int)i * 12;
    if (i < full) UiFillRect(s, x, 30, 10, 10, COL_ENERGY);
    else UiFrameRect(s, x, 30, 10, 10, COL_FAINT);
  }
  UiDrawBar(s, 70, 44, 168, 7, (int)(health % 100), 99, COL_ENERGY);
  UiDrawTextf(s, 244, 30, COL_DIM, "MAX %u", max_health);
  UiDrawTextf(s, 70, 56, COL_RESERVE, "RESERVE %u/%u", (unsigned)samus_reserve_health, (unsigned)samus_max_reserve_health);
  UiDrawTextf(s, 196, 56, COL_DIM, "%s", reserve_health_mode == 1 ? "AUTO" : reserve_health_mode == 2 ? "MANUAL" : "");
#if DEBUG_TOOLS
  DrawCheatButton(s, GodRect(), g_cheats.invincible, "GOD");
  DrawCheatButton(s, MaxRect(), g_cheats.max_mode, "MAX");
#endif

  // Ammo panels.
  static const struct { const char *name; uint32_t col; } kAmmo[3] = {
    { "MSL", COL_MISSILE }, { "SUPER", COL_SUPER }, { "PB", COL_PBOMB },
  };
  const unsigned cur[3] = { samus_missiles, samus_super_missiles, samus_power_bombs };
  const unsigned max[3] = { samus_max_missiles, samus_max_super_missiles, samus_max_power_bombs };
  for (int i = 0; i < 3; i++) {
    const int x = 8 + i * 103;
    UiFillRect(s, x, 73, 98, 26, COL_PANEL);
    UiFrameRect(s, x, 73, 98, 26, COL_BORDER);
    UiDrawText(s, x + 5, 77, 1, kAmmo[i].col, kAmmo[i].name);
    UiDrawTextf(s, x + 5 + 8 * 6, 77, COL_TEXT, "%u/%u", cur[i], max[i]);
    UiDrawBar(s, x + 5, 88, 88, 7, (int)cur[i], (int)max[i], kAmmo[i].col);
  }

  // Items: green = equipped, yellow = collected but switched off, dim = missing.
  UiDrawText(s, 8, 102, 1, COL_DIM, "ITEMS");
  for (int i = 0; i < kSmItemCount; i++) {
    const Rect r = ItemRect(i);
    const bool have = (collected_items & kSmItems[i].mask) != 0;
    const bool on = (equipped_items & kSmItems[i].mask) != 0;
    UiFillRect(s, r.x, r.y, r.w, r.h, Pressed(r) ? COL_PRESSED : COL_PANEL);
    UiDrawText(s, r.x + 4, r.y + 3, 1, have ? (on ? COL_GOOD : COL_WARN) : COL_FAINT, kSmItems[i].name);
  }
  UiDrawText(s, 8, 156, 1, COL_DIM, "BEAMS");
  for (int i = 0; i < kSmBeamCount; i++) {
    const Rect r = BeamRect(i);
    const bool have = (collected_beams & kSmBeams[i].mask) != 0;
    const bool on = (equipped_beams & kSmBeams[i].mask) != 0;
    UiFillRect(s, r.x, r.y, r.w, r.h, Pressed(r) ? COL_PRESSED : COL_PANEL);
    UiDrawText(s, r.x + 4, r.y + 3, 1, have ? (on ? COL_GOOD : COL_WARN) : COL_FAINT, kSmBeams[i].name);
  }

  // Map stations (Ceres has none). Debug builds: grey none, green used, purple forced,
  // orange every cell explored.
  UiDrawText(s, 8, 182, 1, COL_DIM, "MAP STATIONS");
#if DEBUG_TOOLS
  UiDrawText(s, 104, 182, 1, COL_FAINT, "TAP: MAP > EXPLORED > REAL");
#endif
  for (int i = 0; i < 6; i++) {
    const Rect r = StationRect(i);
    const SmMapDebugState st = SmMap_DebugState(i);
    uint32_t body = RGB(22, 26, 38), edge = RGB(45, 52, 70), text = COL_FAINT;
    if (st == kSmMapDebug_Explored) { body = RGB(70, 45, 15); edge = RGB(255, 170, 60); text = COL_TEXT; }
    else if (st == kSmMapDebug_Station) { body = RGB(45, 30, 70); edge = RGB(170, 110, 240); text = COL_TEXT; }
    else if (SmMap_HasMapStation(i)) { body = RGB(18, 62, 32); edge = RGB(70, 220, 110); text = COL_TEXT; }
    UiDrawBoxLabel(s, r, body, edge, text, Pressed(r), kAreaShort[i]);
  }

  // Where and how long.
  const unsigned area = area_index < 8 ? area_index : 7;
  UiDrawTextf(s, 8, 211, COL_TEXT, "%s  ROOM %02X", kSmAreaNames[area], (unsigned)room_index);
  UiDrawTextf(s, 224, 211, COL_DIM, "%02u:%02u:%02u", (unsigned)game_time_hours, (unsigned)game_time_minutes,
              (unsigned)game_time_seconds);
}

static void StatusTouch(int x, int y) {
#if DEBUG_TOOLS
  if (UiIn(GodRect(), x, y)) {
    g_cheats.invincible = !g_cheats.invincible;
    return;
  }
  if (UiIn(MaxRect(), x, y)) {
    ReportGameplay(Cheats_SetMax(!g_cheats.max_mode));
    return;
  }
  for (int i = 0; i < kSmItemCount; i++)
    if (UiIn(ItemRect(i), x, y)) { ReportGameplay(Cheats_ToggleItem(i)); return; }
  for (int i = 0; i < kSmBeamCount; i++)
    if (UiIn(BeamRect(i), x, y)) { ReportGameplay(Cheats_ToggleBeam(i)); return; }
  for (int i = 0; i < 6; i++) {
    if (!UiIn(StationRect(i), x, y)) continue;
    if (!Cheats_InGameplay()) { ReportGameplay(false); return; }
    SmMap_DebugCycle(i);
    static const char *const kMsg[] = { "Map: real state", "Map: station used", "Map: all explored" };
    Toast(kMsg[SmMap_DebugState(i)]);
    return;
  }
#else
  (void)x; (void)y;
#endif
}

// ---- States tab -----------------------------------------------------------------
// Ten save-state slots (saves/saveN.sav). Each save also writes saves/saveN.txt with
// where and when, which is what the rows show. Save and load need a second tap.

typedef struct {
  bool used, has_info;
  long long saved_at;
  unsigned area, room, health, max_health, missiles, max_missiles;
  unsigned hours, minutes;
} SlotInfo;

static SlotInfo g_slots[STATE_SLOTS];
static int g_arm_slot = -1, g_arm_action;   // action 1 = save, 2 = load
static u64 g_arm_ms;

static void RefreshSlot(int i) {
  SlotInfo *si = &g_slots[i];
  memset(si, 0, sizeof(*si));
  char path[32];
  snprintf(path, sizeof(path), "saves/save%d.sav", i);
  struct stat st;
  si->used = stat(path, &st) == 0;
  snprintf(path, sizeof(path), "saves/save%d.txt", i);
  FILE *f = si->used ? fopen(path, "r") : NULL;
  if (!f) return;
  char line[64];
  while (fgets(line, sizeof(line), f)) {
    char key[24];
    long long v;
    if (sscanf(line, "%23[^=]=%lld", key, &v) != 2) continue;
    if (!strcmp(key, "saved_at")) si->saved_at = v;
    else if (!strcmp(key, "area")) si->area = (unsigned)v;
    else if (!strcmp(key, "room")) si->room = (unsigned)v;
    else if (!strcmp(key, "health")) si->health = (unsigned)v;
    else if (!strcmp(key, "max_health")) si->max_health = (unsigned)v;
    else if (!strcmp(key, "missiles")) si->missiles = (unsigned)v;
    else if (!strcmp(key, "max_missiles")) si->max_missiles = (unsigned)v;
    else if (!strcmp(key, "hours")) si->hours = (unsigned)v;
    else if (!strcmp(key, "minutes")) si->minutes = (unsigned)v;
  }
  fclose(f);
  si->has_info = true;
}

static void RefreshSlots(void) {
  for (int i = 0; i < STATE_SLOTS; i++) RefreshSlot(i);
}

static void WriteSlotInfo(int slot) {
  char path[32];
  snprintf(path, sizeof(path), "saves/save%d.txt", slot);
  FILE *f = fopen(path, "w");
  if (!f) return;
  fprintf(f, "saved_at=%lld\narea=%u\nroom=%u\nhealth=%u\nmax_health=%u\nmissiles=%u\nmax_missiles=%u\nhours=%u\nminutes=%u\n",
          (long long)time(NULL), (unsigned)area_index, (unsigned)room_index, (unsigned)samus_health,
          (unsigned)samus_max_health, (unsigned)samus_missiles, (unsigned)samus_max_missiles,
          (unsigned)game_time_hours, (unsigned)game_time_minutes);
  fclose(f);
}

#define SLOT_Y0 38
#define SLOT_PITCH 19
static Rect SlotSaveRect(int i) { return (Rect){ 252, SLOT_Y0 + i * SLOT_PITCH, 28, 17 }; }
static Rect SlotLoadRect(int i) { return (Rect){ 284, SLOT_Y0 + i * SLOT_PITCH, 28, 17 }; }

static bool Armed(int slot, int action) {
  return g_arm_slot == slot && g_arm_action == action && osGetTime() - g_arm_ms < ARM_MS;
}

// 12x12 floppy disk (from mzm) and a 14x11 folder, centred on (cx, cy).
static void DrawFloppy(Surface s, int cx, int cy, uint32_t ink, uint32_t bg) {
  const int x = cx - 6, y = cy - 6;
  UiFillRect(s, x, y, 12, 12, ink);
  UiFillRect(s, x + 3, y, 6, 4, bg);
  UiFillRect(s, x + 6, y + 1, 2, 2, ink);
  UiFillRect(s, x + 2, y + 7, 8, 5, bg);
  UiFillRect(s, x + 3, y + 8, 6, 1, ink);
  UiFillRect(s, x + 3, y + 10, 6, 1, ink);
}

static void DrawFolder(Surface s, int cx, int cy, uint32_t ink, uint32_t bg) {
  const int x = cx - 7, y = cy - 5;
  UiFillRect(s, x, y, 6, 2, ink);            // tab
  UiFillRect(s, x, y + 2, 14, 9, ink);       // body
  UiFillRect(s, x + 1, y + 4, 12, 1, bg);    // the opening
}

static void DrawSlotButton(Surface s, Rect r, bool enabled, bool armed, bool save) {
  const uint32_t body = !enabled ? RGB(30, 34, 40) : armed ? RGB(120, 90, 20) : save ? RGB(24, 60, 34) : RGB(24, 46, 70);
  const uint32_t edge = save ? RGB(70, 150, 90) : RGB(80, 140, 200);
  const uint32_t ink = enabled ? RGB(200, 235, 220) : RGB(90, 100, 115);
  if (armed) {
    UiDrawBoxLabel(s, r, body, edge, RGB(255, 235, 150), Pressed(r), "OK?");
    return;
  }
  UiDrawBox(s, r, body, edge, Pressed(r));
  if (save) DrawFloppy(s, r.x + r.w / 2, r.y + r.h / 2, ink, body);
  else DrawFolder(s, r.x + r.w / 2, r.y + r.h / 2, ink, body);
}

static void DrawStates(Surface s) {
  UiDrawTextCentered(s, SCREEN_W / 2, 28, COL_TITLE, "SAVE STATES");
  for (int i = 0; i < STATE_SLOTS; i++) {
    const SlotInfo *si = &g_slots[i];
    const int y = SLOT_Y0 + i * SLOT_PITCH;
    UiFillRect(s, 8, y, 240, 17, RGB(14, 22, 34));
    UiFillRect(s, 8, y, 240, 1, RGB(60, 80, 110));
    UiDrawTextf(s, 12, y + 5, COL_TEXT, "%d", i);
    if (!si->used) {
      UiDrawText(s, 26, y + 5, 1, COL_FAINT, "- EMPTY -");
    } else if (!si->has_info) {
      UiDrawText(s, 26, y + 5, 1, COL_DIM, "SAVED (NO DETAILS)");
    } else {
      UiDrawTextf(s, 26, y + 5, RGB(170, 210, 245), "%s %02X", kAreaShort[si->area < kSmAreaCount ? si->area : 0], si->room);
      UiDrawTextf(s, 74, y + 5, COL_ENERGY, "E%u", si->health);
      if (si->max_missiles) UiDrawTextf(s, 110, y + 5, COL_MISSILE, "M%u", si->missiles);
      const time_t t = (time_t)si->saved_at;
      struct tm *tm = gmtime(&t);
      if (tm) UiDrawTextf(s, 150, y + 5, COL_DIM, "%02d-%02d %02d:%02d", tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min);
    }
    DrawSlotButton(s, SlotSaveRect(i), true, Armed(i, 1), true);
    DrawSlotButton(s, SlotLoadRect(i), si->used, Armed(i, 2), false);
  }
  UiDrawTextCentered(s, SCREEN_W / 2, 229, COL_DIM, "TAP TWICE TO CONFIRM");
}

static void StatesTouch(int x, int y) {
  for (int i = 0; i < STATE_SLOTS; i++) {
    int action = UiIn(SlotSaveRect(i), x, y) ? 1 : UiIn(SlotLoadRect(i), x, y) ? 2 : 0;
    if (!action) continue;
    if (action == 2 && !g_slots[i].used) return;
    if (Armed(i, action)) {
      g_ui.save_slot = i;
      if (action == 1) g_ui.req_save_state = true;
      else g_ui.req_load_state = true;
      g_arm_slot = -1;
    } else {
      g_arm_slot = i;
      g_arm_action = action;
      g_arm_ms = osGetTime();
    }
    return;
  }
  g_arm_slot = -1;
}

void BottomUi_StateSaved(int slot, bool ok) {
  if (ok) WriteSlotInfo(slot);
  RefreshSlot(slot);
  char buf[32];
  snprintf(buf, sizeof(buf), ok ? "Saved to slot %d" : "Could not save slot %d", slot);
  Toast(buf);
}

// A loaded state or a reset brings its own RAM: the MAX backup and the forced maps no
// longer describe it.
static void ForgetDebugState(void) {
  g_cheats.max_mode = false;
  SmMap_DebugForget();
}

void BottomUi_StateLoaded(int slot, bool ok) {
  if (ok) ForgetDebugState();
  char buf[40];
  snprintf(buf, sizeof(buf), ok ? "Loaded slot %d" : "Slot %d: cannot load", slot);
  Toast(buf);
}

void BottomUi_GameReset(void) {
  ForgetDebugState();
  Toast("Game reset");
}

// ---- Options tab ----------------------------------------------------------------

typedef enum { OPT_PAUSE, OPT_TURBO, OPT_FRAMESKIP, OPT_AUDIO, OPT_FPS, OPT_SPEEDUP, OPT_DISPLAY, OPT_WIDE, OPT_COUNT } OptCell;

static Rect OptRect(int i) { return (Rect){ 8 + (i % 2) * 154, 30 + (i / 2) * 34, 150, 30 }; }
static Rect ResetRect(void) { return (Rect){ 8, 168, 304, 22 }; }

static void DrawOptCell(Surface s, int i, const char *label, const char *value, uint32_t value_col) {
  const Rect r = OptRect(i);
  UiDrawBox(s, r, RGB(24, 32, 50), RGB(50, 80, 130), Pressed(r));
  UiDrawText(s, r.x + 6, r.y + 5, 1, COL_TEXT, label);
  UiDrawText(s, r.x + 6, r.y + 17, 1, value_col, value);
}

static void DrawOnOffCell(Surface s, int i, const char *label, bool on) {
  DrawOptCell(s, i, label, on ? "ON" : "OFF", on ? COL_GOOD : COL_DIM);
}

static void DrawOptions(Surface s) {
  DrawOnOffCell(s, OPT_PAUSE, "PAUSE", g_ui.paused);
  DrawOnOffCell(s, OPT_TURBO, "TURBO", g_ui.turbo);
  DrawOnOffCell(s, OPT_FRAMESKIP, "FRAME SKIP", g_ui.frameskip);
  DrawOnOffCell(s, OPT_AUDIO, "AUDIO", g_ui.audio_on);
  DrawOnOffCell(s, OPT_FPS, "FPS OVERLAY", g_ui.fps_overlay);
  if (g_is_new3ds) DrawOptCell(s, OPT_SPEEDUP, "CPU (NEW 3DS)", g_ui.new3ds_speedup ? "804 MHZ" : "268 MHZ",
                               g_ui.new3ds_speedup ? COL_GOOD : COL_DIM);
  else DrawOptCell(s, OPT_SPEEDUP, "CPU", "268 MHZ (OLD 3DS)", COL_FAINT);
  DrawOptCell(s, OPT_DISPLAY, "DISPLAY", g_ui.pixel_perfect ? "PIXEL PERFECT" : "SCALED", COL_GOOD);
  DrawOnOffCell(s, OPT_WIDE, "WIDE VIEW", g_ui.wide);
  UiDrawBoxLabel(s, ResetRect(), RGB(64, 22, 22), RGB(180, 60, 60), RGB(255, 150, 150), Pressed(ResetRect()), "RESET GAME");

  UiDrawTextCentered(s, SCREEN_W / 2, 196, RGB(90, 115, 145), "SUPER METROID 3DS");
  UiDrawTextCentered(s, SCREEN_W / 2, 207, RGB(90, 115, 145), g_rom_info.version);
  char buf[48];
  snprintf(buf, sizeof(buf), "ROM %.8s%s", g_rom_info.rom_sha1, g_rom_info.rom_had_header ? " (HEADER)" : "");
  UiDrawTextCentered(s, SCREEN_W / 2, 218, RGB(70, 90, 115), buf);
#if DEBUG_TOOLS
  UiDrawTextCentered(s, SCREEN_W / 2, 229, RGB(150, 110, 60), "DEBUG TOOLS BUILD");
#endif
}

static void OptionsTouch(int x, int y) {
  if (UiIn(ResetRect(), x, y)) {
    g_modal = MODAL_RESET;
    return;
  }
  for (int i = 0; i < OPT_COUNT; i++) {
    if (!UiIn(OptRect(i), x, y)) continue;
    switch ((OptCell)i) {
    case OPT_PAUSE: g_ui.paused = !g_ui.paused; break;
    case OPT_TURBO: g_ui.turbo = !g_ui.turbo; break;
    case OPT_FRAMESKIP: g_ui.frameskip = !g_ui.frameskip; break;
    case OPT_AUDIO: g_ui.audio_on = !g_ui.audio_on; break;
    case OPT_FPS: g_ui.fps_overlay = !g_ui.fps_overlay; break;
    case OPT_SPEEDUP:
      if (!g_is_new3ds) return;
      g_ui.new3ds_speedup = !g_ui.new3ds_speedup;
      osSetSpeedupEnable(g_ui.new3ds_speedup);
      break;
    case OPT_DISPLAY: g_ui.pixel_perfect = !g_ui.pixel_perfect; break;
    case OPT_WIDE: g_ui.wide = !g_ui.wide; break;
    default: break;
    }
    return;
  }
}

// ---- Modals ---------------------------------------------------------------------

static Rect YesRect(void) { return (Rect){ 56, 136, 96, 24 }; }
static Rect NoRect(void) { return (Rect){ 168, 136, 96, 24 }; }

static void DrawResetModal(Surface s) {
  UiFillRect(s, 40, 76, 240, 96, COL_MODAL_EDGE);
  UiFillRect(s, 41, 77, 238, 94, COL_MODAL);
  UiDrawTextCentered(s, SCREEN_W / 2, 90, COL_TITLE, "RESET THE GAME?");
  UiDrawTextCentered(s, SCREEN_W / 2, 108, COL_DIM, "PROGRESS SINCE THE LAST SAVE");
  UiDrawTextCentered(s, SCREEN_W / 2, 118, COL_DIM, "IS LOST");
  UiDrawBoxLabel(s, YesRect(), RGB(64, 22, 22), RGB(180, 60, 60), RGB(255, 150, 150), Pressed(YesRect()), "RESET");
  UiDrawBoxLabel(s, NoRect(), COL_BOX, COL_BOX_EDGE, COL_TEXT, Pressed(NoRect()), "CANCEL");
}

static void ResetModalTouch(int x, int y) {
  if (UiIn(YesRect(), x, y)) {
    g_ui.req_reset = true;
    g_modal = MODAL_NONE;
  } else if (UiIn(NoRect(), x, y)) {
    g_modal = MODAL_NONE;
  }
}

// Debug tools: a 2-column grid in a window over the Debug tab, like mzm's.
#if DEBUG_TOOLS
typedef enum {
  TOOL_DUMP, TOOL_FRAME_DUMP, TOOL_LOG, TOOL_MARK, TOOL_PERF, TOOL_PPU, TOOL_GIVE_ALL, TOOL_HEAL, TOOL_RENDERER,
  TOOL_GPU_CHECK, TOOL_COUNT
} Tool;

static Rect ToolRect(int i) { return (Rect){ 16 + (i % 2) * 148, 44 + (i / 2) * 29, 140, 26 }; }
static Rect CloseRect(void) { return (Rect){ 116, 212, 88, 20 }; }

static void DrawToolCell(Surface s, int i, const char *label, const char *state, uint32_t state_col) {
  const Rect r = ToolRect(i);
  UiDrawBox(s, r, RGB(24, 32, 50), RGB(50, 80, 130), Pressed(r));
  UiDrawText(s, r.x + 6, r.y + 4, 1, COL_TEXT, label);
  UiDrawText(s, r.x + 6, r.y + 15, 1, state_col, state);
}

static void DrawToolsModal(Surface s) {
  UiFillRect(s, 10, 26, 300, 210, COL_MODAL_EDGE);
  UiFillRect(s, 11, 27, 298, 208, COL_MODAL);
  UiDrawText(s, 20, 33, 1, COL_TITLE, "DEBUG TOOLS");
  const uint32_t act = RGB(140, 170, 210);
  DrawToolCell(s, TOOL_DUMP, "SCREEN DUMP", "DUMP SET", act);
  DrawToolCell(s, TOOL_FRAME_DUMP, "FRAME DUMP", "SET + PPU/HDMA LOG", act);
  DrawToolCell(s, TOOL_LOG, "LOG TO SD", Debug_LogEnabled() ? Debug_LogName() : "OFF",
               Debug_LogEnabled() ? COL_GOOD : act);
  DrawToolCell(s, TOOL_MARK, "LOG MARK", Debug_LogEnabled() ? "MARK" : "LOG IS OFF", Debug_LogEnabled() ? act : COL_FAINT);
  DrawToolCell(s, TOOL_PERF, "PERF RECORDER", Debug_PerfRecording() ? "RECORDING" : "OFF",
               Debug_PerfRecording() ? COL_BAD : act);
  DrawToolCell(s, TOOL_PPU, "PPU RENDER", g_ui.render_on ? "ON" : "OFF", g_ui.render_on ? COL_GOOD : COL_WARN);
  DrawToolCell(s, TOOL_GIVE_ALL, "GIVE ALL", "ITEMS, BEAMS, MAX", act);
  DrawToolCell(s, TOOL_HEAL, "FULL HEAL", "ENERGY AND AMMO", act);
  DrawToolCell(s, TOOL_RENDERER, "RENDERER", g_ui.gpu_render ? "GPU (CPU FALLBACK)" : "CPU",
               g_ui.gpu_render ? COL_GOOD : act);
  DrawToolCell(s, TOOL_GPU_CHECK, "GPU CHECK", g_ui.gpu_render ? "GPU VS CPU, DUMP SET" : "RENDERER IS CPU",
               g_ui.gpu_render ? act : COL_FAINT);
  UiDrawTextCentered(s, SCREEN_W / 2, 200, COL_WARN, Debug_LastMessage());
  UiDrawBoxLabel(s, CloseRect(), COL_BOX, COL_BOX_EDGE, COL_TEXT, Pressed(CloseRect()), "CLOSE");
}

static void ToolsModalTouch(int x, int y) {
  if (UiIn(CloseRect(), x, y)) {
    g_modal = MODAL_NONE;
    return;
  }
  for (int i = 0; i < TOOL_COUNT; i++) {
    if (!UiIn(ToolRect(i), x, y)) continue;
    switch ((Tool)i) {
    case TOOL_DUMP: g_ui.req_dump = true; break;
    case TOOL_FRAME_DUMP:
      g_ui.req_frame_dump = true;
      if (g_ui.paused) Toast("Frame dump: waits for unpause");
      break;
    case TOOL_LOG: Debug_LogSetEnabled(!Debug_LogEnabled()); Toast(Debug_LastMessage()); break;
    case TOOL_MARK:
      if (Debug_LogEnabled()) { Debug_LogMark(); Toast("Mark written"); }
      else Toast("Turn the log on first");
      break;
    case TOOL_PERF: Debug_PerfToggle(); Toast(Debug_LastMessage()); break;
    case TOOL_PPU: g_ui.render_on = !g_ui.render_on; break;
    case TOOL_GIVE_ALL: if (Cheats_GiveAll()) Toast("Everything"); else ReportGameplay(false); break;
    case TOOL_HEAL: if (Cheats_FullHeal()) Toast("Refilled"); else ReportGameplay(false); break;
    case TOOL_RENDERER: g_ui.gpu_render = !g_ui.gpu_render; break;
    case TOOL_GPU_CHECK:
      if (!g_ui.gpu_render) Toast("Switch the renderer to GPU first");
      else g_ui.req_gpu_check = true;
      break;
    default: break;
    }
    return;
  }
}

// ---- Debug tab ------------------------------------------------------------------

static Rect ToolsButtonRect(void) { return (Rect){ 8, 202, 304, 22 }; }

static void DrawDebug(Surface s, const UiPerf *p) {
  int y = 30;
  UiDrawTextf(s, 8, y, COL_WARN, "%s", Debug_LastMessage()); y += 12;
  UiDrawTextf(s, 8, y, FpsColor(p->game_fps), "SPEED %.1f", p->game_fps);
  UiDrawTextf(s, 104, y, FpsColor(p->fps), "SHOWN %.1f", p->fps);
  UiDrawTextf(s, 208, y, COL_DIM, "%s", p->is_new3ds ? (g_ui.new3ds_speedup ? "N3DS 804" : "N3DS 268") : "O3DS 268");
  y += 12;
  UiDrawTextf(s, 8, y, COL_TEXT, "WORK %.1f  LOGIC+PPU %.1f MS", p->frame_ms, p->logic_ms); y += 12;
  UiDrawTextf(s, 8, y, COL_TEXT, "TOP DRAW %.1f  AUDIO %.1f MS  CORE1 %lu%%", p->draw_ms, p->audio_ms,
              (unsigned long)p->core1_limit);
  y += 12;
  UiDrawTextf(s, 8, y, COL_TEXT, "AUDIO LOCK %.1f SPC %.1f DSP %.1f RS %.1f", p->audio_part_ms[0], p->audio_part_ms[1],
              p->audio_part_ms[2], p->audio_part_ms[3]);
  y += 16;
  UiDrawTextf(s, 8, y, COL_DIM, "%s ROM %.8s%s %uK FREE", g_rom_info.version, g_rom_info.rom_sha1,
              g_rom_info.rom_had_header ? "*" : "", (unsigned)(linearSpaceFree() / 1024)); y += 12;
  UiDrawTextf(s, 8, y, COL_TEXT, "STATE %02X  AREA %u  ROOM %04X", (unsigned)game_state, (unsigned)area_index,
              (unsigned)room_ptr); y += 12;
  UiDrawTextf(s, 8, y, COL_TEXT, "SAMUS X %u Y %u  POSE %02X", (unsigned)samus_x_pos, (unsigned)samus_y_pos,
              (unsigned)samus_pose); y += 12;
  // Boss flags per area, raw: which bit is which boss differs per area.
  UiDrawTextf(s, 8, y, COL_DIM, "BOSS C%02X B%02X N%02X W%02X M%02X T%02X", boss_bits_for_area[0], boss_bits_for_area[1],
              boss_bits_for_area[2], boss_bits_for_area[3], boss_bits_for_area[4], boss_bits_for_area[5]);
  y += 16;
  UiDrawTextf(s, 8, y, g_ui.gpu_render ? COL_GOOD : COL_DIM, "RENDERER %s  GPU %lu  CPU FALLBACK %lu",
              g_ui.gpu_render ? "GPU" : "CPU", (unsigned long)p->gpu_frames, (unsigned long)p->gpu_fallbacks);
  y += 12;
  if (p->gpu_reason) UiDrawTextf(s, 8, y, COL_WARN, "LAST FALLBACK: %s", p->gpu_reason);
  y += 12;
  if (p->gpu_calibration) UiDrawTextf(s, 8, y, COL_DIM, "%s", p->gpu_calibration);
  UiDrawBoxLabel(s, ToolsButtonRect(), RGB(30, 55, 90), RGB(90, 160, 240), RGB(180, 225, 255), Pressed(ToolsButtonRect()),
                 "DEBUG TOOLS");
}

static void DebugTouch(int x, int y) {
  if (UiIn(ToolsButtonRect(), x, y)) g_modal = MODAL_TOOLS;
}
#endif

// ---- Persistent options -----------------------------------------------------
// Saved to config.ini in the data folder whenever one changes. Not persisted on
// purpose: pause, turbo, PPU render off, cheats and the log/perf recorders,
// which would be confusing or harmful to find switched on at the next boot.

#define CONFIG_PATH "config.ini"

typedef struct { int tab, frameskip, audio, fps_overlay, speedup, pixel_perfect, wide; } SavedOptions;

static SavedOptions CurrentOptions(void) {
  return (SavedOptions){ g_tab, g_ui.frameskip, g_ui.audio_on, g_ui.fps_overlay, g_ui.new3ds_speedup,
                         g_ui.pixel_perfect, g_ui.wide };
}

static void SaveConfig(void) {
  FILE *f = fopen(CONFIG_PATH, "w");
  if (!f) return;
  SavedOptions o = CurrentOptions();
  fprintf(f, "# Super Metroid 3DS options (written by the bottom screen)\n");
  fprintf(f, "tab=%d\nframeskip=%d\naudio=%d\nfps_overlay=%d\nnew3ds_speedup=%d\npixel_perfect=%d\nwide=%d\n",
          o.tab, o.frameskip, o.audio, o.fps_overlay, o.speedup, o.pixel_perfect, o.wide);
  fclose(f);
}

static bool TabVisible(Tab t) {
  Tab tabs[TAB_COUNT];
  const int n = VisibleTabs(tabs);
  for (int i = 0; i < n; i++)
    if (tabs[i] == t) return true;
  return false;
}

static void LoadConfig(void) {
  FILE *f = fopen(CONFIG_PATH, "r");
  if (!f) return;
  char line[64];
  while (fgets(line, sizeof(line), f)) {
    char key[32];
    int v;
    if (sscanf(line, "%31[^=]=%d", key, &v) != 2) continue;
    if (!strcmp(key, "tab") && v >= 0 && v < TAB_COUNT && TabVisible((Tab)v)) g_tab = (Tab)v;
    else if (!strcmp(key, "frameskip")) g_ui.frameskip = v != 0;
    else if (!strcmp(key, "audio")) g_ui.audio_on = v != 0;
    else if (!strcmp(key, "fps_overlay")) g_ui.fps_overlay = v != 0;
    else if (!strcmp(key, "new3ds_speedup")) g_ui.new3ds_speedup = v != 0;
    else if (!strcmp(key, "pixel_perfect")) g_ui.pixel_perfect = v != 0;
    else if (!strcmp(key, "wide")) g_ui.wide = v != 0;
  }
  fclose(f);
}

// ---- Touch and frame --------------------------------------------------------

static void SelectTab(Tab t) {
  g_tab = t;
  g_modal = MODAL_NONE;
  g_arm_slot = -1;
  if (t == TAB_STATES) RefreshSlots();
}

static void TouchDownImpl(int x, int y) {
  Tab tabs[TAB_COUNT];
  const int n = VisibleTabs(tabs);
  for (int i = 0; i < n; i++) {
    if (UiIn(TabRect(i), x, y)) {
      SelectTab(tabs[i]);
      return;
    }
  }
  // A window swallows every touch below the tab bar.
  switch (g_modal) {
  case MODAL_RESET: ResetModalTouch(x, y); return;
#if DEBUG_TOOLS
  case MODAL_TOOLS: ToolsModalTouch(x, y); return;
#endif
  default: break;
  }
  switch (g_tab) {
  case TAB_MAP:     MapTouch(x, y); break;
  case TAB_STATUS:  StatusTouch(x, y); break;
  case TAB_STATES:  StatesTouch(x, y); break;
  case TAB_OPTIONS: OptionsTouch(x, y); break;
#if DEBUG_TOOLS
  case TAB_DEBUG:   DebugTouch(x, y); break;
#endif
  default: break;
  }
}

void BottomUi_TouchDown(int x, int y) {
  g_tap_x = x;
  g_tap_y = y;
  g_tap_ms = osGetTime();
  g_tap_flash_pending = true;
  SavedOptions before = CurrentOptions();
  TouchDownImpl(x, y);
  SavedOptions after = CurrentOptions();
  if (memcmp(&before, &after, sizeof(before)) != 0) SaveConfig();
  g_dirty = 2;
}

void BottomUi_TouchMove(int x, int y) { (void)x; (void)y; }
void BottomUi_TouchUp(void) {}

static void DrawBottom(const UiPerf *p) {
  Surface s = UiDraw_Screen(GFX_BOTTOM);
  UiFillRect(s, 0, 0, SCREEN_W, SCREEN_H, COL_BG);
  DrawTabBar(s);
  switch (g_tab) {
  case TAB_MAP:     DrawMap(s, p); break;
  case TAB_STATUS:  DrawStatus(s); break;
  case TAB_STATES:  DrawStates(s); break;
  case TAB_OPTIONS: DrawOptions(s); break;
#if DEBUG_TOOLS
  case TAB_DEBUG:   DrawDebug(s, p); break;
#endif
  default: break;
  }
  switch (g_modal) {
  case MODAL_RESET: DrawResetModal(s); break;
#if DEBUG_TOOLS
  case MODAL_TOOLS: DrawToolsModal(s); break;
#endif
  default: break;
  }
  if (g_toast[0]) {
    // On the map the bottom rows hold the warp buttons, so use the info line there.
    const int y = g_tab == TAB_MAP && g_modal == MODAL_NONE ? 201 : SCREEN_H - 12;
    const int w = g_tab == TAB_MAP && g_modal == MODAL_NONE ? 222 : SCREEN_W;
    UiFillRect(s, 0, y - 2, w, 12, COL_BG);
    UiDrawText(s, g_tab == TAB_MAP ? 4 : 8, y, 1, COL_WARN, g_toast);
  }
}

bool BottomUi_Frame(const UiPerf *p) {
  const u64 now = osGetTime();
  if (g_toast[0] && now > g_toast_until) {
    g_toast[0] = 0;
    g_dirty = 2;
  }
  if (g_tap_flash_pending && now - g_tap_ms >= TAP_FLASH_MS) {
    g_tap_flash_pending = false;
    g_dirty = 2;
  }
  if (p->frames - g_last_redraw >= REFRESH_FRAMES) {
    g_last_redraw = p->frames;
    g_dirty = 2;
  }
  // The battery moves far slower than anything else here.
  if (g_ptmu && p->frames % 120 == 0) {
    PTMU_GetBatteryLevel(&g_battery);
    PTMU_GetBatteryChargeState(&g_charging);
    if (g_battery > 5) g_battery = 5;
  }
  if (g_dirty > 0) {
    g_dirty--;
    DrawBottom(p);
    return true;
  }
  return false;
}

static void DrawOverlay(Surface s, const UiPerf *p) {
  char buf[16];
  UiFillRect(s, 0, 0, 60, 52, RGB(0, 0, 0));
  snprintf(buf, sizeof(buf), "%.1f", p->game_fps);
  UiDrawText(s, 2, 2, 1, FpsColor(p->game_fps), buf);
  snprintf(buf, sizeof(buf), "L%.1f", p->logic_ms);
  UiDrawText(s, 2, 12, 1, COL_TEXT, buf);
  snprintf(buf, sizeof(buf), "D%.1f", p->draw_ms);
  UiDrawText(s, 2, 22, 1, COL_TEXT, buf);
  snprintf(buf, sizeof(buf), "A%.1f", p->audio_ms);
  UiDrawText(s, 2, 32, 1, COL_TEXT, buf);
  snprintf(buf, sizeof(buf), "S%.1f", p->fps);
  UiDrawText(s, 2, 42, 1, COL_DIM, buf);
}

void BottomUi_DrawTopOverlay(const UiPerf *p) {
  // The game is centred (274 or 256 px wide on a 400 px screen); use the left margin,
  // which the game never redraws. The top screen is double buffered, so clear
  // it for two frames after the overlay is switched off.
  static int clear_frames;
  Surface s = UiDraw_Screen(GFX_TOP);
  if (!g_ui.fps_overlay) {
    if (clear_frames > 0) {
      clear_frames--;
      UiFillRect(s, 0, 0, 60, 52, RGB(0, 0, 0));
    }
    return;
  }
  clear_frames = 2;
  DrawOverlay(s, p);
}

bool BottomUi_DrawOverlayInto(uint32_t *px, int w, int h, const UiPerf *p) {
  if (!g_ui.fps_overlay) return false;
  Surface s = { px, w, h };
  UiFillRect(s, 0, 0, w, h, 0);   // transparent around the box
  DrawOverlay(s, p);
  return true;
}

// ---- Init -------------------------------------------------------------------

bool BottomUi_Init(const UiRomInfo *rom) {
  g_rom_info = *rom;
  SmMap_Init();
  SmWarp_Init();
  APT_CheckNew3DS(&g_is_new3ds);
  g_ui.new3ds_speedup = g_is_new3ds;
  LoadConfig();
  if (!g_is_new3ds) g_ui.new3ds_speedup = false;
  if (g_is_new3ds) osSetSpeedupEnable(g_ui.new3ds_speedup);
  g_ptmu = R_SUCCEEDED(ptmuInit());
  if (g_ptmu) {
    PTMU_GetBatteryLevel(&g_battery);
    PTMU_GetBatteryChargeState(&g_charging);
  }
  if (g_tab == TAB_STATES) RefreshSlots();
  g_dirty = 2;
  return UiDraw_Init();
}

void BottomUi_Exit(void) {
  if (g_ptmu) ptmuExit();
  osSetSpeedupEnable(false);
}
