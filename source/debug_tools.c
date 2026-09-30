#include "debug_tools.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "src/types.h"
#include "src/sm_rtl.h"
#include "src/variables.h"
#include "src/snes/ppu.h"
#include "src/snes/snes.h"

#define DEBUG_DIR "debug"
#define LOG_SLOTS 10
#define DUMP_SLOTS 10
#define PERF_SLOTS 10
#define PERF_MAX_FRAMES 3600

extern Snes *g_snes;

static char g_version[48] = "";
static FILE *g_log;
static char g_log_name[64];
static char g_message[64];
static uint32_t g_frame;     // frames seen by the perf hook, used to stamp log lines

// ---- Slots ----------------------------------------------------------------

// Picks the slot to write next: the first one that does not exist yet, else
// the one whose marker file was written longest ago.
static int PickSlot(const char *marker_fmt, int slots) {
  char path[96];
  int oldest = 0;
  time_t oldest_time = 0;
  for (int i = 0; i < slots; i++) {
    snprintf(path, sizeof(path), marker_fmt, i);
    struct stat st;
    if (stat(path, &st) != 0) return i;
    if (i == 0 || st.st_mtime < oldest_time) {
      oldest = i;
      oldest_time = st.st_mtime;
    }
  }
  return oldest;
}

static FILE *OpenSlotFile(const char *fmt, int slot, const char *mode) {
  char path[96];
  snprintf(path, sizeof(path), fmt, slot);
  return fopen(path, mode);
}

static void SetMessage(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void SetMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(g_message, sizeof(g_message), fmt, ap);
  va_end(ap);
}

const char *Debug_LastMessage(void) { return g_message; }

void Debug_Init(const char *version) {
  snprintf(g_version, sizeof(g_version), "%s", version);
  mkdir(DEBUG_DIR, 0777);
}

// ---- Log ------------------------------------------------------------------

void Debug_Log(const char *fmt, ...) {
  if (!g_log) return;
  fprintf(g_log, "[%7lu] ", (unsigned long)g_frame);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(g_log, fmt, ap);
  va_end(ap);
  fputc('\n', g_log);
  fflush(g_log);
}

void Debug_LogSetEnabled(bool on) {
  if (on == (g_log != NULL)) return;
  if (!on) {
    Debug_Log("log closed");
    fclose(g_log);
    g_log = NULL;
    SetMessage("Log off");
    return;
  }
  int slot = PickSlot(DEBUG_DIR "/sm-log-%02d.txt", LOG_SLOTS);
  snprintf(g_log_name, sizeof(g_log_name), "sm-log-%02d.txt", slot);
  g_log = OpenSlotFile(DEBUG_DIR "/sm-log-%02d.txt", slot, "w");
  if (!g_log) {
    g_log_name[0] = 0;
    SetMessage("Log: cannot open file");
    return;
  }
  Debug_Log("Super Metroid 3DS %s, log started", g_version);
  SetMessage("Log -> %s", g_log_name);
}

bool Debug_LogEnabled(void) { return g_log != NULL; }
const char *Debug_LogName(void) { return g_log ? g_log_name : ""; }

void Debug_LogMark(void) {
  if (!g_log) return;
  Debug_Log("USER MARK state=%02X area=%u room=%u x=%u y=%u", (unsigned)game_state, (unsigned)area_index,
            (unsigned)room_index, (unsigned)samus_x_pos, (unsigned)samus_y_pos);
  SetMessage("Mark written");
}

// ---- Dump -----------------------------------------------------------------

static void WriteBin(const char *fmt, int slot, const void *data, size_t n) {
  FILE *f = OpenSlotFile(fmt, slot, "wb");
  if (!f) return;
  fwrite(data, 1, n, f);
  fclose(f);
}

static void WritePpuText(FILE *f, const Ppu *p) {
  fprintf(f, "mode=%u bg3priority=%d forcedBlank=%d brightness=%u overscan=%d pseudoHires=%d directColor=%d\n",
          p->mode, p->bg3priority, p->forcedBlank, p->brightness, p->overscan, p->pseudoHires, p->directColor);
  for (int i = 0; i < 4; i++) {
    const BgLayer *b = &p->bgLayer[i];
    fprintf(f, "bg%d hscroll=%u vscroll=%u tilemapAdr=%04X tileAdr=%04X wider=%d higher=%d big=%d mosaic=%d\n", i + 1,
            b->hScroll, b->vScroll, b->tilemapAdr, b->tileAdr, b->tilemapWider, b->tilemapHigher, b->bigTiles,
            b->mosaicEnabled);
  }
  for (int i = 0; i < 5; i++)
    fprintf(f, "layer%d main=%d sub=%d mainWindowed=%d subWindowed=%d\n", i, p->layer[i].mainScreenEnabled,
            p->layer[i].subScreenEnabled, p->layer[i].mainScreenWindowed, p->layer[i].subScreenWindowed);
  for (int i = 0; i < 6; i++) {
    const WindowLayer *w = &p->windowLayer[i];
    fprintf(f, "window%d w1=%d w2=%d w1inv=%d w2inv=%d logic=%u\n", i, w->window1enabled, w->window2enabled,
            w->window1inversed, w->window2inversed, w->maskLogic);
  }
  fprintf(f, "window1=%u..%u window2=%u..%u\n", p->window1left, p->window1right, p->window2left, p->window2right);
  fprintf(f, "colormath clip=%u prevent=%u addSub=%d sub=%d half=%d fixed=%u,%u,%u enabled=", p->clipMode,
          p->preventMathMode, p->addSubscreen, p->subtractColor, p->halfColor, p->fixedColorR, p->fixedColorG,
          p->fixedColorB);
  for (int i = 0; i < 6; i++) fprintf(f, "%d", p->mathEnabled[i]);
  fprintf(f, "\nobj size=%u tileAdr1=%04X tileAdr2=%04X priority=%d\n", p->objSize, p->objTileAdr1, p->objTileAdr2,
          p->objPriority);
  fprintf(f, "mode7 a=%d b=%d c=%d d=%d x=%d y=%d h=%d v=%d large=%d charFill=%d xflip=%d yflip=%d extBg=%d\n",
          p->m7matrix[0], p->m7matrix[1], p->m7matrix[2], p->m7matrix[3], p->m7matrix[4], p->m7matrix[5],
          p->m7matrix[6], p->m7matrix[7], p->m7largeField, p->m7charFill, p->m7xFlip, p->m7yFlip, p->m7extBg);
  fprintf(f, "mosaicSize=%u\n", p->mosaicSize);
}

static void WriteGameText(FILE *f) {
  fprintf(f, "version=%s\n", g_version);
  fprintf(f, "game_state=%02X area=%u room=%u room_ptr=%04X\n", (unsigned)game_state, (unsigned)area_index,
          (unsigned)room_index, (unsigned)room_ptr);
  fprintf(f, "samus x=%u y=%u pose=%02X movement=%02X xdir=%u\n", (unsigned)samus_x_pos, (unsigned)samus_y_pos,
          (unsigned)samus_pose, (unsigned)samus_movement_type, (unsigned)samus_pose_x_dir);
  fprintf(f, "health=%u/%u missiles=%u/%u super=%u pb=%u\n", (unsigned)samus_health, (unsigned)samus_max_health,
          (unsigned)samus_missiles, (unsigned)samus_max_missiles, (unsigned)samus_super_missiles,
          (unsigned)samus_power_bombs);
  fprintf(f, "layer1 x=%u y=%u frame_counter=%u\n", (unsigned)layer1_x_pos, (unsigned)layer1_y_pos,
          (unsigned)frame_counter_every_frame);
}

int Debug_DumpScreen(const uint8_t *bgra) {
  mkdir(DEBUG_DIR, 0777);
  int slot = PickSlot(DEBUG_DIR "/sm-dump-%02d-top.rgb", DUMP_SLOTS);

  // Top screen, headerless RGB8, 256x240, top to bottom.
  FILE *f = OpenSlotFile(DEBUG_DIR "/sm-dump-%02d-top.rgb", slot, "wb");
  if (!f) {
    SetMessage("Dump: cannot write debug/");
    return -1;
  }
  for (int y = 0; y < 240; y++) {
    uint8_t row[256 * 3];
    for (int x = 0; x < 256; x++) {
      const uint8_t *s = bgra + (y * 256 + x) * 4;
      row[x * 3 + 0] = s[2];
      row[x * 3 + 1] = s[1];
      row[x * 3 + 2] = s[0];
    }
    fwrite(row, 1, sizeof(row), f);
  }
  fclose(f);

  const Ppu *p = g_snes->ppu;
  WriteBin(DEBUG_DIR "/sm-dump-%02d-vram.bin", slot, p->vram, sizeof(p->vram));
  WriteBin(DEBUG_DIR "/sm-dump-%02d-cgram.bin", slot, p->cgram, sizeof(p->cgram));
  WriteBin(DEBUG_DIR "/sm-dump-%02d-oam.bin", slot, p->oam, sizeof(p->oam));
  WriteBin(DEBUG_DIR "/sm-dump-%02d-highoam.bin", slot, p->highOam, sizeof(p->highOam));
  WriteBin(DEBUG_DIR "/sm-dump-%02d-wram.bin", slot, g_ram, 0x20000);

  f = OpenSlotFile(DEBUG_DIR "/sm-dump-%02d-ppu.txt", slot, "w");
  if (f) { WritePpuText(f, p); fclose(f); }
  f = OpenSlotFile(DEBUG_DIR "/sm-dump-%02d-game.txt", slot, "w");
  if (f) { WriteGameText(f); fclose(f); }

  Debug_Log("screen dump -> set %02d", slot);
  SetMessage("Dump set %02d saved", slot);
  return slot;
}

// ---- Frame-time recorder ---------------------------------------------------

typedef struct {
  float logic, draw, audio, work, a_lock, a_spc, a_dsp, a_resample;
  uint16_t state, area, room;
  uint8_t shown;
} PerfSample;

static PerfSample *g_perf;
static int g_perf_count;

bool Debug_PerfRecording(void) { return g_perf != NULL; }

static void PerfStop(void) {
  if (!g_perf) return;
  int slot = PickSlot(DEBUG_DIR "/sm-perf-%02d.csv", PERF_SLOTS);
  FILE *f = OpenSlotFile(DEBUG_DIR "/sm-perf-%02d.csv", slot, "w");
  double sum = 0, max = 0;
  int shown = 0;
  if (f) {
    fprintf(f, "# Super Metroid 3DS %s\nframe,logic_ms,draw_ms,audio_ms,work_ms,shown,game_state,area,room,audio_lock_ms,audio_spc_ms,audio_dsp_ms,audio_resample_ms\n", g_version);
    for (int i = 0; i < g_perf_count; i++) {
      const PerfSample *s = &g_perf[i];
      fprintf(f, "%d,%.3f,%.3f,%.3f,%.3f,%u,%02X,%u,%u,%.3f,%.3f,%.3f,%.3f\n", i, s->logic, s->draw, s->audio, s->work,
              s->shown, s->state, s->area, s->room, s->a_lock, s->a_spc, s->a_dsp, s->a_resample);
      sum += s->work;
      if (s->work > max) max = s->work;
      shown += s->shown;
    }
    fclose(f);
  }
  int n = g_perf_count ? g_perf_count : 1;
  SetMessage("Perf %02d: %d fr, avg %.1f max %.1f ms", slot, g_perf_count, sum / n, max);
  Debug_Log("perf recording -> sm-perf-%02d.csv: %d frames, avg work %.2f ms, max %.2f ms, shown %d", slot,
            g_perf_count, sum / n, max, shown);
  free(g_perf);
  g_perf = NULL;
  g_perf_count = 0;
}

void Debug_PerfToggle(void) {
  if (g_perf) {
    PerfStop();
    return;
  }
  mkdir(DEBUG_DIR, 0777);
  g_perf = (PerfSample *)malloc(sizeof(PerfSample) * PERF_MAX_FRAMES);
  g_perf_count = 0;
  SetMessage(g_perf ? "Perf recording..." : "Perf: out of memory");
}

void Debug_PerfFrame(float logic_ms, float draw_ms, float audio_ms, float work_ms, bool shown, const float audio[4]) {
  g_frame++;
  if (!g_perf) return;
  PerfSample *s = &g_perf[g_perf_count++];
  s->logic = logic_ms;
  s->draw = draw_ms;
  s->audio = audio_ms;
  s->work = work_ms;
  s->a_lock = audio[0];
  s->a_spc = audio[1];
  s->a_dsp = audio[2];
  s->a_resample = audio[3];
  s->shown = shown;
  s->state = game_state;
  s->area = area_index;
  s->room = room_index;
  if (g_perf_count >= PERF_MAX_FRAMES) PerfStop();
}

// ---- Crash note ------------------------------------------------------------

// Replaces newlib's __assert_func: the stock one just aborts, which on the 3DS
// shows up as an unhelpful "generic" error. Leave a note on the SD first.
void __assert_func(const char *file, int line, const char *func, const char *expr) {
  FILE *f = fopen(DEBUG_DIR "/sm-crash.txt", "a");
  if (!f) {
    mkdir(DEBUG_DIR, 0777);
    f = fopen(DEBUG_DIR "/sm-crash.txt", "a");
  }
  if (f) {
    fprintf(f, "ASSERT %s:%d in %s(): %s\n", file, line, func ? func : "?", expr);
    fprintf(f, "  version=%s frame=%lu ", g_version, (unsigned long)g_frame);
    fprintf(f, "state=%02X area=%u room=%u", (unsigned)game_state, (unsigned)area_index, (unsigned)room_index);
    fputc('\n', f);
    fclose(f);
  }
  Debug_Log("ASSERT %s:%d in %s(): %s", file, line, func ? func : "?", expr);
  abort();
}
