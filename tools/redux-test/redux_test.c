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
#include "src/ida_types.h"
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
enum { RUN = 0x01, ITEM_CANCEL = 0x02, START = 0x08, DOWN = 0x20, LEFT = 0x40, RIGHT = 0x80, JUMP = 0x100, SHOOT = 0x200 };

static void Step(int in) {
  samus_health = 99;
  debug_invincibility = 7;
  g_snes->disableRender = 1;
  RtlRunFrame(in);
  SmWarp_AfterFrame();
}

static int RomByte(uint32 addr) { return *RomFixedPtr(addr); }

// Landing Site, Samus standing on the floor under its tallest open column (as tools/air-spin-test).
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
  return samus_movement_type == 0;
}

// The movement checks (P4.26's second batch): 1 when Redux's behaviour shows, 0 when the original's, -1 when it could not be set up.
void HdmaobjPreInstr_XrayFunc1_BeamWidening(uint16 k);
uint8 PlmSetup_D08C_SuperMissileBlockRespawning(uint16 j);

// The third batch's are called directly: the X-ray's first widening frame, a super missile block hit by a power bomb.
enum { kMoveSpace, kMoveQuickMorph, kMoveXray, kMovePbBlock, kMoveRecoil, kMoveCount };
static const char *const kMoveNames[kMoveCount] = { "space jump at 6 px a frame of fall", "quick morph (Y + down, standing)",
                                                    "x-ray open at once to 26", "power bomb reveals a super missile block",
                                                    "super missile fired down in a jump pushes up (jump held)" };

static int MoveCheck(int which) {
  switch (which) {
  case kMoveSpace: {   // a spin jump with Space Jump, falling at 6 px a frame: the jump button jumps again only in REDUX's window
    collected_items |= 0x200, equipped_items |= 0x200;
    for (int k = 0; k < 12; k++) Step(RIGHT);
    for (int k = 0; k < 6; k++) Step(RIGHT | JUMP);   // a running jump: a spin
    for (int k = 0; k < 200 && !(samus_movement_type == 3 && samus_y_dir == 2); k++) Step(RIGHT);
    if (samus_movement_type != 3) return -1;
    samus_y_dir = 2, samus_y_speed = 6, samus_y_subspeed = 0;
    Step(RIGHT | JUMP);
    return samus_y_dir == 1;   // rising: jumped again
  }
  case kMoveQuickMorph: {   // standing, item cancel (Y) and down held: the morph ball
    collected_items |= 4, equipped_items |= 4;
    for (int k = 0; k < 30; k++) Step(ITEM_CANCEL | DOWN);
    return samus_movement_type == 4 || samus_movement_type == 8;
  }
  case kMoveXray: {   // the X-ray button held, the beam's first widening frame
    joypad1_lastkeys = button_config_run_b;
    demo_input = demo_input_new = demo_input_instr_timer = demo_input_instr_ptr = 0, demo_input_pre_instr = 1;
    HdmaobjPreInstr_XrayFunc1_BeamWidening(0);
    return demo_input == 26 && demo_input_pre_instr == 2;
  }
  case kMovePbBlock: {   // a power bomb's explosion (projectile type 0x3xx) on a respawning super missile block's PLM
    projectile_index = 0, projectile_type[0] = 0x300;
    plm_instr_list_ptrs[0] = 0, plm_header_ptr[0] = 0x1234;
    PlmSetup_D08C_SuperMissileBlockRespawning(0);
    return plm_instr_list_ptrs[0] == addr_kPlmInstrList_C922 && plm_header_ptr[0] == 0x1234;
  }
  case kMoveRecoil: {   // a straight jump, then down + shoot with the super missile near the top: Samus rises again. Jump stays
                        // held: let go and the game ends the rise at once, in Redux too (its patch only sets the Y speed).
    collected_items |= 0x8000;
    samus_max_super_missiles = samus_super_missiles = 10, hud_item_index = 2;
    for (int k = 0; k < 8; k++) Step(JUMP);
    for (int k = 0; k < 120 && samus_y_dir != 2; k++) Step(JUMP);
    if (samus_y_dir != 2) return -1;
    int before = samus_y_pos, fired = 0, rose = 0;
    for (int k = 0; k < 12; k++) {
      Step(DOWN | JUMP | (k == 3 ? SHOOT : 0));
      fired |= samus_super_missiles != 10;
      if (samus_y_pos < before) rose = 1;
      before = samus_y_pos;
    }
    return fired ? rose : -1;
  }
  }
  return -1;
}

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

  // Movement (P4.26's second batch): with REDUX off the original's result, with it on Redux's.
  for (int m = 0; m < kMoveCount; m++) {
    int got[2] = { -1, -1 };
    for (int t = 0; t < 2; t++) {
      int fd[2];
      if (pipe(fd)) return 2;
      fflush(stdout);
      pid_t pid = fork();
      if (pid == 0) {
        alarm(120);
        RtlSaveLoad(kSaveLoad_Load, 1);
        g_rtl_redux = t == 1;
        int r = Place() ? MoveCheck(m) : -1;
        if (write(fd[1], &r, sizeof(r)) != sizeof(r)) _exit(2);
        _exit(0);
      }
      int st, r = -1;
      waitpid(pid, &st, 0);
      if (read(fd[0], &r, sizeof(r)) != sizeof(r)) r = -1;
      close(fd[0]), close(fd[1]);
      got[t] = r;
    }
    const bool ok = got[0] == 0 && got[1] == 1;
    printf("%s: off %d, on %d (want 0 then 1)\n", kMoveNames[m], got[0], got[1]);
    fails += !ok;
  }

  printf(fails ? "redux_test: %d FAILED\n" : "redux_test: OK\n", fails);
  return fails != 0;
}
