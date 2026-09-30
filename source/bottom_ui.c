#include "bottom_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>

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
#define REFRESH_FRAMES 15   // periodic redraw so live numbers keep moving

UiOptions g_ui = {
  .audio_on = true,
  .render_on = true,
  .frameskip = true,
  .new3ds_speedup = true,
};

typedef enum { TAB_STATUS, TAB_MAP, TAB_CHEATS, TAB_OPTIONS, TAB_DEBUG, TAB_COUNT } Tab;
static const char *const kTabNames[TAB_COUNT] = { "STATUS", "MAP", "CHEATS", "OPTIONS", "DEBUG" };

static UiRomInfo g_rom_info;
static Tab g_tab = TAB_STATUS;
static int g_dirty = 2;             // frames left to redraw (bottom is double buffered)
static uint32_t g_last_redraw;
static bool g_is_new3ds;
static char g_toast[40];
static u64 g_toast_until;

// ---- Layout (shared by drawing and hit testing) ----------------------------

#define TAB_H 22
#define ROW_Y0 28
#define ROW_H 22
#define ROW_PAD 4

static Rect TabRect(int i) { return (Rect){ i * (SCREEN_W / TAB_COUNT), 0, SCREEN_W / TAB_COUNT - 2, TAB_H }; }
static Rect RowRect(int row) { return (Rect){ 8, ROW_Y0 + row * ROW_H, SCREEN_W - 16, ROW_H - ROW_PAD }; }
static Rect HalfRect(int row, int half) {
  Rect r = RowRect(row);
  r.w = (r.w - 6) / 2;
  if (half) r.x += r.w + 6;
  return r;
}

void BottomUi_Toast(const char *msg) {
  snprintf(g_toast, sizeof(g_toast), "%s", msg);
  g_toast_until = osGetTime() + 1500;
  g_dirty = 2;
}
#define Toast BottomUi_Toast

static const char *OnOff(bool b) { return b ? "ON" : "OFF"; }

static void DrawToggle(Surface s, Rect r, const char *name, bool on) {
  char buf[48];
  snprintf(buf, sizeof(buf), "%s: %s", name, OnOff(on));
  UiDrawButton(s, r, on ? COL_ON : COL_OFF, buf);
}

static uint32_t FpsColor(float fps) { return fps >= 58.0f ? COL_GOOD : fps >= 45.0f ? COL_WARN : COL_BAD; }

// ---- Status tab -------------------------------------------------------------

static void DrawStatus(Surface s, const UiPerf *p) {
  const unsigned health = samus_health, max_health = samus_max_health;

  // Energy panel: number, tanks, current-tank bar, reserve.
  UiFillRect(s, 8, 28, 304, 58, COL_PANEL);
  UiFrameRect(s, 8, 28, 304, 58, COL_BORDER);
  UiDrawText(s, 14, 33, 1, COL_DIM, "ENERGY");
  {
    char big[8];
    snprintf(big, sizeof(big), "%u", health);
    UiFillRect(s, 14, 44, 48, 18, COL_PANEL);
    UiDrawText(s, 14, 44, 2, COL_ENERGY, big);
  }
  const unsigned tanks = max_health >= 199 ? (max_health - 99) / 100 : 0;
  const unsigned full = health >= 100 ? health / 100 : 0;
  for (unsigned i = 0; i < tanks && i < 14; i++) {
    const int x = 70 + (int)i * 12;
    if (i < full) UiFillRect(s, x, 34, 10, 10, COL_ENERGY);
    else UiFrameRect(s, x, 34, 10, 10, COL_FAINT);
  }
  UiDrawBar(s, 70, 50, 168, 7, (int)(health % 100), 99, COL_ENERGY);
  UiDrawTextf(s, 244, 34, COL_DIM, "max %u", max_health);
  UiDrawTextf(s, 70, 66, COL_RESERVE, "RESERVE %u/%u", (unsigned)samus_reserve_health, (unsigned)samus_max_reserve_health);
  UiDrawTextf(s, 214, 66, COL_DIM, "%s", reserve_health_mode == 1 ? "auto" : reserve_health_mode == 2 ? "manual" : "none");

  // Ammo panels.
  static const struct { const char *name; uint32_t col; } kAmmo[3] = {
    { "MSL", COL_MISSILE }, { "SUPER", COL_SUPER }, { "PB", COL_PBOMB },
  };
  const unsigned cur[3] = { samus_missiles, samus_super_missiles, samus_power_bombs };
  const unsigned max[3] = { samus_max_missiles, samus_max_super_missiles, samus_max_power_bombs };
  for (int i = 0; i < 3; i++) {
    const int x = 8 + i * 103;
    UiFillRect(s, x, 92, 98, 28, COL_PANEL);
    UiFrameRect(s, x, 92, 98, 28, COL_BORDER);
    UiDrawText(s, x + 5, 96, 1, kAmmo[i].col, kAmmo[i].name);
    UiDrawTextf(s, x + 5 + (int)(8 * 6), 96, COL_TEXT, "%u/%u", cur[i], max[i]);
    UiDrawBar(s, x + 5, 108, 88, 7, (int)cur[i], (int)max[i], kAmmo[i].col);
  }

  // Items: green = equipped, yellow = collected but switched off, dim = missing.
  UiDrawText(s, 8, 126, 1, COL_DIM, "ITEMS");
  for (int i = 0; i < kSmItemCount; i++) {
    const int x = 8 + (i % 4) * 77, y = 136 + (i / 4) * 15;
    const bool have = (collected_items & kSmItems[i].mask) != 0;
    const bool on = (equipped_items & kSmItems[i].mask) != 0;
    UiFillRect(s, x, y, 74, 13, COL_PANEL);
    UiDrawText(s, x + 4, y + 3, 1, have ? (on ? COL_GOOD : COL_WARN) : COL_FAINT, kSmItems[i].name);
  }
  UiDrawText(s, 8, 184, 1, COL_DIM, "BEAMS");
  for (int i = 0; i < kSmBeamCount; i++) {
    const int x = 8 + i * 61, y = 194;
    const bool have = (collected_beams & kSmBeams[i].mask) != 0;
    const bool on = (equipped_beams & kSmBeams[i].mask) != 0;
    UiFillRect(s, x, y, 58, 13, COL_PANEL);
    UiDrawText(s, x + 4, y + 3, 1, have ? (on ? COL_GOOD : COL_WARN) : COL_FAINT, kSmBeams[i].name);
  }

  // Where and how long.
  const unsigned area = area_index < 8 ? area_index : 7;
  UiDrawTextf(s, 8, 208, COL_TEXT, "%s  room %02X", kSmAreaNames[area], (unsigned)room_index);
  UiDrawTextf(s, 224, 208, COL_DIM, "%02u:%02u:%02u", (unsigned)game_time_hours, (unsigned)game_time_minutes,
              (unsigned)game_time_seconds);
  // Boss flags per area, raw: which bit is which boss differs per area.
  UiDrawTextf(s, 8, 218, COL_DIM, "boss C%02X B%02X N%02X W%02X M%02X T%02X", boss_bits_for_area[0], boss_bits_for_area[1],
              boss_bits_for_area[2], boss_bits_for_area[3], boss_bits_for_area[4], boss_bits_for_area[5]);
  (void)p;
}

// ---- Map tab ----------------------------------------------------------------
// One area at a time: 64x32 cells of 5 px fill the screen width, so no zoom or
// scrolling is needed. Row 0 of the tilemap is an empty margin and is not drawn.

#define MAP_CELL 5
#define MAP_Y0 24
#define MAP_COL_EXPLORED  RGB(214, 96, 150)
#define MAP_COL_KNOWN     RGB(56, 64, 104)
#define MAP_COL_ROOM      RGB(250, 220, 90)
#define MAP_COL_SELECT    RGB(90, 220, 240)

static int g_map_area;              // area being shown
static bool g_map_follow = true;    // follow the area Samus is in
static int g_sel_col = -1, g_sel_row = -1;
static int g_warp_door;             // which of the doors into the selected room to use

static const char *const kAreaShort[kSmAreaCount] = { "CRA", "BRI", "NOR", "WRE", "MAR", "TOU", "CER" };

static Rect AreaButtonRect(int i) { return (Rect){ 2 + i * 45, 180, 43, 13 }; }
static Rect FollowRect(void) { return (Rect){ 226, 194, 92, 13 }; }
static Rect WarpRect(void) { return (Rect){ 4, 218, 150, 14 }; }
static Rect DoorRect(void) { return (Rect){ 160, 218, 96, 14 }; }

static const SmRoom *SelectedRoom(int area) {
  return g_sel_col >= 0 ? SmMap_RoomAt(area, g_sel_col, g_sel_row) : NULL;
}

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
      g_dirty = 2;
      return;
    }
  }
  const SmRoom *room = SelectedRoom(area);
  if (UiIn(FollowRect(), x, y)) {
    g_map_follow = !g_map_follow;
  } else if (room && UiIn(WarpRect(), x, y)) {
    Toast(SmWarp_ResultText(SmWarp_ToRoom(room, g_warp_door)));
  } else if (room && UiIn(DoorRect(), x, y)) {
    const int n = SmWarp_DoorCount(room);
    if (n > 1) g_warp_door = (g_warp_door + 1) % n;
  } else if (y >= MAP_Y0 && y < MAP_Y0 + (kSmMapRows - 1) * MAP_CELL) {
    g_sel_col = x / MAP_CELL;
    g_sel_row = (y - MAP_Y0) / MAP_CELL + 1;
    g_warp_door = 0;
  } else {
    return;
  }
  g_dirty = 2;
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
  const bool here = SmMap_SamusCell(&sa, &sc, &sr) && sa == area;
  if (here) {
    const SmRoom *r = SmMap_CurrentRoom();
    if (r) UiFrameRect(s, r->x * MAP_CELL - 1, MAP_Y0 + r->y * MAP_CELL - 1, r->w * MAP_CELL + 1, r->h * MAP_CELL + 1, MAP_COL_ROOM);
    if ((p->frames / 15) & 1)
      UiFillRect(s, sc * MAP_CELL + 1, MAP_Y0 + (sr - 1) * MAP_CELL + 1, MAP_CELL - 2, MAP_CELL - 2, RGB(255, 255, 255));
  }
  if (g_sel_col >= 0)
    UiFrameRect(s, g_sel_col * MAP_CELL - 1, MAP_Y0 + (g_sel_row - 1) * MAP_CELL - 1, MAP_CELL + 1, MAP_CELL + 1, MAP_COL_SELECT);

  for (int i = 0; i < kSmAreaCount; i++)
    UiDrawButton(s, AreaButtonRect(i), i == area ? COL_TAB_ON : COL_TAB, kAreaShort[i]);
  UiDrawTextf(s, 4, 197, COL_TEXT, "%s  %d/%d CELLS%s", kSmAreaNames[area], seen, total, station ? "  MAP" : "");
  UiDrawButton(s, FollowRect(), g_map_follow ? COL_ON : COL_OFF, g_map_follow ? "FOLLOW: ON" : "FOLLOW: OFF");

  const SmRoom *room = SelectedRoom(area);
  if (room) {
    const int doors = SmWarp_DoorCount(room);
    UiDrawTextf(s, 4, 208, MAP_COL_SELECT, "ROOM %04X  %dX%d AT %d,%d  DOORS %d", room->header, room->w, room->h, room->x, room->y, doors);
    if (doors > 0) {
      UiDrawButton(s, WarpRect(), COL_TAB_ON, "WARP HERE");
      char buf[32];
      snprintf(buf, sizeof(buf), "DOOR %d/%d", g_warp_door + 1, doors);
      UiDrawButton(s, DoorRect(), doors > 1 ? COL_BTN : COL_FAINT, buf);
    } else {
      UiDrawText(s, 4, 222, 1, COL_WARN, "NO DOOR LEADS TO THIS ROOM");
    }
  } else if (g_sel_col >= 0) {
    UiDrawTextf(s, 4, 208, COL_DIM, "NO ROOM AT %d,%d", g_sel_col, g_sel_row - 1);
  } else {
    UiDrawText(s, 4, 208, 1, COL_DIM, "TAP THE MAP TO SELECT A ROOM");
  }
}

// ---- Cheats tab -------------------------------------------------------------

enum { CHT_GOD, CHT_AMMO, CHT_ITEMS, CHT_MAXES, CHT_HEAL };

static void ReportCheat(bool ok, const char *done) {
  Toast(ok ? done : "Only works inside a room");
}

static void CheatsTouch(int x, int y) {
  if (UiIn(RowRect(CHT_GOD), x, y)) g_cheats.invincible = !g_cheats.invincible;
  else if (UiIn(RowRect(CHT_AMMO), x, y)) g_cheats.infinite_ammo = !g_cheats.infinite_ammo;
  else if (UiIn(HalfRect(CHT_ITEMS, 0), x, y)) ReportCheat(Cheats_GiveAllItems(), "All items");
  else if (UiIn(HalfRect(CHT_ITEMS, 1), x, y)) ReportCheat(Cheats_GiveAllBeams(), "All beams");
  else if (UiIn(HalfRect(CHT_MAXES, 0), x, y)) ReportCheat(Cheats_MaxAmmo(), "Max ammo");
  else if (UiIn(HalfRect(CHT_MAXES, 1), x, y)) ReportCheat(Cheats_MaxEnergy(), "Max energy");
  else if (UiIn(HalfRect(CHT_HEAL, 0), x, y)) ReportCheat(Cheats_FullHeal(), "Refilled");
  else if (UiIn(HalfRect(CHT_HEAL, 1), x, y)) {
    bool ok = Cheats_GiveAllItems();
    ok &= Cheats_GiveAllBeams();
    ok &= Cheats_MaxAmmo();
    ok &= Cheats_MaxEnergy();
    ReportCheat(ok, "Everything");
  }
  else return;
  g_dirty = 2;
}

static void DrawCheats(Surface s) {
  DrawToggle(s, RowRect(CHT_GOD), "God mode (invincible)", g_cheats.invincible);
  DrawToggle(s, RowRect(CHT_AMMO), "Infinite ammo", g_cheats.infinite_ammo);
  UiDrawButton(s, HalfRect(CHT_ITEMS, 0), COL_BTN, "ALL ITEMS");
  UiDrawButton(s, HalfRect(CHT_ITEMS, 1), COL_BTN, "ALL BEAMS");
  UiDrawButton(s, HalfRect(CHT_MAXES, 0), COL_BTN, "MAX AMMO");
  UiDrawButton(s, HalfRect(CHT_MAXES, 1), COL_BTN, "MAX ENERGY");
  UiDrawButton(s, HalfRect(CHT_HEAL, 0), COL_BTN, "FULL HEAL");
  UiDrawButton(s, HalfRect(CHT_HEAL, 1), COL_BTN, "EVERYTHING");
  const int y = ROW_Y0 + 5 * ROW_H + 8;
  if (Cheats_InGameplay()) UiDrawText(s, 8, y, 1, COL_GOOD, "In a room: cheats active");
  else UiDrawText(s, 8, y, 1, COL_WARN, "Not in a room: cheats paused");
  UiDrawText(s, 8, y + 14, 1, COL_DIM, "God mode and infinite ammo refill");
  UiDrawText(s, 8, y + 26, 1, COL_DIM, "around every frame. Items and");
  UiDrawText(s, 8, y + 38, 1, COL_DIM, "beams apply at once; the suit");
  UiDrawText(s, 8, y + 50, 1, COL_DIM, "colour may update on next room.");
}

// ---- Options tab ------------------------------------------------------------

typedef enum {
  OPT_PAUSE, OPT_TURBO, OPT_FRAMESKIP, OPT_AUDIO, OPT_FPS, OPT_SPEEDUP, OPT_SLOT, OPT_STATE, OPT_RESET, OPT_ROWS
} OptRow;

static void OptionsTouch(int x, int y) {
  if (UiIn(RowRect(OPT_PAUSE), x, y)) g_ui.paused = !g_ui.paused;
  else if (UiIn(RowRect(OPT_TURBO), x, y)) g_ui.turbo = !g_ui.turbo;
  else if (UiIn(RowRect(OPT_FRAMESKIP), x, y)) g_ui.frameskip = !g_ui.frameskip;
  else if (UiIn(RowRect(OPT_AUDIO), x, y)) g_ui.audio_on = !g_ui.audio_on;
  else if (UiIn(RowRect(OPT_FPS), x, y)) g_ui.fps_overlay = !g_ui.fps_overlay;
  else if (UiIn(RowRect(OPT_SPEEDUP), x, y)) {
    if (!g_is_new3ds) return;
    g_ui.new3ds_speedup = !g_ui.new3ds_speedup;
    osSetSpeedupEnable(g_ui.new3ds_speedup);
  }
  else if (UiIn(HalfRect(OPT_SLOT, 0), x, y)) g_ui.save_slot = (g_ui.save_slot + 9) % 10;
  else if (UiIn(HalfRect(OPT_SLOT, 1), x, y)) g_ui.save_slot = (g_ui.save_slot + 1) % 10;
  else if (UiIn(HalfRect(OPT_STATE, 0), x, y)) { g_ui.req_save_state = true; Toast("State saved"); }
  else if (UiIn(HalfRect(OPT_STATE, 1), x, y)) { g_ui.req_load_state = true; Toast("State loaded"); }
  else if (UiIn(RowRect(OPT_RESET), x, y)) { g_ui.req_reset = true; Toast("Game reset"); }
  else return;
  g_dirty = 2;
}

static void DrawOptions(Surface s) {
  DrawToggle(s, RowRect(OPT_PAUSE), "Pause", g_ui.paused);
  DrawToggle(s, RowRect(OPT_TURBO), "Turbo", g_ui.turbo);
  DrawToggle(s, RowRect(OPT_FRAMESKIP), "Frameskip", g_ui.frameskip);
  DrawToggle(s, RowRect(OPT_AUDIO), "Audio", g_ui.audio_on);
  DrawToggle(s, RowRect(OPT_FPS), "FPS overlay (top)", g_ui.fps_overlay);
  if (g_is_new3ds) DrawToggle(s, RowRect(OPT_SPEEDUP), "New3DS 804MHz", g_ui.new3ds_speedup);
  else UiDrawButton(s, RowRect(OPT_SPEEDUP), COL_BTN, "New3DS 804MHz: n/a");
  char buf[24];
  snprintf(buf, sizeof(buf), "< slot %d", g_ui.save_slot);
  UiDrawButton(s, HalfRect(OPT_SLOT, 0), COL_BTN, buf);
  UiDrawButton(s, HalfRect(OPT_SLOT, 1), COL_BTN, "slot >");
  UiDrawButton(s, HalfRect(OPT_STATE, 0), COL_BTN, "SAVE STATE");
  UiDrawButton(s, HalfRect(OPT_STATE, 1), COL_BTN, "LOAD STATE");
  UiDrawButton(s, RowRect(OPT_RESET), COL_BTN, "RESET GAME");
}

// ---- Debug tab --------------------------------------------------------------

enum { DBG_PPU, DBG_AUDIO, DBG_LOG, DBG_ACTIONS, DBG_PERF };

static void DebugTouch(int x, int y) {
  if (UiIn(RowRect(DBG_PPU), x, y)) g_ui.render_on = !g_ui.render_on;
  else if (UiIn(RowRect(DBG_AUDIO), x, y)) g_ui.audio_on = !g_ui.audio_on;
  else if (UiIn(RowRect(DBG_LOG), x, y)) { Debug_LogSetEnabled(!Debug_LogEnabled()); Toast(Debug_LastMessage()); }
  else if (UiIn(HalfRect(DBG_ACTIONS, 0), x, y)) { Debug_LogMark(); Toast(Debug_LogEnabled() ? Debug_LastMessage() : "Turn the log on first"); }
  else if (UiIn(HalfRect(DBG_ACTIONS, 1), x, y)) g_ui.req_dump = true;
  else if (UiIn(RowRect(DBG_PERF), x, y)) { Debug_PerfToggle(); Toast(Debug_LastMessage()); }
  else return;
  g_dirty = 2;
}

static void DrawDebug(Surface s, const UiPerf *p) {
  DrawToggle(s, RowRect(DBG_PPU), "PPU render", g_ui.render_on);
  DrawToggle(s, RowRect(DBG_AUDIO), "Audio", g_ui.audio_on);
  char buf[48];
  snprintf(buf, sizeof(buf), "Log to SD: %s", Debug_LogEnabled() ? Debug_LogName() : "OFF");
  UiDrawButton(s, RowRect(DBG_LOG), Debug_LogEnabled() ? COL_ON : COL_OFF, buf);
  UiDrawButton(s, HalfRect(DBG_ACTIONS, 0), COL_BTN, "LOG MARK");
  UiDrawButton(s, HalfRect(DBG_ACTIONS, 1), COL_BTN, "DUMP SCREEN");
  UiDrawButton(s, RowRect(DBG_PERF), Debug_PerfRecording() ? COL_ON : COL_BTN,
               Debug_PerfRecording() ? "PERF: RECORDING (tap to stop)" : "PERF: RECORD FRAME TIMES");
  int y = ROW_Y0 + 5 * ROW_H + 4;
  UiDrawTextf(s, 8, y, COL_WARN, "%s", Debug_LastMessage()); y += 12;
  UiDrawTextf(s, 8, y, FpsColor(p->game_fps), "speed %.1f", p->game_fps);
  UiDrawTextf(s, 104, y, FpsColor(p->fps), "shown %.1f", p->fps);
  UiDrawTextf(s, 208, y, COL_DIM, "%s", p->is_new3ds ? (g_ui.new3ds_speedup ? "N3DS 804" : "N3DS 268") : "O3DS 268");
  y += 12;
  UiDrawTextf(s, 8, y, COL_TEXT, "work %.1f  logic+PPU %.1f ms", p->frame_ms, p->logic_ms); y += 12;
  UiDrawTextf(s, 8, y, COL_TEXT, "top draw %.1f  audio %.1f ms", p->draw_ms, p->audio_ms); y += 12;
  UiDrawTextf(s, 8, y, COL_TEXT, "audio lock %.1f spc %.1f dsp %.1f rs %.1f", p->audio_part_ms[0], p->audio_part_ms[1], p->audio_part_ms[2], p->audio_part_ms[3]); y += 12;
  UiDrawTextf(s, 8, y, COL_TEXT, "%s ROM %.8s%s %uK free", g_rom_info.version, g_rom_info.rom_sha1,
              g_rom_info.rom_had_header ? "*" : "", (unsigned)(linearSpaceFree() / 1024)); y += 12;
  UiDrawTextf(s, 8, y, COL_TEXT, "state %02X  samus x %u y %u", (unsigned)game_state, (unsigned)samus_x_pos, (unsigned)samus_y_pos);
}

// ---- Persistent options -----------------------------------------------------
// Saved to config.ini in the data folder whenever one changes. Not persisted on
// purpose: pause, turbo, PPU render off, cheats and the log/perf recorders,
// which would be confusing or harmful to find switched on at the next boot.

#define CONFIG_PATH "config.ini"

typedef struct { int tab, frameskip, audio, fps_overlay, speedup, save_slot; } SavedOptions;

static SavedOptions CurrentOptions(void) {
  return (SavedOptions){ g_tab, g_ui.frameskip, g_ui.audio_on, g_ui.fps_overlay, g_ui.new3ds_speedup, g_ui.save_slot };
}

static void SaveConfig(void) {
  FILE *f = fopen(CONFIG_PATH, "w");
  if (!f) return;
  SavedOptions o = CurrentOptions();
  fprintf(f, "# Super Metroid 3DS options (written by the bottom screen)\n");
  fprintf(f, "tab=%d\nframeskip=%d\naudio=%d\nfps_overlay=%d\nnew3ds_speedup=%d\nsave_slot=%d\n", o.tab, o.frameskip,
          o.audio, o.fps_overlay, o.speedup, o.save_slot);
  fclose(f);
}

static void LoadConfig(void) {
  FILE *f = fopen(CONFIG_PATH, "r");
  if (!f) return;
  char line[64];
  while (fgets(line, sizeof(line), f)) {
    char key[32];
    int v;
    if (sscanf(line, "%31[^=]=%d", key, &v) != 2) continue;
    if (!strcmp(key, "tab") && v >= 0 && v < TAB_COUNT) g_tab = (Tab)v;
    else if (!strcmp(key, "frameskip")) g_ui.frameskip = v != 0;
    else if (!strcmp(key, "audio")) g_ui.audio_on = v != 0;
    else if (!strcmp(key, "fps_overlay")) g_ui.fps_overlay = v != 0;
    else if (!strcmp(key, "new3ds_speedup")) g_ui.new3ds_speedup = v != 0;
    else if (!strcmp(key, "save_slot") && v >= 0 && v < 10) g_ui.save_slot = v;
  }
  fclose(f);
}

// ---- Touch and frame --------------------------------------------------------

static void TouchDownImpl(int x, int y) {
  for (int i = 0; i < TAB_COUNT; i++) {
    if (UiIn(TabRect(i), x, y)) {
      g_tab = (Tab)i;
      g_dirty = 2;
      return;
    }
  }
  switch (g_tab) {
  case TAB_MAP:     MapTouch(x, y); break;
  case TAB_CHEATS:  CheatsTouch(x, y); break;
  case TAB_OPTIONS: OptionsTouch(x, y); break;
  case TAB_DEBUG:   DebugTouch(x, y); break;
  default: break;
  }
}

void BottomUi_TouchDown(int x, int y) {
  SavedOptions before = CurrentOptions();
  TouchDownImpl(x, y);
  SavedOptions after = CurrentOptions();
  if (memcmp(&before, &after, sizeof(before)) != 0) SaveConfig();
}

void BottomUi_TouchMove(int x, int y) { (void)x; (void)y; }
void BottomUi_TouchUp(void) {}

static void DrawBottom(const UiPerf *p) {
  Surface s = UiDraw_Screen(GFX_BOTTOM);
  UiFillRect(s, 0, 0, SCREEN_W, SCREEN_H, COL_BG);
  for (int i = 0; i < TAB_COUNT; i++)
    UiDrawButton(s, TabRect(i), i == (int)g_tab ? COL_TAB_ON : COL_TAB, kTabNames[i]);
  switch (g_tab) {
  case TAB_STATUS:  DrawStatus(s, p); break;
  case TAB_MAP:     DrawMap(s, p); break;
  case TAB_CHEATS:  DrawCheats(s); break;
  case TAB_OPTIONS: DrawOptions(s); break;
  case TAB_DEBUG:   DrawDebug(s, p); break;
  default: break;
  }
  if (g_toast[0]) {
    // On the map the bottom rows hold the warp buttons, so show it on the info line.
    const int ty = g_tab == TAB_MAP ? 197 : SCREEN_H - 11;
    UiFillRect(s, 0, ty - 2, g_tab == TAB_MAP ? 222 : SCREEN_W, 11, COL_BG);
    UiDrawText(s, g_tab == TAB_MAP ? 4 : 8, ty, 1, COL_WARN, g_toast);
  }
  // Visible from every tab, over the right end of the tab bar.
  if (Debug_PerfRecording()) UiDrawText(s, SCREEN_W - 32, 7, 1, COL_BAD, "REC");
  if (g_cheats.invincible || g_cheats.infinite_ammo) UiDrawText(s, SCREEN_W - 32, SCREEN_H - 11, 1, COL_WARN, "CHT");
}

bool BottomUi_Frame(const UiPerf *p) {
  if (g_toast[0] && osGetTime() > g_toast_until) {
    g_toast[0] = 0;
    g_dirty = 2;
  }
  if (p->frames - g_last_redraw >= REFRESH_FRAMES) {
    g_last_redraw = p->frames;
    g_dirty = 2;
  }
  if (g_dirty > 0) {
    g_dirty--;
    DrawBottom(p);
    return true;
  }
  return false;
}

void BottomUi_DrawTopOverlay(const UiPerf *p) {
  // The game is centred (274 px wide on a 400 px screen); use the left margin,
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
  g_dirty = 2;
  return UiDraw_Init();
}

void BottomUi_Exit(void) {
  osSetSpeedupEnable(false);
}
