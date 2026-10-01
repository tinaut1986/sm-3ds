// Host check of the GPU renderer's frame building (source/gpu_ppu.c): every tested
// frame is drawn three ways and compared.
//   A  the CPU renderer as the game normally runs it
//   B  the same frame captured line by line, then drawn by the CPU from the capture
//      (ppu_replayLines): must equal A, or the capture misses a register
//   C  the capture turned into a GpuFrame and drawn by gpu_ppu_ref.c: must equal B
// A failing frame is written as a PPM (B | C | difference).
//
// usage: gpu_ppu_test ROM state FILE.sav [FRAMES]      frames from a console state
//        gpu_ppu_test ROM rooms [FRAMES] [ROOM_HEX]   boot, warp into every room
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "src/types.h"
#include "src/sm_rtl.h"
#include "src/sm_cpu_infra.h"
#include "src/config.h"
#include "src/snes/ppu.h"
#include "src/snes/snes.h"
#include "src/spc_player.h"
#include "src/variables.h"
#include "src/funcs.h"
#include "gpu_ppu.h"
#include "gpu_ppu_ref.h"
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

bool GpuBackend_TexCreate(GpuTex *t, int w, int h) {
  t->px = calloc((size_t)w * h, 2);
  t->w = w;
  t->h = h;
  return t->px != NULL;
}
void GpuBackend_TexFree(GpuTex *t) { free(t->px); t->px = NULL; }
void GpuBackend_TexWritten(GpuTex *t, int y0, int y1) {}

enum { kPitch = 256 * 4 };
static uint8_t g_px[kPitch * 240], g_a[kPitch * 240], g_b[kPitch * 240], g_c[kPitch * 240];
static PpuLineCapture g_cap;
static int g_input;   // controller bits for TestFrame's frames
static GpuFrame g_frame;
static int g_frames, g_capture_bad, g_gpu_bad, g_refused, g_dumped;
static long g_tiles, g_composed, g_quads, g_max_quads, g_composed_frames;
static double g_build_us, g_build_max_us, g_replay_us;
static char g_refuse_reasons[16][64];
static int g_refuse_counts[16];

static void NoteRefusal(const char *why) {
  for (int i = 0; i < 16; i++) {
    if (!g_refuse_reasons[i][0]) snprintf(g_refuse_reasons[i], 64, "%s", why);
    if (!strcmp(g_refuse_reasons[i], why)) { g_refuse_counts[i]++; return; }
  }
}

// Pixels that differ in rows 0..223; also their bounding box.
static int Diff(const uint8_t *a, const uint8_t *b, int *bx0, int *by0, int *bx1, int *by1) {
  int n = 0;
  *bx0 = *by0 = 999, *bx1 = *by1 = -1;
  for (int y = 0; y < kGpuRows; y++)
    for (int x = 0; x < 256; x++) {
      const uint32_t pa = *(const uint32_t *)&a[y * kPitch + x * 4] & 0xffffff;
      const uint32_t pb = *(const uint32_t *)&b[y * kPitch + x * 4] & 0xffffff;
      if (pa == pb) continue;
      n++;
      if (x < *bx0) *bx0 = x;
      if (y < *by0) *by0 = y;
      if (x > *bx1) *bx1 = x;
      if (y > *by1) *by1 = y;
    }
  return n;
}

static void DumpTriptych(const char *name, const uint8_t *b, const uint8_t *c) {
  FILE *f = fopen(name, "wb");
  if (!f) return;
  fprintf(f, "P6\n768 224\n255\n");
  for (int y = 0; y < kGpuRows; y++)
    for (int x = 0; x < 768; x++) {
      const int k = x & 255;
      const uint8_t *pb = &b[y * kPitch + k * 4], *pc = &c[y * kPitch + k * 4];
      uint8_t rgb[3];
      if (x < 256) rgb[0] = pb[2], rgb[1] = pb[1], rgb[2] = pb[0];
      else if (x < 512) rgb[0] = pc[2], rgb[1] = pc[1], rgb[2] = pc[0];
      else {
        const bool d = memcmp(pb, pc, 3) != 0;
        rgb[0] = d ? 255 : pb[2] / 4, rgb[1] = d ? 0 : pb[1] / 4, rgb[2] = d ? 255 : pb[0] / 4;
      }
      fwrite(rgb, 1, 3, f);
    }
  fclose(f);
}

// Runs one frame and checks it. `check_capture` also runs it a second time the
// normal way (from a save state) to compare the capture replay against it.
static void TestFrame(const char *label, bool check_capture) {
  if (check_capture) RtlSaveLoad(kSaveLoad_Save, 8);
  if (check_capture) {
    g_snes->disableRender = false;
    RtlRunFrame(g_input);
    memcpy(g_a, g_px, sizeof(g_a));
    RtlSaveLoad(kSaveLoad_Load, 8);
  }
  g_ppu_line_capture = &g_cap;
  g_snes->disableRender = false;
  RtlRunFrame(g_input);
  g_ppu_line_capture = NULL;
  memset(g_px, 0, sizeof(g_px));
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  ppu_replayLines(g_snes->ppu, &g_cap, 1, g_cap.last_line);
  clock_gettime(CLOCK_MONOTONIC, &t1);
  g_replay_us += (t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3;
  memcpy(g_b, g_px, sizeof(g_b));
  g_frames++;
  int x0, y0, x1, y1, n;
  if (check_capture && (n = Diff(g_a, g_b, &x0, &y0, &x1, &y1))) {
    g_capture_bad++;
    printf("%s: CAPTURE REPLAY differs from the normal render: %d px in %d,%d..%d,%d\n", label, n, x0, y0, x1, y1);
  }
  const char *why;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  const bool built = GpuPpu_BuildFrame(g_snes->ppu, &g_cap, &g_frame, &why);
  clock_gettime(CLOCK_MONOTONIC, &t1);
  const double us = (t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3;
  g_build_us += us;
  if (us > g_build_max_us) g_build_max_us = us;
  if (!built) {
    g_refused++;
    NoteRefusal(why);
    if (getenv("MODE7_SURVEY") && strstr(why, "mode")) {
      // One line per distinct Mode 7 setup: modes on the frame's lines, whether the
      // matrix changes between lines, field/fill/extbg, layers and colour math.
      static char seen7[128][160];
      int modes = 0, m7lines = 0, matrix_changes = 0, first = 0;
      for (int l = 1; l <= g_cap.last_line && l < kPpuCaptureLines; l++) {
        const PpuLineState *t = &g_cap.line[l];
        if (t->forcedBlank) continue;
        modes |= 1 << t->mode;
        if (t->mode != 7) continue;
        if (!first) first = l;
        if (m7lines && memcmp(t->m7matrix, g_cap.line[first].m7matrix, 8 * 2)) matrix_changes++;
        m7lines++;
      }
      if (first) {
        const PpuLineState *m = &g_cap.line[first];
        char key[160];
        snprintf(key, sizeof(key), "modes %02x m7 lines %d matrix varies on %d | large %d fill %d extbg %d flip %d%d | main %02x sub %02x math %d%d%d%d%d%d add %d",
                 modes, m7lines, matrix_changes ? 1 : 0, m->m7largeField, m->m7charFill, m->m7extBg, m->m7xFlip,
                 m->m7yFlip, m->screenEnabled[0], m->screenEnabled[1], m->mathEnabled[0], m->mathEnabled[1],
                 m->mathEnabled[2], m->mathEnabled[3], m->mathEnabled[4], m->mathEnabled[5], m->addSubscreen);
        int k;
        for (k = 0; k < 128 && seen7[k][0] && strcmp(seen7[k], key); k++) {}
        if (k < 128 && !seen7[k][0]) {
          strcpy(seen7[k], key);
          printf("%s: %s | m7 %d %d %d %d %d %d\n", label, key, m->m7matrix[0], m->m7matrix[1], m->m7matrix[2],
                 m->m7matrix[3], m->m7matrix[4], m->m7matrix[5]);
        }
      }
    }
    if (getenv("WINDOW_SURVEY") && strstr(why, "window")) {
      // One line per distinct window setup: which layers are windowed where, the
      // colour-window modes, and the window bounds on a sample of lines.
      static char seen[64][96];
      char key[96];
      const PpuLineState *m = &g_cap.line[112];
      snprintf(key, sizeof(key), "sel %08x mainW %02x subW %02x main %02x sub %02x clip %d prevent %d", m->windowsel,
               m->screenWindowed[0], m->screenWindowed[1], m->screenEnabled[0], m->screenEnabled[1], m->clipMode,
               m->preventMathMode);
      int k;
      for (k = 0; k < 64 && seen[k][0] && strcmp(seen[k], key); k++) {}
      if (k < 64 && !seen[k][0]) {
        strcpy(seen[k], key);
        printf("%s: %s: %s | w1 %d..%d w2 %d..%d at line 112\n", label, why, key, m->window1left, m->window1right,
               m->window2left, m->window2right);
      }
    }
    return;
  }
  const GpuPpuStats *st = GpuPpu_LastStats();
  g_tiles += st->tiles_decoded;
  g_composed += st->screen_rows_composed;
  g_composed_frames += st->screen_rows_composed > 0;
  if (getenv("SHOW_COMPOSE") && st->screen_rows_composed) printf("%s: composed %d rows\n", label, st->screen_rows_composed);
  g_quads += g_frame.quad_count;
  if (g_frame.quad_count > g_max_quads) g_max_quads = g_frame.quad_count;
  memset(g_c, 0, sizeof(g_c));
  GpuRef_DrawFrame(&g_frame, g_c, kPitch);
  if ((n = Diff(g_b, g_c, &x0, &y0, &x1, &y1))) {
    g_gpu_bad++;
    printf("%s: GPU differs: %d px in %d,%d..%d,%d (%d bands, %d quads)\n", label, n, x0, y0, x1, y1,
           g_frame.band_count, g_frame.quad_count);
    if (g_dumped < 20) {
      char name[64];
      snprintf(name, sizeof(name), "diff-%02d.ppm", g_dumped++);
      DumpTriptych(name, g_b, g_c);
      printf("  -> %s\n", name);
    }
  }
}

static int g_music_rooms, g_music_stuck, g_music_wrong;

static void Report(void) {
  if (g_music_rooms)
    printf("MUSIC rooms %d, music queue stuck in %d, wrong music bank in %d\n", g_music_rooms, g_music_stuck,
           g_music_wrong);
  printf("RESULT frames %d, capture mismatches %d, GPU mismatches %d, refused %d\n", g_frames, g_capture_bad, g_gpu_bad,
         g_refused);
  // Game state at the end, for tools/test: any change to the game logic changes it.
  uint64_t h = 1469598103934665603ull;
  for (int i = 0; i < 0x20000; i++) h = (h ^ g_ram[i]) * 1099511628211ull;
  printf("WRAM hash %016llx (game_state %02x room %04x)\n", (unsigned long long)h, (unsigned)game_state,
         (unsigned)room_ptr);
  const int drawn = g_frames - g_refused;
  if (drawn)
    printf("  per drawn frame: %.1f quads (max %ld), %.1f tiles decoded, %ld frames composed rows on the CPU (%.1f rows each)\n",
           (double)g_quads / drawn, g_max_quads, (double)g_tiles / drawn, g_composed_frames,
           g_composed_frames ? (double)g_composed / g_composed_frames : 0.0);
  if (g_frames)
    printf("  host time per frame: build %.0f us (max %.0f), CPU renderer %.0f us\n", g_build_us / g_frames,
           g_build_max_us, g_replay_us / g_frames);
  for (int i = 0; i < 16 && g_refuse_reasons[i][0]; i++) printf("  refused %d: %s\n", g_refuse_counts[i], g_refuse_reasons[i]);
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: gpu_ppu_test ROM state FILE.sav [FRAMES] | gpu_ppu_test ROM rooms [FRAMES] [ROOM]\n");
    return 1;
  }
  ParseConfigFile(NULL);
  Snes *snes = SnesInit(argv[1]);
  if (!snes) return 2;
  g_spc_player = SpcPlayer_Create();
  SpcPlayer_Initialize(g_spc_player);
  PpuBeginDrawing(snes->snes_ppu, g_px, kPitch, 0);
  if (!strcmp(argv[2], "state")) {
    // The state is copied to saves/save9.sav by run.sh.
    if (!RtlSaveLoad(kSaveLoad_Load, 9)) return 4;
    GpuPpu_Invalidate();
    const int frames = argc > 4 ? atoi(argv[4]) : 60;
    for (int i = 0; i < frames; i++) {
      char label[32];
      snprintf(label, sizeof(label), "frame %d", i);
      if (i % 50 == 0) printf("%s\n", label);
      TestFrame(label, i % 10 == 0);
    }
    Report();
    return g_capture_bad || g_gpu_bad;
  }
  if (!strcmp(argv[2], "pbomb")) {
    // A power bomb explodes where Samus stands; every frame is checked, and the normal
    // render is written as pb-NNN.ppm (CPU | GPU list | difference).
    if (!RtlSaveLoad(kSaveLoad_Load, 9)) return 4;
    GpuPpu_Invalidate();
    const int frames = argc > 4 ? atoi(argv[4]) : 240;
    for (int i = 0; i < frames; i++) {
      if (i == 2) {
        power_bomb_explosion_x_pos = samus_x_pos;
        power_bomb_explosion_y_pos = samus_y_pos;
        EnableHdmaObjects();
        SpawnPowerBombExplosion();
      }
      char label[32];
      snprintf(label, sizeof(label), "frame %d", i);
      TestFrame(label, true);
      char name[32];
      snprintf(name, sizeof(name), "pb-%03d.ppm", i);
      DumpTriptych(name, g_a, g_c);
    }
    Report();
    return g_capture_bad || g_gpu_bad;
  }
  // rooms: boot with scripted inputs until gameplay, then warp into every room.
  // boot: the same boot, every frame tested from power-on: title, file select (with
  // saves/sm.srm the first file is continued), loading the game and what follows.
  RtlReadSram();
  enum { A = 0x100, START = 0x08 };
  if (!strcmp(argv[2], "boot")) {
    const int frames = argc > 3 ? atoi(argv[3]) : 1500;
    int soft_resets = 0;
    for (int i = 0; i < frames; i++) {
      char label[48];
      snprintf(label, sizeof(label), "boot frame %d (state %02x room %04x)", i, (unsigned)game_state, (unsigned)room_ptr);
      if (i % 100 == 0) printf("%s\n", label);
      g_input = (i % 60 < 6) ? (i % 120 < 60 ? START : A) : 0;
      if (game_state == 8) g_input = 0;
      // CERES_BOOM: once in the Ceres elevator room (DF45), jump to "made it to the
      // elevator" so the escape cutscene (Ceres explodes) plays.
      if (getenv("CERES_BOOM") && game_state == 8 && room_ptr == 0xDF45) {
        printf("frame %d: game_state 8 -> 0x20 in DF45\n", i);
        game_state = 0x20;
      }
      // MASH_B=N: from the file-select map on, B on every other frame for N frames.
      static int mash_left = -1;
      // MASH_B_STATE: the game state to start at (default 5, the file-select map)
      const int mash_state = getenv("MASH_B_STATE") ? (int)strtol(getenv("MASH_B_STATE"), 0, 16) : 5;
      if (getenv("MASH_B") && mash_left < 0 && game_state == mash_state) mash_left = atoi(getenv("MASH_B"));
      if (mash_left > 0) {
        mash_left--;
        g_input = (i & 1) ? 0x01 : 0;
        if (i % 20 == 0) printf("mash: frame %d state %02x coroutine %04x\n", i, (unsigned)game_state, (unsigned)coroutine_state_0);
      }
      TestFrame(label, i % 10 == 0);
      soft_resets += game_state == 0xffff;
    }
    printf("soft resets seen: %d\n", soft_resets);
    Report();
    return g_capture_bad || g_gpu_bad;
  }
  for (int i = 0; i < 20000; i++) {
    g_snes->disableRender = true;
    RtlRunFrame((i % 60 < 6) ? (i % 120 < 60 ? START : A) : 0);
    if (game_state == 8 && i > 100) break;
  }
  const int frames = argc > 3 ? atoi(argv[3]) : 20;
  const int only = argc > 4 ? (int)strtol(argv[4], 0, 16) : 0;
  int n;
  const SmRoom *rooms = SmMap_Rooms(&n);
  SmWarp_Init();
  RtlSaveLoad(kSaveLoad_Save, 7);
  for (int r = 0; r < n; r++) {
    if (only && rooms[r].header != only) continue;
    if (SmWarp_DoorCount(&rooms[r]) == 0) continue;
    if (!getenv("MUSIC_CHAIN")) {   // MUSIC_CHAIN: warp from wherever the last warp left
      RtlSaveLoad(kSaveLoad_Load, 7);
      GpuPpu_Invalidate();
    }
    for (int k = 0; k < 5; k++) { g_snes->disableRender = true; RtlRunFrame(0); }
    if (SmWarp_ToRoom(&rooms[r], 0) != kWarp_Ok) continue;
    for (int k = 0; k < 400 && !(game_state == 8 && room_ptr == rooms[r].header); k++) {
      samus_health = 99;
      g_snes->disableRender = true;
      RtlRunFrame(0);
      SmWarp_AfterFrame();
    }
    if (getenv("MUSIC_CHECK")) {
      // The music queue after a warp: pending delays with read == write and no timer is
      // the state that hangs the next door (DoorTransition_WaitForMusicToClear).
      int stuck_at = -1;
      const int settle = getenv("MUSIC_SETTLE") ? atoi(getenv("MUSIC_SETTLE")) : 600;
      for (int k = 0; k < settle && stuck_at < 0; k++) {
        samus_health = 99;
        g_snes->disableRender = true;
        RtlRunFrame(0);
        SmWarp_AfterFrame();
        if (getenv("MUSIC_TRACE"))
          printf("  %04X f%d state %02x: r%d w%d timer %d entry %04x | %d:%d %d:%d %d:%d %d:%d %d:%d %d:%d %d:%d %d:%d\n",
                 rooms[r].header, k, (unsigned)game_state, music_queue_read_pos, music_queue_write_pos, music_timer,
                 music_entry, music_queue_track[0], music_queue_delay[0], music_queue_track[1], music_queue_delay[1],
                 music_queue_track[2], music_queue_delay[2], music_queue_track[3], music_queue_delay[3],
                 music_queue_track[4], music_queue_delay[4], music_queue_track[5], music_queue_delay[5],
                 music_queue_track[6], music_queue_delay[6], music_queue_track[7], music_queue_delay[7]);
        if (HasQueuedMusic() && music_queue_read_pos == music_queue_write_pos && music_timer == 0) stuck_at = k;
      }
      g_music_rooms++;
      // After settling, the area's music bank must be the one uploaded.
      if (settle >= 600 && room_music_data_index && music_data_index != room_music_data_index) {
        g_music_wrong++;
        printf("room %04X: music bank %02x, room wants %02x\n", rooms[r].header, music_data_index, room_music_data_index);
      }
      if (stuck_at >= 0) {
        g_music_stuck++;
        printf("room %04X: music queue stuck %d frames after arrival (delays %d %d %d %d %d %d %d %d, pos %d)\n",
               rooms[r].header, stuck_at, music_queue_delay[0], music_queue_delay[1], music_queue_delay[2],
               music_queue_delay[3], music_queue_delay[4], music_queue_delay[5], music_queue_delay[6],
               music_queue_delay[7], music_queue_read_pos);
      }
      continue;
    }
    for (int k = 0; k < frames; k++) {
      samus_health = 99;
      if (getenv("PBOMB") && k == 2) {   // a power bomb where Samus stands
        power_bomb_explosion_x_pos = samus_x_pos;
        power_bomb_explosion_y_pos = samus_y_pos;
        EnableHdmaObjects();
        SpawnPowerBombExplosion();
      }
      char label[48];
      snprintf(label, sizeof(label), "room %04X frame %d", rooms[r].header, k);
      TestFrame(label, k == 0);
      SmWarp_AfterFrame();
    }
  }
  Report();
  return g_capture_bad || g_gpu_bad;
}
