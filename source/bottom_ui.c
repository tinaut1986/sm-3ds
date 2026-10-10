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
#include "game_text.h"
#include "modern_controls.h"
#include "debug_tools.h"
#include "scene_rec.h"
#include "sm_map.h"
#include "sm_warp.h"
#include "stereo_depth.h"
#include "ui_draw.h"
#include "updater.h"
#include "sm_wide.h"
#include "states_store.h"
#include "ui_lang.h"
#include "retro_ach.h"

#define SCREEN_W 320
#define SCREEN_H 240
#define REFRESH_FRAMES 15   // periodic redraw so live numbers (and the clock) keep moving
#define TAP_FLASH_MS 150    // how long a tapped button shows pressed
#define ARM_MS 2000         // a save-state button waits this long for its second tap

UiOptions g_ui = {
  .audio_on = true,
  .pacing = kPaceAuto,
  .auto_update = true,
  .update_beta = DEBUG_TOOLS != 0,   // pre-release builds follow the betas
  .new3ds_speedup = true,
  // On in every build: it is what makes Old 3DS playable (2DS: ~60 fps against ~25 with
  // the CPU renderer), and any frame it cannot draw goes to the CPU renderer anyway.
  // DEBUG_TOOLS builds can switch it off for the session (Debug tab, RENDERER).
  .gpu_render = true,
};

// Tab order as drawn, like mzm. DEBUG exists only in DEBUG_TOOLS builds.
// The values are what config.ini stores (`tab`): append only.
typedef enum { TAB_MAP, TAB_STATUS, TAB_DEBUG, TAB_STATES, TAB_OPTIONS, TAB_ACHIEVEMENTS, TAB_COUNT } Tab;

typedef enum { MODAL_NONE, MODAL_RESET, MODAL_TOOLS, MODAL_RA_DETAIL, MODAL_REPORT, MODAL_STATE, MODAL_NOTES, MODAL_ITEMS, MODAL_CONTROLS } Modal;

static UiRomInfo g_rom_info;
static Tab g_tab = TAB_STATUS;
static Modal g_modal;
static int g_dirty = 2;             // frames left to redraw (bottom is double buffered)
// A part of the screen changed without a full redraw (the STATUS tab's clock, the MAP tab's blinking marker): only that
// rectangle is converted and presented, for two frames like g_dirty (P2.7).
static int g_part;
static Rect g_part_rect;
static bool g_present_part;         // what the last BottomUi_Frame asked for is g_present_rect only
static Rect g_present_rect;
static uint32_t g_live_key;         // what the open live tab shows (LiveKey) at its last full redraw
static uint32_t g_last_redraw;
static bool g_is_new3ds;
// OPTIONS -> SPOILERS. Off (the default, like mzm) hides what Samus has not found yet: the names on
// the STATUS tab, the item totals of the map and its window, and the map dots the game does not draw.
static bool g_show_spoilers;
static char g_toast[64];   // UTF-8
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

// A tap that landed on a window or the update prompt (an overlay) lights only that overlay's buttons,
// not the ones of the tab under it that happen to sit at the same place.
static bool g_tap_on_overlay, g_drawing_overlay;
static bool Pressed(Rect r) {
  return osGetTime() - g_tap_ms < TAP_FLASH_MS && g_tap_on_overlay == g_drawing_overlay && UiIn(r, g_tap_x, g_tap_y);
}

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
  out[n++] = TAB_ACHIEVEMENTS;
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
  case TAB_ACHIEVEMENTS:   // trophy: cup, handles, stem, base
    R(-4, -6, 9, 6); R(-3, 0, 7, 1); R(-6, -6, 2, 1); R(-7, -5, 1, 3); R(-6, -2, 2, 1);
    R(5, -6, 2, 1); R(7, -5, 1, 3); R(5, -2, 2, 1); R(-1, 1, 3, 3); R(-4, 4, 9, 2);
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
    if (g_charging) {
      // Plugged in and charging: a lightning bolt over the battery, 5x8, dark edge so it
      // reads on the fill, on the shell and on the empty part alike.
      static const uint8_t kBolt[8] = { 0x03, 0x06, 0x0C, 0x1E, 0x07, 0x06, 0x0C, 0x18 };   // bit 4 = left
      for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < 8; y++) {
          for (int x = 0; x < 5; x++) {
            if (!(kBolt[y] >> (4 - x) & 1)) continue;
            if (pass == 0) {
              UiFillRect(s, bx + 5 + x - 1, by + y, 3, 1, COL_BG);
              UiFillRect(s, bx + 5 + x, by + y - 1, 1, 3, COL_BG);
            } else {
              UiFillRect(s, bx + 5 + x, by + y, 1, 1, RGB(255, 245, 160));
            }
          }
        }
      }
    }
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

// Low energy (P1.10, as mzm's RenderTabBar): in gameplay the tab buttons blink yellow below 60 energy and red below 30,
// 8 frames bright and 8 dim. Only the buttons change, so a blink is a partial update (TabBlinkUpdate), not a redraw.
static uint32_t g_ui_frames;        // UiPerf.frames of the frame being drawn
static int g_tab_warn_drawn;        // 0 none, 1 yellow, 2 red: what the buttons show
static bool g_tab_bright_drawn;

static int TabWarn(void) {
  if (!SmWide_Gameplay()) return 0;
  return samus_health < 30 ? 2 : samus_health < 60 ? 1 : 0;
}

static bool TabBright(void) { return (g_ui_frames & 15) < 8; }

// The tab buttons only; returns the rectangle they cover (and how many there are in *count, if asked).
static Rect DrawTabButtons(Surface s, int *count) {
  Tab tabs[TAB_COUNT];
  const int n = VisibleTabs(tabs);
  const int warn = TabWarn();
  const bool bright = TabBright();
  for (int i = 0; i < n; i++) {
    const bool on = tabs[i] == g_tab;
    const Rect r = TabRect(i);
    uint32_t edge, body, icon;
    if (warn == 2) {
      edge = bright ? (on ? RGB(230, 55, 40) : RGB(100, 30, 24)) : (on ? RGB(160, 38, 30) : RGB(65, 22, 18));
      body = bright ? (on ? RGB(140, 28, 22) : RGB(60, 18, 16)) : (on ? RGB(80, 18, 18) : RGB(35, 12, 12));
      icon = on ? RGB(255, 200, 190) : RGB(200, 120, 110);
    } else if (warn == 1) {
      edge = bright ? (on ? RGB(230, 180, 45) : RGB(100, 80, 28)) : (on ? RGB(170, 130, 35) : RGB(70, 58, 22));
      body = bright ? (on ? RGB(130, 100, 24) : RGB(55, 44, 16)) : (on ? RGB(90, 70, 18) : RGB(38, 32, 12));
      icon = on ? RGB(255, 245, 200) : RGB(200, 175, 110);
    } else {
      edge = on ? RGB(45, 150, 240) : RGB(50, 56, 75);
      body = on ? RGB(18, 70, 130) : RGB(26, 30, 42);
      icon = on ? RGB(255, 255, 255) : RGB(140, 150, 175);
    }
    UiFillRect(s, r.x, r.y, r.w, r.h, edge);
    UiFillRect(s, r.x + 1, r.y + 1, r.w - 2, r.h - 2, body);
    DrawTabIcon(s, tabs[i], r.x + r.w / 2, r.y + r.h / 2, icon);
  }
  g_tab_warn_drawn = warn;
  g_tab_bright_drawn = bright;
  if (count) *count = n;
  const Rect a = TabRect(0), b = TabRect(n - 1);
  return (Rect){ a.x, a.y, b.x + b.w - a.x, a.h };
}

static void DrawTabBar(Surface s) {
  int n;
  DrawTabButtons(s, &n);
  // Things that are on whatever tab is shown.
  int x = TabRect(n).x;
  if (SceneRec_Active()) { UiDrawText(s, x, 9, 1, COL_BAD, "REC"); x += 24; }
  if (Debug_PerfRecording()) { UiDrawText(s, x, 9, 1, COL_BAD, "PRF"); x += 24; }
  if (g_cheats.invincible || g_cheats.max_mode) UiDrawText(s, x, 9, 1, COL_WARN, "CHT");
  DrawSystemStatus(s);
}

// ---- Map tab ------------------------------------------------------------------
// One area at a time: 64x31 cells of 5 px fill the screen width, so no zoom or
// scrolling is needed. Row 0 of the tilemap is an empty margin and is not drawn.
// Picking a room and warping into it exists in DEBUG_TOOLS builds only.

// The map canvas is the strip between the tab bar and the area buttons. Zoom 0 fits a
// whole area (64 cells of 5 px); the others show the game's own 8 px tiles or bigger and
// scroll by dragging.
#define MAP_Y0 23
#define MAP_VIEW_Y1 183
#define MAP_ZOOMS 3
static const int kMapCellPx[MAP_ZOOMS] = { 5, 8, 12 };
#define MAP_CELL (kMapCellPx[g_map_zoom])
enum { kMapDragSlop = 6 };   // a stylus wobbles this much on a tap; past it the touch scrolls


#define MAP_BG_R 14   // COL_BG, to average transparent tile pixels with
#define MAP_BG_G 16
#define MAP_BG_B 28
#define MAP_COL_ROOM      RGB(250, 220, 90)   // the room Samus is in
#define MAP_COL_SELECT    RGB(60, 255, 70)    // the room picked for a warp
#define MAP_COL_DOOR      RGB(255, 80, 40)    // its door

static int g_map_area;              // area being shown
static bool g_map_follow = true;    // follow the area Samus is in
static int g_sel_col = -1, g_sel_row = -1;
static int g_map_zoom;              // index into kMapCellPx
static int g_scroll_x, g_scroll_y;  // canvas offset in px (0 at zoom 0)
static bool g_map_centre = true;    // keep the view on Samus (or the area's middle) until dragged
static struct { bool active, dragging; int start_x, start_y, last_x, last_y; } g_map_touch;

static Rect AreaButtonRect(int i) { return (Rect){ 2 + i * 45, 183, 43, 13 }; }
static Rect FollowRect(void) { return (Rect){ 226, 198, 92, 13 }; }
static Rect ZoomRect(void) { return (Rect){ 192, 198, 30, 13 }; }
static Rect ItemsRect(void) { return (Rect){ 132, 198, 56, 13 }; }   // opens the items window

// Map cell <-> canvas pixel. Row 0 is the empty margin of the map, where the arrow names sit.
static int MapPxX(int col) { return col * MAP_CELL - g_scroll_x; }
static int MapPxY(int row) { return MAP_Y0 + row * MAP_CELL - g_scroll_y; }

static void MapClampScroll(void) {
  const int max_x = kSmMapCols * MAP_CELL - SCREEN_W;
  const int max_y = kSmMapRows * MAP_CELL - (MAP_VIEW_Y1 - MAP_Y0);
  if (g_scroll_x > max_x) g_scroll_x = max_x;
  if (g_scroll_y > max_y) g_scroll_y = max_y;
  if (g_scroll_x < 0) g_scroll_x = 0;
  if (g_scroll_y < 0) g_scroll_y = 0;
}

// Scrolls so that cell (col, row) is in the middle of the canvas.
static void MapCentreOn(int col, int row) {
  g_scroll_x = col * MAP_CELL + MAP_CELL / 2 - SCREEN_W / 2;
  g_scroll_y = row * MAP_CELL + MAP_CELL / 2 - (MAP_VIEW_Y1 - MAP_Y0) / 2;
  MapClampScroll();
}

static bool MapCellAt(int x, int y, int *col, int *row) {
  if (y < MAP_Y0 || y >= MAP_VIEW_Y1) return false;
  *col = (x + g_scroll_x) / MAP_CELL;
  *row = (y - MAP_Y0 + g_scroll_y) / MAP_CELL;
  return *col < kSmMapCols && *row < kSmMapRows;
}

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

static void OpenItems(int area);

static void MapTouch(int x, int y) {
  const int area = ShownMapArea();
  if (UiIn(ItemsRect(), x, y)) {
    OpenItems(area);
    return;
  }
  for (int i = 0; i < kSmAreaCount; i++) {
    if (UiIn(AreaButtonRect(i), x, y)) {
      g_map_area = i;
      g_map_follow = false;
      g_map_centre = true;
      g_sel_col = g_sel_row = -1;
      return;
    }
  }
  if (UiIn(FollowRect(), x, y)) {
    g_map_follow = !g_map_follow;
    g_map_centre = true;
    return;
  }
  if (UiIn(ZoomRect(), x, y)) {
    // Following Samus (or the area's middle) keeps doing so; otherwise the zoom stays
    // on the centre of what was on screen.
    const int old_cell = MAP_CELL, cx = g_scroll_x + SCREEN_W / 2, cy = g_scroll_y + (MAP_VIEW_Y1 - MAP_Y0) / 2;
    g_map_zoom = (g_map_zoom + 1) % MAP_ZOOMS;
    if (!g_map_centre) {
      g_scroll_x = cx * MAP_CELL / old_cell - SCREEN_W / 2;
      g_scroll_y = cy * MAP_CELL / old_cell - (MAP_VIEW_Y1 - MAP_Y0) / 2;
      MapClampScroll();
    }
    return;
  }
#if DEBUG_TOOLS
  const SmRoom *room = SelectedRoom(area);
  if (room && UiIn(WarpRect(), x, y)) {
    Toast(SmWarp_ResultText(SmWarp_ToRoom(room, g_warp_door)));
    return;
  }
  if (room && UiIn(DoorRect(), x, y)) {
    const int n = SmWarp_DoorCount(room);
    if (n > 1) g_warp_door = (g_warp_door + 1) % n;
    return;
  }
#else
  (void)area;
#endif
  // On the canvas: a tap picks a room (debug builds), a drag scrolls. Which one it was
  // is only known when the stylus lifts or moves past the slop.
  g_map_touch.active = y >= MAP_Y0 && y < MAP_VIEW_Y1;
  g_map_touch.dragging = false;
  g_map_touch.start_x = g_map_touch.last_x = x;
  g_map_touch.start_y = g_map_touch.last_y = y;
}

static bool MapTouchMove(int x, int y) {
  if (!g_map_touch.active) return false;
  if (!g_map_touch.dragging && abs(x - g_map_touch.start_x) < kMapDragSlop && abs(y - g_map_touch.start_y) < kMapDragSlop)
    return false;   // still a tap: keep last_* where it landed
  g_map_touch.dragging = true;
  const int sx = g_scroll_x, sy = g_scroll_y;
  g_scroll_x += g_map_touch.last_x - x;
  g_scroll_y += g_map_touch.last_y - y;
  MapClampScroll();
  g_map_touch.last_x = x;
  g_map_touch.last_y = y;
  if (g_scroll_x != sx || g_scroll_y != sy) g_map_centre = false;
  return g_scroll_x != sx || g_scroll_y != sy;
}

static void MapTouchUp(void) {
  if (!g_map_touch.active) return;
  g_map_touch.active = false;
#if DEBUG_TOOLS
  int col, row;
  if (!g_map_touch.dragging && MapCellAt(g_map_touch.start_x, g_map_touch.start_y, &col, &row)) {
    g_sel_col = col;
    g_sel_row = row;
    g_warp_door = 0;
  }
#endif
}

// One map cell as the game draws it (its 8x8 pause-map tile) at MAP_CELL px. Smaller than
// 8 each pixel averages the source pixels it covers (transparent ones are the background); bigger,
// each source pixel is a block. Cells off the canvas are skipped by the caller.
static void DrawMapTile(Surface s, int x, int y, uint16_t tile) {
  uint16_t px[64];
  SmMap_TilePixels(tile, px);
  const int cell = MAP_CELL;
  if (cell >= 8) {
    for (int sy = 0; sy < 8; sy++) {
      for (int sx = 0; sx < 8; sx++) {
        const uint16_t c = px[sy * 8 + sx];
        const uint32_t col = c ? RGB((c & 31) << 3, (c >> 5 & 31) << 3, (c >> 10 & 31) << 3) : COL_BG;
        const int x0 = sx * cell / 8, y0 = sy * cell / 8;
        UiFillRect(s, x + x0, y + y0, (sx + 1) * cell / 8 - x0, (sy + 1) * cell / 8 - y0, col);
      }
    }
    return;
  }
  for (int dy = 0; dy < cell; dy++) {
    const int y0 = dy * 8 / cell, y1 = ((dy + 1) * 8 + cell - 1) / cell;
    for (int dx = 0; dx < cell; dx++) {
      const int x0 = dx * 8 / cell, x1 = ((dx + 1) * 8 + cell - 1) / cell;
      int r = 0, g = 0, b = 0, n = 0;
      for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) {
          const uint16_t c = px[yy * 8 + xx];
          if (c) { r += (c & 31) << 3; g += (c >> 5 & 31) << 3; b += (c >> 10 & 31) << 3; }
          else { r += MAP_BG_R; g += MAP_BG_G; b += MAP_BG_B; }
          n++;
        }
      }
      UiFillRect(s, x + dx, y + dy, 1, 1, RGB(r / n, g / n, b / n));
    }
  }
}

static int FloorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static int CeilDiv(int a, int b) { return -FloorDiv(-a, b); }

// One of the game's menu sprites (a refill, a boss, the ship, an arrow's name) at the
// map's scale. The source is the game's 8-px-per-cell picture: shrunk by averaging the
// opaque pixels each one covers, enlarged by repeating them. Clipped by the canvas clip.
static void DrawMapSprite(Surface s, const SmMapIcon *ic) {
  static SmMapSprite spr;   // 6 KB: keep it off the stack
  if (!SmMap_RenderSprite(ic->sprite, ic->chr, &spr)) return;
  const int cell = MAP_CELL;
  // Top-left of the image in map px.
  const int sx0 = ic->x + spr.x0, sy0 = ic->y + spr.y0;
  const int dx0 = FloorDiv(sx0 * cell, 8), dx1 = CeilDiv((sx0 + spr.w) * cell, 8);
  const int dy0 = FloorDiv(sy0 * cell, 8), dy1 = CeilDiv((sy0 + spr.h) * cell, 8);
  for (int dy = dy0; dy < dy1; dy++) {
    const int cy = MAP_Y0 + dy - g_scroll_y;
    if (cy < MAP_Y0 || cy >= MAP_VIEW_Y1) continue;
    int ya = FloorDiv(dy * 8, cell), yb = cell >= 8 ? ya + 1 : CeilDiv((dy + 1) * 8, cell);
    for (int dx = dx0; dx < dx1; dx++) {
      const int cx = dx - g_scroll_x;
      if (cx < 0 || cx >= SCREEN_W) continue;
      const int xa = FloorDiv(dx * 8, cell), xb = cell >= 8 ? xa + 1 : CeilDiv((dx + 1) * 8, cell);
      int r = 0, g = 0, b = 0, opaque = 0, total = 0;
      for (int yy = ya; yy < yb; yy++) {
        for (int xx = xa; xx < xb; xx++) {
          total++;
          const int ix = xx - sx0, iy = yy - sy0;
          if (ix < 0 || iy < 0 || ix >= spr.w || iy >= spr.h) continue;
          const uint16_t c = spr.px[iy * kSmSpriteMaxW + ix];
          if (!c) continue;
          r += (c & 31) << 3; g += (c >> 5 & 31) << 3; b += (c >> 10 & 31) << 3;
          opaque++;
        }
      }
      if (opaque * 2 >= total && opaque > 0) UiFillRect(s, cx, cy, 1, 1, RGB(r / opaque, g / opaque, b / opaque));
    }
  }
}

// Outline of a room's own cells (not its box, which also covers blank cells and the
// cells of rooms drawn over it): an edge wherever the next cell is not the room's.
static void DrawRoomOutline(Surface s, const SmRoom *room, uint32_t color) {
  if (!room) return;
  const int cell = MAP_CELL, t = cell >= 8 ? 2 : 1;   // edge thickness, inside the cell
  for (int row = room->y + 1; row < room->y + 1 + room->h; row++) {
    for (int col = room->x; col < room->x + room->w; col++) {
      if (!SmMap_RoomOwnsCell(room, col, row)) continue;
      const int x = MapPxX(col), y = MapPxY(row);
      if (!SmMap_RoomOwnsCell(room, col - 1, row)) UiFillRect(s, x, y, t, cell, color);
      if (!SmMap_RoomOwnsCell(room, col + 1, row)) UiFillRect(s, x + cell - t, y, t, cell, color);
      if (!SmMap_RoomOwnsCell(room, col, row - 1)) UiFillRect(s, x, y, cell, t, color);
      if (!SmMap_RoomOwnsCell(room, col, row + 1)) UiFillRect(s, x, y + cell - t, cell, t, color);
    }
  }
}

#if DEBUG_TOOLS
// Where the warp's door is: its cell, with a thicker bar on the side its cap is on.
static void DrawDoorMark(Surface s, const SmWarpDoorMark *d) {
  const int cell = MAP_CELL, t = cell >= 8 ? 3 : 2;
  const int x = MapPxX(d->col), y = MapPxY(d->row);
  switch (d->side) {
  case 0: UiFillRect(s, x, y, t, cell, MAP_COL_DOOR); break;
  case 1: UiFillRect(s, x + cell - t, y, t, cell, MAP_COL_DOOR); break;
  case 2: UiFillRect(s, x, y, cell, t, MAP_COL_DOOR); break;
  default: UiFillRect(s, x, y + cell - t, cell, t, MAP_COL_DOOR); break;
  }
}
#endif

// Item marks over the map, as in Zero Mission: a big hollow ball on a cell with an item still to
// take, a small dot where every item was taken. The game's own map has a small dot on most item
// cells (TileItemDot); the hidden items have none, and with SPOILERS off they get a mark only once
// taken. `fill` is the cell's colour in the middle (0 if transparent), to hollow the ball with.
static uint32_t Bgr555ToRgb(uint16_t c) { return RGB((c & 31) << 3, (c >> 5 & 31) << 3, (c >> 10 & 31) << 3); }

static bool TileItemDot(uint16_t tile, uint32_t *col, uint32_t *fill) {
  uint16_t px[64];
  SmMap_TilePixels(tile, px);
  const uint16_t c = px[3 * 8 + 3];
  *fill = c ? Bgr555ToRgb(c) : 0;
  if (!c || px[3 * 8 + 4] != c || px[4 * 8 + 3] != c || px[4 * 8 + 4] != c) return false;
  // A dot: the 2x2 middle stands out from the ring around it (a filled cell does not).
  static const uint8_t kRing[] = { 2 * 8 + 3, 2 * 8 + 4, 5 * 8 + 3, 5 * 8 + 4, 3 * 8 + 2, 4 * 8 + 2, 3 * 8 + 5, 4 * 8 + 5 };
  for (unsigned i = 0; i < sizeof(kRing); i++)
    if (px[kRing[i]] == c) return false;
  *col = Bgr555ToRgb(c);
  *fill = px[2 * 8 + 3] ? Bgr555ToRgb(px[2 * 8 + 3]) : 0;
  return true;
}

static void DrawItemMarks(Surface s, int area, bool station) {
  int n;
  const SmPickup *items = SmMap_Pickups(&n);
  const int cell = MAP_CELL;
  for (int i = 0; i < n; i++) {
    const SmPickup *it = &items[i];
    if (it->area != area) continue;
    bool first = true, left = false, taken = false;   // a cell can hold two items: one left makes it big
    for (int j = 0; j < n; j++) {
      if (items[j].area != area || items[j].col != it->col || items[j].row != it->row) continue;
      if (j < i) first = false;
      if (SmMap_PickupTaken(&items[j])) taken = true;
      else left = true;
    }
    if (!first) continue;
    bool exists, explored;
    SmMap_Cell(area, it->col, it->row, &exists, &explored);
    if (!exists || (!explored && !station)) continue;
    const int x = MapPxX(it->col), y = MapPxY(it->row);
    if (x + cell <= 0 || x >= SCREEN_W || y + cell <= MAP_Y0 || y >= MAP_VIEW_Y1) continue;
    uint32_t col = COL_TEXT, fill = 0;
    if (!TileItemDot(SmMap_CellTile(area, it->col, it->row, explored), &col, &fill) && !g_show_spoilers) {
      if (!taken) continue;
      left = false;   // the hidden item's mark only says where one was found
    }
    if (left) {
      // A ring in the dot's colour, hollowed with the cell's own (which hides the game's small dot).
      const int d = (cell + 1) / 2, bx = x + (cell - d) / 2, by = y + (cell - d) / 2;
      if (fill) UiFillRect(s, bx + 1, by + 1, d - 2, d - 2, fill);
      if (d >= 4) {   // corners cut: a ball
        UiFillRect(s, bx + 1, by, d - 2, 1, col);
        UiFillRect(s, bx + 1, by + d - 1, d - 2, 1, col);
        UiFillRect(s, bx, by + 1, 1, d - 2, col);
        UiFillRect(s, bx + d - 1, by + 1, 1, d - 2, col);
      } else {
        UiFrameRect(s, bx, by, d, d, col);
      }
    } else {
      const int d = cell / 4, o = (cell - d) / 2;
      UiFillRect(s, x + o, y + o, d, d, col);
    }
  }
}

// Samus's mark on the MAP tab blinks every 15 frames: the pixels under it are kept, so a blink rewrites only the cell
// (LiveUpdate) instead of the whole screen.
enum { kMarkMax = 16 };
static struct {
  Rect r;          // clipped to the screen and the map's rows
  bool valid, on;
  uint32_t under[kMarkMax * kMarkMax];
} g_mark;

static void MarkSet(Surface s, bool on) {
  const Rect r = g_mark.r;
  for (int x = 0; x < r.w; x++)
    for (int y = 0; y < r.h; y++)
      s.px[(r.x + x) * s.h + (s.h - 1 - (r.y + y))] = on ? RGB(255, 255, 255) : g_mark.under[x * kMarkMax + y];
  g_mark.on = on;
}

static void MarkInit(Surface s, Rect r, bool on) {
  if (r.x < 0) r.w += r.x, r.x = 0;
  if (r.y < MAP_Y0) r.h -= MAP_Y0 - r.y, r.y = MAP_Y0;
  if (r.x + r.w > SCREEN_W) r.w = SCREEN_W - r.x;
  if (r.y + r.h > MAP_VIEW_Y1) r.h = MAP_VIEW_Y1 - r.y;
  if (r.w <= 0 || r.h <= 0 || r.w > kMarkMax || r.h > kMarkMax) return;
  g_mark.r = r;
  for (int x = 0; x < r.w; x++)
    for (int y = 0; y < r.h; y++) g_mark.under[x * kMarkMax + y] = s.px[(r.x + x) * s.h + (s.h - 1 - (r.y + y))];
  g_mark.valid = true;
  MarkSet(s, on);
}

static void DrawMap(Surface s, const UiPerf *p) {
  const int area = ShownMapArea();
  const bool station = SmMap_HasMapStation(area);
  int total = 0, seen = 0;
  int min_c = kSmMapCols, max_c = -1, min_r = kSmMapRows, max_r = -1;   // shown cells
  for (int row = 1; row < kSmMapRows; row++) {
    for (int col = 0; col < kSmMapCols; col++) {
      bool exists, explored;
      SmMap_Cell(area, col, row, &exists, &explored);
      if (!exists) continue;
      total++;
      if (explored) seen++;
      if (!explored && !station) continue;
      if (col < min_c) min_c = col;
      if (col > max_c) max_c = col;
      if (row < min_r) min_r = row;
      if (row > max_r) max_r = row;
    }
  }

  // Zoomed in, the view sits on Samus when he is in this area, else on the middle of
  // what the area shows, until the player drags it.
  int sa, sc, sr;
  const bool samus_here = SmMap_SamusCell(&sa, &sc, &sr) && sa == area;
  if (g_map_centre) {
    if (samus_here) MapCentreOn(sc, sr);
    else if (max_c >= 0) MapCentreOn((min_c + max_c) / 2, (min_r + max_r) / 2);
    else MapCentreOn(kSmMapCols / 2, kSmMapRows / 2);
  }
  MapClampScroll();

  UiClipY(MAP_Y0, MAP_VIEW_Y1);
  for (int row = min_r; row <= max_r; row++) {
    const int y = MapPxY(row);
    if (y + MAP_CELL <= MAP_Y0 || y >= MAP_VIEW_Y1) continue;
    for (int col = min_c; col <= max_c; col++) {
      const int x = MapPxX(col);
      if (x + MAP_CELL <= 0 || x >= SCREEN_W) continue;
      bool exists, explored;
      SmMap_Cell(area, col, row, &exists, &explored);
      if (!exists || (!explored && !station)) continue;
      DrawMapTile(s, x, y, SmMap_CellTile(area, col, row, explored));
    }
  }

  // The sprites the game puts over the pause map. The list is in the game's order, where an
  // earlier one is on top, so draw it backwards.
  SmMapIcon icons[kSmMapMaxIcons];
  const int icon_count = SmMap_Icons(area, icons, kSmMapMaxIcons);
  for (int i = icon_count - 1; i >= 0; i--) DrawMapSprite(s, &icons[i]);
  DrawItemMarks(s, area, station);

  // Outline the room Samus is in; Samus's blinking mark goes on last (below).
  g_mark.valid = false;
  if (samus_here) DrawRoomOutline(s, SmMap_CurrentRoom(), MAP_COL_ROOM);
#if DEBUG_TOOLS
  {
    // The room picked for a warp, and the door the warp will use.
    const SmRoom *picked = SelectedRoom(area);
    if (picked) {
      DrawRoomOutline(s, picked, MAP_COL_SELECT);
      SmWarpDoorMark door;
      if (SmWarp_DoorOnMap(picked, g_warp_door, &door)) DrawDoorMark(s, &door);
    } else if (g_sel_col >= 0) {
      UiFrameRect(s, MapPxX(g_sel_col), MapPxY(g_sel_row), MAP_CELL, MAP_CELL, MAP_COL_SELECT);
    }
  }
#endif
  if (samus_here) MarkInit(s, (Rect){ MapPxX(sc) + 1, MapPxY(sr) + 1, MAP_CELL - 2, MAP_CELL - 2 }, (p->frames / 15) & 1);
  UiNoClip();

  for (int i = 0; i < kSmAreaCount; i++)
    UiDrawButton(s, AreaButtonRect(i), i == area ? COL_TAB_ON : COL_TAB, TrAreaShort(i));
  // The area's name is on its button above; the line holds the cells seen and, after it, the items.
  UiDrawTextf(s, 4, 201, COL_TEXT, "%d/%d %s%s%s", seen, total, Tr(kStrCells), station ? "  " : "",
              station ? Tr(kStrMapMark) : "");
  {
    int all[kSmPickupKinds], got[kSmPickupKinds], sum_all = 0, sum_got = 0;
    SmMap_PickupCounts(area, all, got);
    for (int k = 0; k < kSmPickupKinds; k++) sum_all += all[k], sum_got += got[k];
    const Rect r = ItemsRect();
    UiDrawBox(s, r, COL_PANEL, COL_BORDER, Pressed(r));
    UiFillRect(s, r.x + 5, r.y + 4, 4, 5, COL_TEXT);   // a ball
    UiFillRect(s, r.x + 4, r.y + 5, 6, 3, COL_TEXT);
    char buf[16];
    if (g_show_spoilers) snprintf(buf, sizeof(buf), "%d/%d", sum_got, sum_all);
    else snprintf(buf, sizeof(buf), "%d", sum_got);
    UiDrawText(s, r.x + 14, r.y + 3, 1, g_show_spoilers && sum_all > 0 && sum_got == sum_all ? COL_GOOD : COL_TEXT, buf);
  }
  char follow[32];
  snprintf(follow, sizeof(follow), "%s: %s", Tr(kStrFollow), Tr(g_map_follow ? kStrOn : kStrOff));
  UiDrawButton(s, FollowRect(), g_map_follow ? COL_ON : COL_OFF, follow);
  const char *zoom_label[MAP_ZOOMS] = { "1X", "2X", "3X" };
  UiDrawButton(s, ZoomRect(), COL_BTN, zoom_label[g_map_zoom]);

#if DEBUG_TOOLS
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
// A tap on an ammo panel, the grapple or the X-ray selects it as SELECT would (Hud_RequestSelect);
// a tap on what is selected drops it, like Y.
// In DEBUG_TOOLS builds it is also where the cheats live, as in mzm: with FN on (the small
// button beside ALL, it stays on until tapped again) a tap on an item or beam adds or removes
// it instead; ALL (the free item cell) gives every item and beam, GOD and MAX sit next to the
// energy, and the map-station boxes unlock an area's map (so any room can be picked for a warp).

static Rect ReserveModeRect(void) { return (Rect){ 254, 29, 54, 14 }; }   // AUTO / MANUAL
static Rect AmmoRect(int i) { return (Rect){ 8 + i * 103, 73, 98, 26 }; }
static Rect ItemRect(int i) { return (Rect){ 8 + (i % 4) * 77, 111 + (i / 4) * 14, 74, 13 }; }
static Rect BeamRect(int i) { return (Rect){ 8 + i * 61, 165, 58, 13 }; }
static Rect StationRect(int i) { return (Rect){ 8 + i * 51, 191, 49, 14 }; }

#if DEBUG_TOOLS
static Rect GodRect(void) { return (Rect){ 254, 48, 26, 16 }; }
static Rect MaxRect(void) { return (Rect){ 282, 48, 26, 16 }; }
// The free cell after the last item: ALL, and FN at its right end.
static Rect AllRect(void) { const Rect r = ItemRect(kSmItemCount); return (Rect){ r.x, r.y, r.w - 28, r.h }; }
static Rect FnRect(void) { const Rect r = ItemRect(kSmItemCount); return (Rect){ r.x + r.w - 26, r.y, 26, r.h }; }
static bool g_status_fn;   // FN: item and beam taps edit instead of selecting (not saved)
#define COL_FN RGB(255, 140, 40)

static void DrawCheatButton(Surface s, Rect r, bool on, const char *label) {
  UiDrawBoxLabel(s, r, on ? RGB(110, 85, 20) : RGB(24, 34, 52), on ? RGB(255, 220, 90) : RGB(60, 90, 140),
                 on ? RGB(255, 220, 90) : RGB(150, 190, 230), Pressed(r), label);
}
#endif

// The game time, the only thing on the STATUS tab that changes every second: redrawn on its own (LiveUpdate).
static const Rect kStatusTimeRect = { 224, 210, 96, 9 };
static uint32_t g_time_drawn;

static uint32_t GameTimeKey(void) {
  return (uint32_t)game_time_hours << 16 | (uint32_t)game_time_minutes << 8 | (uint32_t)game_time_seconds;
}

static void DrawStatusTime(Surface s) {
  const Rect r = kStatusTimeRect;
  UiFillRect(s, r.x, r.y, r.w, r.h, COL_BG);
  UiDrawTextf(s, 224, 211, COL_DIM, "%02u:%02u:%02u", (unsigned)game_time_hours, (unsigned)game_time_minutes,
              (unsigned)game_time_seconds);
  g_time_drawn = GameTimeKey();
}

// The corner mark of what a stylus tap selects (an ammo panel, the grapple, the X-ray): a small
// folded corner at the bottom right, bright when SELECT would take it now, dim when not (no ammo,
// switched off). Nothing on what Samus does not have yet.
static void DrawTapMark(Surface s, Rect r, bool ready) {
  const uint32_t c = ready ? COL_ACCENT : COL_FAINT;
  for (int i = 0; i < 6; i++) UiFillRect(s, r.x + r.w - 2 - i, r.y + r.h - 7 + i, i + 1, 1, c);
}

// A HUD item on STATUS has two looks: marked (a tinted body: the one chosen) and active (a double
// yellow frame: the one the game uses now, its HUD highlight). With the original controls they are
// the same item, SELECT's. With the modern ones (modern_controls.h) the marked one is the missile kind
// R + X fires (the game's HUD draws it too, g_rtl_hud_marked) and the active one what a button holds.
#define COL_HUD_MARKED RGB(48, 44, 14)
static bool StatusHudMarked(int item) {
  return ModernControls_On() ? (g_rtl_hud_marked >> item & 1) != 0 : hud_item_index == (uint16)item;
}
static bool StatusHudActive(int item) { return hud_item_index == (uint16)item; }
// With the modern controls a tap only marks the missile kind; the other items are the buttons' job.
static bool StatusHudTappable(int item) {
  return !ModernControls_On() || item == kSmHudMissiles || item == kSmHudSupers;
}

static void DrawHudActiveFrame(Surface s, Rect r) {
  UiFrameRect(s, r.x, r.y, r.w, r.h, COL_WARN);
  UiFrameRect(s, r.x + 1, r.y + 1, r.w - 2, r.h - 2, COL_WARN);
}

// The HUD's own icons: 2bpp BG3 tiles at $9AB200 laid out as the game puts them in its HUD
// (kHudTilemaps_Missiles, sm_80.c: rows of `w` tiles, flip bits included), coloured with the HUD's
// palettes (BG3 palette 5, 4 when highlighted) from the game's palette buffer. Outside of gameplay
// that buffer holds other screens' colours, so the last ones seen in a room are kept.
enum { kHudIconMissiles, kHudIconSupers, kHudIconPowerBombs, kHudIconGrapple, kHudIconXray };
static const struct { int w, h; uint16_t t[6]; } kHudIcons[] = {
  { 3, 2, { 0x344B, 0x3449, 0x744B, 0x344C, 0x344A, 0x744C } },
  { 2, 2, { 0x3434, 0x7434, 0x3435, 0x7435 } },
  { 2, 2, { 0x3436, 0x7436, 0x3437, 0x7437 } },
  { 2, 2, { 0x3438, 0x7438, 0x3439, 0x7439 } },
  { 2, 2, { 0x343A, 0x743A, 0x343B, 0x743B } },
};

static void DrawHudIcon(Surface s, int x, int y, int icon, bool highlight, bool dim) {
  static uint16_t pal[2][4] = { { 0, 0x7FFF, 0x4A52, 0x2108 }, { 0, 0x6318, 0x3DEF, 0x18C6 } };
  if (game_state == 0x08 && (palette_buffer[4 * 4 + 1] | palette_buffer[5 * 4 + 1])) {
    memcpy(pal[0], &palette_buffer[4 * 4], sizeof(pal[0]));
    memcpy(pal[1], &palette_buffer[5 * 4], sizeof(pal[1]));
  }
  const uint16_t *p = pal[highlight ? 0 : 1];
  for (int ty = 0; ty < kHudIcons[icon].h; ty++) {
    for (int tx = 0; tx < kHudIcons[icon].w; tx++) {
      const uint16_t t = kHudIcons[icon].t[ty * kHudIcons[icon].w + tx];
      const uint8_t *gfx = RomPtr(0x9AB200 + (t & 0x3FF) * 16);
      for (int py = 0; py < 8; py++) {
        const uint8_t *row = gfx + ((t & 0x8000) ? 7 - py : py) * 2;
        for (int px = 0; px < 8; px++) {
          const int bit = (t & 0x4000) ? px : 7 - px;
          const int c = (row[0] >> bit & 1) | (row[1] >> bit & 1) << 1;
          if (!c) continue;
          const uint16_t v = p[c];
          UiFillRect(s, x + tx * 8 + px, y + ty * 8 + py, 1, 1,
                     dim ? COL_FAINT : RGB((v & 31) << 3, (v >> 5 & 31) << 3, (v >> 10 & 31) << 3));
        }
      }
    }
  }
}

static void DrawStatus(Surface s) {
  const unsigned health = samus_health, max_health = samus_max_health;

  // Energy panel, three rows: a label on the left, its picture on the right.
  //   ENERGY      energy tanks (yellow)          [AUTO]
  //   247/499     the current tank's bar          GOD MAX
  //   RESERVE     reserve tanks (grey) 160/300
  // The reserve row only once Samus has a reserve tank. Those hold 100 each, filled by the energy
  // picked up beyond the maximum, so one can be part full: each fills from the left in proportion
  // (9 px = 100). AUTO pours them in when the energy runs out, MANUAL from the pause menu; the
  // button switches it as the pause menu does (Hud_ToggleReserveMode).
  UiFillRect(s, 8, 26, 304, 44, COL_PANEL);
  UiFrameRect(s, 8, 26, 304, 44, COL_BORDER);
  UiDrawText(s, 14, 30, 1, COL_DIM, Tr(kStrEnergy));
  {
    char big[8];
    snprintf(big, sizeof(big), "%u", health);
    UiDrawText(s, 14, 40, 2, COL_ENERGY, big);
    UiDrawTextf(s, 14 + UiTextWidth(big, 2) + 1, 47, COL_DIM, "/%u", max_health);
  }
  const unsigned tanks = max_health >= 199 ? (max_health - 99) / 100 : 0;
  const unsigned full = health >= 100 ? health / 100 : 0;
  for (unsigned i = 0; i < tanks && i < 14; i++) {
    const int x = 96 + (int)i * 11;
    if (i < full) UiFillRect(s, x, 30, 9, 9, COL_ENERGY);
    else UiFrameRect(s, x, 30, 9, 9, COL_FAINT);
  }
  UiDrawBar(s, 96, 43, 152, 7, (int)(health % 100), 99, COL_ENERGY);
  if (samus_max_reserve_health) {
    UiDrawText(s, 14, 57, 1, COL_DIM, Tr(kStrReserve));
    const unsigned rtanks = samus_max_reserve_health / 100, rhealth = samus_reserve_health;
    unsigned i = 0;
    for (; i < rtanks && i < 4; i++) {
      const int x = 96 + (int)i * 11;
      const unsigned in = rhealth >= (i + 1) * 100 ? 100 : rhealth > i * 100 ? rhealth - i * 100 : 0;
      UiFrameRect(s, x, 56, 9, 9, COL_FAINT);
      if (in) UiFillRect(s, x, 56, ((int)in * 9 + 50) / 100 > 0 ? ((int)in * 9 + 50) / 100 : 1, 9, RGB(190, 196, 212));
    }
    UiDrawTextf(s, 96 + (int)i * 11 + 4, 57, COL_TEXT, "%u/%u", rhealth, (unsigned)samus_max_reserve_health);
    const Rect mr = ReserveModeRect();
    const bool manual = reserve_health_mode == 2;
    UiDrawBoxLabel(s, mr, COL_BOX, COL_BOX_EDGE, manual ? COL_TEXT : COL_RESERVE, Pressed(mr), Tr(manual ? kStrManual : kStrAuto));
  }
#if DEBUG_TOOLS
  DrawCheatButton(s, GodRect(), g_cheats.invincible, "GOD");
  DrawCheatButton(s, MaxRect(), g_cheats.max_mode, "MAX");
#endif

  // Ammo panels.
  static const uint32_t kAmmoCol[3] = { COL_MISSILE, COL_SUPER, COL_PBOMB };
  const unsigned cur[3] = { samus_missiles, samus_super_missiles, samus_power_bombs };
  const unsigned max[3] = { samus_max_missiles, samus_max_super_missiles, samus_max_power_bombs };
  for (int i = 0; i < 3; i++) {
    const Rect ar = AmmoRect(i);
    const int x = ar.x;
    // Missiles 1, super missiles 2, power bombs 3, marked and active as above. The top screen's HUD
    // can be hidden while this tab is open, so it shows here.
    UiFillRect(s, x, 73, 98, 26, StatusHudMarked(kSmHudMissiles + i) ? COL_HUD_MARKED : COL_PANEL);
    UiFrameRect(s, x, 73, 98, 26, COL_BORDER);
    if (StatusHudActive(kSmHudMissiles + i)) DrawHudActiveFrame(s, ar);
    // The HUD's icon on the left (grey while Samus has none), the count and its bar beside it.
    if (max[i] || g_show_spoilers)
      DrawHudIcon(s, x + 4 + (24 - kHudIcons[i].w * 8) / 2, 78, kHudIconMissiles + i, StatusHudActive(kSmHudMissiles + i), !max[i]);
    if (max[i] || g_show_spoilers) UiDrawTextf(s, x + 32, 77, max[i] ? COL_TEXT : COL_FAINT, "%u/%u", cur[i], max[i]);
    else UiDrawText(s, x + 32, 77, 1, COL_FAINT, "---");
    UiDrawBar(s, x + 32, 88, 60, 7, (int)cur[i], (int)max[i], kAmmoCol[i]);
    if (max[i] && StatusHudTappable(kSmHudMissiles + i)) DrawTapMark(s, ar, ModernControls_On() || cur[i] > 0);
  }

  // Items: green = equipped, yellow = collected but switched off, dim = missing.
  UiDrawText(s, 8, 102, 1, COL_DIM, Tr(kStrItems));
  for (int i = 0; i < kSmItemCount; i++) {
    const Rect r = ItemRect(i);
    const bool have = (collected_items & kSmItems[i].mask) != 0;
    const bool on = (equipped_items & kSmItems[i].mask) != 0;
    // The grapple beam (4) and the X-ray scope (5) are HUD items too.
    const int hud = kSmItems[i].mask == 0x4000 ? kSmHudGrapple : kSmItems[i].mask == 0x8000 ? kSmHudXray : -1;
    UiFillRect(s, r.x, r.y, r.w, r.h, Pressed(r) ? COL_PRESSED : hud >= 0 && StatusHudMarked(hud) ? COL_HUD_MARKED : COL_PANEL);
    UiDrawText(s, r.x + 4, r.y + 3, 1, have ? (on ? COL_GOOD : COL_WARN) : COL_FAINT, have || g_show_spoilers ? TrItem(i) : "---");
#if DEBUG_TOOLS
    if (g_status_fn) UiFrameRect(s, r.x, r.y, r.w, r.h, COL_FN);
#endif
    if (hud >= 0 && have && StatusHudTappable(hud)) DrawTapMark(s, r, on);
    if (hud >= 0 && StatusHudActive(hud)) DrawHudActiveFrame(s, r);
  }
#if DEBUG_TOOLS
  {
    bool all = true;
    for (int i = 0; i < kSmItemCount; i++) all &= (collected_items & kSmItems[i].mask) != 0;
    for (int i = 0; i < kSmBeamCount; i++) all &= (collected_beams & kSmBeams[i].mask) != 0;
    DrawCheatButton(s, AllRect(), all, "ALL");
    const Rect fr = FnRect();
    UiDrawBoxLabel(s, fr, g_status_fn ? RGB(110, 55, 10) : RGB(24, 34, 52), g_status_fn ? COL_FN : RGB(60, 90, 140),
                   g_status_fn ? COL_FN : RGB(150, 190, 230), Pressed(fr), "FN");
  }
#endif
  UiDrawText(s, 8, 156, 1, COL_DIM, Tr(kStrBeams));
  for (int i = 0; i < kSmBeamCount; i++) {
    const Rect r = BeamRect(i);
    const bool have = (collected_beams & kSmBeams[i].mask) != 0;
    const bool on = (equipped_beams & kSmBeams[i].mask) != 0;
    UiFillRect(s, r.x, r.y, r.w, r.h, Pressed(r) ? COL_PRESSED : COL_PANEL);
    UiDrawText(s, r.x + 4, r.y + 3, 1, have ? (on ? COL_GOOD : COL_WARN) : COL_FAINT, have || g_show_spoilers ? TrBeam(i) : "---");
#if DEBUG_TOOLS
    if (g_status_fn) UiFrameRect(s, r.x, r.y, r.w, r.h, COL_FN);
#endif
  }

  // Map stations (Ceres has none). Debug builds: grey none, green used, purple forced,
  // orange every cell explored.
  UiDrawText(s, 8, 182, 1, COL_DIM, Tr(kStrMapStations));
#if DEBUG_TOOLS
  UiDrawText(s, 20 + UiTextWidth(Tr(kStrMapStations), 1), 182, 1, COL_FAINT, "TAP: MAP > EXPLORED > REAL");
#endif
  for (int i = 0; i < 6; i++) {
    const Rect r = StationRect(i);
    const SmMapDebugState st = SmMap_DebugState(i);
    uint32_t body = RGB(22, 26, 38), edge = RGB(45, 52, 70), text = COL_FAINT;
    if (st == kSmMapDebug_Explored) { body = RGB(70, 45, 15); edge = RGB(255, 170, 60); text = COL_TEXT; }
    else if (st == kSmMapDebug_Station) { body = RGB(45, 30, 70); edge = RGB(170, 110, 240); text = COL_TEXT; }
    else if (SmMap_HasMapStation(i)) { body = RGB(18, 62, 32); edge = RGB(70, 220, 110); text = COL_TEXT; }
    UiDrawBoxLabel(s, r, body, edge, text, Pressed(r), TrAreaShort(i));
  }

  // Where and how long.
  const unsigned area = area_index < 8 ? area_index : 7;
  UiDrawTextf(s, 8, 211, COL_TEXT, "%s  %s %02X", TrArea(area), Tr(kStrRoom), (unsigned)room_index);
  DrawStatusTime(s);
}

// What a tap selects: the HUD item, or -1.
static int StatusHudItemAt(int x, int y) {
  for (int i = 0; i < 3; i++)
    if (UiIn(AmmoRect(i), x, y)) return kSmHudMissiles + i;
  for (int i = 0; i < kSmItemCount; i++) {
    if (!UiIn(ItemRect(i), x, y)) continue;
    return kSmItems[i].mask == 0x4000 ? kSmHudGrapple : kSmItems[i].mask == 0x8000 ? kSmHudXray : -1;
  }
  return -1;
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
  if (UiIn(AllRect(), x, y)) {
    ReportGameplay(Cheats_GiveAll());
    return;
  }
  if (UiIn(FnRect(), x, y)) {
    g_status_fn = !g_status_fn;
    return;
  }
  if (g_status_fn) {
    for (int i = 0; i < kSmItemCount; i++)
      if (UiIn(ItemRect(i), x, y)) { ReportGameplay(Cheats_ToggleItem(i)); return; }
    for (int i = 0; i < kSmBeamCount; i++)
      if (UiIn(BeamRect(i), x, y)) { ReportGameplay(Cheats_ToggleBeam(i)); return; }
  }
  for (int i = 0; i < 6; i++) {
    if (!UiIn(StationRect(i), x, y)) continue;
    if (!Cheats_InGameplay()) { ReportGameplay(false); return; }
    SmMap_DebugCycle(i);
    static const char *const kMsg[] = { "Map: real state", "Map: station used", "Map: all explored" };
    Toast(kMsg[SmMap_DebugState(i)]);
    return;
  }
#endif
  if (samus_max_reserve_health && UiIn(ReserveModeRect(), x, y)) {
    Hud_ToggleReserveMode();
    return;
  }
  const int item = StatusHudItemAt(x, y);
  if (item < 0 || !StatusHudTappable(item)) return;
  if (ModernControls_On()) {
    if (item != ModernControls_Missile() && (item == kSmHudMissiles ? samus_max_missiles : samus_max_super_missiles)) {
      ModernControls_SetMissile(item);
      g_rtl_hud_click = true;
    }
    return;
  }
  Hud_RequestSelect(hud_item_index == item ? kSmHudNone : item);
}

// ---- States tab -----------------------------------------------------------------
// Any number of save states (states_store.c), newest first as cards, scrolled by dragging the
// list or its bar like the achievements. NEW saves into a fresh one; a tap on a card opens it
// (MODAL_STATE): the screenshot of the top screen, what Samus had, a colour mark to tell it
// apart, and LOAD / SAVE OVER / DELETE, each with a second tap to confirm.

enum {
  kStListY0 = 46, kStListY1 = 238,
  kStCardX = 6, kStCardW = 300, kStCardPitch = 34, kStCardH = 32,
  kStBarX = 310, kStBarW = 4, kStBarHitX = 306,
  kStDragSlop = 6,
  kStListMax = 512,   // shown; the rest are older ones (the count says how many there are)
};
static StateInfo g_states[kStListMax];
static int g_states_n, g_states_total;
static int g_st_scroll;
static struct { bool active, dragging, bar; int start_y, last_y; } g_st_touch;
static StateInfo g_st_detail;               // the card opened
static uint16_t g_st_shot[kStateShotW * kStateShotH];
static bool g_st_shot_ok;
static int g_arm_state = -1, g_arm_action;   // action 1 = load, 2 = save over, 3 = delete
static u64 g_arm_ms;

static const uint32_t kMarkColors[kStateMarks] = {
  RGB(70, 85, 110), RGB(235, 70, 70), RGB(240, 150, 50), RGB(235, 215, 70),
  RGB(80, 210, 110), RGB(70, 205, 215), RGB(80, 130, 245), RGB(185, 105, 240),
};

static void RefreshSlots(void) {
  g_states_total = States_Scan(g_states, kStListMax);
  g_states_n = g_states_total < kStListMax ? g_states_total : kStListMax;
}

// The number a state shows: its place in time, 1 the oldest (the list is newest first). It is not
// the file's id, which only ever grows: delete the 4 and the 5 becomes the 4.
static int StPosition(int id) {
  for (int i = 0; i < g_states_n; i++)
    if (g_states[i].id == id) return g_states_total - i;
  return id;
}

static int StMaxScroll(void) {
  const int content = g_states_n * kStCardPitch - (kStCardPitch - kStCardH);
  return content > kStListY1 - kStListY0 ? content - (kStListY1 - kStListY0) : 0;
}

static void StClampScroll(void) {
  const int max = StMaxScroll();
  if (g_st_scroll > max) g_st_scroll = max;
  if (g_st_scroll < 0) g_st_scroll = 0;
}

static void StScrollToBar(int y) {
  g_st_scroll = (y - kStListY0) * StMaxScroll() / (kStListY1 - kStListY0);
  StClampScroll();
}

static Rect StNewRect(void) { return (Rect){ 232, 27, 80, 15 }; }

// The state's own numbers from the game's RAM, for the file next to it.
static void CurrentStateInfo(StateInfo *si, int id, int mark) {
  memset(si, 0, sizeof(*si));
  si->id = id;
  si->has_info = true;
  si->saved_at = (long long)time(NULL);
  si->area = area_index, si->room = room_index;
  si->health = samus_health, si->max_health = samus_max_health, si->reserve = samus_reserve_health, si->max_reserve = samus_max_reserve_health;
  si->missiles = samus_missiles, si->max_missiles = samus_max_missiles;
  si->supers = samus_super_missiles, si->max_supers = samus_max_super_missiles;
  si->pbs = samus_power_bombs, si->max_pbs = samus_max_power_bombs;
  si->hours = game_time_hours, si->minutes = game_time_minutes;
  si->mark = mark;
  snprintf(si->version, sizeof(si->version), "%s", g_rom_info.version);
}

static void DrawStateCard(Surface s, int y, const StateInfo *si) {
  const int x = kStCardX;
  UiFillRect(s, x, y, kStCardW, kStCardH, RGB(35, 45, 65));
  UiFillRect(s, x + 1, y + 1, kStCardW - 2, kStCardH - 2, RGB(18, 22, 34));
  UiFillRect(s, x + 1, y + 1, 6, kStCardH - 2, kMarkColors[si->mark]);
  UiDrawTextf(s, x + 14, y + 6, COL_TEXT, "%d", g_states_total - (int)(si - g_states));
  if (!si->has_info) {
    UiDrawText(s, x + 14, y + 19, 1, COL_DIM, Tr(kStrSavedNoDetails));
    return;
  }
  UiDrawTextf(s, x + 54, y + 6, RGB(170, 210, 245), "%s %02X", TrAreaShort(si->area < kSmAreaCount ? si->area : 0), si->room);
  UiDrawTextf(s, x + 110, y + 6, COL_ENERGY, "E%u", si->health);
  if (si->max_missiles) UiDrawTextf(s, x + 152, y + 6, COL_MISSILE, "M%u", si->missiles);
  UiDrawTextf(s, x + 14, y + 19, COL_DIM, "%u:%02u", si->hours, si->minutes);
  const time_t t = (time_t)si->saved_at;
  struct tm *tm = gmtime(&t);
  if (tm) UiDrawTextf(s, x + 70, y + 19, COL_DIM, "%04d-%02d-%02d %02d:%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min);
}

static void DrawStates(Surface s) {
  char buf[48];
  snprintf(buf, sizeof(buf), "%s: %d", Tr(kStrSaveStates), g_states_total);
  UiDrawText(s, 8, 31, 1, COL_TITLE, buf);
  UiDrawBoxLabel(s, StNewRect(), RGB(24, 60, 34), RGB(70, 150, 90), RGB(200, 235, 220), Pressed(StNewRect()), Tr(kStrNewState));
  if (!g_states_n) {
    UiDrawTextCentered(s, SCREEN_W / 2, 120, COL_DIM, Tr(kStrStatesNone));
    return;
  }
  StClampScroll();
  UiClipY(kStListY0, kStListY1);
  for (int i = 0; i < g_states_n; i++) {
    const int y = kStListY0 - g_st_scroll + i * kStCardPitch;
    if (y + kStCardH <= kStListY0) continue;
    if (y >= kStListY1) break;
    DrawStateCard(s, y, &g_states[i]);
  }
  UiNoClip();
  const int max = StMaxScroll();
  if (max > 0) {
    const int track = kStListY1 - kStListY0;
    int thumb = track * track / (track + max);
    if (thumb < 16) thumb = 16;
    UiFillRect(s, kStBarX, kStListY0, kStBarW, track, RGB(60, 24, 36));
    UiFillRect(s, kStBarX, kStListY0 + g_st_scroll * (track - thumb) / max, kStBarW, thumb, RGB(80, 160, 240));
  }
}

static void StatesTouch(int x, int y) {
  if (UiIn(StNewRect(), x, y)) {
    g_ui.save_slot = States_NextId();
    g_ui.req_save_state = true;
  } else if (y >= kStListY0 && y < kStListY1 && g_states_n) {
    // A card opens on release, and only if the touch never became a drag.
    g_st_touch.active = true;
    g_st_touch.dragging = false;
    g_st_touch.bar = x >= kStBarHitX && StMaxScroll() > 0;
    g_st_touch.start_y = g_st_touch.last_y = y;
    if (g_st_touch.bar) StScrollToBar(y);
  }
}

static bool StatesTouchMove(int y) {
  if (!g_st_touch.active) return false;
  const int before = g_st_scroll;
  if (g_st_touch.bar) {
    StScrollToBar(y);
  } else if (g_st_touch.dragging || abs(y - g_st_touch.start_y) >= kStDragSlop) {
    g_st_touch.dragging = true;
    g_st_scroll += g_st_touch.last_y - y;
    StClampScroll();
  } else {
    return false;
  }
  g_st_touch.last_y = y;
  return g_st_scroll != before;
}

static void StatesTouchUp(void) {
  if (!g_st_touch.active) return;
  g_st_touch.active = false;
  if (g_st_touch.dragging || g_st_touch.bar) return;
  const int at = g_st_touch.start_y - kStListY0 + g_st_scroll;
  const int i = at / kStCardPitch;
  if (i >= 0 && i < g_states_n && at % kStCardPitch < kStCardH) {
    g_st_detail = g_states[i];
    g_st_shot_ok = States_ReadShot(g_st_detail.id, g_st_shot);
    g_arm_state = -1;
    g_modal = MODAL_STATE;
  }
}

// ---- One state in full -----------------------------------------------------------
static Rect StMarkRect(int i) { return (Rect){ 62 + i * 30, 172, 26, 14 }; }
static Rect StButtonRect(int i) { return (Rect){ 12 + i * 76, 198, 72, 22 }; }   // LOAD, SAVE OVER, DELETE, CLOSE

static bool StArmed(int action) { return g_arm_state == g_st_detail.id && g_arm_action == action && osGetTime() - g_arm_ms < ARM_MS; }

static void DrawStateDetail(Surface s) {
  const StateInfo *si = &g_st_detail;
  UiFillRect(s, 6, 26, 308, 210, COL_TITLE);
  UiFillRect(s, 8, 28, 304, 206, RGB(8, 11, 20));
  UiDrawTextf(s, 16, 33, COL_TITLE, "%d", StPosition(si->id));
  if (si->has_info) {
    const time_t t = (time_t)si->saved_at;
    struct tm *tm = gmtime(&t);
    char buf[24] = "";
    if (tm) snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min);
    UiDrawText(s, 304 - UiTextWidth(buf, 1), 33, 1, COL_DIM, buf);
  }
  // The screenshot, 1:1.
  const int sx = 14, sy = 46;
  UiFillRect(s, sx - 1, sy - 1, kStateShotW + 2, kStateShotH + 2, RGB(60, 75, 100));
  if (g_st_shot_ok) {
    for (int y = 0; y < kStateShotH; y++) {
      for (int x = 0; x < kStateShotW; x++) {
        const uint16_t c = g_st_shot[y * kStateShotW + x];
        const unsigned r = c >> 11 & 31, g = c >> 5 & 63, b = c & 31;
        s.px[(sx + x) * s.h + (s.h - 1 - (sy + y))] = RGB(r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2);
      }
    }
  } else {
    UiFillRect(s, sx, sy, kStateShotW, kStateShotH, RGB(14, 18, 28));
    UiDrawTextCentered(s, sx + kStateShotW / 2, sy + kStateShotH / 2 - 3, COL_FAINT, Tr(kStrNoImage));
  }
  // What Samus had.
  const int tx = 222;
  int y = 46;
  if (si->has_info) {
    UiDrawTextf(s, tx, y, RGB(170, 210, 245), "%s %02X", TrAreaShort(si->area < kSmAreaCount ? si->area : 0), si->room), y += 13;
    UiDrawTextf(s, tx, y, COL_ENERGY, "E %u/%u", si->health, si->max_health), y += 12;
    if (si->max_reserve) UiDrawTextf(s, tx, y, COL_RESERVE, "RES %u/%u", si->reserve, si->max_reserve), y += 12;
    UiDrawTextf(s, tx, y, COL_MISSILE, "M %u/%u", si->missiles, si->max_missiles), y += 12;
    UiDrawTextf(s, tx, y, COL_GOOD, "S %u/%u", si->supers, si->max_supers), y += 12;
    UiDrawTextf(s, tx, y, COL_WARN, "PB %u/%u", si->pbs, si->max_pbs), y += 12;
    UiDrawTextf(s, tx, y, COL_DIM, "%s %u:%02u", Tr(kStrTime), si->hours, si->minutes), y += 14;
    if (si->version[0] && si->version[0] != '-') UiDrawTextf(s, tx, y, COL_FAINT, "%.14s", si->version);
  } else {
    UiDrawText(s, tx, y, 1, COL_DIM, Tr(kStrSavedNoDetails));
  }
  // The mark.
  UiDrawText(s, 16, 175, 1, COL_DIM, Tr(kStrMark));
  for (int i = 0; i < kStateMarks; i++) {
    const Rect r = StMarkRect(i);
    UiFillRect(s, r.x - 1, r.y - 1, r.w + 2, r.h + 2, i == si->mark ? RGB(255, 255, 255) : RGB(30, 38, 55));
    UiFillRect(s, r.x, r.y, r.w, r.h, kMarkColors[i]);
  }
  // LOAD, SAVE OVER and DELETE take a second tap.
  static const UiStr kLabels[3] = { kStrLoad, kStrSaveOver, kStrDelete };
  for (int i = 0; i < 3; i++) {
    const Rect r = StButtonRect(i);
    const bool armed = StArmed(i + 1);
    const bool enabled = i != 0 || true;
    (void)enabled;
    if (armed) UiDrawBoxLabel(s, r, RGB(120, 90, 20), i == 2 ? RGB(180, 60, 60) : RGB(80, 140, 200), RGB(255, 235, 150), Pressed(r), "OK?");
    else if (i == 2) UiDrawBoxLabel(s, r, RGB(64, 22, 22), RGB(180, 60, 60), RGB(255, 150, 150), Pressed(r), Tr(kLabels[i]));
    else UiDrawBoxLabel(s, r, i == 0 ? RGB(24, 46, 70) : RGB(24, 60, 34), i == 0 ? RGB(80, 140, 200) : RGB(70, 150, 90), COL_TEXT, Pressed(r), Tr(kLabels[i]));
  }
  UiDrawBoxLabel(s, StButtonRect(3), COL_BOX, COL_BOX_EDGE, COL_TEXT, Pressed(StButtonRect(3)), Tr(kStrClose));
}

static void StateDetailTouch(int x, int y) {
  StateInfo *si = &g_st_detail;
  for (int i = 0; i < kStateMarks; i++) {
    if (!UiIn(StMarkRect(i), x, y)) continue;
    si->mark = i;
    if (si->has_info) States_WriteInfo(si);
    for (int k = 0; k < g_states_n; k++)
      if (g_states[k].id == si->id) g_states[k].mark = i;
    return;
  }
  if (UiIn(StButtonRect(3), x, y)) {
    g_modal = MODAL_NONE;
    return;
  }
  for (int i = 0; i < 3; i++) {
    if (!UiIn(StButtonRect(i), x, y)) continue;
    const int action = i + 1;
    if (!StArmed(action)) {
      g_arm_state = si->id;
      g_arm_action = action;
      g_arm_ms = osGetTime();
      return;
    }
    g_arm_state = -1;
    if (action == 3) {
      const int number = StPosition(si->id);
      BottomUi_Busy();
      States_Delete(si->id);
      RefreshSlots();
      StClampScroll();
      char buf[48];
      snprintf(buf, sizeof(buf), Tr(kStrStateDeleted), number);
      Toast(buf);
      g_modal = MODAL_NONE;
    } else {
      g_ui.save_slot = si->id;
      if (action == 1) g_ui.req_load_state = true;
      else g_ui.req_save_state = true;
      g_modal = MODAL_NONE;
    }
    return;
  }
  g_arm_state = -1;
}

void BottomUi_StateSaved(int slot, bool ok, const uint16_t *shot) {
  if (ok) {
    // Saving over a state keeps its mark.
    StateInfo old, now;
    const bool had = States_ReadInfo(slot, &old);
    CurrentStateInfo(&now, slot, had ? old.mark : 0);
    States_WriteInfo(&now);
    if (shot) States_WriteShot(slot, shot);
  }
  RefreshSlots();
  char buf[64];
  snprintf(buf, sizeof(buf), Tr(ok ? kStrSavedSlot : kStrSaveFailed), StPosition(slot));
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
  char buf[64];
  snprintf(buf, sizeof(buf), Tr(ok ? kStrLoadedSlot : kStrLoadFailed), StPosition(slot));
  Toast(buf);
}

void BottomUi_GameReset(void) {
  ForgetDebugState();
  Toast(Tr(kStrGameReset));
}

// ---- Options tab ----------------------------------------------------------------
// Two columns of slots. A slot is one button, or two half buttons that share its width
// (each shows its own setting and its current value, a tap changes it).
//
//   FRAMES | AUDIO    CONTROLS [?]
//   FPS | CPU         LANGUAGE | SPOILERS
//   IMAGE | VIEW      HUD
//   UPDATE | CHANNEL  UPDATES | WHAT'S NEW
//   RESET GAME (the whole width)

typedef enum {
  OPT_PACING, OPT_AUDIO, OPT_FPS, OPT_SPEEDUP, OPT_LANGUAGE, OPT_DISPLAY, OPT_WIDE, OPT_AUTO_UPDATE, OPT_CHANNEL,
  OPT_UPDATES, OPT_HUD, OPT_NOTES, OPT_SPOILERS, OPT_CONTROLS, OPT_COUNT
} OptCell;

enum { kOptHudSlot = 5, kOptUpdateSlot = 6, kOptNewsSlot = 7, kOptResetRow = 8, kOptSpeakerW = 34 };

static Rect OptSlot(int slot) { return (Rect){ 8 + (slot % 2) * 154, 30 + (slot / 2) * 34, 150, 30 }; }
static Rect OptHalf(int slot, int half) {
  const Rect r = OptSlot(slot);
  return (Rect){ r.x + half * 77, r.y, 73, r.h };
}

static Rect OptRect(OptCell c) {
  switch (c) {
  case OPT_PACING: return OptHalf(0, 0);
  case OPT_AUDIO: return OptHalf(0, 1);
  case OPT_CONTROLS: { const Rect r = OptSlot(1); return (Rect){ r.x, r.y, r.w - kOptSpeakerW - 2, r.h }; }
  case OPT_FPS: return OptHalf(2, 0);
  case OPT_SPEEDUP: return OptHalf(2, 1);
  case OPT_LANGUAGE: return OptHalf(3, 0);
  case OPT_SPOILERS: return OptHalf(3, 1);
  case OPT_DISPLAY: return OptHalf(4, 0);
  case OPT_WIDE: return OptHalf(4, 1);
  case OPT_AUTO_UPDATE: return OptHalf(kOptUpdateSlot, 0);
  case OPT_CHANNEL: return OptHalf(kOptUpdateSlot, 1);
  case OPT_HUD: return OptSlot(kOptHudSlot);
  case OPT_NOTES: return OptHalf(kOptNewsSlot, 1);
  default: return OptHalf(kOptNewsSlot, 0);   // OPT_UPDATES
  }
}
// The "?" beside CONTROLS: the window with both schemes' buttons.
static Rect ControlsHelpRect(void) { const Rect r = OptSlot(1); return (Rect){ r.x + r.w - kOptSpeakerW, r.y, kOptSpeakerW, r.h }; }
static Rect ResetRect(void) {
  const Rect l = OptSlot(kOptResetRow), r = OptSlot(kOptResetRow + 1);
  return (Rect){ l.x, l.y, r.x + r.w - l.x, l.h };
}

static void DrawOptCell(Surface s, Rect r, const char *label, const char *value, uint32_t value_col) {
  UiDrawBox(s, r, RGB(24, 32, 50), RGB(50, 80, 130), Pressed(r));
  UiDrawText(s, r.x + 6, r.y + 5, 1, COL_TEXT, label);
  UiDrawText(s, r.x + 6, r.y + 17, 1, value_col, value);
}

// A loudspeaker, 16x12 around (cx, cy): waves when the sound is on, a red cross when it is off.
static void DrawSpeaker(Surface s, int cx, int cy, bool on, uint32_t ink) {
  UiFillRect(s, cx - 8, cy - 2, 4, 5, ink);                       // the box
  for (int i = 0; i < 4; i++) UiFillRect(s, cx - 4 + i, cy - 2 - i, 1, 5 + 2 * i, ink);   // the cone
  if (on) {
    UiFillRect(s, cx + 2, cy - 2, 1, 5, ink);
    UiFillRect(s, cx + 3, cy - 3, 1, 1, ink), UiFillRect(s, cx + 3, cy + 3, 1, 1, ink);
    UiFillRect(s, cx + 5, cy - 4, 1, 9, ink);
  } else {
    for (int i = 0; i < 7; i++) {
      UiFillRect(s, cx + 1 + i, cy - 3 + i, 2, 1, COL_BAD);
      UiFillRect(s, cx + 1 + i, cy + 3 - i, 2, 1, COL_BAD);
    }
  }
}

// The FPS counter's corner: a small screen with a block in the corner, or NO.
static void DrawFpsCornerIcon(Surface s, Rect r, int corner) {
  const int x = r.x + 6, y = r.y + 16;
  if (!corner) {
    UiDrawText(s, x, y + 1, 1, COL_DIM, Tr(kStrNo));
    return;
  }
  UiFrameRect(s, x, y, 22, 11, RGB(110, 130, 170));
  const int bx = corner == 2 || corner == 4 ? x + 22 - 9 : x + 1, by = corner >= 3 ? y + 11 - 5 : y + 1;
  UiFillRect(s, bx, by, 8, 4, COL_GOOD);
}

static void DrawOptions(Surface s) {
  static const UiStr kPaceNames[kPaceCount] = { kStrAuto, kStrPaceLock, kStrPaceNoSkip };
  DrawOptCell(s, OptRect(OPT_PACING), Tr(kStrPacing), Tr(kPaceNames[g_ui.pacing]), g_ui.pacing == kPaceNoSkip ? COL_WARN : COL_GOOD);

  // AUDIO: a tap switches the sound; the loudspeaker beside the value shows it.
  const Rect ar = OptRect(OPT_AUDIO);
  DrawOptCell(s, ar, Tr(kStrAudio), Tr(g_ui.audio_on ? kStrOn : kStrOff), g_ui.audio_on ? COL_GOOD : COL_DIM);
  DrawSpeaker(s, ar.x + ar.w - 14, ar.y + 20, g_ui.audio_on, g_ui.audio_on ? COL_GOOD : COL_DIM);
  // CONTROLS, and beside it "?": what each button does in both schemes (MODAL_CONTROLS).
  DrawOptCell(s, OptRect(OPT_CONTROLS), Tr(kStrControls), Tr(ModernControls_On() ? kStrModern : kStrClassic), COL_GOOD);
  UiDrawBoxLabel(s, ControlsHelpRect(), RGB(24, 32, 50), RGB(50, 80, 130), COL_ACCENT, Pressed(ControlsHelpRect()), "?");

  const Rect fr = OptRect(OPT_FPS);
  DrawOptCell(s, fr, "FPS", "", COL_TEXT);
  DrawFpsCornerIcon(s, fr, g_ui.fps_overlay);
  if (g_is_new3ds) DrawOptCell(s, OptRect(OPT_SPEEDUP), "CPU", g_ui.new3ds_speedup ? "804 MHZ" : "268 MHZ",
                               g_ui.new3ds_speedup ? COL_GOOD : COL_DIM);
  else DrawOptCell(s, OptRect(OPT_SPEEDUP), "CPU", "268 MHZ", COL_FAINT);
  DrawOptCell(s, OptRect(OPT_LANGUAGE), Tr(kStrLanguage), UiLang_Name(g_ui_lang), COL_GOOD);
  DrawOptCell(s, OptRect(OPT_SPOILERS), Tr(kStrSpoilers), Tr(g_show_spoilers ? kStrOn : kStrOff),
              g_show_spoilers ? COL_GOOD : COL_DIM);
  DrawOptCell(s, OptRect(OPT_DISPLAY), Tr(kStrDisplay), Tr(g_ui.pixel_perfect ? kStrPixelP : kStrScaled), COL_GOOD);
  DrawOptCell(s, OptRect(OPT_WIDE), Tr(kStrView), Tr(g_ui.wide ? kStrViewWide : kStrViewOriginal), g_ui.wide ? COL_GOOD : COL_DIM);
  DrawOptCell(s, OptRect(OPT_AUTO_UPDATE), Tr(kStrUpdate), Tr(g_ui.auto_update ? kStrAuto : kStrNo),
              g_ui.auto_update ? COL_GOOD : COL_DIM);
  DrawOptCell(s, OptRect(OPT_CHANNEL), Tr(kStrChannel), Tr(g_ui.update_beta ? kStrChanBeta : kStrChanStable),
              g_ui.update_beta ? COL_WARN : COL_GOOD);
  {
    // A half button holds about 11 characters: the short forms; the prompts say it in full.
    const char *value;
    uint32_t col = COL_GOOD;
    switch (Updater_State()) {
    case UPD_CHECKING: value = Tr(kStrUpdsChecking); col = COL_DIM; break;
    case UPD_UP_TO_DATE: value = Tr(kStrUpdUpToDate); break;
    case UPD_AVAILABLE: value = Tr(kStrUpdsNew); col = COL_WARN; break;
    case UPD_DOWNLOADING: value = Tr(kStrUpdsInstalling); col = COL_DIM; break;
    case UPD_INSTALLED: value = Tr(kStrUpdsRestart); break;
    case UPD_ERROR: value = Tr(kStrUpdsError); col = COL_WARN; break;
    default: value = Tr(kStrUpdsCheck); col = COL_DIM; break;
    }
    DrawOptCell(s, OptRect(OPT_UPDATES), Tr(kStrUpdsLabel), value, col);
  }
  DrawOptCell(s, OptRect(OPT_HUD), Tr(kStrHud), Tr(g_ui.hud_auto_hide ? kStrHudHidden : kStrHudShown),
              g_ui.hud_auto_hide ? COL_GOOD : COL_DIM);
  DrawOptCell(s, OptRect(OPT_NOTES), Tr(kStrWhatsNew), Updater_RemoteTag(), COL_DIM);
  UiDrawBoxLabel(s, ResetRect(), RGB(64, 22, 22), RGB(180, 60, 60), RGB(255, 150, 150), Pressed(ResetRect()),
                 Tr(kStrResetGame));

  UiDrawTextCentered(s, SCREEN_W / 2, 200, RGB(90, 115, 145), "SUPER METROID 3DS");
  UiDrawTextCentered(s, SCREEN_W / 2, 209, RGB(90, 115, 145), g_rom_info.version);
  char buf[48];
  snprintf(buf, sizeof(buf), "ROM %.8s%s", g_rom_info.rom_sha1, g_rom_info.rom_had_header ? " (HEADER)" : "");
  UiDrawTextCentered(s, SCREEN_W / 2, 219, RGB(70, 90, 115), buf);
#if DEBUG_TOOLS
  UiDrawTextCentered(s, SCREEN_W / 2, 229, RGB(150, 110, 60), "DEBUG TOOLS BUILD");
#endif
}

static void OpenNotes(void);

static void OpenControlsHelp(void);

// OPTIONS -> CONTROLS (and config.ini): the scheme and the item boxes' lines that go with it.
static void SetControls(bool modern) {
  ModernControls_Set(modern);
  GameText_SetModernLines(modern);
}

static void OptionsTouch(int x, int y) {
  if (UiIn(ResetRect(), x, y)) {
    g_modal = MODAL_RESET;
    return;
  }
  if (UiIn(ControlsHelpRect(), x, y)) {
    OpenControlsHelp();
    return;
  }
  for (int i = 0; i < OPT_COUNT; i++) {
    if (!UiIn(OptRect((OptCell)i), x, y)) continue;
    switch ((OptCell)i) {
    case OPT_PACING:
      g_ui.pacing = (g_ui.pacing + 1) % kPaceCount;
      if (g_ui.pacing == kPaceNoSkip) Toast(Tr(kStrFrameSkipOffToast));
      break;
    case OPT_AUDIO: g_ui.audio_on = !g_ui.audio_on; break;
    case OPT_FPS: g_ui.fps_overlay = (g_ui.fps_overlay + 1) % 5; break;
    case OPT_SPEEDUP:
      if (!g_is_new3ds) return;
      g_ui.new3ds_speedup = !g_ui.new3ds_speedup;
      osSetSpeedupEnable(g_ui.new3ds_speedup);
      break;
    case OPT_DISPLAY: g_ui.pixel_perfect = !g_ui.pixel_perfect; break;
    case OPT_WIDE: g_ui.wide = !g_ui.wide; break;
    case OPT_LANGUAGE: UiLang_Set((g_ui_lang + 1) % UiLang_Count()); break;
    case OPT_AUTO_UPDATE: g_ui.auto_update = !g_ui.auto_update; break;
    case OPT_CHANNEL:
      g_ui.update_beta = !g_ui.update_beta;
      Updater_SetBeta(g_ui.update_beta);
      break;
    case OPT_HUD: g_ui.hud_auto_hide = !g_ui.hud_auto_hide; break;
    case OPT_CONTROLS: SetControls(!ModernControls_On()); break;
    case OPT_SPOILERS: g_show_spoilers = !g_show_spoilers; break;
    case OPT_NOTES: OpenNotes(); break;
    case OPT_UPDATES: Updater_CheckNow(); break;   // a newer build asks (the prompt); the result shows in the cell
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
  UiDrawTextCentered(s, SCREEN_W / 2, 90, COL_TITLE, Tr(kStrResetQuestion));
  UiDrawTextCentered(s, SCREEN_W / 2, 108, COL_DIM, Tr(kStrResetLost1));
  UiDrawTextCentered(s, SCREEN_W / 2, 118, COL_DIM, Tr(kStrResetLost2));
  UiDrawBoxLabel(s, YesRect(), RGB(64, 22, 22), RGB(180, 60, 60), RGB(255, 150, 150), Pressed(YesRect()), Tr(kStrReset));
  UiDrawBoxLabel(s, NoRect(), COL_BOX, COL_BOX_EDGE, COL_TEXT, Pressed(NoRect()), Tr(kStrCancel));
}

static void ResetModalTouch(int x, int y) {
  if (UiIn(YesRect(), x, y)) {
    g_ui.req_reset = true;
    g_modal = MODAL_NONE;
  } else if (UiIn(NoRect(), x, y)) {
    g_modal = MODAL_NONE;
  }
}


// ---- Controls window ------------------------------------------------------------
// OPTIONS -> "?" beside CONTROLS: what each button does, a tab for each scheme (the one in use
// first). The original's buttons are read from the game's own configuration (CONTROLLER SETTING
// MODE can move them); the modern ones are fixed (modern_controls.c).

static bool g_controls_help_modern;   // the tab shown
static Rect ControlsTabRect(int i) { return (Rect){ 150 + i * 80, 30, 76, 16 }; }
static Rect ControlsCloseRect(void) { return (Rect){ 116, 212, 88, 18 }; }

static void OpenControlsHelp(void) {
  g_controls_help_modern = ModernControls_On();
  g_modal = MODAL_CONTROLS;
}

// A SNES button mask of the game's configuration as the 3DS button's name (`fallback`: none set).
static const char *ButtonName(uint16_t mask, const char *fallback) {
  static const struct { uint16_t bit; const char *name; } kNames[] = {
    { 0x0080, "A" }, { 0x8000, "B" }, { 0x0040, "X" }, { 0x4000, "Y" }, { 0x0020, "L" }, { 0x0010, "R" },
    { 0x2000, "SELECT" }, { 0x1000, "START" },
  };
  for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++)
    if (mask & kNames[i].bit) return kNames[i].name;
  return fallback;
}

// A key as the console shows it: a small box with its name, or the D-pad (DOWN: its lower arm lit).
// Returns its width.
static int DrawKey(Surface s, int x, int y, const char *name) {
  if (!strcmp(name, "DPAD") || !strcmp(name, "DOWN")) {
    const uint32_t ink = RGB(150, 190, 230);
    UiFillRect(s, x + 4, y, 3, 11, ink);
    UiFillRect(s, x, y + 4, 11, 3, ink);
    if (name[1] == 'O') UiFillRect(s, x + 4, y + 7, 3, 4, COL_WARN);
    return 11;
  }
  const int w = UiTextWidth(name, 1) + 6;
  UiFillRect(s, x, y, w, 11, RGB(36, 48, 72));
  UiFrameRect(s, x, y, w, 11, RGB(110, 140, 190));
  UiDrawText(s, x + 3, y + 2, 1, COL_TEXT, name);
  return w;
}

// "R+X": the keys of one row with a + between them.
static void DrawKeys(Surface s, int x, int y, const char *keys) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%s", keys);
  bool first = true;
  for (char *k = strtok(buf, "+"); k; k = strtok(NULL, "+"), first = false) {
    if (!first) {
      UiDrawText(s, x, y + 2, 1, COL_DIM, "+");
      x += 8;
    }
    x += DrawKey(s, x, y, k) + 2;
  }
}

static void DrawControlsModal(Surface s) {
  UiFillRect(s, 8, 26, 304, 210, COL_MODAL_EDGE);
  UiFillRect(s, 9, 27, 302, 208, COL_MODAL);
  UiDrawText(s, 16, 34, 1, COL_TITLE, Tr(kStrControls));
  for (int i = 0; i < 2; i++) {
    const Rect t = ControlsTabRect(i);
    const bool on = (i == 1) == g_controls_help_modern;
    UiDrawBoxLabel(s, t, on ? RGB(28, 54, 44) : COL_BOX, on ? RGB(90, 220, 150) : COL_BOX_EDGE,
                   on ? RGB(180, 255, 210) : COL_DIM, Pressed(t), Tr(i ? kStrModern : kStrClassic));
  }
  typedef struct { const char *keys; UiStr what; } Row;
  enum { kRows = 12, kY0 = 54, kPitch = 13, kTextX = 100 };
  Row rows[kRows];
  int n = 0;
  if (!g_controls_help_modern) {
    const struct { uint16_t mask; const char *def; UiStr what; } kClassic[] = {
      { button_config_jump_a, "A", kStrCtlJump }, { button_config_run_b, "B", kStrCtlRun },
      { button_config_shoot_x, "X", kStrCtlFire }, { button_config_itemswitch, "SELECT", kStrCtlChoose },
      { button_config_itemcancel_y, "Y", kStrCtlCancel }, { button_config_aim_up_R, "R", kStrCtlAimUp },
      { button_config_aim_down_L, "L", kStrCtlAimDown },
    };
    rows[n++] = (Row){ "DPAD", kStrCtlMove };
    for (size_t i = 0; i < sizeof(kClassic) / sizeof(kClassic[0]); i++)
      rows[n++] = (Row){ ButtonName(kClassic[i].mask, kClassic[i].def), kClassic[i].what };
    rows[n++] = (Row){ "START", kStrCtlPause };
  } else {
    static const Row kModern[] = {
      { "DPAD", kStrCtlMoveRun }, { "A", kStrCtlJump }, { "B", kStrCtlWalk }, { "X", kStrCtlFireBomb },
      { "Y", kStrCtlMorph }, { "L", kStrCtlAimUp }, { "L+DOWN", kStrCtlAimDownLock }, { "R+X", kStrCtlMissile },
      { "R+Y", kStrCtlGrapple }, { "R+B", kStrCtlXray }, { "SELECT", kStrCtlMissileKind }, { "START", kStrCtlPause },
    };
    for (size_t i = 0; i < sizeof(kModern) / sizeof(kModern[0]) && n < kRows; i++) rows[n++] = kModern[i];
  }
  for (int i = 0; i < n; i++) {
    const int y = kY0 + i * kPitch;
    if (i & 1) UiFillRect(s, 12, y - 1, 296, kPitch, COL_PANEL);
    DrawKeys(s, 16, y, rows[i].keys);
    UiDrawText(s, kTextX, y + 2, 1, COL_TEXT, Tr(rows[i].what));
  }
  if (!g_controls_help_modern) UiDrawText(s, 16, kY0 + n * kPitch + 6, 1, COL_DIM, Tr(kStrCtlConfigNote));
  UiDrawBoxLabel(s, ControlsCloseRect(), COL_BOX, COL_BOX_EDGE, COL_TEXT, Pressed(ControlsCloseRect()), Tr(kStrClose));
}

static void ControlsModalTouch(int x, int y) {
  if (UiIn(ControlsCloseRect(), x, y)) g_modal = MODAL_NONE;
  for (int i = 0; i < 2; i++)
    if (UiIn(ControlsTabRect(i), x, y)) g_controls_help_modern = i == 1;
}

// ---- Items window ---------------------------------------------------------------
// Opened from the items box on the MAP tab: what has been taken in each area, kind by kind
// (SmMap_PickupCounts), and under it the unique items of the area picked (the one the map
// shows when it opens; a tap on a row picks another). SPOILERS off shows the counts taken
// only, never a total, and lists only the unique items found.

enum { kItemsAreas = 6 };   // Ceres has no items
static int g_items_area;
static Rect ItemsRowRect(int i) { return (Rect){ 12, 56 + i * 13, 296, 12 }; }
static Rect ItemsCloseRect(void) { return (Rect){ 116, 212, 88, 18 }; }
static const int kItemsColX[kSmPickupKinds + 1] = { 46, 112, 152, 190, 84, 226, 262 };   // by SmPickupKind, then TOT

// A major item's name from its PLM type (kSmItems / kSmBeams order in cheats.c).
static const char *MajorName(int type) {
  static const int8_t kItem[kSmPickupTypes] = { -1, -1, -1, -1, 3, -1, -1, 4, 6, -1, -1, 8, 0, 1, 10, -1, 9, 5, 7, 2, -1 };
  static const int8_t kBeam[kSmPickupTypes] = { -1, -1, -1, -1, -1, 0, 1, -1, -1, 2, 3, -1, -1, -1, -1, 4, -1, -1, -1, -1, -1 };
  if (type < 0 || type >= kSmPickupTypes) return "?";
  return kItem[type] >= 0 ? TrItem(kItem[type]) : kBeam[type] >= 0 ? TrBeam(kBeam[type]) : "?";
}

static void OpenItems(int area) {
  g_items_area = area >= 0 && area < kItemsAreas ? area : -1;
  g_modal = MODAL_ITEMS;
}

static void DrawItemsCount(Surface s, int x, int y, int got, int all, uint32_t col) {
  char buf[16];
  if (g_show_spoilers) {
    if (all == 0) {
      UiDrawText(s, x, y, 1, COL_FAINT, "-");
      return;
    }
    snprintf(buf, sizeof(buf), "%d/%d", got, all);
    UiDrawText(s, x, y, 1, got == all ? COL_GOOD : col, buf);
  } else {
    snprintf(buf, sizeof(buf), "%d", got);
    UiDrawText(s, x, y, 1, got ? col : COL_FAINT, buf);
  }
}

static void DrawItemsModal(Surface s) {
  static const uint32_t kKindCol[kSmPickupKinds] = { COL_ENERGY, COL_MISSILE, COL_SUPER, COL_PBOMB, COL_RESERVE, COL_TEXT };
  UiFillRect(s, 8, 26, 304, 210, COL_MODAL_EDGE);
  UiFillRect(s, 9, 27, 302, 208, COL_MODAL);

  int sum_all[kSmPickupKinds] = { 0 }, sum_got[kSmPickupKinds] = { 0 }, all_total = 0, got_total = 0;
  int all[kItemsAreas][kSmPickupKinds], got[kItemsAreas][kSmPickupKinds];
  for (int a = 0; a < kItemsAreas; a++) {
    SmMap_PickupCounts(a, all[a], got[a]);
    for (int k = 0; k < kSmPickupKinds; k++) {
      sum_all[k] += all[a][k], sum_got[k] += got[a][k];
      all_total += all[a][k], got_total += got[a][k];
    }
  }
  char title[48];
  if (g_show_spoilers)
    snprintf(title, sizeof(title), "%s: %d/%d (%d%%)", Tr(kStrItems), got_total, all_total, all_total ? got_total * 100 / all_total : 0);
  else
    snprintf(title, sizeof(title), "%s: %d", Tr(kStrItems), got_total);
  UiDrawText(s, 14, 31, 1, COL_TITLE, title);

  // Column heads: energy and reserve tanks are E and R, the ammo has its own short names.
  const int hy = 45;
  UiDrawText(s, 14, hy, 1, COL_ACCENT, Tr(kStrArea));
  UiDrawText(s, kItemsColX[kSmPickupEnergy], hy, 1, COL_ENERGY, "E");
  UiDrawText(s, kItemsColX[kSmPickupReserve], hy, 1, COL_RESERVE, "R");
  UiDrawText(s, kItemsColX[kSmPickupMissile], hy, 1, COL_MISSILE, TrAmmo(0));
  UiDrawText(s, kItemsColX[kSmPickupSuper], hy, 1, COL_SUPER, TrAmmo(1));
  UiDrawText(s, kItemsColX[kSmPickupPowerBomb], hy, 1, COL_PBOMB, TrAmmo(2));
  UiDrawText(s, kItemsColX[kSmPickupMajor], hy, 1, COL_TEXT, Tr(kStrMajorShort));
  UiDrawText(s, kItemsColX[kSmPickupKinds], hy, 1, COL_TEXT, Tr(kStrTotalShort));

  for (int a = 0; a <= kItemsAreas; a++) {   // the last row is the whole game
    const Rect r = ItemsRowRect(a);
    const bool total_row = a == kItemsAreas, picked = a == g_items_area;
    const int *ra = total_row ? sum_all : all[a], *rg = total_row ? sum_got : got[a];
    if (total_row) UiFillRect(s, r.x, r.y - 1, r.w, 1, COL_BORDER);
    else UiFillRect(s, r.x, r.y, r.w, r.h, picked ? RGB(28, 54, 44) : (a & 1) ? COL_MODAL : COL_PANEL);
    if (picked) UiFrameRect(s, r.x, r.y, r.w, r.h, RGB(90, 220, 150));
    UiDrawText(s, 14, r.y + 3, 1, total_row ? COL_TITLE : picked ? RGB(180, 255, 210) : COL_TEXT,
               total_row ? Tr(kStrTotalShort) : TrAreaShort(a));
    int row_all = 0, row_got = 0;
    for (int k = 0; k < kSmPickupKinds; k++) {
      DrawItemsCount(s, kItemsColX[k], r.y + 3, rg[k], ra[k], kKindCol[k]);
      row_all += ra[k], row_got += rg[k];
    }
    DrawItemsCount(s, kItemsColX[kSmPickupKinds], r.y + 3, row_got, row_all, COL_TEXT);
  }

  // The unique items of the area picked, two columns, in the game's item order.
  const int ly = ItemsRowRect(kItemsAreas).y + 18;
  if (g_items_area >= 0) {
    char head[48];
    snprintf(head, sizeof(head), "%s - %s", Tr(kStrUniqueItems), TrArea(g_items_area));
    UiDrawText(s, 14, ly, 1, COL_ACCENT, head);
    int n, shown = 0;
    const SmPickup *items = SmMap_Pickups(&n);
    for (int i = 0; i < n; i++) {
      const SmPickup *it = &items[i];
      if (it->area != g_items_area || it->kind != kSmPickupMajor) continue;
      const bool taken = SmMap_PickupTaken(it);
      if (!taken && !g_show_spoilers) continue;   // not even a "---": that would tell how many are left
      const int x = 20 + (shown % 2) * 146, y = ly + 12 + (shown / 2) * 10;
      UiFillRect(s, x - 6, y + 2, 3, 3, taken ? COL_GOOD : COL_FAINT);
      UiDrawText(s, x, y, 1, taken ? COL_GOOD : COL_DIM, MajorName(it->type));
      shown++;
    }
    if (!shown) UiDrawText(s, 20, ly + 12, 1, COL_FAINT, Tr(kStrNone));
  }
  UiDrawBoxLabel(s, ItemsCloseRect(), COL_BOX, COL_BOX_EDGE, COL_TEXT, Pressed(ItemsCloseRect()), Tr(kStrClose));
}

static void ItemsModalTouch(int x, int y) {
  if (UiIn(ItemsCloseRect(), x, y)) {
    g_modal = MODAL_NONE;
    return;
  }
  for (int a = 0; a < kItemsAreas; a++)
    if (UiIn(ItemsRowRect(a), x, y)) g_items_area = a;
}

// ---- What's new ----------------------------------------------------------------------
// The release notes the updater read on its last check (Updater_CopyNotes), in a window over
// any tab: opened from OPTIONS, or from the "new version" prompt, which hides while it is up
// and comes back on CLOSE. The text is wrapped once, when the window opens.
enum {
  kNotesCols = 44, kNotesMaxLines = 128, kNotesLineBytes = 96, kNotesY0 = 50, kNotesY1 = 206, kNotesPitch = 10,
  kNotesBarX = 302, kNotesBarW = 4, kNotesBarHitX = 294,
};
static Rect NotesCloseRect(void) { return (Rect){ 116, 210, 88, 20 }; }
static char g_notes_text[6144];
static char g_notes_lines[kNotesMaxLines][kNotesLineBytes];
static bool g_notes_head[kNotesMaxLines];   // a "== vX ==" line
static int g_notes_count, g_notes_scroll;   // lines, pixels
static struct { bool active, bar; int last_y; } g_notes_touch;

// Appends one wrapped line; false when the table is full.
static bool NotesAddLine(const char *text, int bytes, bool head) {
  if (g_notes_count >= kNotesMaxLines) return false;
  if (bytes > kNotesLineBytes - 1) bytes = kNotesLineBytes - 1;
  memcpy(g_notes_lines[g_notes_count], text, bytes);
  g_notes_lines[g_notes_count][bytes] = 0;
  g_notes_head[g_notes_count++] = head;
  return true;
}

static void WrapNotes(void) {
  g_notes_count = 0;
  g_notes_scroll = 0;
  const char *p = g_notes_text;
  while (*p) {
    const char *e = strchr(p, '\n');
    if (!e) e = p + strlen(p);
    const bool head = !strncmp(p, "== ", 3);
    const char *t = p;
    bool first = true;
    while (t < e) {
      const int indent = head || first || strncmp(p, "- ", 2) ? 0 : 2;   // a bullet's continuation hangs under its text
      const int room = kNotesCols - indent;
      int n = 0, cut = 0;
      const char *q = t, *cut_at = t;
      while (q < e && n < room) {   // by characters: a UTF-8 sequence is one
        q++;
        while (q < e && (*q & 0xC0) == 0x80) q++;
        n++;
        if (q >= e || *q == ' ') cut = n, cut_at = q;
      }
      if (q >= e) cut_at = e;
      else if (!cut) cut_at = q;
      char line[kNotesLineBytes];
      int len = snprintf(line, sizeof(line), "%*s%.*s", indent, "", (int)(cut_at - t), t);
      if (len >= (int)sizeof(line)) len = sizeof(line) - 1;
      if (!NotesAddLine(line, len, head)) return;
      t = cut_at;
      while (t < e && *t == ' ') t++;
      first = false;
    }
    p = *e ? e + 1 : e;
  }
}

static void OpenNotes(void) {
  Updater_CopyNotes(g_notes_text, sizeof(g_notes_text));
  WrapNotes();
  g_notes_touch.active = false;
  g_modal = MODAL_NOTES;
}

static int NotesMaxScroll(void) {
  const int content = g_notes_count * kNotesPitch, view = kNotesY1 - kNotesY0;
  return content > view ? content - view : 0;
}

static void NotesClamp(void) {
  const int max = NotesMaxScroll();
  if (g_notes_scroll > max) g_notes_scroll = max;
  if (g_notes_scroll < 0) g_notes_scroll = 0;
}

static void NotesScrollToBar(int y) {
  const int track = kNotesY1 - kNotesY0;
  g_notes_scroll = (y - kNotesY0) * (NotesMaxScroll() + track) / track - track / 2;
  NotesClamp();
}

static void DrawNotes(Surface s) {
  UiFillRect(s, 6, 26, 308, 210, COL_TITLE);
  UiFillRect(s, 8, 28, 304, 206, RGB(8, 11, 20));
  UiDrawText(s, 14, 34, 1, COL_TITLE, Tr(kStrWhatsNew));
  const char *tag = Updater_RemoteTag();
  UiDrawText(s, 306 - 6 * (int)strlen(tag), 34, 1, COL_DIM, tag);
  UiFillRect(s, 14, 44, 292, 1, COL_FAINT);
  if (!g_notes_count) {
    UiDrawTextCentered(s, SCREEN_W / 2, 120, COL_DIM, Tr(kStrNotesEmpty));
  } else {
    NotesClamp();
    UiClipY(kNotesY0, kNotesY1);
    for (int i = 0; i < g_notes_count; i++) {
      const int y = kNotesY0 + 2 - g_notes_scroll + i * kNotesPitch;
      if (y + kNotesPitch <= kNotesY0) continue;
      if (y >= kNotesY1) break;
      UiDrawText(s, 14, y, 1, g_notes_head[i] ? COL_TITLE : RGB(220, 235, 255), g_notes_lines[i]);
    }
    UiNoClip();
    const int max = NotesMaxScroll();
    if (max > 0) {
      const int track = kNotesY1 - kNotesY0;
      int thumb = track * track / (track + max);
      if (thumb < 16) thumb = 16;
      UiFillRect(s, kNotesBarX, kNotesY0, kNotesBarW, track, RGB(60, 24, 36));
      UiFillRect(s, kNotesBarX, kNotesY0 + g_notes_scroll * (track - thumb) / max, kNotesBarW, thumb, RGB(80, 160, 240));
    }
  }
  UiDrawBoxLabel(s, NotesCloseRect(), COL_BOX, COL_BOX_EDGE, COL_TEXT, Pressed(NotesCloseRect()), Tr(kStrClose));
}

static void NotesTouch(int x, int y) {
  if (UiIn(NotesCloseRect(), x, y)) {
    g_modal = MODAL_NONE;
  } else if (y >= kNotesY0 && y < kNotesY1) {
    g_notes_touch.active = true;
    g_notes_touch.bar = x >= kNotesBarHitX && NotesMaxScroll() > 0;
    g_notes_touch.last_y = y;
    if (g_notes_touch.bar) NotesScrollToBar(y);
  }
}

static bool NotesTouchMove(int y) {
  if (!g_notes_touch.active) return false;
  const int before = g_notes_scroll;
  if (g_notes_touch.bar) NotesScrollToBar(y);
  else g_notes_scroll += g_notes_touch.last_y - y, NotesClamp();
  g_notes_touch.last_y = y;
  return g_notes_scroll != before;
}

// ---- Update prompt ------------------------------------------------------------
// The updater (updater.c) asks over any tab: a newer build is there (install?), it is installing
// (a bar), it is installed (restart?), or the install failed. Nothing else answers a touch while
// one is up.

static bool NotesAvailable(void) {
  char c[2];
  return Updater_CopyNotes(c, sizeof(c)) > 0;
}

// The "new version" prompt grows a WHAT'S NEW button under YES / NO when the check brought notes.
static Rect PromptNotesRect(void) { return (Rect){ 88, 166, 144, 20 }; }

static void DrawUpdatePrompt(Surface s) {
  const UpdPrompt prompt = Updater_Prompt();
  if (prompt == UPD_PROMPT_NONE || g_modal == MODAL_NOTES) return;   // the notes window has the screen
  const bool notes = prompt == UPD_PROMPT_ASK_INSTALL && NotesAvailable();
  const int h = notes ? 120 : 96;
  UiFillRect(s, 40, 76, 240, h, COL_MODAL_EDGE);
  UiFillRect(s, 41, 77, 238, h - 2, COL_MODAL);
  char line[64];
  switch (prompt) {
  case UPD_PROMPT_ASK_INSTALL:
    snprintf(line, sizeof(line), Tr(kStrUpdAsk), Updater_RemoteTag());
    UiDrawTextCentered(s, SCREEN_W / 2, 90, COL_TITLE, line);
    UiDrawTextCentered(s, SCREEN_W / 2, 104, COL_DIM, Tr(kStrUpdAsk2));
    snprintf(line, sizeof(line), Tr(kStrUpdFrom), g_rom_info.version, Updater_RemoteTag());
    UiDrawTextCentered(s, SCREEN_W / 2, 118, COL_FAINT, line);
    if (notes) UiDrawBoxLabel(s, PromptNotesRect(), COL_BOX, COL_BOX_EDGE, COL_TEXT, Pressed(PromptNotesRect()), Tr(kStrWhatsNew));
    UiDrawBoxLabel(s, YesRect(), COL_BOX, COL_BOX_EDGE, COL_GOOD, Pressed(YesRect()), Tr(kStrYes));
    UiDrawBoxLabel(s, NoRect(), COL_BOX, COL_BOX_EDGE, COL_TEXT, Pressed(NoRect()), Tr(kStrNo));
    break;
  case UPD_PROMPT_PROGRESS: {
    UiDrawTextCentered(s, SCREEN_W / 2, 96, COL_TITLE, Tr(kStrUpdInstalling));
    const int pct = Updater_Progress();
    UiFrameRect(s, 60, 120, 200, 12, COL_MODAL_EDGE);
    UiFillRect(s, 62, 122, 196 * pct / 100, 8, COL_GOOD);
    snprintf(line, sizeof(line), "%d%%", pct);
    UiDrawTextCentered(s, SCREEN_W / 2, 142, COL_DIM, line);
    break;
  }
  case UPD_PROMPT_ASK_RESTART:
    UiDrawTextCentered(s, SCREEN_W / 2, 100, COL_TITLE, Tr(kStrUpdRestart));
    UiDrawBoxLabel(s, YesRect(), COL_BOX, COL_BOX_EDGE, COL_GOOD, Pressed(YesRect()), Tr(kStrYes));
    UiDrawBoxLabel(s, NoRect(), COL_BOX, COL_BOX_EDGE, COL_TEXT, Pressed(NoRect()), Tr(kStrNo));
    break;
  case UPD_PROMPT_ERROR:
    UiDrawTextCentered(s, SCREEN_W / 2, 88, COL_WARN, Tr(kStrUpdFailed));
    UiDrawTextCentered(s, SCREEN_W / 2, 104, COL_DIM, Updater_Message());
    if (Updater_KeptCia()) UiDrawTextCentered(s, SCREEN_W / 2, 116, COL_DIM, Tr(kStrUpdKept));
    UiDrawBoxLabel(s, (Rect){ 112, 136, 96, 24 }, COL_BOX, COL_BOX_EDGE, COL_TEXT, Pressed((Rect){ 112, 136, 96, 24 }),
                   Tr(kStrOk));
    break;
  default: break;
  }
}

static void UpdatePromptTouch(int x, int y) {
  switch (Updater_Prompt()) {
  case UPD_PROMPT_ASK_INSTALL:
  case UPD_PROMPT_ASK_RESTART:
    if (UiIn(YesRect(), x, y)) Updater_AnswerPrompt(true);
    else if (UiIn(NoRect(), x, y)) Updater_AnswerPrompt(false);
    else if (Updater_Prompt() == UPD_PROMPT_ASK_INSTALL && UiIn(PromptNotesRect(), x, y) && NotesAvailable()) OpenNotes();
    break;
  case UPD_PROMPT_ERROR:
    if (UiIn((Rect){ 112, 136, 96, 24 }, x, y)) Updater_AnswerPrompt(true);
    break;
  default: break;
  }
}

// Debug tools: a 2-column grid in a window over the Debug tab, like mzm's.
#if DEBUG_TOOLS

// ---- Report: say what is wrong before a capture is written ---------------------------
// SCREEN DUMP, FRAME DUMP and stopping SCENE REC open this window instead of writing at
// once. The game pauses while it is open (nothing moves under the reason being typed); a
// reason, or a text typed on the keyboard, becomes the capture's note (debug/sm-<kind>-NNNN-
// note.txt, Debug_SetNote) and the capture is written; CANCEL writes nothing. A recording
// being stopped also has RESUME (keep recording); CANCEL throws it away.
typedef enum { REPORT_DUMP, REPORT_FRAME_DUMP, REPORT_SCENE_REC } ReportKind;
static ReportKind g_report_kind;
static bool g_report_was_paused;

static const char *const kReportReasons[] = {
  "3D DEPTH WRONG", "WIDE VIEW BUG", "SPRITE MISSING", "SPRITE FLASHES",
  "WRONG COLOURS", "GLITCH OR GARBAGE", "GAME LOGIC", "PERFORMANCE",
};
enum { kReportReasonCount = sizeof(kReportReasons) / sizeof(kReportReasons[0]) };

static Rect ReasonRect(int i) { return (Rect){ 16 + (i % 2) * 148, 62 + (i / 2) * 28, 140, 24 }; }
static Rect CustomRect(void) { return (Rect){ 16, 182, 140, 24 }; }
// A recording being stopped has two ways out: RESUME (the play triangle) keeps it recording,
// the cross throws it away. For a dump they are the same, so there is one CANCEL.
static Rect ReportResumeRect(void) { return (Rect){ 164, 182, 68, 24 }; }
static Rect ReportCancelRect(void) {
  return g_report_kind == REPORT_SCENE_REC ? (Rect){ 236, 182, 68, 24 } : (Rect){ 164, 182, 140, 24 };
}

static void OpenReport(ReportKind kind) {
  if (g_modal == MODAL_REPORT) return;
  g_report_kind = kind;
  g_report_was_paused = g_ui.paused;
  g_ui.paused = true;
  g_modal = MODAL_REPORT;
  g_dirty = 2;
}

static void DrawReportModal(Surface s) {
  static const char *const kKind[] = { "SCREEN DUMP", "FRAME DUMP", "SCENE REC" };
  UiFillRect(s, 10, 26, 300, 210, COL_MODAL_EDGE);
  UiFillRect(s, 11, 27, 298, 208, COL_MODAL);
  UiDrawText(s, 20, 33, 1, COL_TITLE, "WHAT IS WRONG?");
  UiDrawTextf(s, 20, 45, COL_DIM, "%s - THE GAME IS PAUSED", kKind[g_report_kind]);
  for (int i = 0; i < kReportReasonCount; i++) {
    const Rect r = ReasonRect(i);
    UiDrawBoxLabel(s, r, RGB(24, 32, 50), RGB(50, 80, 130), COL_TEXT, Pressed(r), kReportReasons[i]);
  }
  UiDrawBoxLabel(s, CustomRect(), RGB(30, 55, 90), RGB(90, 160, 240), RGB(180, 225, 255), Pressed(CustomRect()),
                 "WRITE IT...");
  const Rect cancel = ReportCancelRect();
  if (g_report_kind != REPORT_SCENE_REC) {
    UiDrawBoxLabel(s, cancel, RGB(64, 22, 22), RGB(180, 60, 60), RGB(255, 150, 150), Pressed(cancel), "CANCEL");
    UiDrawTextCentered(s, SCREEN_W / 2, 216, COL_FAINT, "CANCEL WRITES NOTHING");
    return;
  }
  const Rect resume = ReportResumeRect();
  UiDrawBox(s, resume, RGB(22, 44, 30), RGB(60, 150, 90), Pressed(resume));
  const int rx = resume.x + resume.w / 2 - 2, ry = resume.y + resume.h / 2;
  for (int dy = -6; dy <= 6; dy++) {   // play triangle: widest in the middle row
    const int len = 11 - (dy < 0 ? -dy : dy) * 11 / 6;
    if (len > 0) UiFillRect(s, rx - 4, ry + dy, len, 1, RGB(120, 230, 140));
  }
  UiDrawBox(s, cancel, RGB(64, 22, 22), RGB(180, 60, 60), Pressed(cancel));
  const int cx = cancel.x + cancel.w / 2, cy = cancel.y + cancel.h / 2;
  for (int d = -5; d <= 5; d++) {   // a cross: both diagonals, 2 px thick
    UiFillRect(s, cx + d - 1, cy + d - 1, 2, 2, RGB(255, 110, 110));
    UiFillRect(s, cx + d - 1, cy - d - 1, 2, 2, RGB(255, 110, 110));
  }
  UiDrawTextCentered(s, SCREEN_W / 2, 216, COL_FAINT, "PLAY KEEPS RECORDING   X THROWS IT AWAY");
}

// The reason is chosen: write the capture, and give the pause back as it was. The dumps
// are taken by the main loop on its next frame, so a game that was paused runs that one
// frame (g_ui.repause) and pauses again.
static void ReportDone(const char *reason) {
  g_modal = MODAL_NONE;
  g_dirty = 2;
  if (!reason) {
    Debug_SetNote(NULL);
    if (g_report_kind == REPORT_SCENE_REC) SceneRec_Discard();   // stopped and cancelled: no file, no note
    g_ui.paused = g_report_was_paused;
    Toast(Debug_LastMessage()[0] && g_report_kind == REPORT_SCENE_REC ? Debug_LastMessage() : "Nothing written");
    return;
  }
  Debug_SetNote(reason);
  switch (g_report_kind) {
  case REPORT_DUMP: g_ui.req_dump = true; break;
  case REPORT_FRAME_DUMP: g_ui.req_frame_dump = true; break;
  case REPORT_SCENE_REC:
    BottomUi_Busy();
    SceneRec_Toggle();   // stopping writes the file: a few seconds with the game frozen
    Toast(Debug_LastMessage());
    g_ui.paused = g_report_was_paused;
    return;
  }
  g_ui.paused = false;
  g_ui.repause = g_report_was_paused;
}

static void ReportTouch(int x, int y) {
  for (int i = 0; i < kReportReasonCount; i++)
    if (UiIn(ReasonRect(i), x, y)) {
      ReportDone(kReportReasons[i]);
      return;
    }
  if (UiIn(CustomRect(), x, y)) {
    SwkbdState kb;
    char text[120] = "";
    swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, sizeof(text) - 1);
    swkbdSetHintText(&kb, "What is wrong in this capture?");
    if (swkbdInputText(&kb, text, sizeof(text)) == SWKBD_BUTTON_CONFIRM && text[0]) ReportDone(text);
    g_dirty = 2;   // a cancelled or empty text leaves the window open
  } else if (g_report_kind == REPORT_SCENE_REC && UiIn(ReportResumeRect(), x, y)) {
    g_modal = MODAL_NONE;   // back to the game, still recording
    g_ui.paused = g_report_was_paused;
    g_dirty = 2;
  } else if (UiIn(ReportCancelRect(), x, y)) {
    ReportDone(NULL);
  }
}

typedef enum {
  TOOL_DUMP, TOOL_FRAME_DUMP, TOOL_LOG, TOOL_MARK, TOOL_SCENE_REC, TOOL_PERF, TOOL_RENDERER, TOOL_GPU_CHECK, TOOL_PLANE_TINT, TOOL_FORCE_3D, TOOL_GPU_TEST,
  TOOL_COUNT
} Tool;

// GPU TEST's states, in the order of kGpuTest* (gpu_ppu_3ds.h).
static const char *const kGpuTestName[] = { "OFF", "BG NOT TEXTURED", "NO BG", "NO SPRITES", "NO TOP PASS", "NO COLOUR MATH", "NO CLEARS", "NO STRIP RUNS" };
enum { kGpuTestModes = sizeof(kGpuTestName) / sizeof(kGpuTestName[0]) };

static Rect ToolRect(int i) { return (Rect){ 16 + (i % 2) * 148, 42 + (i / 2) * 25, 140, 23 }; }
static Rect CloseRect(void) { return (Rect){ 116, 212, 88, 20 }; }
// Cells with something that runs (log, scene recorder), as in mzm: the right side is a
// start/stop button, the rest of the cell changes its option.
static Rect SideRect(int i) {
  const Rect r = ToolRect(i);
  return (Rect){ r.x + r.w - 32, r.y, 32, r.h };
}

static void DrawToolCell(Surface s, int i, const char *label, const char *state, uint32_t state_col) {
  const Rect r = ToolRect(i);
  UiDrawBox(s, r, RGB(24, 32, 50), RGB(50, 80, 130), Pressed(r));
  UiDrawText(s, r.x + 6, r.y + 3, 1, COL_TEXT, label);
  UiDrawText(s, r.x + 6, r.y + 13, 1, state_col, state);
}

// Green play triangle while stopped (tap to start), red stop square while running.
static void DrawSideButton(Surface s, int i, bool running) {
  const Rect r = SideRect(i);
  const uint32_t fg = running ? RGB(255, 90, 90) : RGB(120, 230, 140);
  UiFillRect(s, r.x, r.y + 3, 1, r.h - 6, RGB(90, 110, 150));
  UiFillRect(s, r.x + 1, r.y + 3, r.w - 4, r.h - 6, Pressed(r) ? COL_PRESSED : running ? RGB(70, 22, 22) : RGB(22, 44, 30));
  const int cx = r.x + 1 + (r.w - 4) / 2, cy = r.y + r.h / 2;
  if (running) {
    UiFillRect(s, cx - 5, cy - 5, 10, 10, fg);
  } else {
    for (int dy = -6; dy <= 6; dy++) {   // pointing right: widest in the middle row
      const int len = 11 - (dy < 0 ? -dy : dy) * 11 / 6;
      if (len > 0) UiFillRect(s, cx - 4, cy + dy, len, 1, fg);
    }
  }
}

static void DrawToolsModal(Surface s) {
  UiFillRect(s, 10, 26, 300, 210, COL_MODAL_EDGE);
  UiFillRect(s, 11, 27, 298, 208, COL_MODAL);
  UiDrawText(s, 20, 33, 1, COL_TITLE, "DEBUG TOOLS");
  const uint32_t act = RGB(140, 170, 210), opt = RGB(255, 215, 0);
  DrawToolCell(s, TOOL_DUMP, "SCREEN DUMP", "DUMP SET", act);
  DrawToolCell(s, TOOL_FRAME_DUMP, "FRAME DUMP", "SET + PPU/HDMA LOG", act);
  DrawToolCell(s, TOOL_LOG, "LOG TO SD",
               Debug_LogBuffered() ? "BUFFERED" : "DIRECT", Debug_LogBuffered() ? opt : COL_WARN);
  DrawSideButton(s, TOOL_LOG, Debug_LogEnabled());
  DrawToolCell(s, TOOL_MARK, "LOG MARK", Debug_LogEnabled() ? "MARK" : "LOG IS OFF", Debug_LogEnabled() ? act : COL_FAINT);
  {
    char rec[16];
    if (SceneRec_Active()) snprintf(rec, sizeof(rec), "%d/%d", SceneRec_Frames(), SceneRec_Capacity());
    else snprintf(rec, sizeof(rec), "%s", SceneRec_RateLabel());
    DrawToolCell(s, TOOL_SCENE_REC, "SCENE REC", rec, SceneRec_Active() ? COL_BAD : opt);
    DrawSideButton(s, TOOL_SCENE_REC, SceneRec_Active());
  }
  DrawToolCell(s, TOOL_PERF, "PERF RECORDER", Debug_PerfRecording() ? "RECORDING" : "OFF",
               Debug_PerfRecording() ? COL_BAD : act);
  DrawToolCell(s, TOOL_RENDERER, "RENDERER", g_ui.gpu_render ? "GPU (CPU FALLBACK)" : "CPU",
               g_ui.gpu_render ? COL_GOOD : act);
  DrawToolCell(s, TOOL_GPU_CHECK, "GPU CHECK", g_ui.gpu_render ? "GPU VS CPU, DUMP SET" : "RENDERER IS CPU",
               g_ui.gpu_render ? act : COL_FAINT);
  static const char *const kTintName[] = { "OFF", "PLANES", "DRAW ORDER", "STEREO DEPTH" };
  DrawToolCell(s, TOOL_PLANE_TINT, "PLANE TINT", !g_ui.gpu_render ? "RENDERER IS CPU" : kTintName[g_ui.plane_tint & 3],
               !g_ui.gpu_render ? COL_FAINT : g_ui.plane_tint ? COL_GOOD : act);
  DrawToolCell(s, TOOL_FORCE_3D, "FORCE 3D", !g_ui.gpu_render ? "RENDERER IS CPU" : g_ui.force_3d ? "ON: 2 EYES" : "OFF",
               !g_ui.gpu_render ? COL_FAINT : g_ui.force_3d ? COL_WARN : COL_DIM);
  DrawToolCell(s, TOOL_GPU_TEST, "GPU TEST", !g_ui.gpu_render ? "RENDERER IS CPU" : kGpuTestName[g_ui.gpu_test % kGpuTestModes],
               !g_ui.gpu_render ? COL_FAINT : g_ui.gpu_test ? COL_WARN : COL_DIM);
  if (g_ui.plane_tint >= 2 && g_ui.gpu_render) {   // legend of the ramps: back dark .. front bright
    UiDrawText(s, 16, 195, 1, COL_DIM, "BACK");
    for (int i = 0; i < 160; i++) {
      const uint32_t c = StereoDepth_RampColor(i / 159.0f, g_ui.plane_tint == 3 ? kRampDepth : kRampOrder);
      UiFillRect(s, 52 + i, 196, 1, 6, RGB(c >> 16 & 255, c >> 8 & 255, c & 255));
    }
    UiDrawText(s, 216, 195, 1, COL_DIM, "FRONT");
  } else if (g_ui.plane_tint == 1 && g_ui.gpu_render) {   // legend: a chip and the name of each plane, nearest first
    int x = 16;
    for (int p = 0; p < kStereoPlaneCount - 1; p++) {
      const uint32_t c = StereoDepth_PlaneColor((StereoPlane)p);
      UiFillRect(s, x, 196, 6, 6, RGB(c >> 16 & 255, c >> 8 & 255, c & 255));
      UiDrawText(s, x + 8, 195, 1, COL_DIM, StereoDepth_PlaneName((StereoPlane)p));
      x += 8 + (int)strlen(StereoDepth_PlaneName((StereoPlane)p)) * 6 + 6;
    }
  }
  UiDrawTextCentered(s, SCREEN_W / 2, 204, COL_WARN, Debug_LastMessage());
  UiDrawBoxLabel(s, CloseRect(), COL_BOX, COL_BOX_EDGE, COL_TEXT, Pressed(CloseRect()), "CLOSE");
}

static void ToolsModalTouch(int x, int y) {
  if (UiIn(CloseRect(), x, y)) {
    g_modal = MODAL_NONE;
    return;
  }
  for (int i = 0; i < TOOL_COUNT; i++) {
    if (!UiIn(ToolRect(i), x, y)) continue;
    const bool side = UiIn(SideRect(i), x, y);
    switch ((Tool)i) {
    case TOOL_DUMP: OpenReport(REPORT_DUMP); break;
    case TOOL_FRAME_DUMP: OpenReport(REPORT_FRAME_DUMP); break;
    case TOOL_LOG:
      if (side) {
        if (Debug_LogEnabled()) BottomUi_Busy();   // stopping writes what is buffered
        Debug_LogSetEnabled(!Debug_LogEnabled());
        Toast(Debug_LastMessage());
      } else {
        Debug_LogSetBuffered(!Debug_LogBuffered());
        Toast(Debug_LogBuffered() ? "Log: buffered, 16 KB blocks" : "Log: direct, every line");
      }
      break;
    case TOOL_MARK:
      if (Debug_LogEnabled()) { Debug_LogMark(); Toast("Mark written"); }
      else Toast("Turn the log on first");
      break;
    case TOOL_SCENE_REC:
      if (side) {
        if (SceneRec_Active()) {
          OpenReport(REPORT_SCENE_REC);   // stopping writes the file: asks what is wrong first
        } else {
          SceneRec_Toggle();
          Toast(Debug_LastMessage());
        }
      } else if (SceneRec_Active()) {
        Toast("Stop the recorder to change the rate");
      } else {
        SceneRec_CycleRate();
      }
      break;
    case TOOL_PERF:
      if (Debug_PerfRecording()) BottomUi_Busy();   // stopping writes the CSV
      Debug_PerfToggle();
      Toast(Debug_LastMessage());
      break;
    case TOOL_RENDERER: g_ui.gpu_render = !g_ui.gpu_render; break;
    case TOOL_GPU_CHECK:
      if (!g_ui.gpu_render) Toast("Switch the renderer to GPU first");
      else g_ui.req_gpu_check = true;
      break;
    case TOOL_PLANE_TINT:
      if (!g_ui.gpu_render) Toast("Switch the renderer to GPU first");
      else g_ui.plane_tint = (g_ui.plane_tint + 1) % 4;
      break;
    case TOOL_FORCE_3D:
      if (!g_ui.gpu_render) Toast("Switch the renderer to GPU first");
      else g_ui.force_3d = !g_ui.force_3d;
      break;
    case TOOL_GPU_TEST:
      if (!g_ui.gpu_render) Toast("Switch the renderer to GPU first");
      else g_ui.gpu_test = (g_ui.gpu_test + 1) % kGpuTestModes;
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

// ---- Achievements tab -------------------------------------------------------------
// RetroAchievements (retro_ach.h), laid out after mzm's achievement windows: the account
// and the settings (log in, on/off, where the notice shows with a sample, the sound) on
// top, then the set as cards with their badges, scrolled by dragging the list or its bar.
// A tap on a card opens it (MODAL_RA_DETAIL). The unlock notice is drawn over any tab.

enum {
  kRaListY0 = 106, kRaListY1 = 238,   // the cards' band
  kRaCardX = 6, kRaCardW = 300, kRaCardPitch = 34, kRaCardH = 32,
  kRaBarX = 310, kRaBarW = 4, kRaBarHitX = 306,
  kRaDragSlop = 6,   // a stylus wobbles this much on a tap; past it the touch scrolls
};
static int g_ra_scroll;   // pixels
static struct { bool active, dragging, bar; int start_y, last_y; } g_ra_touch;
static RaAchievement g_ra_detail;   // the card opened, copied: the list may change under it

static Rect RaCellRect(int i) { return (Rect){ 8 + (i % 2) * 154, 50 + (i / 2) * 19, 150, 16 }; }
static Rect RaPreviewRect(void) { const Rect r = RaCellRect(2); return (Rect){ r.x + r.w - 20, r.y, 20, r.h }; }
static Rect RaSortRect(void) { return (Rect){ 170, 88, 120, 14 }; }
static Rect RaDirRect(void) { return (Rect){ 292, 88, 20, 14 }; }
static Rect RaDetailCloseRect(void) { return (Rect){ 116, 210, 88, 20 }; }

// Copies at most `chars` characters (UTF-8) of `src`.
static void ClipText(char *dst, size_t size, const char *src, int chars) {
  size_t n = 0;
  for (int c = 0; src[n] && c < chars; c++) {
    size_t len = 1;
    while (src[n + len] && (src[n + len] & 0xC0) == 0x80) len++;
    if (n + len >= size) break;
    n += len;
  }
  memcpy(dst, src, n);
  dst[n] = 0;
}

// `text` on up to `lines` lines of `chars` characters, broken at spaces, 10 px apart.
// Returns the lines used.
static int DrawWrapped(Surface s, int x, int y, int chars, int lines, uint32_t col, const char *text) {
  int line = 0;
  for (; line < lines && *text; line++) {
    int n = 0, cut = 0;
    size_t at = 0, cut_at = 0;
    while (text[at] && n < chars) {
      size_t len = 1;
      while (text[at + len] && (text[at + len] & 0xC0) == 0x80) len++;
      at += len, n++;
      if (text[at] == ' ' || !text[at]) cut = n, cut_at = at;
    }
    if (!text[at] || !cut) cut_at = at;
    char buf[160];
    snprintf(buf, sizeof(buf), "%.*s", (int)cut_at, text);
    UiDrawText(s, x, y + line * 10, 1, col, buf);
    text += cut_at;
    while (*text == ' ') text++;
  }
  return line;
}

static bool RaLoggedIn(void) {
  const RaStatus st = RetroAch_Status();
  return st == kRaOnline || st == kRaOffline || st == kRaConnecting;
}

static int RaMaxScroll(void) {
  const int content = RetroAch_Count() * kRaCardPitch - (kRaCardPitch - kRaCardH);
  return content > kRaListY1 - kRaListY0 ? content - (kRaListY1 - kRaListY0) : 0;
}

static void RaClampScroll(void) {
  const int max = RaMaxScroll();
  if (g_ra_scroll > max) g_ra_scroll = max;
  if (g_ra_scroll < 0) g_ra_scroll = 0;
}

// Scrollbar track position -> scroll offset.
static void RaScrollToBar(int y) {
  g_ra_scroll = (y - kRaListY0) * RaMaxScroll() / (kRaListY1 - kRaListY0);
  RaClampScroll();
}

static uint32_t RaTypeColor(RaType t) {
  switch (t) {
  case kRaTypeMissable: return RGB(255, 140, 40);
  case kRaTypeProgression: return RGB(90, 160, 255);
  case kRaTypeWin: return RGB(210, 120, 255);
  default: return COL_DIM;
  }
}

// RA's glyph for a special type, 12x12 at (x, y), as on its website: a warning triangle
// (missable), rising bars (progression), a chequered flag (win condition).
static void DrawTypeIcon(Surface s, int x, int y, RaType t, bool dim) {
  if (t == kRaTypeStandard) return;
  uint32_t c = RaTypeColor(t);
  if (dim) c = ((c >> 24) / 2 + 30) << 24 | ((c >> 16 & 0xFF) / 2 + 30) << 16 | ((c >> 8 & 0xFF) / 2 + 30) << 8 | 0xFF;
  const uint32_t bg = RGB(12, 16, 24);
  if (t == kRaTypeMissable) {
    for (int r = 0; r < 6; r++) UiFillRect(s, x + 5 - r, y + r * 2, 2 + r * 2, 2, c);
    UiFillRect(s, x + 5, y + 3, 2, 4, bg);
    UiFillRect(s, x + 5, y + 8, 2, 2, bg);
  } else if (t == kRaTypeProgression) {
    UiFillRect(s, x, y + 7, 3, 5, c);
    UiFillRect(s, x + 4, y + 4, 3, 8, c);
    UiFillRect(s, x + 8, y + 1, 3, 11, c);
  } else {
    UiFillRect(s, x + 1, y, 2, 12, c);
    UiFillRect(s, x + 3, y + 1, 8, 6, c);
    static const int8_t kSquares[5][2] = { { 3, 1 }, { 7, 1 }, { 5, 3 }, { 3, 5 }, { 7, 5 } };
    for (int i = 0; i < 5; i++) UiFillRect(s, x + kSquares[i][0], y + kSquares[i][1], 2, 2, bg);
  }
}

// The badge, or while it loads a box with a check (unlocked) or a question mark.
static void DrawBadge(Surface s, int x, int y, int size, const RaAchievement *a) {
  const uint32_t *px = RetroAch_Badge(a->badge, size);
  if (px) {
    UiBlit(s, x, y, size, px, !a->unlocked);
    return;
  }
  UiFillRect(s, x, y, size, size, RGB(12, 16, 24));
  const int m = size / 5;
  UiFillRect(s, x + m, y + m, size - 2 * m, size - 2 * m, a->unlocked ? RGB(60, 200, 100) : RGB(40, 50, 70));
  UiDrawText(s, x + size / 2 - 2, y + size / 2 - 3, 1, a->unlocked ? COL_TEXT : RGB(120, 140, 170),
             a->unlocked ? "*" : "?");
}

// 8x8 padlock at (x, y): open and green when unlocked, closed and grey otherwise.
static void DrawPadlock(Surface s, int x, int y, bool open) {
  const uint32_t c = open ? RGB(80, 255, 120) : RGB(110, 130, 160);
  if (open) {
    UiFillRect(s, x + 3, y, 4, 1, c);
    UiFillRect(s, x + 6, y + 1, 1, 2, c);
  } else {
    UiFillRect(s, x + 2, y, 4, 1, c);
    UiFillRect(s, x + 2, y + 1, 1, 2, c);
    UiFillRect(s, x + 5, y + 1, 1, 2, c);
  }
  UiFillRect(s, x + 1, y + 3, 6, 4, c);
}

static void DrawCard(Surface s, int y, const RaAchievement *a) {
  const int x = kRaCardX;
  UiFillRect(s, x, y, kRaCardW, kRaCardH, a->unlocked ? RGB(35, 120, 65) : RGB(35, 45, 65));
  UiFillRect(s, x + 1, y + 1, kRaCardW - 2, kRaCardH - 2, a->unlocked ? RGB(16, 38, 26) : RGB(18, 22, 34));
  UiFillRect(s, x + 1, y + 1, 30, 30, a->unlocked ? RGB(80, 255, 120) : RGB(60, 75, 100));
  DrawBadge(s, x + 2, y + 2, kRaBadgeSmall, a);
  char buf[96];
  ClipText(buf, sizeof(buf), a->title, 40);
  UiDrawText(s, x + 36, y + 6, 1, a->unlocked ? RGB(140, 240, 170) : RGB(220, 235, 255), buf);
  snprintf(buf, sizeof(buf), Tr(kStrRaPoints), (unsigned)a->points);
  UiDrawText(s, x + 36, y + 19, 1, a->unlocked ? RGB(120, 255, 160) : RGB(140, 160, 190), buf);
  DrawPadlock(s, x + kRaCardW - 13, y + 4, a->unlocked);
  DrawTypeIcon(s, x + kRaCardW - 15, y + 16, a->type, !a->unlocked);
}

static void DrawAchievements(Surface s) {
  UiDrawText(s, 8, 28, 1, COL_TITLE, "RETROACHIEVEMENTS");
  UiDrawText(s, SCREEN_W - 8 - UiTextWidth("SOFTCORE", 1), 28, 1, COL_DIM, "SOFTCORE");
  // One line: the server's last word beats the connection state.
  char buf[96];
  uint32_t col = COL_DIM;
  if (RetroAch_Message()[0]) {
    ClipText(buf, sizeof(buf), RetroAch_Message(), 50);
    col = COL_WARN;
  } else {
    switch (RetroAch_Status()) {
    case kRaOff: snprintf(buf, sizeof(buf), "%s", Tr(kStrRaDisabled)); break;
    case kRaNoAccount: snprintf(buf, sizeof(buf), "%s", Tr(kStrRaNoAccount)); break;
    case kRaConnecting: snprintf(buf, sizeof(buf), "%s", Tr(kStrRaConnecting)); col = COL_WARN; break;
    case kRaOnline: snprintf(buf, sizeof(buf), Tr(kStrRaOnline), RetroAch_User()); col = COL_GOOD; break;
    case kRaOffline: snprintf(buf, sizeof(buf), "%s", Tr(kStrRaOffline)); col = COL_WARN; break;
    case kRaLoginError: snprintf(buf, sizeof(buf), "%s", Tr(kStrRaLoginError)); col = COL_BAD; break;
    }
  }
  UiDrawText(s, 8, 39, 1, col, buf);

  // Settings, two by two, as mzm's SETTINGS window.
  const bool on = RetroAch_Enabled();
  UiDrawBoxLabel(s, RaCellRect(0), COL_BOX, COL_BOX_EDGE, COL_TEXT, Pressed(RaCellRect(0)),
                 Tr(RaLoggedIn() ? kStrRaLogout : kStrRaLogin));
  snprintf(buf, sizeof(buf), "%s: %s", Tr(kStrRaAchievements), Tr(on ? kStrOn : kStrOff));
  UiDrawBoxLabel(s, RaCellRect(1), on ? RGB(20, 70, 40) : COL_BOX, on ? COL_ON : COL_BOX_EDGE, COL_TEXT,
                 Pressed(RaCellRect(1)), buf);
  Rect notice = RaCellRect(2);
  notice.w -= RaPreviewRect().w + 2;
  snprintf(buf, sizeof(buf), "%s: %s", Tr(kStrRaNotify), Tr(RetroAch_NotifyTop() ? kStrRaTop : kStrRaBottom));
  UiDrawBoxLabel(s, notice, COL_BOX, COL_BOX_EDGE, COL_TEXT, Pressed(notice), buf);
  const Rect p = RaPreviewRect();   // the sample: a play triangle
  UiDrawBox(s, p, COL_BTN, COL_BORDER, Pressed(p));
  for (int i = 0; i < 4; i++) UiFillRect(s, p.x + 8 + i, p.y + 4 + i, 1, 8 - 2 * i, COL_GOOD);
  const bool snd = RetroAch_Sound();
  snprintf(buf, sizeof(buf), "%s: %s", Tr(kStrRaSound), Tr(snd ? kStrOn : kStrOff));
  UiDrawBoxLabel(s, RaCellRect(3), snd ? RGB(20, 70, 40) : COL_BOX, snd ? COL_ON : COL_BOX_EDGE, COL_TEXT,
                 Pressed(RaCellRect(3)), buf);

  const int n = RetroAch_Count();
  if (!n) {
    const bool loading = on && RetroAch_Status() == kRaOnline && !RetroAch_Message()[0];
    UiDrawTextCentered(s, SCREEN_W / 2, 160, COL_DIM, Tr(loading ? kStrRaLoading : kStrRaNoList));
    return;
  }
  snprintf(buf, sizeof(buf), Tr(kStrRaSummary), RetroAch_UnlockedCount(), n, (unsigned)RetroAch_Points(true),
           (unsigned)RetroAch_Points(false));
  UiDrawText(s, 8, 92, 1, COL_GOOD, buf);
  // The order, and its direction as a chevron (down = descending).
  static const UiStr kSorts[kRaSortCount] = { kStrRaSortDefault, kStrRaSortTitle, kStrRaSortPoints, kStrRaSortRecent };
  UiDrawBoxLabel(s, RaSortRect(), RGB(24, 40, 70), RGB(70, 110, 170), RGB(190, 220, 255), Pressed(RaSortRect()),
                 Tr(kSorts[RetroAch_Sort()]));
  const Rect d = RaDirRect();
  UiDrawBox(s, d, RGB(24, 40, 70), RGB(70, 110, 170), Pressed(d));
  for (int row = 0; row < 4; row++) {
    const int w = 1 + 2 * (RetroAch_Descending() ? 3 - row : row);
    UiFillRect(s, d.x + d.w / 2 - w / 2, d.y + 4 + row * 2, w, 2, RGB(150, 200, 255));
  }

  RaClampScroll();
  UiClipY(kRaListY0, kRaListY1);
  for (int i = 0; i < n; i++) {
    const int y = kRaListY0 - g_ra_scroll + i * kRaCardPitch;
    if (y + kRaCardH <= kRaListY0) continue;
    if (y >= kRaListY1) break;
    DrawCard(s, y, RetroAch_Get(i));
  }
  UiNoClip();
  const int max = RaMaxScroll();
  if (max > 0) {
    const int track = kRaListY1 - kRaListY0;
    int thumb = track * track / (track + max);
    if (thumb < 16) thumb = 16;
    UiFillRect(s, kRaBarX, kRaListY0, kRaBarW, track, RGB(60, 24, 36));
    UiFillRect(s, kRaBarX, kRaListY0 + g_ra_scroll * (track - thumb) / max, kRaBarW, thumb, RGB(80, 160, 240));
  }
}

static void AchievementsTouch(int x, int y) {
  if (UiIn(RaCellRect(0), x, y)) {
    if (RaLoggedIn()) RetroAch_Logout();
    else RetroAch_PromptLogin();
  } else if (UiIn(RaCellRect(1), x, y)) {
    RetroAch_SetEnabled(!RetroAch_Enabled());
  } else if (UiIn(RaPreviewRect(), x, y)) {
    RetroAch_ShowPreview();
  } else if (UiIn(RaCellRect(2), x, y)) {
    RetroAch_SetNotifyTop(!RetroAch_NotifyTop());
  } else if (UiIn(RaCellRect(3), x, y)) {
    RetroAch_SetSound(!RetroAch_Sound());
  } else if (!RetroAch_Count()) {
    return;
  } else if (UiIn(RaSortRect(), x, y)) {
    RetroAch_SetSort((RaSort)((RetroAch_Sort() + 1) % kRaSortCount), RetroAch_Descending());
    g_ra_scroll = 0;   // the old offset means nothing in a new order
  } else if (UiIn(RaDirRect(), x, y)) {
    RetroAch_SetSort(RetroAch_Sort(), !RetroAch_Descending());
    g_ra_scroll = 0;
  } else if (y >= kRaListY0 && y < kRaListY1) {
    // A card opens on release, and only if the touch never became a drag.
    g_ra_touch.active = true;
    g_ra_touch.dragging = false;
    g_ra_touch.bar = x >= kRaBarHitX && RaMaxScroll() > 0;
    g_ra_touch.start_y = g_ra_touch.last_y = y;
    if (g_ra_touch.bar) RaScrollToBar(y);
  }
}

static bool AchievementsTouchMove(int y) {
  if (!g_ra_touch.active) return false;
  const int before = g_ra_scroll;
  if (g_ra_touch.bar) {
    RaScrollToBar(y);
  } else if (g_ra_touch.dragging || abs(y - g_ra_touch.start_y) >= kRaDragSlop) {
    g_ra_touch.dragging = true;
    g_ra_scroll += g_ra_touch.last_y - y;
    RaClampScroll();
  } else {
    return false;   // still a tap: keep last_y where it landed
  }
  g_ra_touch.last_y = y;
  return g_ra_scroll != before;
}

static void AchievementsTouchUp(void) {
  if (!g_ra_touch.active) return;
  g_ra_touch.active = false;
  if (g_ra_touch.dragging || g_ra_touch.bar) return;
  const int i = (g_ra_touch.start_y - kRaListY0 + g_ra_scroll) / kRaCardPitch;
  const RaAchievement *a = RetroAch_Get(i);
  if (a && (g_ra_touch.start_y - kRaListY0 + g_ra_scroll) % kRaCardPitch < kRaCardH) {
    g_ra_detail = *a;
    g_modal = MODAL_RA_DETAIL;
  }
}

// One achievement in full, as mzm's detail window: the badge at full size, the title,
// points, state with the unlock date, type, and the whole description.
static void DrawRaDetail(Surface s) {
  const RaAchievement *a = &g_ra_detail;
  UiFillRect(s, 6, 26, 308, 210, COL_TITLE);
  UiFillRect(s, 8, 28, 304, 206, RGB(8, 11, 20));
  UiFillRect(s, 16, 36, kRaBadgeBig + 4, kRaBadgeBig + 4, a->unlocked ? RGB(80, 255, 120) : RGB(60, 75, 100));
  DrawBadge(s, 18, 38, kRaBadgeBig, a);
  const int tx = 18 + kRaBadgeBig + 10;
  const int lines = DrawWrapped(s, tx, 38, (306 - tx) / 6, 3, RGB(255, 230, 120), a->title);
  int y = 42 + lines * 10;
  char buf[64];
  snprintf(buf, sizeof(buf), Tr(kStrRaPoints), (unsigned)a->points);
  UiDrawText(s, tx, y, 1, RGB(120, 255, 160), buf);
  y += 12;
  UiDrawText(s, tx, y, 1, a->unlocked ? RGB(80, 255, 120) : RGB(150, 170, 200),
             Tr(a->unlocked ? kStrRaUnlockedState : kStrRaLockedState));
  y += 12;
  if (a->unlocked && a->unlock_time) {
    const time_t t = (time_t)a->unlock_time;
    const struct tm *tm = gmtime(&t);
    if (tm && strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", tm)) UiDrawText(s, tx, y, 1, RGB(150, 180, 210), buf);
  }
  if (a->type != kRaTypeStandard) {
    static const UiStr kTypes[] = { kStrRaMissable, kStrRaMissable, kStrRaProgression, kStrRaWin };
    DrawTypeIcon(s, 18, 114, a->type, false);
    UiDrawText(s, 36, 117, 1, RaTypeColor(a->type), Tr(kTypes[a->type]));
  }
  DrawWrapped(s, 18, 136, 47, 7, RGB(220, 235, 255), a->description);
  UiDrawBoxLabel(s, RaDetailCloseRect(), COL_BOX, COL_BOX_EDGE, COL_TEXT, Pressed(RaDetailCloseRect()), Tr(kStrClose));
}

static void RaDetailTouch(int x, int y) {
  if (UiIn(RaDetailCloseRect(), x, y)) g_modal = MODAL_NONE;
}

// The unlock notice's box, 300x36 at (x, y), with the badge when it has been loaded.
static void DrawNoticeBox(Surface s, int x, int y, const RaAchievement *a) {
  const Rect r = { x, y, 300, 36 };
  UiFillRect(s, r.x, r.y, r.w, r.h, COL_GOOD);
  UiFillRect(s, r.x + 1, r.y + 1, r.w - 2, r.h - 2, RGB(14, 20, 32));
  const uint32_t *badge = RetroAch_Badge(a->badge, kRaBadgeSmall);
  int tx = r.x + 10;
  if (badge) {
    UiBlit(s, r.x + 4, r.y + 4, kRaBadgeSmall, badge, false);
    tx = r.x + 4 + kRaBadgeSmall + 6;
  } else {
    UiFillRect(s, r.x + 3, r.y + 3, 2, r.h - 6, RGB(40, 150, 90));
  }
  UiDrawText(s, tx, r.y + 8, 1, COL_GOOD, Tr(kStrRaUnlocked));
  char title[96], line[128];
  ClipText(title, sizeof(title), a->title, badge ? 34 : 38);
  snprintf(line, sizeof(line), "%s (+%u)", title, (unsigned)a->points);
  UiDrawText(s, tx, r.y + 21, 1, COL_TEXT, line);
}

// On the bottom screen: over the tab bar on any tab, for as long as RetroAch_Toast says.
static void DrawUnlockNotice(Surface s) {
  const RaAchievement *a = RetroAch_Toast();
  if (a && !RetroAch_NotifyTop()) DrawNoticeBox(s, 10, 2, a);
}

// ---- Persistent options -----------------------------------------------------
// Saved to config.ini in the data folder whenever one changes. Not persisted on
// purpose: pause, turbo, cheats and the log/perf recorders,
// which would be confusing or harmful to find switched on at the next boot.

#define CONFIG_PATH "config.ini"

typedef struct {
  int tab, pacing, audio, fps_overlay, speedup, pixel_perfect, wide, language, map_zoom, auto_update, update_beta, hud_hide, spoilers,
      controls, missile;
} SavedOptions;

static SavedOptions CurrentOptions(void) {
  return (SavedOptions){ g_tab, g_ui.pacing, g_ui.audio_on, g_ui.fps_overlay, g_ui.new3ds_speedup,
                         g_ui.pixel_perfect, g_ui.wide, g_ui_lang, g_map_zoom, g_ui.auto_update, g_ui.update_beta, g_ui.hud_auto_hide,
                         g_show_spoilers, ModernControls_On(), ModernControls_Missile() };
}

static SavedOptions g_saved_options;   // what config.ini holds

static void SaveConfig(void) {
  FILE *f = fopen(CONFIG_PATH, "w");
  if (!f) return;
  SavedOptions o = CurrentOptions();
  g_saved_options = o;
  fprintf(f, "# Super Metroid 3DS options (written by the bottom screen)\n");
  fprintf(f, "tab=%d\npacing=%d\naudio=%d\nfps_overlay=%d\nnew3ds_speedup=%d\npixel_perfect=%d\nwide=%d\n"
          "language=%s\nmap_zoom=%d\nauto_update=%d\nupdate_beta=%d\nhud_auto_hide=%d\nspoilers=%d\ncontrols=%s\nmodern_missile=%d\n",
          o.tab, o.pacing, o.audio, o.fps_overlay, o.speedup, o.pixel_perfect, o.wide,
          UiLang_Code(o.language), o.map_zoom, o.auto_update, o.update_beta, o.hud_hide, o.spoilers,
          o.controls ? "modern" : "classic", o.missile);
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
    char code[16];
    // The language by its code ("language=es"); builds before the language files stored an index.
    static const char *const kOldLangs[] = { "en", "es", "ca", "fr", "pt", "ja" };
    if (sscanf(line, "language=%d", &v) == 1) {
      if (v >= 0 && v < (int)(sizeof(kOldLangs) / sizeof(kOldLangs[0])) && UiLang_FromCode(kOldLangs[v]) >= 0)
        UiLang_Set(UiLang_FromCode(kOldLangs[v]));
      continue;
    }
    if (sscanf(line, "language=%15[^\r\n]", code) == 1) {
      if (UiLang_FromCode(code) >= 0) UiLang_Set(UiLang_FromCode(code));
      continue;
    }
    if (sscanf(line, "controls=%15[^\r\n]", code) == 1) {
      SetControls(!strcmp(code, "modern"));
      continue;
    }
    if (sscanf(line, "%31[^=]=%d", key, &v) != 2) continue;
    if (!strcmp(key, "tab") && v >= 0 && v < TAB_COUNT && TabVisible((Tab)v)) g_tab = (Tab)v;
    else if (!strcmp(key, "pacing") && v >= 0 && v < kPaceCount) g_ui.pacing = (int)v;
    else if (!strcmp(key, "frameskip")) g_ui.pacing = v ? kPaceAuto : kPaceNoSkip;   // the old on/off setting
    else if (!strcmp(key, "audio")) g_ui.audio_on = v != 0;
    else if (!strcmp(key, "fps_overlay") && v >= 0 && v <= 4) g_ui.fps_overlay = (int)v;   // 1 was "on": the top-left corner
    else if (!strcmp(key, "new3ds_speedup")) g_ui.new3ds_speedup = v != 0;
    else if (!strcmp(key, "pixel_perfect")) g_ui.pixel_perfect = v != 0;
    else if (!strcmp(key, "wide")) g_ui.wide = v != 0;
    else if (!strcmp(key, "auto_update")) g_ui.auto_update = v != 0;
    else if (!strcmp(key, "update_beta")) g_ui.update_beta = v != 0;
    else if (!strcmp(key, "hud_auto_hide")) g_ui.hud_auto_hide = v != 0;
    else if (!strcmp(key, "spoilers")) g_show_spoilers = v != 0;
    else if (!strcmp(key, "modern_missile")) ModernControls_SetMissile(v);
    else if (!strcmp(key, "map_zoom") && v >= 0 && v < MAP_ZOOMS) g_map_zoom = v;
  }
  fclose(f);
}

// ---- Touch and frame --------------------------------------------------------

static void SelectTab(Tab t) {
  g_tab = t;
  g_modal = MODAL_NONE;
  g_arm_state = -1;
  if (t == TAB_STATES) RefreshSlots();
}

static void TouchDownImpl(int x, int y) {
  Tab tabs[TAB_COUNT];
  const int n = VisibleTabs(tabs);
  if (g_modal == MODAL_NOTES && Updater_Prompt() != UPD_PROMPT_NONE) {   // opened from the prompt: it steps aside
    NotesTouch(x, y);
    return;
  }
  if (Updater_Prompt() != UPD_PROMPT_NONE) {   // the update prompt is over everything
    UpdatePromptTouch(x, y);
    return;
  }
#if DEBUG_TOOLS
  if (g_modal == MODAL_REPORT) {   // the game is paused until a reason or CANCEL: nothing else works
    ReportTouch(x, y);
    return;
  }
#endif
  for (int i = 0; i < n; i++) {
    if (UiIn(TabRect(i), x, y)) {
      SelectTab(tabs[i]);
      return;
    }
  }
  // A window swallows every touch below the tab bar.
  switch (g_modal) {
  case MODAL_RESET: ResetModalTouch(x, y); return;
  case MODAL_RA_DETAIL: RaDetailTouch(x, y); return;
  case MODAL_STATE: StateDetailTouch(x, y); return;
  case MODAL_NOTES: NotesTouch(x, y); return;
  case MODAL_ITEMS: ItemsModalTouch(x, y); return;
  case MODAL_CONTROLS: ControlsModalTouch(x, y); return;
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
  case TAB_ACHIEVEMENTS: AchievementsTouch(x, y); break;
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
  g_tap_on_overlay = Updater_Prompt() != UPD_PROMPT_NONE || g_modal != MODAL_NONE;
  SavedOptions before = CurrentOptions();
  TouchDownImpl(x, y);
  SavedOptions after = CurrentOptions();
  if (memcmp(&before, &after, sizeof(before)) != 0) SaveConfig();
  g_dirty = 2;
}

void BottomUi_TouchMove(int x, int y) {
  if (g_tab == TAB_ACHIEVEMENTS && g_modal == MODAL_NONE && AchievementsTouchMove(y)) g_dirty = 2;
  if (g_tab == TAB_STATES && g_modal == MODAL_NONE && StatesTouchMove(y)) g_dirty = 2;
  if (g_tab == TAB_MAP && g_modal == MODAL_NONE && MapTouchMove(x, y)) g_dirty = 2;
  if (g_modal == MODAL_NOTES && NotesTouchMove(y)) g_dirty = 2;
}

void BottomUi_TouchUp(void) {
  if (g_tab == TAB_ACHIEVEMENTS && g_modal == MODAL_NONE) AchievementsTouchUp();
  if (g_tab == TAB_STATES && g_modal == MODAL_NONE) StatesTouchUp();
  if (g_tab == TAB_MAP && g_modal == MODAL_NONE) MapTouchUp();
  g_map_touch.active = false;
  g_ra_touch.active = false;
  g_notes_touch.active = false;
  g_dirty = 2;
}

int BottomUi_HudHidden(void) {
  if (!g_ui.hud_auto_hide) return 0;
  return g_tab == TAB_STATUS ? 1 : g_tab == TAB_MAP ? 2 : 0;
}

void BottomUi_Busy(void) {
  // The buffer still holds the last frame of the UI: put the box over it and show it now.
  Surface s = UiDraw_Screen(GFX_BOTTOM);
  const Rect r = { 70, 98, 180, 40 };
  UiFillRect(s, r.x, r.y, r.w, r.h, COL_MODAL_EDGE);
  UiFillRect(s, r.x + 1, r.y + 1, r.w - 2, r.h - 2, COL_MODAL);
  UiDrawTextCentered(s, r.x + r.w / 2, r.y + 16, COL_TITLE, Tr(kStrWait));
  UiDraw_Present(GFX_BOTTOM, false);
  gfxFlushBuffers();
  gfxScreenSwapBuffers(GFX_BOTTOM, false);
  UiDraw_Swapped();
  g_dirty = 2;   // the next frame draws the tab again
}

static void DrawBottom(const UiPerf *p) {
  Surface s = UiDraw_Screen(GFX_BOTTOM);
  UiFillRect(s, 0, 0, SCREEN_W, SCREEN_H, COL_BG);
  DrawTabBar(s);
  switch (g_tab) {
  case TAB_MAP:     DrawMap(s, p); break;
  case TAB_STATUS:  DrawStatus(s); break;
  case TAB_STATES:  DrawStates(s); break;
  case TAB_OPTIONS: DrawOptions(s); break;
  case TAB_ACHIEVEMENTS: DrawAchievements(s); break;
#if DEBUG_TOOLS
  case TAB_DEBUG:   DrawDebug(s, p); break;
#endif
  default: break;
  }
  g_drawing_overlay = true;
  switch (g_modal) {
  case MODAL_RESET: DrawResetModal(s); break;
  case MODAL_RA_DETAIL: DrawRaDetail(s); break;
  case MODAL_STATE: DrawStateDetail(s); break;
  case MODAL_NOTES: DrawNotes(s); break;
  case MODAL_ITEMS: DrawItemsModal(s); break;
  case MODAL_CONTROLS: DrawControlsModal(s); break;
#if DEBUG_TOOLS
  case MODAL_TOOLS: DrawToolsModal(s); break;
  case MODAL_REPORT: DrawReportModal(s); break;
#endif
  default: break;
  }
  DrawUpdatePrompt(s);
  g_drawing_overlay = false;
  if (g_toast[0]) {
    // On the map the bottom rows hold the warp buttons, so use the info line there.
    const int y = g_tab == TAB_MAP && g_modal == MODAL_NONE ? 201 : SCREEN_H - 12;
    const int w = g_tab == TAB_MAP && g_modal == MODAL_NONE ? 222 : SCREEN_W;
    UiFillRect(s, 0, y - 2, w, 12, COL_BG);
    UiDrawText(s, g_tab == TAB_MAP ? 4 : 8, y, 1, COL_WARN, g_toast);
  }
  DrawUnlockNotice(s);
}

// Does the tab or window on screen change by itself (game values, markers, live numbers, a
// timeout)? Those are redrawn every REFRESH_FRAMES; the others only when an event marks them.
static bool UiIsLive(void) {
  switch (g_modal) {
  case MODAL_NONE: case MODAL_RESET: case MODAL_RA_DETAIL: case MODAL_NOTES: case MODAL_ITEMS: case MODAL_CONTROLS: break;
#if DEBUG_TOOLS
  // The tools window covers the tab: only the scene recorder's frame count moves by itself (the rest changes on a tap).
  case MODAL_TOOLS: return SceneRec_Active();
  case MODAL_REPORT: return false;   // the game is paused under it
#endif
  default: return true;   // a state's detail (its two-tap confirm times out)
  }
  if (RetroAch_Toast()) return true;
  switch (g_tab) {
  case TAB_STATUS: case TAB_MAP: return true;
#if DEBUG_TOOLS
  case TAB_DEBUG: return true;
#endif
  case TAB_STATES: return g_arm_state >= 0;
  default: return false;
  }
}

// The clock, battery and Wi-Fi bars are drawn on every tab; true when what they show changed.
static bool ChromeChanged(void) {
  static long minute = -1;
  static int wifi = -1, battery = -1, charging = -1;
  const long m = (long)(time(NULL) / 60);
  const int w = osGetWifiStrength();
  if (m == minute && w == wifi && g_battery == battery && g_charging == charging) return false;
  if (minute >= 0 && m == minute)   // not just the clock: say which, in the debug log (it showed every 2 s once)
    Debug_Log("bottom UI: redraw, wifi %d -> %d, battery %d -> %d, charging %d -> %d", wifi, w, battery, g_battery, charging, g_charging);
  minute = m, wifi = w, battery = g_battery, charging = g_charging;
  return true;
}

// The STATUS and MAP tabs show game values that change by themselves (P2.7). With nothing over them they are redrawn
// whole only when one of those values changes (LiveKey), and their clock or blinking mark rewrite only their own
// rectangle (LiveUpdate); otherwise (a window, a notice, the update prompt) every REFRESH_FRAMES as before.
static bool LiveOk(void) {
  return (g_tab == TAB_STATUS || g_tab == TAB_MAP) && g_modal == MODAL_NONE && !RetroAch_Toast() &&
         Updater_Prompt() == UPD_PROMPT_NONE;
}

static uint32_t LiveKey(void) {
  uint32_t h = 2166136261u;
#define MIX(v) (h = (h ^ (uint32_t)(v)) * 16777619u)
  MIX(g_tab);
  if (g_tab == TAB_STATUS) {
    MIX(samus_health), MIX(samus_max_health), MIX(samus_reserve_health), MIX(samus_max_reserve_health), MIX(reserve_health_mode);
    MIX(samus_missiles), MIX(samus_max_missiles), MIX(samus_super_missiles), MIX(samus_max_super_missiles);
    MIX(samus_power_bombs), MIX(samus_max_power_bombs), MIX(hud_item_index), MIX(g_rtl_hud_marked), MIX(ModernControls_On());
    MIX(collected_items), MIX(equipped_items), MIX(collected_beams), MIX(equipped_beams);
    for (int i = 0; i < 6; i++) MIX(SmMap_HasMapStation(i)), MIX(SmMap_DebugState(i));
    MIX(area_index), MIX(room_index);
#if DEBUG_TOOLS
    MIX(g_cheats.invincible), MIX(g_cheats.max_mode);
#endif
  } else if (g_tab == TAB_MAP) {
    const int area = ShownMapArea();
    int sa = -1, sc = 0, sr = 0;
    MIX(area), MIX(SmMap_SamusCell(&sa, &sc, &sr)), MIX(sa), MIX(sc), MIX(sr);
    MIX((uintptr_t)SmMap_CurrentRoom()), MIX(SmMap_AreaKey(area));
  }
#undef MIX
  return h;
}

static void PartAdd(Rect r) {
  if (g_part) {
    const int x1 = g_part_rect.x + g_part_rect.w > r.x + r.w ? g_part_rect.x + g_part_rect.w : r.x + r.w;
    const int y1 = g_part_rect.y + g_part_rect.h > r.y + r.h ? g_part_rect.y + g_part_rect.h : r.y + r.h;
    if (r.x > g_part_rect.x) r.x = g_part_rect.x;
    if (r.y > g_part_rect.y) r.y = g_part_rect.y;
    r.w = x1 - r.x, r.h = y1 - r.y;
  }
  g_part_rect = r;
  g_part = 2;
}

// The low-energy blink and its start or end: the tab buttons alone. The unlock notice sits over them: they wait for it.
static void TabBlinkUpdate(void) {
  const int warn = TabWarn();
  if (warn == g_tab_warn_drawn && (!warn || TabBright() == g_tab_bright_drawn)) return;
  if (RetroAch_Toast()) return;
  PartAdd(DrawTabButtons(UiDraw_Screen(GFX_BOTTOM), NULL));
}

static void LiveUpdate(const UiPerf *p) {
  Surface s = UiDraw_Screen(GFX_BOTTOM);
  if (g_tab == TAB_STATUS && GameTimeKey() != g_time_drawn) {
    DrawStatusTime(s);
    PartAdd(kStatusTimeRect);
  } else if (g_tab == TAB_MAP && g_mark.valid && (bool)((p->frames / 15) & 1) != g_mark.on) {
    MarkSet(s, !g_mark.on);
    PartAdd(g_mark.r);
  }
}

bool BottomUi_PresentRect(Rect *r) {
  if (!g_present_part) return false;
  *r = g_present_rect;
  return true;
}

bool BottomUi_Frame(const UiPerf *p) {
  const u64 now = osGetTime();
  {   // Options changed away from the touch screen (SELECT's missile kind with the modern controls, the
      // game's CONTROLS row): saved once the game is out of gameplay, so the SD card is not written mid-run.
    const SavedOptions o = CurrentOptions();
    if (game_state != 0x08 && memcmp(&o, &g_saved_options, sizeof(o)) != 0) SaveConfig();
  }
  g_ui_frames = p->frames;
  const char *why = NULL;   // what asked for a redraw this frame, for the debug log
  if (g_toast[0] && now > g_toast_until) {
    g_toast[0] = 0;
    g_dirty = 2;
    why = "toast ended";
  }
  static uint32_t ra_seen;
  if (RetroAch_Version() != ra_seen) {
    ra_seen = RetroAch_Version();
    g_dirty = 2;
    why = "RetroAchievements changed";
  }
  static uint32_t updater_seen;
  if (Updater_Version() != updater_seen) {
    updater_seen = Updater_Version();
    g_dirty = 2;
    why = "updater changed";
  }
  if (g_tap_flash_pending && now - g_tap_ms >= TAP_FLASH_MS) {
    g_tap_flash_pending = false;
    g_dirty = 2;
    why = "tap flash ended";
  }
  // The bottom screen costs ~15 ms to redraw and present on an Old 3DS, so it is redrawn only
  // when something it shows has changed: on a tab or window whose content moves by itself every
  // REFRESH_FRAMES, elsewhere when the clock, battery or Wi-Fi bars change.
  if (UiIsLive()) {
    if (LiveOk()) {
      const uint32_t key = LiveKey();
      if (key != g_live_key) g_live_key = key, g_dirty = 2, why = "tab values changed";
    } else if (p->frames - g_last_redraw >= REFRESH_FRAMES) {
      g_last_redraw = p->frames;
      g_dirty = 2;
    }
  }
  if (ChromeChanged()) g_dirty = 2, why = "clock, wifi or battery changed";
  const bool notice = RetroAch_Toast() != NULL;   // the unlock notice times out on its own
  static bool notice_seen;
#if DEBUG_TOOLS
  static bool perf_seen;   // the perf recorder stops by itself when its buffer is full: the tools window shows it
  if (Debug_PerfRecording() != perf_seen) {
    perf_seen = !perf_seen;
    g_dirty = 2;
  }
#endif
  if (notice != notice_seen) {
    notice_seen = notice;
    g_dirty = 2;
    why = "unlock notice";
  }
  // The battery moves far slower than anything else here, and the two PTM calls cost 2-3 ms on an
  // Old 3DS (the perf CSV: ui_ms 2-3 with no present, every 120 frames, enough to overrun the frame):
  // every 20 s.
  if (g_ptmu && p->frames % 1200 == 0) {
    PTMU_GetBatteryLevel(&g_battery);
    PTMU_GetBatteryChargeState(&g_charging);
    if (g_battery > 5) g_battery = 5;
  }
  if (why && !UiIsLive() && g_dirty == 2) Debug_Log("bottom UI: redraw, %s", why);   // not the live tabs: they redraw every 15 frames
  g_present_part = false;
  // A change needs two frames (the screen is double buffered): the first draws it, the second
  // only presents the same picture to the other buffer.
  if (g_dirty == 2) DrawBottom(p), g_live_key = LiveOk() ? LiveKey() : 0;
  if (LiveOk()) LiveUpdate(p);
  TabBlinkUpdate();
  if (g_dirty > 0) {
    g_dirty--;
    if (g_part) g_part--;   // a full present carries the part too
    return true;
  }
  if (g_part > 0) {
    g_part--;
    g_present_part = true;
    g_present_rect = g_part_rect;
    return true;
  }
  return false;
}

// The overlay's box: 2 px around the text, as wide as its longest line (6 px a character,
// the last one 5), five lines 10 px apart. `box` is its colour: opaque black on the CPU
// path (it sits in the black margin), see-through black on the GPU path (it may sit over
// the game with WIDE).
enum { kOverlayMaxW = 60, kOverlayH = 51 };

static void DrawTopToastCpu(void);

// The corner of the surface the overlay sits in: 1 top-left, 2 top-right, 3 bottom-left, 4 bottom-right.
static int OverlayX(Surface s, int corner, int w) { return corner == 2 || corner == 4 ? s.w - w : 0; }
static int OverlayY(Surface s, int corner, int h) { return corner >= 3 ? s.h - h : 0; }

static void DrawOverlay(Surface s, int corner, const UiPerf *p, uint32_t box) {
  char line[5][12];
  snprintf(line[0], sizeof(line[0]), "%.1f", p->game_fps);
  snprintf(line[1], sizeof(line[1]), "L%.1f", p->logic_ms);
  snprintf(line[2], sizeof(line[2]), "D%.1f", p->draw_ms);
  snprintf(line[3], sizeof(line[3]), "A%.1f", p->audio_ms);
  snprintf(line[4], sizeof(line[4]), "S%.1f", p->fps);
  int chars = 0;
  for (int i = 0; i < 5; i++) {
    const int n = (int)strlen(line[i]);
    if (n > chars) chars = n;
  }
  int w = chars * 6 + 3;
  if (w > kOverlayMaxW) w = kOverlayMaxW;
  const int x0 = OverlayX(s, corner, w), y0 = OverlayY(s, corner, kOverlayH);
  UiFillRect(s, x0, y0, w, kOverlayH, box);
  const uint32_t col[5] = { FpsColor(p->game_fps), COL_TEXT, COL_TEXT, COL_TEXT, COL_DIM };
  for (int i = 0; i < 5; i++) UiDrawText(s, x0 + 2, y0 + 2 + i * 10, 1, col[i], line[i]);
}

void BottomUi_DrawTopOverlay(const UiPerf *p) {
  DrawTopToastCpu();
  // The game is centred (274 or 256 px wide on a 400 px screen); the overlay sits in a margin,
  // which the game never redraws, at the corner chosen in OPTIONS. Where it was is cleared for
  // two frames after a change: the top screen is double buffered.
  static int clear_frames, drawn_corner, clear_corner;
  Surface s = UiDraw_Screen(GFX_TOP);
  const int corner = g_ui.fps_overlay;
  if (corner != drawn_corner) {
    if (drawn_corner) clear_corner = drawn_corner, clear_frames = 2;
    drawn_corner = corner;
  }
  if (clear_frames > 0) {
    clear_frames--;
    UiFillRect(s, OverlayX(s, clear_corner, kOverlayMaxW), OverlayY(s, clear_corner, kOverlayH), kOverlayMaxW, kOverlayH,
               RGB(0, 0, 0));
  }
  if (!corner) return;
  // The box shrinks with the numbers: clear what a wider one left in this buffer.
  UiFillRect(s, OverlayX(s, corner, kOverlayMaxW), OverlayY(s, corner, kOverlayH), kOverlayMaxW, kOverlayH, RGB(0, 0, 0));
  DrawOverlay(s, corner, p, RGB(0, 0, 0));
}

// The achievement notice on the top screen, CPU path: drawn over the frame; once gone, the
// margins it covered are cleared for both buffers (the game redraws its own columns).
static void DrawTopToastCpu(void) {
  static int clear_frames;
  Surface s = UiDraw_Screen(GFX_TOP);
  const RaAchievement *a = RetroAch_Toast();
  if (a && RetroAch_NotifyTop()) {
    DrawNoticeBox(s, 50, 4, a);
    clear_frames = 2;
  } else if (clear_frames > 0) {
    clear_frames--;
    const int game_x0 = g_ui.pixel_perfect ? 72 : 63;
    UiFillRect(s, 50, 4, game_x0 - 50, 36, RGB(0, 0, 0));
    UiFillRect(s, 400 - game_x0, 4, game_x0 - 50, 36, RGB(0, 0, 0));
  }
}

bool BottomUi_DrawTopToastInto(uint32_t *px) {
  const RaAchievement *a = RetroAch_Toast();
  if (!a || !RetroAch_NotifyTop()) return false;
  Surface s = { px, 512, 64 };
  DrawNoticeBox(s, 0, 0, a);
  return true;
}

bool BottomUi_DrawOverlayInto(uint32_t *px, int w, int h, const UiPerf *p) {
  if (!g_ui.fps_overlay) return false;
  Surface s = { px, w, h };
  UiFillRect(s, 0, 0, w, h, 0);   // transparent around the box
  DrawOverlay(s, g_ui.fps_overlay, p, 0x00000090u);  // black, alpha 0x90 (RGBA8: alpha is the low byte)
  return true;
}

// The game's OPTION MODE picked a language (game_text_screens.c): saved as if tapped here.
static void OnGameLanguage(void) {
  SaveConfig();
  g_dirty = 2;
}

// The game's CONTROLLER SETTING MODE -> CLASSIC / MODERN row (sm_rtl.h, RtlControlsMenu); saved by BottomUi_Frame.
static bool ControlsMenuModern(void) { return ModernControls_On(); }
static void ControlsMenuSetModern(bool on) {
  SetControls(on);
  g_dirty = 2;
}
static const RtlControlsMenu kControlsMenu = { ControlsMenuModern, ControlsMenuSetModern };

// ---- Init -------------------------------------------------------------------

bool BottomUi_Init(const UiRomInfo *rom) {
  g_rom_info = *rom;
  SmMap_Init();
  SmWarp_Init();
  APT_CheckNew3DS(&g_is_new3ds);
  g_ui.new3ds_speedup = g_is_new3ds;
  UiLang_Set(UiLang_FromSystem());   // until config.ini says otherwise
  LoadConfig();
  g_saved_options = CurrentOptions();
  g_ui_lang_on_change = OnGameLanguage;
  g_rtl_controls_menu = &kControlsMenu;
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
  const SavedOptions o = CurrentOptions();
  if (memcmp(&o, &g_saved_options, sizeof(o)) != 0) SaveConfig();
  if (g_ptmu) ptmuExit();
  osSetSpeedupEnable(false);
}
