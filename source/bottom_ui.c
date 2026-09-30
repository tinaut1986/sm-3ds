#include "bottom_ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>

#include "src/types.h"
#include "src/sm_rtl.h"
#include "src/variables.h"
#include "debug_tools.h"

#define SCREEN_W 320
#define SCREEN_H 240
#define REFRESH_FRAMES 15   // periodic redraw so live numbers keep moving

UiOptions g_ui = {
  .audio_on = true,
  .render_on = true,
  .frameskip = true,
  .new3ds_speedup = true,
};

typedef enum { TAB_STATUS, TAB_OPTIONS, TAB_DEBUG, TAB_COUNT } Tab;
static const char *const kTabNames[TAB_COUNT] = { "STATUS", "OPTIONS", "DEBUG" };

static UiRomInfo g_rom_info;
static uint8_t g_font[128 * 128];   // 1 = ink, indexed [y * 128 + x], 16x16 cells of 8x8
static Tab g_tab = TAB_STATUS;
static int g_dirty = 2;             // frames left to redraw (bottom is double buffered)
static uint32_t g_last_redraw;
static bool g_is_new3ds;
static char g_toast[40];
static u64 g_toast_until;

// ---- Colors: memory order in the RGBA8 framebuffer is A,B,G,R -------------

#define RGB(r, g, b) (((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | 0xFFu)
#define COL_BG      RGB(14, 16, 28)
#define COL_TAB     RGB(34, 38, 64)
#define COL_TAB_ON  RGB(70, 96, 190)
#define COL_BTN     RGB(44, 50, 82)
#define COL_ON      RGB(30, 140, 84)
#define COL_OFF     RGB(120, 52, 52)
#define COL_TEXT    RGB(230, 232, 240)
#define COL_DIM     RGB(140, 148, 170)
#define COL_GOOD    RGB(110, 220, 130)
#define COL_WARN    RGB(240, 200, 80)
#define COL_BAD     RGB(240, 100, 100)

// ---- Drawing --------------------------------------------------------------

typedef struct { uint32_t *px; int w, h; } Surface;   // column-major, h px per column

static Surface SurfaceOf(gfxScreen_t screen) {
  u16 w, h;   // libctru reports the rotated size: w = 240, h = width in pixels
  u8 *fb = gfxGetFramebuffer(screen, GFX_LEFT, &w, &h);
  return (Surface){ (uint32_t *)fb, h, w };
}

static void FillRect(Surface s, int x, int y, int w, int h, uint32_t c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > s.w) w = s.w - x;
  if (y + h > s.h) h = s.h - y;
  for (int xx = x; xx < x + w; xx++) {
    uint32_t *col = s.px + xx * s.h + (s.h - 1 - y);
    for (int yy = 0; yy < h; yy++) col[-yy] = c;
  }
}

static int TextWidth(const char *str, int scale) { return (int)strlen(str) * 8 * scale; }

static void DrawText(Surface s, int x, int y, int scale, uint32_t c, const char *str) {
  for (; *str; str++, x += 8 * scale) {
    unsigned ch = (unsigned char)*str;
    if (ch >= 128 || ch == ' ') continue;
    int gx = (ch % 16) * 8, gy = (ch / 16) * 8;
    if (scale == 1) {
      // Common case: write pixels directly, no per-pixel rectangle clipping.
      if (x < 0 || x + 8 > s.w || y < 0 || y + 8 > s.h) continue;
      for (int xx = 0; xx < 8; xx++) {
        uint32_t *col = s.px + (x + xx) * s.h + (s.h - 1 - y);
        for (int yy = 0; yy < 8; yy++)
          if (g_font[(gy + yy) * 128 + gx + xx]) col[-yy] = c;
      }
      continue;
    }
    for (int yy = 0; yy < 8; yy++)
      for (int xx = 0; xx < 8; xx++)
        if (g_font[(gy + yy) * 128 + gx + xx]) FillRect(s, x + xx * scale, y + yy * scale, scale, scale, c);
  }
}

static void DrawTextf(Surface s, int x, int y, uint32_t c, const char *fmt, ...) __attribute__((format(printf, 5, 6)));
static void DrawTextf(Surface s, int x, int y, uint32_t c, const char *fmt, ...) {
  char buf[64];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  DrawText(s, x, y, 1, c, buf);
}

static void DrawButton(Surface s, int x, int y, int w, int h, uint32_t bg, const char *label) {
  FillRect(s, x, y, w, h, bg);
  DrawText(s, x + (w - TextWidth(label, 1)) / 2, y + (h - 8) / 2, 1, COL_TEXT, label);
}

// ---- Layout (shared by drawing and hit testing) ----------------------------

#define TAB_H 22
#define ROW_Y0 28
#define ROW_H 22
#define ROW_PAD 4

typedef struct { int x, y, w, h; } Rect;

static Rect TabRect(int i) { return (Rect){ i * (SCREEN_W / TAB_COUNT), 0, SCREEN_W / TAB_COUNT - 2, TAB_H }; }
static Rect RowRect(int row) { return (Rect){ 8, ROW_Y0 + row * ROW_H, SCREEN_W - 16, ROW_H - ROW_PAD }; }
static Rect HalfRect(int row, int half) {
  Rect r = RowRect(row);
  r.w = (r.w - 6) / 2;
  if (half) r.x += r.w + 6;
  return r;
}
static bool In(Rect r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

typedef enum {
  OPT_PAUSE, OPT_TURBO, OPT_FRAMESKIP, OPT_AUDIO, OPT_FPS, OPT_SPEEDUP, OPT_SLOT, OPT_STATE, OPT_RESET, OPT_ROWS
} OptRow;

void BottomUi_Toast(const char *msg) {
  snprintf(g_toast, sizeof(g_toast), "%s", msg);
  g_toast_until = osGetTime() + 1500;
  g_dirty = 2;
}
#define Toast BottomUi_Toast

static void OptionsTouch(int x, int y) {
  if (In(RowRect(OPT_PAUSE), x, y)) g_ui.paused = !g_ui.paused;
  else if (In(RowRect(OPT_TURBO), x, y)) g_ui.turbo = !g_ui.turbo;
  else if (In(RowRect(OPT_FRAMESKIP), x, y)) g_ui.frameskip = !g_ui.frameskip;
  else if (In(RowRect(OPT_AUDIO), x, y)) g_ui.audio_on = !g_ui.audio_on;
  else if (In(RowRect(OPT_FPS), x, y)) g_ui.fps_overlay = !g_ui.fps_overlay;
  else if (In(RowRect(OPT_SPEEDUP), x, y)) {
    if (!g_is_new3ds) return;
    g_ui.new3ds_speedup = !g_ui.new3ds_speedup;
    osSetSpeedupEnable(g_ui.new3ds_speedup);
  }
  else if (In(HalfRect(OPT_SLOT, 0), x, y)) g_ui.save_slot = (g_ui.save_slot + 9) % 10;
  else if (In(HalfRect(OPT_SLOT, 1), x, y)) g_ui.save_slot = (g_ui.save_slot + 1) % 10;
  else if (In(HalfRect(OPT_STATE, 0), x, y)) { g_ui.req_save_state = true; Toast("State saved"); }
  else if (In(HalfRect(OPT_STATE, 1), x, y)) { g_ui.req_load_state = true; Toast("State loaded"); }
  else if (In(RowRect(OPT_RESET), x, y)) { g_ui.req_reset = true; Toast("Game reset"); }
  else return;
  g_dirty = 2;
}

enum { DBG_PPU, DBG_AUDIO, DBG_LOG, DBG_ACTIONS, DBG_PERF };

static void DebugTouch(int x, int y) {
  if (In(RowRect(DBG_PPU), x, y)) g_ui.render_on = !g_ui.render_on;
  else if (In(RowRect(DBG_AUDIO), x, y)) g_ui.audio_on = !g_ui.audio_on;
  else if (In(RowRect(DBG_LOG), x, y)) { Debug_LogSetEnabled(!Debug_LogEnabled()); Toast(Debug_LastMessage()); }
  else if (In(HalfRect(DBG_ACTIONS, 0), x, y)) { Debug_LogMark(); Toast(Debug_LogEnabled() ? Debug_LastMessage() : "Turn the log on first"); }
  else if (In(HalfRect(DBG_ACTIONS, 1), x, y)) g_ui.req_dump = true;
  else if (In(RowRect(DBG_PERF), x, y)) { Debug_PerfToggle(); Toast(Debug_LastMessage()); }
  else return;
  g_dirty = 2;
}

// ---- Persistent options --------------------------------------------------
// Saved to config.ini in the data folder whenever one changes. Not persisted on
// purpose: pause, turbo, PPU render off and the log/perf recorders, which would
// be confusing or harmful to find switched on at the next boot.

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

static void TouchDownImpl(int x, int y);

void BottomUi_TouchDown(int x, int y) {
  SavedOptions before = CurrentOptions();
  TouchDownImpl(x, y);
  SavedOptions after = CurrentOptions();
  if (memcmp(&before, &after, sizeof(before)) != 0) SaveConfig();
}

static void TouchDownImpl(int x, int y) {
  for (int i = 0; i < TAB_COUNT; i++) {
    if (In(TabRect(i), x, y)) {
      g_tab = (Tab)i;
      g_dirty = 2;
      return;
    }
  }
  if (g_tab == TAB_OPTIONS) OptionsTouch(x, y);
  else if (g_tab == TAB_DEBUG) DebugTouch(x, y);
}

void BottomUi_TouchMove(int x, int y) { (void)x; (void)y; }
void BottomUi_TouchUp(void) {}

// ---- Tabs -----------------------------------------------------------------

static const char *OnOff(bool b) { return b ? "ON" : "OFF"; }

static void DrawToggle(Surface s, Rect r, const char *name, bool on) {
  char buf[48];
  snprintf(buf, sizeof(buf), "%s: %s", name, OnOff(on));
  DrawButton(s, r.x, r.y, r.w, r.h, on ? COL_ON : COL_OFF, buf);
}

static uint32_t FpsColor(float fps) { return fps >= 58.0f ? COL_GOOD : fps >= 45.0f ? COL_WARN : COL_BAD; }

static void DrawStatus(Surface s, const UiPerf *p) {
  int y = 30;
  DrawText(s, 8, y, 1, COL_TEXT, "Super Metroid 3DS"); DrawText(s, 8 + 18 * 8, y, 1, COL_DIM, g_rom_info.version); y += 14;
  DrawTextf(s, 8, y, COL_DIM, "ROM: %.32s", g_rom_info.rom_name); y += 12;
  DrawTextf(s, 8, y, COL_DIM, "sha1 %.12s.. OK%s", g_rom_info.rom_sha1, g_rom_info.rom_had_header ? " (hdr cut)" : ""); y += 18;

  DrawTextf(s, 8, y, FpsColor(p->game_fps), "speed %.1f", p->game_fps);
  DrawTextf(s, 104, y, FpsColor(p->fps), "shown %.1f", p->fps);
  y += 12;
  DrawTextf(s, 8, y, COL_DIM, "%s", p->is_new3ds ? (g_ui.new3ds_speedup ? "New 3DS 804MHz" : "New 3DS 268MHz") : "Old 3DS 268MHz");
  y += 14;
  DrawTextf(s, 8, y, COL_TEXT, "work  %5.1f ms", p->frame_ms); y += 12;
  DrawTextf(s, 8, y, COL_TEXT, " logic+PPU %5.1f ms", p->logic_ms); y += 12;
  DrawTextf(s, 8, y, COL_TEXT, " top draw  %5.1f ms", p->draw_ms); y += 12;
  DrawTextf(s, 8, y, COL_TEXT, " audio blk %5.1f ms", p->audio_ms); y += 18;

  DrawTextf(s, 8, y, COL_DIM, "game state %02X  area %u  room %u", (unsigned)game_state, (unsigned)area_index, (unsigned)room_index); y += 12;
  DrawTextf(s, 8, y, COL_DIM, "HP %u/%u  M %u/%u  S %u  PB %u", (unsigned)samus_health, (unsigned)samus_max_health,
            (unsigned)samus_missiles, (unsigned)samus_max_missiles, (unsigned)samus_super_missiles, (unsigned)samus_power_bombs);
}

static void DrawOptions(Surface s) {
  DrawToggle(s, RowRect(OPT_PAUSE), "Pause", g_ui.paused);
  DrawToggle(s, RowRect(OPT_TURBO), "Turbo", g_ui.turbo);
  DrawToggle(s, RowRect(OPT_FRAMESKIP), "Frameskip", g_ui.frameskip);
  DrawToggle(s, RowRect(OPT_AUDIO), "Audio", g_ui.audio_on);
  DrawToggle(s, RowRect(OPT_FPS), "FPS overlay (top)", g_ui.fps_overlay);
  Rect r = RowRect(OPT_SPEEDUP);
  if (g_is_new3ds) DrawToggle(s, r, "New3DS 804MHz", g_ui.new3ds_speedup);
  else DrawButton(s, r.x, r.y, r.w, r.h, COL_BTN, "New3DS 804MHz: n/a");
  Rect a = HalfRect(OPT_SLOT, 0), b = HalfRect(OPT_SLOT, 1);
  char buf[24];
  snprintf(buf, sizeof(buf), "< slot %d", g_ui.save_slot);
  DrawButton(s, a.x, a.y, a.w, a.h, COL_BTN, buf);
  DrawButton(s, b.x, b.y, b.w, b.h, COL_BTN, "slot >");
  a = HalfRect(OPT_STATE, 0); b = HalfRect(OPT_STATE, 1);
  DrawButton(s, a.x, a.y, a.w, a.h, COL_BTN, "SAVE STATE");
  DrawButton(s, b.x, b.y, b.w, b.h, COL_BTN, "LOAD STATE");
  r = RowRect(OPT_RESET);
  DrawButton(s, r.x, r.y, r.w, r.h, COL_BTN, "RESET GAME");
}

static void DrawDebug(Surface s, const UiPerf *p) {
  DrawToggle(s, RowRect(DBG_PPU), "PPU render", g_ui.render_on);
  DrawToggle(s, RowRect(DBG_AUDIO), "Audio", g_ui.audio_on);
  char buf[48];
  snprintf(buf, sizeof(buf), "Log to SD: %s", Debug_LogEnabled() ? Debug_LogName() : "OFF");
  Rect r = RowRect(DBG_LOG);
  DrawButton(s, r.x, r.y, r.w, r.h, Debug_LogEnabled() ? COL_ON : COL_OFF, buf);
  Rect a = HalfRect(DBG_ACTIONS, 0), b = HalfRect(DBG_ACTIONS, 1);
  DrawButton(s, a.x, a.y, a.w, a.h, COL_BTN, "LOG MARK");
  DrawButton(s, b.x, b.y, b.w, b.h, COL_BTN, "DUMP SCREEN");
  r = RowRect(DBG_PERF);
  DrawButton(s, r.x, r.y, r.w, r.h, Debug_PerfRecording() ? COL_ON : COL_BTN,
             Debug_PerfRecording() ? "PERF: RECORDING (tap to stop)" : "PERF: RECORD FRAME TIMES");
  int y = ROW_Y0 + 5 * ROW_H + 6;
  DrawTextf(s, 8, y, COL_WARN, "%s", Debug_LastMessage()); y += 14;
  DrawTextf(s, 8, y, COL_TEXT, "audio lock %.1f spc %.1f dsp %.1f rs %.1f", p->audio_part_ms[0], p->audio_part_ms[1], p->audio_part_ms[2], p->audio_part_ms[3]); y += 12;
  DrawTextf(s, 8, y, COL_TEXT, "frames %lu   linear free %u KB", (unsigned long)p->frames, (unsigned)(linearSpaceFree() / 1024)); y += 12;
  DrawTextf(s, 8, y, COL_TEXT, "samus x %u y %u   frame ctr %u", (unsigned)samus_x_pos, (unsigned)samus_y_pos, (unsigned)frame_counter_every_frame); y += 12;
  DrawText(s, 8, y, 1, COL_DIM, "files in debug/ on the SD card");
}

static void DrawBottom(const UiPerf *p) {
  Surface s = SurfaceOf(GFX_BOTTOM);
  FillRect(s, 0, 0, SCREEN_W, SCREEN_H, COL_BG);
  for (int i = 0; i < TAB_COUNT; i++) {
    Rect r = TabRect(i);
    DrawButton(s, r.x, r.y, r.w, r.h, i == (int)g_tab ? COL_TAB_ON : COL_TAB, kTabNames[i]);
  }
  switch (g_tab) {
  case TAB_STATUS:  DrawStatus(s, p); break;
  case TAB_OPTIONS: DrawOptions(s); break;
  case TAB_DEBUG:   DrawDebug(s, p); break;
  default: break;
  }
  if (g_toast[0]) DrawText(s, 8, SCREEN_H - 12, 1, COL_WARN, g_toast);
  // Visible from every tab, over the right end of the tab bar.
  if (Debug_PerfRecording()) DrawText(s, SCREEN_W - 32, 7, 1, COL_BAD, "REC");
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
  Surface s = SurfaceOf(GFX_TOP);
  if (!g_ui.fps_overlay) {
    if (clear_frames > 0) {
      clear_frames--;
      FillRect(s, 0, 0, 60, 52, RGB(0, 0, 0));
    }
    return;
  }
  clear_frames = 2;
  char buf[16];
  FillRect(s, 0, 0, 60, 52, RGB(0, 0, 0));
  snprintf(buf, sizeof(buf), "%.1f", p->game_fps);
  DrawText(s, 2, 2, 1, FpsColor(p->game_fps), buf);
  snprintf(buf, sizeof(buf), "S%.1f", p->fps);
  DrawText(s, 2, 42, 1, COL_DIM, buf);
  snprintf(buf, sizeof(buf), "L%.1f", p->logic_ms);
  DrawText(s, 2, 12, 1, COL_TEXT, buf);
  snprintf(buf, sizeof(buf), "D%.1f", p->draw_ms);
  DrawText(s, 2, 22, 1, COL_TEXT, buf);
  snprintf(buf, sizeof(buf), "A%.1f", p->audio_ms);
  DrawText(s, 2, 32, 1, COL_TEXT, buf);
}

// ---- Init -----------------------------------------------------------------

static bool LoadFont(void) {
  FILE *f = fopen("romfs:/font.bmp", "rb");
  if (!f) return false;
  uint8_t hdr[138];
  bool ok = fread(hdr, 1, sizeof(hdr), f) == sizeof(hdr) && hdr[0] == 'B' && hdr[1] == 'M';
  int32_t w = 0, h = 0;
  uint16_t bpp = 0;
  uint32_t off = 0;
  if (ok) {
    memcpy(&off, hdr + 10, 4); memcpy(&w, hdr + 18, 4); memcpy(&h, hdr + 22, 4); memcpy(&bpp, hdr + 28, 2);
    ok = w == 128 && h == 128 && bpp == 32 && off >= sizeof(hdr);
  }
  if (ok) {
    uint8_t *raw = (uint8_t *)malloc(128 * 128 * 4);
    fseek(f, (long)off, SEEK_SET);
    ok = raw && fread(raw, 1, 128 * 128 * 4, f) == 128 * 128 * 4;
    if (ok)
      for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++)   // BMP rows are bottom-up; ink is opaque white
          g_font[y * 128 + x] = raw[((127 - y) * 128 + x) * 4 + 3] != 0;
    free(raw);
  }
  fclose(f);
  return ok;
}

bool BottomUi_Init(const UiRomInfo *rom) {
  g_rom_info = *rom;
  APT_CheckNew3DS(&g_is_new3ds);
  g_ui.new3ds_speedup = g_is_new3ds;
  LoadConfig();
  if (!g_is_new3ds) g_ui.new3ds_speedup = false;
  if (g_is_new3ds) osSetSpeedupEnable(g_ui.new3ds_speedup);
  g_dirty = 2;
  return LoadFont();
}

void BottomUi_Exit(void) {
  osSetSpeedupEnable(false);
}
