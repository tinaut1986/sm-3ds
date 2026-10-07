#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "3ds.h"
#include "build_config.h"
#include "src/types.h"
#include "src/sm_rtl.h"
#include "src/variables.h"
#include "bottom_ui.h"
#include "ui_draw.h"
#include "updater.h"
#include "states_store.h"
#include <sys/stat.h>
#include "ui_lang.h"
#include "cheats.h"
#include "debug_tools.h"
#include "src/sm_cpu_infra.h"
#include "sm_map.h"
static uint8_t fb[2][400 * 240 * 4];
static u64 g_now = 100000;   // advanced between shots so tap flashes and toasts expire
void gfxFlushBuffers(void) {}
void gfxScreenSwapBuffers(gfxScreen_t screen, bool hasStereo) { (void)screen, (void)hasStereo; }
u8 *gfxGetFramebuffer(gfxScreen_t s, gfx3dSide_t side, u16 *w, u16 *h) { if (w) *w = 240; if (h) *h = s == GFX_TOP ? 400 : 320; return fb[s]; }
u64 osGetTime(void) { return g_now; }
u64 svcGetSystemTick(void) { return 0; }
void gspWaitForVBlank(void) {}
size_t linearSpaceFree(void) { return 20 * 1024 * 1024; }
int APT_CheckNew3DS(bool *o) { *o = true; return 0; }
void osSetSpeedupEnable(bool e) {}
Result ptmuInit(void) { return 0; }
void ptmuExit(void) {}
Result PTMU_GetBatteryLevel(u8 *out) { *out = 3; return 0; }
Result PTMU_GetBatteryChargeState(u8 *out) { *out = 1; return 0; }
u8 osGetWifiStrength(void) { return 2; }
void NORETURN Die(const char *e) { exit(1); }
void Warning(const char *e) {}

static void Dump(const char *name) {
  FILE *f = fopen(name, "wb");
  fprintf(f, "P6\n320 240\n255\n");
  for (int y = 0; y < 240; y++) for (int x = 0; x < 320; x++) {
    const uint8_t *p = &((const uint8_t *)UiDraw_Screen(GFX_BOTTOM).px)[(x * 240 + (239 - y)) * 4];   // A,B,G,R
    fputc(p[3], f); fputc(p[2], f); fputc(p[1], f);
  }
  fclose(f);
}

static UiPerf perf = { .fps = 60, .game_fps = 60, .frame_ms = 13.7f, .logic_ms = 9.8f, .draw_ms = 2.8f, .audio_ms = 4.7f,
                       .audio_part_ms = {0, 0.1f, 4.6f, 0.1f}, .frames = 1234, .is_new3ds = true };
static int g_shot;

// Lets tap flashes expire, draws both buffers and writes shotNN.ppm.
static void Shot(const char *what) {
  g_now += 1000;
  perf.frames += 16;
  BottomUi_Frame(&perf);
  BottomUi_Frame(&perf);
  char name[64];
  snprintf(name, sizeof(name), "shot%02d.ppm", g_shot++);
  Dump(name);
  printf("%s: %s\n", name, what);
}

static void Tap(int x, int y) {
  BottomUi_TouchDown(x, y);
  BottomUi_TouchUp();
}

static void Drag(int x0, int y0, int x1, int y1) {
  BottomUi_TouchDown(x0, y0);
  BottomUi_TouchMove(x1, y1);
  BottomUi_TouchUp();
}

// Tab x positions: 4 + slot * 34, slots in drawing order.
#if DEBUG_TOOLS
enum { kMap = 0, kStatus = 1, kDebug = 2, kStates = 3, kAchievements = 4, kOptions = 5 };
#else
enum { kMap = 0, kStatus = 1, kStates = 2, kAchievements = 3, kOptions = 4 };
#endif
static void TapTab(int slot) { Tap(4 + slot * 34 + 15, 12); }

int main(int argc, char **argv) {
  if (!SnesInit(argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
  UiRomInfo rom = { "Super Metroid (Japan, USA).sfc", "da957f0d63d14cb441d215462904c4fa8519c613", false,
                    "v0.1.1-dev.30.4+abcdef0" };
  // A plausible mid-game state: Landing Site, some Crateria explored.
  game_state = 8; area_index = 0; room_index = 0x1A; room_ptr = 0x91F8;
  samus_x_pos = 700; samus_y_pos = 300;
  samus_health = 247; samus_max_health = 499; samus_reserve_health = 60; samus_max_reserve_health = 100; reserve_health_mode = 1;
  samus_missiles = 45; samus_max_missiles = 75; samus_super_missiles = 5; samus_max_super_missiles = 10; samus_power_bombs = 0; samus_max_power_bombs = 10;
  collected_items = 0x0001 | 0x0004 | 0x1000 | 0x0100; equipped_items = 0x0001 | 0x0004 | 0x1000;
  collected_beams = 0x1000 | 0x0002; equipped_beams = 0x1000;
  game_time_hours = 3; game_time_minutes = 27; game_time_seconds = 9;
  hud_item_index = 2;   // the super missiles selected with SELECT
  boss_bits_for_area[1] = 3; boss_bits_for_area[0] = 1;
  map_station_byte_array[1] = 1;
  int n; const SmRoom *rooms = SmMap_Rooms(&n);
  for (int i = 0; i < n; i++) {
    if (rooms[i].area != 0 || rooms[i].x < 14) continue;
    for (int j = 0; j < rooms[i].h; j++) for (int k = 0; k < rooms[i].w; k++) {
      int col = rooms[i].x + k, row = rooms[i].y + 1 + j, idx = (col >= 32 ? 1024 : 0) + row * 32 + (col & 31);
      map_tiles_explored[idx >> 3] |= 0x80 >> (idx & 7);
    }
  }
  // Fourteen states with different marks, areas and times, a screenshot on every other one.
  mkdir("saves", 0777);
  static uint16_t shot[kStateShotW * kStateShotH];
  for (int i = 0; i < kStateShotH; i++)
    for (int j = 0; j < kStateShotW; j++) shot[i * kStateShotW + j] = (uint16_t)((j * 31 / kStateShotW) << 11 | (i * 63 / kStateShotH) << 5 | 12);
  for (int i = 0; i < 14; i++) {
    StateInfo si = { .id = i, .has_info = i != 5, .saved_at = 1759400000 + i * 7200 + (i % 3) * 86400, .area = (unsigned)(i % 6),
                     .room = 0x10 + i * 3, .health = 99 + i * 20, .max_health = 299, .reserve = i % 2 ? 50 : 0, .missiles = 5 + i,
                     .max_missiles = 45, .supers = i % 4, .max_supers = 10, .pbs = i % 3, .max_pbs = 10, .hours = (unsigned)i / 2,
                     .minutes = (unsigned)(i * 7) % 60, .mark = i % kStateMarks };
    snprintf(si.version, sizeof(si.version), "v0.2.3-dev.%d+abc1234", i);
    char name[32];
    snprintf(name, sizeof(name), "saves/save%d.sav", i);
    FILE *f = fopen(name, "wb");
    if (f) fclose(f);
    if (si.has_info) States_WriteInfo(&si);
    if (i % 2 == 0) States_WriteShot(i, shot);
  }
  BottomUi_Init(&rom);

  TapTab(kMap);
  Tap(24 * 5 + 2, 26 + 3 * 5 + 2);   // pick a map cell
  Shot("map, a room picked");
  Tap(21 * 5 + 2, 23 + 8 * 5 + 2);   // a room whose box overlaps its neighbours (92FD)
  Shot("map, a room that is not a rectangle picked");
  Tap(24 * 5 + 2, 23 + 3 * 5 + 2);   // back to the first one
  Tap(192 + 15, 198 + 6);            // zoom 2X
  Shot("map, zoom 2X");
  Drag(200, 100, 100, 80);           // scroll
  Shot("map, zoom 2X dragged");
  Tap(192 + 15, 198 + 6);            // zoom 3X
  Shot("map, zoom 3X");
  Tap(192 + 15, 198 + 6);            // back to 1X
  TapTab(kStatus);
  Shot("status");
#if DEBUG_TOOLS
  Tap(260, 55);                      // GOD
  Tap(290, 55);                      // MAX
  Tap(8 + 1 * 77 + 5, 111 + 5);      // GRAV on
  Tap(8 + 4 * 61 + 5, 165 + 5);      // PLASMA on
  Tap(8 + 0 * 51 + 5, 191 + 5);      // Crateria: station
  Tap(8 + 2 * 51 + 5, 191 + 5);      // Norfair: station
  Tap(8 + 2 * 51 + 5, 191 + 5);      // Norfair: explored
  Shot("status after debug taps (GOD, MAX, GRAV, PLASMA, CRA station, NOR explored)");
  TapTab(kMap);
  Tap(2 + 2 * 45 + 5, 183 + 5);      // show Norfair
  Shot("map, Norfair all explored");
  Tap(192 + 15, 198 + 6);            // zoom 2X, icons with letters
  Shot("map, Norfair zoom 2X");
  Tap(192 + 15, 198 + 6);
  Tap(192 + 15, 198 + 6);            // back to 1X
  TapTab(kDebug);
  Shot("debug");
  Tap(160, 210);
  Shot("debug tools window");
  Tap(16 + 5, 44 + 5);               // SCREEN DUMP: asks what is wrong first
  Shot("report window (screen dump)");
  Tap(164 + 5, 182 + 5);             // CANCEL
  Shot("after cancel: nothing written, back to the debug tab");
  Tap(160, 210);
  Tap(16 + 5, 44 + 4 * 29 + 5);      // PLANE TINT: planes
  Shot("debug tools window, plane tint: planes");
  Tap(16 + 5, 44 + 4 * 29 + 5);
  Shot("debug tools window, plane tint: draw order");
  Tap(16 + 5, 44 + 4 * 29 + 5);
  Shot("debug tools window, plane tint: stereo depth");
  Tap(16 + 5, 44 + 4 * 29 + 5);   // back to off
  Tap(16 + 140 - 16, 44 + 2 * 29 + 13);   // SCENE REC: start
  Tap(16 + 140 - 16, 44 + 2 * 29 + 13);   // and stop: the report window, with RESUME and the cross
  Shot("report window (stopping a scene recording)");
  Tap(236 + 5, 182 + 5);             // the cross: discard
  Tap(160, 220);                     // close
#endif
  TapTab(kStates);
  Shot("states");
  BottomUi_TouchDown(150, 160);      // drag the list up by 90 px
  BottomUi_TouchMove(150, 70);
  BottomUi_TouchUp();
  Shot("states, scrolled");
  Tap(150, 46 + 5);                  // the card at the top: its detail window
  Shot("state detail window");
  Tap(62 + 3 * 30 + 5, 172 + 5);     // a mark
  Tap(12 + 2 * 76 + 5, 198 + 5);     // DELETE: armed
  Shot("state detail window, delete armed");
  Tap(12 + 3 * 76 + 5, 198 + 5);     // CLOSE
  BottomUi_Busy();
  Shot("states, please wait (a save or a dump in progress)");
  extern bool g_preview_ra_toast;
  TapTab(kAchievements);
  Shot("achievements");
  BottomUi_TouchDown(150, 220);      // drag the list up by 60 px
  for (int y = 216; y >= 160; y -= 8) BottomUi_TouchMove(150, y);
  BottomUi_TouchUp();
  Shot("achievements, list dragged");
  BottomUi_TouchDown(312, 237);      // the scrollbar's bottom end
  BottomUi_TouchUp();
  Shot("achievements, scrollbar to the end");
  Tap(150, 130);                     // a card: its detail window
  Shot("achievements, detail window");
  Tap(160, 220);                     // close
  BottomUi_TouchDown(312, 106);      // back to the top
  BottomUi_TouchUp();
  Tap(150, 106 + 2 * 34 + 10);       // the third card (missable, long title)
  Shot("achievements, detail of a locked one");
  Tap(160, 220);
  Tap(150, 106 + 10);                // the first card (unlocked): its date
  Shot("achievements, detail of an unlocked one");
  Tap(160, 220);
  g_preview_ra_toast = true;
  Shot("achievements, unlock notice");
  g_preview_ra_toast = false;
  Tap(100, 70 + 5);                  // NOTICE: top
  Tap(100 + 154, 70 + 5);            // SOUND: off
  Tap(230, 95);                      // ORDER: next
  Shot("achievements, settings changed");
  TapTab(kOptions);
  Tap(8 + 5, 30 + 2 * 34 + 5);       // DISPLAY -> PIXEL PERFECT
  Shot("options");
  Tap(240, 147);                     // RESET GAME
  Shot("options, reset window");
  Tap(216, 148);                     // cancel
  // The updater: its cell in every state, and the prompts over the tab.
  extern UpdState g_preview_upd_state;
  extern UpdPrompt g_preview_upd_prompt;
  extern int g_preview_upd_progress;
  extern bool g_preview_upd_kept;
  extern const char *g_preview_upd_message;
  // The half buttons and the loudspeaker: a tap changes each.
  Tap(8 + 5, 64 + 5);                // FPS: top-left
  Tap(8 + 5, 64 + 5);                // top-right
  Tap(8 + 5 + 154 - 40 + 130 - 60, 30 + 5);   // the loudspeaker: sound off
  Tap(8 + 5, 30 + 5);                // FRAMES: LOCK 30
  Tap(162 + 77 + 5, 98 + 5);         // CHANNEL: + BETAS
  Tap(8 + 77 + 5, 98 + 5);           // VIEW: WIDE
  Shot("options, half buttons changed (FPS corner, sound off, LOCK 30, + BETAS, WIDE)");
  Tap(8 + 5, 30 + 5);                // FRAMES: NO SKIP (the toast)
  Shot("options, NO SKIP toast");
  Tap(8 + 5, 30 + 5);                // FRAMES: AUTO again
  Tap(8 + 5, 64 + 5), Tap(8 + 5, 64 + 5), Tap(8 + 5, 64 + 5);   // FPS back to NO
  Tap(8 + 5 + 154 - 40 + 130 - 60, 30 + 5);   // sound on
  Tap(162 + 77 + 5, 98 + 5);         // CHANNEL: back
  Tap(8 + 77 + 5, 98 + 5);           // VIEW: back
  Tap(162 + 5, 166 + 5);             // WHAT'S NEW before any check
  Shot("what's new, nothing yet");
  Tap(160, 220);                     // CLOSE
  Tap(8 + 5, 132 + 5);               // UPDATES: check
  if (g_preview_upd_state != UPD_CHECKING) { fprintf(stderr, "UPDATES did not start a check\n"); return 1; }
  Shot("options, checking for updates");
  g_preview_upd_state = UPD_UP_TO_DATE;
  Shot("options, up to date");
  g_preview_upd_state = UPD_AVAILABLE;
  g_preview_upd_prompt = UPD_PROMPT_ASK_INSTALL;
  Shot("update prompt: install?");
  Tap(160, 176);                     // WHAT'S NEW, from the prompt
  Shot("what's new, from the prompt");
  BottomUi_TouchDown(160, 120), BottomUi_TouchMove(160, 60), BottomUi_TouchUp();   // drag: scroll
  Shot("what's new, scrolled");
  Tap(160, 220);                     // CLOSE: the prompt comes back
  Shot("update prompt: install? again");
  g_preview_upd_prompt = UPD_PROMPT_PROGRESS;
  g_preview_upd_state = UPD_DOWNLOADING;
  g_preview_upd_progress = 40;
  Shot("update prompt: installing");
  g_preview_upd_prompt = UPD_PROMPT_ASK_RESTART;
  g_preview_upd_state = UPD_INSTALLED;
  Shot("update prompt: restart?");
  g_preview_upd_prompt = UPD_PROMPT_ERROR;
  g_preview_upd_state = UPD_ERROR;
  g_preview_upd_message = "AM FINISH OW 0xC8A04402";
  g_preview_upd_kept = true;
  Shot("update prompt: failed");
  Tap(160, 148);                     // OK
  Shot("options, update error");
  g_preview_upd_state = UPD_IDLE;
  // Every other language: the player-facing tabs and the reset window. LANGUAGE is the
  // last option cell (left column, fourth row) and cycles.
  for (int lang = 1; lang < kLangCount; lang++) {
    TapTab(kOptions);
    Tap(162 + 5, 64 + 5);            // LANGUAGE
    if (g_ui_lang != (UiLang)lang) { fprintf(stderr, "LANGUAGE did not cycle\n"); return 1; }
    char what[64];
    Tap(8 + 5, 30 + 5), Tap(8 + 5, 30 + 5);   // FRAMES: LOCK 30, then NO SKIP: its toast
    snprintf(what, sizeof(what), "options, frame skip off toast, %s", UiLang_Name(g_ui_lang));
    Shot(what);
    Tap(8 + 5, 30 + 5);              // FRAMES: AUTO again
    Tap(240, 147);                   // RESET GAME
    snprintf(what, sizeof(what), "options, reset window, %s", UiLang_Name(g_ui_lang));
    Shot(what);
    Tap(216, 148);                   // cancel
    TapTab(kStatus);
    snprintf(what, sizeof(what), "status, %s", UiLang_Name(g_ui_lang));
    Shot(what);
    TapTab(kStates);
    snprintf(what, sizeof(what), "states, %s", UiLang_Name(g_ui_lang));
    Shot(what);
    TapTab(kMap);
    snprintf(what, sizeof(what), "map, %s", UiLang_Name(g_ui_lang));
    Shot(what);
    TapTab(kAchievements);
    g_preview_ra_toast = true;
    snprintf(what, sizeof(what), "achievements, unlock notice, %s", UiLang_Name(g_ui_lang));
    Shot(what);
    g_preview_ra_toast = false;
    Tap(150, 106 + 2 * 34 + 10);
    snprintf(what, sizeof(what), "achievements, detail window, %s", UiLang_Name(g_ui_lang));
    Shot(what);
    Tap(160, 220);
  }
  // Translations must keep the English conversions (they go through snprintf).
  int bad = 0;
  for (int lang = 0; lang < kLangCount; lang++) {
    for (int id = 0; id < kStrCount; id++) {
      g_ui_lang = kLangEn;
      const char *en = Tr((UiStr)id);
      g_ui_lang = (UiLang)lang;
      const char *t = Tr((UiStr)id), *a = en, *b = t;
      for (;;) {
        a = strchr(a, '%'), b = strchr(b, '%');
        if (!a || !b) break;
        if (a[1] != b[1]) break;
        a += 2, b += 2;
      }
      if (a || b) printf("FORMAT MISMATCH: %s / %s\n", en, t), bad++;
    }
  }
  return bad ? 1 : 0;
}
bool g_debug_flag, g_is_turbo, g_want_dump_memmap_flags, g_new_ppu = true, g_other_image;
struct SpcPlayer *g_spc_player;
int g_got_mismatch_count;
void RtlDrawPpuFrame(uint8 *b, size_t p, uint32 f) {}
void RtlApuLock(void) {}
void RtlApuUnlock(void) {}
void RtlApuQueueLock(void) {}
void RtlApuQueueUnlock(void) {}
int SDL_GetKeyFromName(const char *n) { return 0; }
