// Host test: from a real gameplay state, warp into every room and check the game
// settles in normal gameplay inside the target room.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
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
bool g_debug_flag, g_is_turbo, g_want_dump_memmap_flags, g_new_ppu = true, g_other_image;
struct SpcPlayer *g_spc_player;
int g_got_mismatch_count;
extern Snes *g_snes;
static uint8_t px[256*4*240];
void NORETURN Die(const char *e) { fprintf(stderr, "Die: %s\n", e); _exit(3); }
void Warning(const char *e) {}
void RtlDrawPpuFrame(uint8 *b, size_t p, uint32 f) {}
void RtlApuLock(void) {} void RtlApuUnlock(void) {} void RtlApuQueueLock(void) {} void RtlApuQueueUnlock(void) {}
int SDL_GetKeyFromName(const char*n){return 0;}
enum { A=0x100, START=0x08 };
static void step(int in) {
  samus_health = 99;   // idle Samus would eventually be killed by enemies in some rooms
  g_snes->disableRender = 1;
  RtlRunFrame(in);
  SmWarp_AfterFrame();
}
int main(int argc, char **argv) {
  ParseConfigFile(NULL);
  Snes *snes = SnesInit(argv[1]);
  g_spc_player = SpcPlayer_Create(); SpcPlayer_Initialize(g_spc_player);
  PpuBeginDrawing(snes->snes_ppu, px, 256*4, 0);
  RtlReadSram();
  for (int i = 0; i < 20000; i++) {
    step((i % 60 < 6) ? (i % 120 < 60 ? START : A) : 0);
    if (game_state == 8 && i > 100) break;
  }
  for (int i = 0; i < 120; i++) step(0);   // settle
  printf("gameplay: state %02x area %u room %04x\n", (unsigned)game_state, (unsigned)area_index, (unsigned)room_ptr);
  fflush(stdout);
  RtlSaveLoad(kSaveLoad_Save, 1);
  int n; const SmRoom *rooms = SmMap_Rooms(&n);
  SmWarp_Init();
  int only = argc > 2 ? strtol(argv[2], 0, 16) : 0;
  int ok = 0, fail = 0, nodoor = 0;
  for (int i = 0; i < n; i++) {
    const SmRoom *r = &rooms[i];
    if (only && r->header != only) continue;
    if (SmWarp_DoorCount(r) == 0) { nodoor++; printf("room %04x area %d: NO DOOR\n", r->header, r->area); continue; }
    fflush(stdout);
    for (int door = 0; door < SmWarp_DoorCount(r); door++) {
    if (getenv("WARP_ONLY_DOOR") && door != atoi(getenv("WARP_ONLY_DOOR"))) continue;
    pid_t pid = fork();
    if (pid == 0) {
      alarm(30);
      RtlSaveLoad(kSaveLoad_Load, 1);
      for (int k = 0; k < 10; k++) step(0);
      // Stress mode: start from a screen-relative position no real door gives
      // (the game keeps the low byte of Samus's old position across a door).
      if (getenv("WARP_STRESS")) {
        samus_x_pos = (samus_x_pos & 0xFF00) | 0xF0;
        samus_y_pos = (samus_y_pos & 0xFF00) | 0xF0;
      }
      SmWarpResult wr = SmWarp_ToRoom(r, door);
      if (wr != kWarp_Ok) { printf("room %04x: warp refused (%d)\n", r->header, wr); fflush(stdout); _exit(4); }
      int frames = 0; int seen8 = 0;
      for (; frames < 900; frames++) {
        step(0);
        if (game_state == 8) { if (++seen8 > 30) break; } else seen8 = 0;
      }
      bool good = game_state == 8 && room_ptr == r->header && area_index == r->area;
      if (getenv("WARP_TRACE2")) { for (int k = 0; k < 40; k++) { printf("   +%d x %u y %u state %02x pose %02x xspd %u\n", k, (unsigned)samus_x_pos, (unsigned)samus_y_pos, (unsigned)game_state, (unsigned)samus_pose, (unsigned)*(uint16*)(g_ram+0xB2A)); step(0); } }
      if (good) {
        // Not stuck in the "just loaded a game" state (pose 0 while the fanfare plays).
        if (samus_pose == 0) { printf("room %04x door %d: SAMUS STILL IN THE LOAD POSE\n", r->header, door); good = false; }
        // Samus must end up inside the room, not inside a solid block.
        const SmRoom *cur = r;
        int px_w = cur->w * 256, px_h = cur->h * 256;
        if (samus_x_pos >= px_w || samus_y_pos >= px_h) { printf("room %04x door %d: SAMUS OUT OF ROOM at %u,%u (room %dx%d px)\n", r->header, door, (unsigned)samus_x_pos, (unsigned)samus_y_pos, px_w, px_h); good = false; }
        else {
          int blk = (samus_y_pos >> 4) * (int)room_width_in_blocks + (samus_x_pos >> 4);
          int type = (level_data[blk] >> 12) & 0xF;
          int tgx, tgy; bool vert;
          if (SmWarp_LastTarget(&tgx, &tgy, &vert)) {
            // Through a ceiling door she drops and falls; only check how far she is sideways.
            int dx = (int)samus_x_pos - tgx, dy = vert ? 0 : (int)samus_y_pos - tgy;
            // Informational only: with no floor near the door (a shaft) or a boss room that
            // holds Samus in place, she legitimately ends up elsewhere.
            if (dx * dx + dy * dy > 64 * 64) printf("note: room %04x door %d: samus at %u,%u, aimed for %d,%d\n", r->header, door, (unsigned)samus_x_pos, (unsigned)samus_y_pos, tgx, tgy);
            if (type == 9) { printf("room %04x door %d: SAMUS INSIDE A DOOR BLOCK\n", r->header, door); good = false; }
          }
          if (type == 8) { printf("room %04x door %d: SAMUS IN SOLID BLOCK at %u,%u\n", r->header, door, (unsigned)samus_x_pos, (unsigned)samus_y_pos); good = false; }
        }
      }
      // Keep playing for a while: bad Samus coordinates only show up in the per-frame
      // collision code (run this under ASAN to catch out-of-bounds level reads).
      for (int k = 0; k < 180 && good; k++) step(0);
      if (getenv("WARP_VERBOSE")) printf("room %04x door %d: fixups so far %d\n", r->header, door, SmWarp_FixupCount());
      if (!good) printf("room %04x door %d area %d: FAIL after %d frames: state %02x room %04x area %u\n", r->header, door, r->area, frames, (unsigned)game_state, (unsigned)room_ptr, (unsigned)area_index);
      fflush(stdout); _exit(good ? 0 : 1);
    }
    int st; waitpid(pid, &st, 0);
    if (WIFEXITED(st) && WEXITSTATUS(st) == 0) ok++;
    else { fail++; if (!WIFEXITED(st)) printf("room %04x door %d area %d: CRASH signal %d\n", r->header, door, r->area, WTERMSIG(st)); else if (WEXITSTATUS(st) == 3) printf("room %04x door %d: Die()\n", r->header, door); }
    }
  }
  printf("RESULT ok %d fail %d nodoor %d of %d\n", ok, fail, nodoor, n);
  return 0;
}
