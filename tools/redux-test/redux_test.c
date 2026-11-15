// Host check of REDUX (P4.26): see run.sh.
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

// The frontend's pad bits: bit i is the SNES button 0x8000 >> i (main.c, RtlRunFrame).
enum { START = 0x08, LEFT = 0x40, RIGHT = 0x80, JUMP = 0x100, SHOOT = 0x200 };

static void Step(int in) {
  samus_health = 99;
  debug_invincibility = 7;
  g_snes->disableRender = 1;
  RtlRunFrame(in);
  SmWarp_AfterFrame();
}

static int RomByte(uint32 addr) { return *RomFixedPtr(addr); }

// Arrives in `room` through its door number `which` (a left or right door), turns to the door it came
// through, shoots it open and walks into it. Returns the frames from the door hit (game state 9) back to
// gameplay (8), or -1 when it never got there.
static int DoorFrames(uint16 header, int which) {
  int n;
  const SmRoom *rooms = SmMap_Rooms(&n), *room = NULL;
  for (int i = 0; i < n; i++)
    if (rooms[i].header == header) room = &rooms[i];
  SmWarpDoorMark mark;
  if (!room || !SmWarp_DoorOnMap(room, which, &mark) || mark.side > 1) return -1;
  SmWarp_ToRoom(room, which);
  for (int k = 0, s8 = 0; k < 900; k++) {
    Step(0);
    if (game_state == 8) { if (++s8 > 60) break; } else s8 = 0;
  }
  if (room_ptr != header || game_state != 8) return -1;
  const int toward = mark.side == 0 ? LEFT : RIGHT;
  for (int k = 0; k < 4; k++) Step(toward);
  for (int k = 0; k < 20; k++) Step(k & 4 ? SHOOT : 0);   // shots at the closed door
  int start = -1;
  for (int k = 0; k < 600; k++) {
    Step(toward);
    if (start < 0 && game_state == 9) start = k;
    if (start >= 0 && game_state == 8) return k - start;
  }
  return -1;
}

int main(int argc, char **argv) {
  ParseConfigFile(NULL);
  Snes *snes = SnesInit(argv[1]);
  g_spc_player = SpcPlayer_Create();
  SpcPlayer_Initialize(g_spc_player);
  PpuBeginDrawing(snes->snes_ppu, px, 256 * 4, 0);
  RtlReadSram();
  for (int i = 0; i < 20000; i++) {
    Step((i % 60 < 6) ? (i % 120 < 60 ? START : 0x100) : 0);
    if (game_state == 8 && i > 100) break;
  }
  for (int i = 0; i < 120; i++) Step(0);
  SmMap_Init();
  SmWarp_Init();
  RtlSaveLoad(kSaveLoad_Save, 1);
  int fails = 0;

  // The ROM data: Redux's while the option is on, the original's back when it goes off.
  {
    const int before = RomByte(0x84C32C);
    g_rtl_redux = true;
    Step(0);
    const int on = RomByte(0x84C32C), on2 = RomByte(0x84BEB0);
    g_rtl_redux = false;
    Step(0);
    const int off = RomByte(0x84C32C), off2 = RomByte(0x84BEB0);
    const bool ok = before == 5 && on == 1 && on2 == 1 && off == 5 && off2 == 4;
    printf("rom data: missile door hits %d -> on %d -> off %d (want 5 1 5), a door frame %d -> %d (want 1 4)\n",
           before, on, off, on2, off2);
    fails += !ok;
  }

  // A door transition (Landing Site's lower left door, from the Parlor's side is fine too): REDUX's scrolls
  // and fades in about half the frames.
  static const struct { uint16 room; int which; } kDoors[] = { { 0x92FD, 0 }, { 0x92FD, 1 }, { 0x91F8, 0 }, { 0x91F8, 1 }, { 0x93AA, 0 } };
  int frames[2] = { -1, -1 };
  for (int d = 0; d < (int)(sizeof(kDoors) / sizeof(kDoors[0])) && (frames[0] < 0 || frames[1] < 0); d++) {
    for (int t = 0; t < 2; t++) {
      int fd[2];
      if (pipe(fd)) return 2;
      fflush(stdout);
      pid_t pid = fork();
      if (pid == 0) {
        alarm(120);
        RtlSaveLoad(kSaveLoad_Load, 1);
        g_rtl_redux = t == 1;
        int f = DoorFrames(kDoors[d].room, kDoors[d].which);
        if (write(fd[1], &f, sizeof(f)) != sizeof(f)) _exit(2);
        _exit(0);
      }
      int st, f = -1;
      waitpid(pid, &st, 0);
      if (read(fd[0], &f, sizeof(f)) != sizeof(f)) f = -1;
      close(fd[0]), close(fd[1]);
      frames[t] = f;
    }
    if (frames[0] < 0 || frames[1] < 0) {
      printf("door: room %04X door %d gave %d / %d frames, trying another\n", kDoors[d].room, kDoors[d].which, frames[0], frames[1]);
      frames[0] = frames[1] = -1;
    }
  }
  const bool door_ok = frames[0] > 0 && frames[1] > 0 && frames[1] * 4 < frames[0] * 3;
  printf("door transition: %d frames, with REDUX %d (want at most 3/4)\n", frames[0], frames[1]);
  fails += !door_ok;

  printf(fails ? "redux_test: %d FAILED\n" : "redux_test: OK\n", fails);
  return fails != 0;
}
