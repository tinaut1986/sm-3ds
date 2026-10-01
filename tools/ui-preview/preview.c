#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "3ds.h"
#include "build_config.h"
#include "src/types.h"
#include "src/sm_rtl.h"
#include "src/variables.h"
#include "bottom_ui.h"
#include "cheats.h"
#include "debug_tools.h"
#include "src/sm_cpu_infra.h"
#include "sm_map.h"
static uint8_t fb[2][400 * 240 * 4];
static u64 g_now = 100000;   // advanced between shots so tap flashes and toasts expire
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
Result PTMU_GetBatteryChargeState(u8 *out) { *out = 0; return 0; }
u8 osGetWifiStrength(void) { return 2; }
void NORETURN Die(const char *e) { exit(1); }
void Warning(const char *e) {}

static void Dump(const char *name) {
  FILE *f = fopen(name, "wb");
  fprintf(f, "P6\n320 240\n255\n");
  for (int y = 0; y < 240; y++) for (int x = 0; x < 320; x++) {
    const uint8_t *p = &fb[GFX_BOTTOM][(x * 240 + (239 - y)) * 4];   // A,B,G,R
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

// Tab x positions: 4 + slot * 34, slots in drawing order.
#if DEBUG_TOOLS
enum { kMap = 0, kStatus = 1, kDebug = 2, kStates = 3, kOptions = 4 };
#else
enum { kMap = 0, kStatus = 1, kStates = 2, kOptions = 3 };
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
  BottomUi_Init(&rom);

  TapTab(kMap);
  Tap(24 * 5 + 2, 26 + 3 * 5 + 2);   // pick a map cell
  Shot("map, a room picked");
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
  TapTab(kDebug);
  Shot("debug");
  Tap(160, 210);
  Shot("debug tools window");
  Tap(160, 220);                     // close
#endif
  TapTab(kStates);
  Tap(260, 38 + 2 * 19 + 5);         // arm save on slot 2
  Shot("states, slot 2 save armed");
  TapTab(kOptions);
  Tap(8 + 5, 30 + 3 * 34 + 5);       // DISPLAY -> PIXEL PERFECT
  Shot("options");
  Tap(160, 178);
  Shot("options, reset window");
  return 0;
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
