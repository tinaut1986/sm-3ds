#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "3ds.h"
#include "src/types.h"
#include "src/sm_rtl.h"
#include "src/variables.h"
#include "bottom_ui.h"
#include "cheats.h"
#include "debug_tools.h"
#include "src/sm_cpu_infra.h"
#include "sm_map.h"
static uint8_t fb[2][400 * 240 * 4];
u8 *gfxGetFramebuffer(gfxScreen_t s, gfx3dSide_t side, u16 *w, u16 *h) { if (w) *w = 240; if (h) *h = s == GFX_TOP ? 400 : 320; return fb[s]; }
u64 osGetTime(void) { return 1000; }
size_t linearSpaceFree(void) { return 20 * 1024 * 1024; }
int APT_CheckNew3DS(bool *o) { *o = true; return 0; }
void osSetSpeedupEnable(bool e) {}
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
int main(int argc, char **argv) {
  const char *rom_path = getenv("SM_ROM");   // optional: needed for the MAP tab
  if (rom_path && !SnesInit(rom_path)) { fprintf(stderr, "cannot load %s\n", rom_path); return 1; }
  UiRomInfo rom = { "Super Metroid (Japan, USA).sfc", "da957f0d63d14cb441d215462904c4fa8519c613", false, "0.1.0" };
  BottomUi_Init(&rom);
  UiPerf perf = { .fps = 60, .game_fps = 60, .frame_ms = 13.7f, .logic_ms = 9.8f, .draw_ms = 2.8f, .audio_ms = 4.7f,
                  .audio_part_ms = {0, 0.1f, 4.6f, 0.1f}, .frames = 1234, .is_new3ds = true };
  // A plausible mid-game state.
  game_state = 8; area_index = 1; room_index = 0x1A;
  samus_health = 247; samus_max_health = 499; samus_reserve_health = 60; samus_max_reserve_health = 100; reserve_health_mode = 1;
  samus_missiles = 45; samus_max_missiles = 75; samus_super_missiles = 5; samus_max_super_missiles = 10; samus_power_bombs = 0; samus_max_power_bombs = 10;
  collected_items = 0x0001 | 0x0004 | 0x1000 | 0x0100; equipped_items = 0x0001 | 0x0004 | 0x1000;
  collected_beams = 0x1000 | 0x0002; equipped_beams = 0x1000;
  game_time_hours = 3; game_time_minutes = 27; game_time_seconds = 9;
  boss_bits_for_area[1] = 3; boss_bits_for_area[0] = 1;
  if (rom_path) {
    // Pretend Samus is in the Landing Site and has explored the Crateria rooms near it.
    room_ptr = 0x91F8; area_index = 0; samus_x_pos = 700; samus_y_pos = 300;
    int n; const SmRoom *rooms = SmMap_Rooms(&n);
    for (int i = 0; i < n; i++) {
      if (rooms[i].area != 0 || rooms[i].x < 14) continue;
      for (int j = 0; j < rooms[i].h; j++) for (int k = 0; k < rooms[i].w; k++) {
        int col = rooms[i].x + k, row = rooms[i].y + 1 + j, idx = (col >= 32 ? 1024 : 0) + row * 32 + (col & 31);
        map_tiles_explored[idx >> 3] |= 0x80 >> (idx & 7);
      }
    }
    printf("rooms found: %d\n", n);
  }
  for (int tab = 0; tab < 5; tab++) {
    if (tab == 1 && !rom_path) continue;
    BottomUi_TouchDown(tab * 64 + 10, 10);   // select the tab
    if (tab == 1) BottomUi_TouchDown(24 * 5 + 2, 24 + 3 * 5 + 2);   // select a map cell
    if (tab == 2) g_cheats.invincible = true;
    char name[64];
    snprintf(name, sizeof(name), "tab%d.ppm", tab);
    BottomUi_Toast(tab == 1 ? "All items" : "Preview");
    BottomUi_Frame(&perf);
    Dump(name);
  }
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
