// Host benchmark and bit-exactness check of the audio path (SPC driver + S-DSP).
// Loads a save state, runs the game (no rendering) and renders one frame of audio after
// each game frame, like the 3DS audio thread does. Prints the time spent in the DSP and
// a hash of every output sample, so a DSP optimisation can be checked to change nothing.
//
// usage: audio_bench ROM [FRAMES]          state in saves/save9.sav (see run.sh)
//        audio_bench ROM rooms [FRAMES]    boot, then every room reachable by a door
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "src/types.h"
#include "src/sm_rtl.h"
#include "src/sm_cpu_infra.h"
#include "src/snes/ppu.h"
#include "src/config.h"
#include "src/snes/snes.h"
#include "src/snes/dsp.h"
#include "src/spc_player.h"
#include "src/variables.h"
#include "sm_map.h"
#include "sm_warp.h"

bool g_debug_flag, g_is_turbo, g_want_dump_memmap_flags, g_new_ppu = true, g_other_image;
struct SpcPlayer *g_spc_player;
int g_got_mismatch_count;
extern Snes *g_snes;
void NORETURN Die(const char *e) { fprintf(stderr, "Die: %s\n", e); exit(3); }
void Warning(const char *e) {}
void RtlDrawPpuFrame(uint8 *b, size_t p, uint32 f) {}
void RtlApuLock(void) {}
void RtlApuUnlock(void) {}
void RtlApuQueueLock(void) {}
void RtlApuQueueUnlock(void) {}
int SDL_GetKeyFromName(const char *n) { return 0; }

static double Now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: audio_bench ROM [FRAMES]\n");
    return 1;
  }
  ParseConfigFile(NULL);
  Snes *snes = SnesInit(argv[1]);
  if (!snes) return 2;
  g_spc_player = SpcPlayer_Create();
  SpcPlayer_Initialize(g_spc_player);
  static uint8 px[256 * 4 * 240];
  PpuBeginDrawing(snes->snes_ppu, px, 256 * 4, 0);
  const bool rooms = argc > 2 && !strcmp(argv[2], "rooms");
  const int frames = argc > 2 + rooms ? atoi(argv[2 + rooms]) : (rooms ? 120 : 3600);
  int16 out[736 * 2];
  uint64_t hash = 1469598103934665603ull;
  double audio_us = 0;
  long active = 0, echo_frames = 0, total = 0;
  int room_count = 1, room_i = 0;
  const SmRoom *room_list = NULL;
  if (rooms) {
    // Boot with scripted inputs until gameplay (like tools/gpu-ppu-test), save, and warp
    // from there into each room.
    RtlReadSram();
    for (int i = 0; i < 20000; i++) {
      g_snes->disableRender = true;
      RtlRunFrame((i % 60 < 6) ? (i % 120 < 60 ? 0x08 : 0x100) : 0);
      RtlRenderAudio(out, 736, 2);
      if (game_state == 8 && i > 100) break;
    }
    room_list = SmMap_Rooms(&room_count);
    SmWarp_Init();
    RtlSaveLoad(kSaveLoad_Save, 7);
  } else if (!RtlSaveLoad(kSaveLoad_Load, 9)) {
    return 4;
  }
  for (room_i = 0; room_i < room_count; room_i++) {
  if (rooms) {
    if (SmWarp_DoorCount(&room_list[room_i]) == 0) continue;
    RtlSaveLoad(kSaveLoad_Load, 7);
    if (SmWarp_ToRoom(&room_list[room_i], 0) != kWarp_Ok) continue;
  }
  for (int i = 0; i < frames; i++) {
    samus_health = 99;
    g_snes->disableRender = true;
    RtlRunFrame(0);
    if (rooms) SmWarp_AfterFrame();
    total++;
    const double t0 = Now();
    RtlRenderAudio(out, 736, 2);
    audio_us += Now() - t0;
    for (int k = 0; k < 736 * 2; k++) hash = (hash ^ (uint16_t)out[k]) * 1099511628211ull;
    const Dsp *d = g_spc_player->dsp;
    for (int c = 0; c < 8; c++) active += d->channel[c].gain != 0;
    echo_frames += d->echoVolumeL || d->echoVolumeR || d->feedbackVolume;
  }
  }
  printf("frames %ld  audio %.1f us/frame  voices sounding %.2f  echo on %.0f%%  hash %016llx\n", total,
         audio_us / total, (double)active / total, 100.0 * echo_frames / total,
         (unsigned long long)hash);
  return 0;
}
