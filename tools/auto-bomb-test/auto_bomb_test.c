// Host check of the automatic bomb jump (auto_bomb.c): see run.sh.
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
#include "sm_map.h"
#include "sm_warp.h"
#include "auto_bomb.h"
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

enum { START = 0x08, DOWN = 0x20, A = 0x100, X = 0x200 };   // the frontend's pad bits (main.c)

static void Step(int in) {
  samus_health = 99;
  debug_invincibility = 7;   // no knockback from an enemy: only the bombs move her
  g_snes->disableRender = 1;
  RtlRunFrame(in);
  SmWarp_AfterFrame();
}

static bool Ball(void) { return samus_movement_type == 4 || samus_movement_type == 8; }

// The room, the column (blocks) and the row of the floor under the tallest open column (found by
// scanning every room for empty blocks with a floor below), and the least climb that passes (px).
static const struct { uint16 room; int col, floor, want; } kShafts[] = {
  { 0x91F8, 48, 77, 1150 },    // Landing Site: 75 blocks open
  { 0x9AD9, 40, 186, 1080 },   // Green Brinstar main shaft: 70
  { 0x9CB3, 71, 108, 1300 },   // Dachora's room: 94, a ledge stops her near the top
};

// Into `room`, Samus on the floor at (col, floor) in the morph ball with bombs.
static bool Place(uint16 room, int col, int floor) {
  int n;
  const SmRoom *rooms = SmMap_Rooms(&n);
  for (int i = 0; i < n; i++)
    if (rooms[i].header == room) SmWarp_ToRoom(&rooms[i], 0);
  for (int k = 0, s8 = 0; k < 900; k++) {
    Step(0);
    if (game_state == 8) { if (++s8 > 60) break; } else s8 = 0;
  }
  if (room_ptr != room) return false;
  equipped_items |= 0x1004, collected_items |= 0x1004;
  samus_x_pos = col * 16 + 8, samus_y_pos = floor * 16 - 24, samus_y_dir = 2;
  for (int k = 0; k < 120; k++) Step(0);
  for (int k = 0; k < 40 && !Ball(); k++) Step(k & 1 ? 0 : DOWN);
  for (int k = 0; k < 30; k++) Step(0);
  return Ball();
}

// With the charge beam, holds the bomb button standing until the beam is charged, then rolls into
// the ball still holding it, through AutoBomb_Translate. True if the bomb spread went off.
static bool Spread(void) {
  equipped_beams |= 0x1000, collected_beams |= 0x1000;
  for (int k = 0; k < 40 && Ball(); k++) Step(AutoBomb_Translate(k & 1 ? 0 : 0x10));   // Up: out of the ball
  for (int k = 0; k < 30; k++) Step(0);
  for (int k = 0; k < 90; k++) Step(AutoBomb_Translate(X));
  bool spread = false;
  for (int k = 0; k < 120; k++) {
    Step(AutoBomb_Translate(X | (!Ball() && !(k & 1) ? DOWN : 0)));
    if (projectile_type[5] == 0x8500) spread = true;   // BombSpread's bombs
  }
  return spread;
}

// Holds the bomb button through AutoBomb_Translate, as main.c does. Returns the height climbed.
static int Climb(int frames, int *laid) {
  const int y0 = samus_y_pos;
  int top = y0;
  *laid = 0;
  for (int f = 0; f < frames && Ball(); f++) {
    const int before = bomb_counter;
    Step(AutoBomb_Translate(X));
    if (bomb_counter > before) ++*laid;
    if (samus_y_pos < top) top = samus_y_pos;
  }
  return y0 - top;
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
  const int shafts = sizeof(kShafts) / sizeof(kShafts[0]);
  for (int i = 0; i < 2 * shafts + 2; i++) {
    // The shafts, then again with HEAVY PHYSICS (#65); then the option off, then the bomb spread
    // with it on, both in the first room.
    const bool heavy = i >= shafts && i < 2 * shafts, off = i == 2 * shafts, spread = i == 2 * shafts + 1;
    const int r = i < 2 * shafts ? i % shafts : 0;
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
      alarm(120);
      RtlSaveLoad(kSaveLoad_Load, 1);
      if (!Place(kShafts[r].room, kShafts[r].col, kShafts[r].floor)) {
        printf("room %04x: could not place Samus in the morph ball\n", kShafts[r].room);
        fflush(stdout);
        _exit(1);
      }
      AutoBomb_Set(!off);
      g_rtl_heavy_physics = heavy;
      if (spread) {
        const bool ok = Spread();
        printf("spread: %s\n", ok ? "the charged beam became the bomb spread" : "NO BOMB SPREAD");
        fflush(stdout);
        _exit(ok ? 0 : 1);
      }
      int laid;
      const int rise = Climb(off ? 300 : 3000, &laid);
      if (off) {
        printf("off: %d bomb(s) laid holding the button\n", laid);
        fflush(stdout);
        _exit(laid == 1 ? 0 : 1);
      }
      printf("room %04x%s: climbed %d px (want %d), %d bombs\n", kShafts[r].room, heavy ? " (heavy physics)" : "", rise,
             kShafts[r].want, laid);
      fflush(stdout);
      _exit(rise >= kShafts[r].want ? 0 : 1);
    }
    int st;
    waitpid(pid, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) fails++;
  }
  printf(fails ? "auto_bomb_test: %d FAILED\n" : "auto_bomb_test: OK\n", fails);
  return fails != 0;
}
