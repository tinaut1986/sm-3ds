// Host check of AIR SPIN (#68) and the HEAVY PHYSICS wall jump (#65): see run.sh.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "src/types.h"
#include "src/sm_rtl.h"
#include "src/sm_cpu_infra.h"
#include "src/config.h"
#include "src/snes/ppu.h"
#include "src/spc_player.h"
#include "src/variables.h"
#include "src/funcs.h"
#include "sm_map.h"
#include "sm_warp.h"
bool g_debug_flag, g_is_turbo, g_want_dump_memmap_flags, g_new_ppu = true, g_other_image;
struct SpcPlayer *g_spc_player;
int g_got_mismatch_count;
extern Snes *g_snes;
static uint8_t px[256 * 4 * 240];
void NORETURN Die(const char *e) { fprintf(stderr, "Die: %s\n", e); _exit(3); }
void Warning(const char *e) {}
void RtlDrawPpuFrame(uint8 *b, size_t p, uint32 f) {}
void RtlApuLock(void) {} void RtlApuUnlock(void) {} void RtlApuQueueLock(void) {} void RtlApuQueueUnlock(void) {}
int SDL_GetKeyFromName(const char *n) { return 0; }

enum { START = 0x08, DOWN = 0x20, A = 0x100 };   // the frontend's pad bits (main.c)

static void Step(int in) {
  samus_health = 99;
  debug_invincibility = 7;
  g_snes->disableRender = 1;
  RtlRunFrame(in);
  SmWarp_AfterFrame();
}

// Landing Site, Samus standing on the floor under its tallest open column (as tools/auto-bomb-test).
static bool Place(void) {
  int n;
  const SmRoom *rooms = SmMap_Rooms(&n);
  for (int i = 0; i < n; i++)
    if (rooms[i].header == 0x91F8) SmWarp_ToRoom(&rooms[i], 0);
  for (int k = 0, s8 = 0; k < 900; k++) {
    Step(0);
    if (game_state == 8) { if (++s8 > 60) break; } else s8 = 0;
  }
  if (room_ptr != 0x91F8) return false;
  samus_x_pos = 48 * 16 + 8, samus_y_pos = 77 * 16 - 24, samus_y_dir = 2;
  for (int k = 0; k < 120; k++) Step(0);
  return samus_movement_type == 0;   // standing
}

// A jump straight up (A alone: a plain jump), then A released and pressed again `after` frames in.
// Returns the movement type 3 frames after the second press, and the Y speed just before and after it.
static int JumpThenPress(int after, int *speed_before, int *speed_after, int *dir_after) {
  for (int k = 0; k < 4; k++) Step(A);
  for (int k = 0; k < after; k++) Step(0);
  *speed_before = samus_y_speed;
  Step(A);
  for (int k = 0; k < 2; k++) Step(A);
  *speed_after = samus_y_speed, *dir_after = samus_y_dir;
  return samus_movement_type;
}

int main(int argc, char **argv) {
  ParseConfigFile(NULL);
  Snes *snes = SnesInit(argv[1]);
  g_spc_player = SpcPlayer_Create();
  SpcPlayer_Initialize(g_spc_player);
  PpuBeginDrawing(snes->snes_ppu, px, 256 * 4, 0);
  RtlReadSram();
  for (int i = 0; i < 20000; i++) {
    Step((i % 60 < 6) ? (i % 120 < 60 ? START : A) : 0);
    if (game_state == 8 && i > 100) break;
  }
  for (int i = 0; i < 120; i++) Step(0);
  SmWarp_Init();
  RtlSaveLoad(kSaveLoad_Save, 1);
  int fails = 0;
  // 0: off, a second press in a plain jump stays a plain jump. 1: on, rising: a spin, no new jump.
  // 2: on, falling (well after the top): a spin that keeps falling. 3: HEAVY PHYSICS' wall jump speed.
  for (int t = 0; t < 4; t++) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
      alarm(120);
      RtlSaveLoad(kSaveLoad_Load, 1);
      if (!Place()) {
        printf("could not place Samus in Landing Site\n");
        fflush(stdout);
        _exit(1);
      }
      bool ok = true;
      if (t < 3) {
        g_rtl_air_spin = t > 0;
        int before, after, dir, type;
        if (t == 2) {   // dropped from 30 blocks up: a fall (movement type 6), then the button
          samus_y_pos -= 30 * 16, samus_y_dir = 2;
          for (int k = 0; k < 20; k++) Step(0);
          before = samus_movement_type;
          for (int k = 0; k < 3; k++) Step(A);
          type = samus_movement_type, dir = samus_y_dir, after = 0;
          printf("on, falling: movement type %d -> ", before);
          ok = before == 6;
        } else {
          type = JumpThenPress(6, &before, &after, &dir);
        }
        if (t == 0) {
          ok = type == 2;
          printf("off: second press in a plain jump -> movement type %d (want 2)\n", type);
        } else if (t == 1) {
          ok = type == 3 && after <= before;
          printf("on, rising: movement type %d (want 3), Y speed %d -> %d (no new jump)\n", type, before, after);
        } else {
          ok = ok && type == 3 && dir == 2;
          printf("%d (want 6 -> 3), still falling %s\n", type, dir == 2 ? "yes" : "NO");
        }
      } else {
        g_rtl_heavy_physics = true;
        equipped_items &= ~0x120;   // no Hi-Jump, no Gravity Suit: the air values
        Samus_InitWallJump();
        const int wall = samus_y_speed;
        Samus_InitJump();
        const int jump = samus_y_speed;
        ok = wall == jump;
        printf("heavy physics: wall jump Y speed %d, jump %d (want equal)\n", wall, jump);
      }
      fflush(stdout);
      _exit(ok ? 0 : 1);
    }
    int st;
    waitpid(pid, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) fails++;
  }
  printf(fails ? "air_spin_test: %d FAILED\n" : "air_spin_test: OK\n", fails);
  return fails != 0;
}
