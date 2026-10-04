// Host frame capture: load a save state from the console, run a few frames and write
// the same dump set the Debug tab's FRAME DUMP writes (debug/sm-dump-NNNN-*), so the
// per-scanline PPU writes of a scene can be studied on the PC.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/types.h"
#include "src/sm_rtl.h"
#include "src/sm_cpu_infra.h"
#include "src/config.h"
#include "src/snes/ppu.h"
#include "src/snes/snes.h"
#include "src/spc_player.h"
#include "src/variables.h"
#include "debug_tools.h"

bool g_debug_flag, g_is_turbo, g_want_dump_memmap_flags, g_new_ppu = true, g_other_image;
struct SpcPlayer *g_spc_player;
int g_got_mismatch_count;
extern Snes *g_snes;
static uint8_t px[256 * 4 * 240];
void NORETURN Die(const char *e) { fprintf(stderr, "Die: %s\n", e); exit(3); }
void Warning(const char *e) {}
void RtlDrawPpuFrame(uint8 *b, size_t p, uint32 f) {}
void RtlApuLock(void) {}
void RtlApuUnlock(void) {}
void RtlApuQueueLock(void) {}
void RtlApuQueueUnlock(void) {}
int SDL_GetKeyFromName(const char *n) { return 0; }

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: frame_capture ROM SLOT [FRAMES]\n");
    return 1;
  }
  ParseConfigFile(NULL);
  Snes *snes = SnesInit(argv[1]);
  if (!snes) return 2;
  g_spc_player = SpcPlayer_Create();
  SpcPlayer_Initialize(g_spc_player);
  PpuBeginDrawing(snes->snes_ppu, px, 256 * 4, 0);
  Debug_Init("host");
  RtlSaveLoad(kSaveLoad_Load, atoi(argv[2]));
  const int frames = argc > 3 ? atoi(argv[3]) : 10;
  for (int i = 0; i < frames; i++) RtlRunFrame(0);
  Debug_FrameCaptureBegin();
  RtlRunFrame(0);
  int set = Debug_FrameCaptureEnd(px);
  printf("state %02X area %u room %04X -> set %02d: %s\n", (unsigned)game_state, (unsigned)area_index,
         (unsigned)room_ptr, set, Debug_LastMessage());
  return set < 0;
}
