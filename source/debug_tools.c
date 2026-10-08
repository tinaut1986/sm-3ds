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
#include "src/snes/dma.h"
#include "src/snes/ppu.h"
#include "src/snes/snes.h"

#define DEBUG_DIR "debug"
#define LOG_SLOTS 10
#define DUMP_SLOTS 0   // no limit: every dump gets the next number (Debug_NextSlot)
#define PERF_SLOTS 10
#define PERF_MAX_FRAMES 3600

extern Snes *g_snes;

static char g_version[48] = "";
static FILE *g_log;
static char g_log_name[64];
static char g_message[64];
static uint32_t g_frame;     // frames seen by the perf hook, used to stamp log lines

// ---- Slots ----------------------------------------------------------------

// Picks the slot to write next: the first one that does not exist yet, else the one
// after the slot written last, which debug/sm-<kind>-last.txt remembers. Not the file
// times: on the console they did not tell slots apart (libctru's stat() most likely
// leaves st_mtime at 0), so "least recently written" was always slot 00 (issue #8).
int Debug_NextSlot(const char *kind, const char *marker_fmt, int slots) {
  char path[96];
  int slot = -1;
  snprintf(path, sizeof(path), DEBUG_DIR "/sm-%s-last.txt", kind);
  int last = -1;
  FILE *f = fopen(path, "r");
  if (f) {
    if (fscanf(f, "%d", &last) != 1) last = -1;
    fclose(f);
  }
  if (slots > 0) {
    for (int i = 0; i < slots && slot < 0; i++) {
      snprintf(path, sizeof(path), marker_fmt, i);
      struct stat st;
      if (stat(path, &st) != 0) slot = i;
    }
    if (slot < 0) slot = last >= 0 ? (last + 1) % slots : 0;
  } else {
    // No limit: the number after the last one written, or the first free one when the
    // note is missing. A set is never overwritten.
    slot = last >= 0 ? last + 1 : 0;
    for (; slot < 9999; slot++) {
      snprintf(path, sizeof(path), marker_fmt, slot);
      struct stat st;
      if (stat(path, &st) != 0) break;
    }
  }
  snprintf(path, sizeof(path), DEBUG_DIR "/sm-%s-last.txt", kind);
  f = fopen(path, "w");
  if (f) {
    fprintf(f, "%d\n", slot);
    fclose(f);
  }
  return slot;
}

static char g_note[160];
void Debug_SetNote(const char *text) { snprintf(g_note, sizeof(g_note), "%s", text ? text : ""); }

void Debug_WriteNote(const char *kind, int slot) {
  if (!g_note[0]) return;
  char path[96];
  snprintf(path, sizeof(path), DEBUG_DIR "/sm-%s-%04d-note.txt", kind, slot);
  FILE *f = fopen(path, "w");
  if (f) {
    fprintf(f, "%s\n", g_note);
    fclose(f);
  }
  Debug_Log("%s %04d note: %s", kind, slot, g_note);
  g_note[0] = 0;
}

static FILE *OpenSlotFile(const char *fmt, int slot, const char *mode) {
  char path[96];
  snprintf(path, sizeof(path), fmt, slot);
  return fopen(path, mode);
}

void Debug_SetMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(g_message, sizeof(g_message), fmt, ap);
  va_end(ap);
}

const char *Debug_LastMessage(void) { return g_message; }
const char *Debug_Version(void) { return g_version; }

void Debug_Init(const char *version) {
  snprintf(g_version, sizeof(g_version), "%s", version);
  mkdir(DEBUG_DIR, 0777);
}

// ---- Log ------------------------------------------------------------------

// Buffered (the default): lines collect in RAM and reach the card a block at a time,
// when the buffer fills, on a mark, when buffering or the log is switched off, and at
// each exit step. Direct: every line is written and flushed at once, so the last line
// before a hang or crash is on disk, at the cost of an SD write per line.
// Main thread only, like every caller so far.
static bool g_log_buffered = true;
static char g_log_buf[16384];
static size_t g_log_len;

void Debug_LogFlush(void) {
  if (!g_log || !g_log_len) return;
  fwrite(g_log_buf, 1, g_log_len, g_log);
  fflush(g_log);
  g_log_len = 0;
}

void Debug_Log(const char *fmt, ...) {
  if (!g_log) return;
  char line[512];
  int n = snprintf(line, sizeof(line), "[%7lu] ", (unsigned long)g_frame);
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line + n, sizeof(line) - n - 1, fmt, ap);
  va_end(ap);
  n = (int)strlen(line);
  line[n++] = '\n';
  if (g_log_len + n > sizeof(g_log_buf)) Debug_LogFlush();
  memcpy(g_log_buf + g_log_len, line, n);
  g_log_len += n;
  if (!g_log_buffered) Debug_LogFlush();
}

void Debug_LogSetBuffered(bool on) {
  if (!on) Debug_LogFlush();
  g_log_buffered = on;
  Debug_Log("log %s", on ? "buffered" : "direct");
}

bool Debug_LogBuffered(void) { return g_log_buffered; }

void Debug_LogSetEnabled(bool on) {
  if (on == (g_log != NULL)) return;
  if (!on) {
    Debug_Log("log closed");
    Debug_LogFlush();
    fclose(g_log);
    g_log = NULL;
    Debug_SetMessage("Log off");
    return;
  }
  int slot = Debug_NextSlot("log", DEBUG_DIR "/sm-log-%02d.txt", LOG_SLOTS);
  snprintf(g_log_name, sizeof(g_log_name), "sm-log-%02d.txt", slot);
  g_log = OpenSlotFile(DEBUG_DIR "/sm-log-%02d.txt", slot, "w");
  if (!g_log) {
    g_log_name[0] = 0;
    Debug_SetMessage("Log: cannot open file");
    return;
  }
  g_log_len = 0;
  Debug_Log("Super Metroid 3DS %s, log started (%s)", g_version, g_log_buffered ? "buffered" : "direct");
  Debug_SetMessage("Log -> %s", g_log_name);
}

bool Debug_LogEnabled(void) { return g_log != NULL; }
const char *Debug_LogName(void) { return g_log ? g_log_name : ""; }

void Debug_LogMark(void) {
  if (!g_log) return;
  Debug_Log("USER MARK state=%02X area=%u room=%u x=%u y=%u", (unsigned)game_state, (unsigned)area_index,
            (unsigned)room_index, (unsigned)samus_x_pos, (unsigned)samus_y_pos);
  Debug_LogFlush();
  Debug_SetMessage("Mark written");
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

// Headerless RGB8, 256x240, top to bottom.
static bool WriteRgb(const char *fmt, int slot, const uint8_t *bgra) {
  FILE *f = OpenSlotFile(fmt, slot, "wb");
  if (!f) return false;
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
  return true;
}

void Debug_DumpExtraImage(int slot, const char *suffix, const uint8_t *bgra) {
  char fmt[64];
  snprintf(fmt, sizeof(fmt), DEBUG_DIR "/sm-dump-%%04d-%s.rgb", suffix);
  WriteRgb(fmt, slot, bgra);
}

static void (*g_obj_dump)(FILE *f);
void Debug_SetObjDump(void (*fn)(FILE *f)) { g_obj_dump = fn; }

int Debug_DumpScreen(const uint8_t *bgra) {
  mkdir(DEBUG_DIR, 0777);
  int slot = Debug_NextSlot("dump", DEBUG_DIR "/sm-dump-%04d-top.rgb", DUMP_SLOTS);
  if (!WriteRgb(DEBUG_DIR "/sm-dump-%04d-top.rgb", slot, bgra)) {
    Debug_SetMessage("Dump: cannot write debug/");
    return -1;
  }
  const Ppu *p = g_snes->ppu;
  WriteBin(DEBUG_DIR "/sm-dump-%04d-vram.bin", slot, p->vram, sizeof(p->vram));
  WriteBin(DEBUG_DIR "/sm-dump-%04d-cgram.bin", slot, p->cgram, sizeof(p->cgram));
  WriteBin(DEBUG_DIR "/sm-dump-%04d-oam.bin", slot, p->oam, sizeof(p->oam));
  WriteBin(DEBUG_DIR "/sm-dump-%04d-highoam.bin", slot, p->highOam, sizeof(p->highOam));
  WriteBin(DEBUG_DIR "/sm-dump-%04d-wram.bin", slot, g_ram, 0x20000);

  FILE *f = OpenSlotFile(DEBUG_DIR "/sm-dump-%04d-ppu.txt", slot, "w");
  if (f) { WritePpuText(f, p); fclose(f); }
  f = OpenSlotFile(DEBUG_DIR "/sm-dump-%04d-game.txt", slot, "w");
  if (f) { WriteGameText(f); fclose(f); }
  f = OpenSlotFile(DEBUG_DIR "/sm-dump-%04d-obj.txt", slot, "w");
  if (f) { if (g_obj_dump) g_obj_dump(f); fclose(f); }
  // Files left in this slot by an older set would pass for this set's.
  char path[96];
  snprintf(path, sizeof(path), DEBUG_DIR "/sm-dump-%04d-frame.txt", slot);
  remove(path);
  snprintf(path, sizeof(path), DEBUG_DIR "/sm-dump-%04d-gpu.rgb", slot);
  remove(path);

  // A note left by an older set in this slot would pass for this set's.
  snprintf(path, sizeof(path), DEBUG_DIR "/sm-dump-%04d-note.txt", slot);
  remove(path);
  Debug_WriteNote("dump", slot);
  Debug_Log("screen dump -> set %04d", slot);
  Debug_SetMessage("Dump set %04d saved", slot);
  return slot;
}

// ---- Frame capture ---------------------------------------------------------
// Every write to a PPU register ($2100-$213F) during one frame, stamped with the
// scanline. The game logic runs first, in vblank (stamped -1); then DrawFrameToPpu
// renders lines 0..224, and HDMA and the IRQ handler write between them. HDMA writes
// at the end of line N, so they take effect from line N+1; the IRQ handler runs after
// its line has been drawn and is stamped with the next one.

typedef struct {
  int16_t line;
  uint8_t reg;      // $21xx low byte
  uint8_t val;      // last value written
  uint16_t count;   // consecutive writes to the same data port, folded into one entry
} FrameWrite;

enum { kFrameWritesMax = 16384 };
static FrameWrite *g_fw;
static int g_fw_count;
static bool g_fw_overflow;

static const char *const kPpuRegNames[0x34] = {
  "INIDISP", "OBSEL", "OAMADDL", "OAMADDH", "OAMDATA", "BGMODE", "MOSAIC", "BG1SC",
  "BG2SC", "BG3SC", "BG4SC", "BG12NBA", "BG34NBA", "BG1HOFS", "BG1VOFS", "BG2HOFS",
  "BG2VOFS", "BG3HOFS", "BG3VOFS", "BG4HOFS", "BG4VOFS", "VMAIN", "VMADDL", "VMADDH",
  "VMDATAL", "VMDATAH", "M7SEL", "M7A", "M7B", "M7C", "M7D", "M7X",
  "M7Y", "CGADD", "CGDATA", "W12SEL", "W34SEL", "WOBJSEL", "WH0", "WH1",
  "WH2", "WH3", "WBGLOG", "WOBJLOG", "TM", "TS", "TMW", "TSW",
  "CGWSEL", "CGADSUB", "COLDATA", "SETINI",
};

// Data ports: a DMA upload writes them thousands of times in a row.
static bool IsDataPort(uint8_t reg) { return reg == 0x04 || reg == 0x18 || reg == 0x19 || reg == 0x22; }

static void FrameWriteHook(uint8_t reg, uint8_t val) {
  const int16_t line = g_snes->inVblank ? -1 : (int16_t)g_snes->vPos;
  if (g_fw_count > 0) {
    FrameWrite *last = &g_fw[g_fw_count - 1];
    if (last->line == line && last->reg == reg && IsDataPort(reg) && last->count < 0xFFFF) {
      last->val = val;
      last->count++;
      return;
    }
  }
  if (g_fw_count >= kFrameWritesMax) {
    g_fw_overflow = true;
    return;
  }
  g_fw[g_fw_count++] = (FrameWrite){ line, reg, val, 1 };
}

bool Debug_FrameCaptureBegin(void) {
  free(g_fw);
  g_fw = (FrameWrite *)malloc(sizeof(FrameWrite) * kFrameWritesMax);
  g_fw_count = 0;
  g_fw_overflow = false;
  if (!g_fw) {
    Debug_SetMessage("Frame dump: out of memory");
    return false;
  }
  g_ppu_write_hook = FrameWriteHook;
  return true;
}

static const char *RegName(uint8_t reg) { return reg < 0x34 ? kPpuRegNames[reg] : "?"; }

static void WriteFrameText(FILE *f, int slot) {
  fprintf(f, "# Super Metroid 3DS %s, frame capture, dump set %04d\n", g_version, slot);
  fprintf(f, "# game_state=%02X area=%u room=%u room_ptr=%04X\n", (unsigned)game_state, (unsigned)area_index,
          (unsigned)room_index, (unsigned)room_ptr);
  fprintf(f, "# %d entries%s. line -1 = game logic (vblank); HDMA at line N applies from N+1.\n", g_fw_count,
          g_fw_overflow ? " (BUFFER FULL, later writes lost)" : "");

  // HDMA/DMA channel setup as the frame left it: the table start (aAdr) does not move.
  const Dma *dma = g_snes->dma;
  fprintf(f, "\n[hdma]\n");
  for (int i = 0; i < 8; i++) {
    const DmaChannel *c = &dma->channel[i];
    fprintf(f, "ch%d hdma=%d mode=%u dest=21%02X %-8s table=%02X:%04X indirect=%d ind_bank=%02X\n", i,
            c->hdmaActive, c->mode, c->bAdr, RegName(c->bAdr), c->aBank, c->aAdr, c->indirect, c->indBank);
  }

  // Per register: writes in vblank, writes during the visible lines, and on how many
  // different lines. A register with visible-line writes is a per-scanline effect.
  fprintf(f, "\n[summary] reg name vblank_writes line_writes lines first..last\n");
  for (int reg = 0; reg < 0x40; reg++) {
    int vbl = 0, vis = 0, lines = 0, first = -1, last = -1, prev_line = -2;
    for (int i = 0; i < g_fw_count; i++) {
      const FrameWrite *w = &g_fw[i];
      if (w->reg != reg) continue;
      if (w->line < 0) { vbl += w->count; continue; }
      vis += w->count;
      if (w->line != prev_line) { lines++; prev_line = w->line; }
      if (first < 0) first = w->line;
      last = w->line;
    }
    if (vbl || vis) {
      fprintf(f, "21%02X %-8s %6d %6d %4d", reg, RegName((uint8_t)reg), vbl, vis, lines);
      if (vis) fprintf(f, " %d..%d", first, last);
      fputc('\n', f);
    }
  }

  fprintf(f, "\n[writes] line reg name value [xcount]\n");
  for (int i = 0; i < g_fw_count; i++) {
    const FrameWrite *w = &g_fw[i];
    fprintf(f, "%4d 21%02X %-8s %02X", w->line, w->reg, RegName(w->reg), w->val);
    if (w->count > 1) fprintf(f, " x%u", w->count);
    fputc('\n', f);
  }
}

int Debug_FrameCaptureEnd(const uint8_t *bgra) {
  g_ppu_write_hook = NULL;
  if (!g_fw) return -1;
  int slot = Debug_DumpScreen(bgra);
  if (slot >= 0) {
    FILE *f = OpenSlotFile(DEBUG_DIR "/sm-dump-%04d-frame.txt", slot, "w");
    if (f) {
      WriteFrameText(f, slot);
      fclose(f);
      Debug_Log("frame capture -> set %04d, %d writes", slot, g_fw_count);
      Debug_SetMessage("Frame dump set %04d (%d writes)", slot, g_fw_count);
    } else {
      Debug_SetMessage("Frame dump: cannot write debug/");
    }
  }
  free(g_fw);
  g_fw = NULL;
  return slot;
}

// ---- Frame-time recorder ---------------------------------------------------

typedef struct {
  float logic, draw, audio, work, a_lock, a_spc, a_dsp, a_resample;
  uint16_t state, area, room;
  uint8_t shown;
  DebugPerfExtra x;
} PerfSample;

static PerfSample *g_perf;
static int g_perf_count;

bool Debug_PerfRecording(void) { return g_perf != NULL; }

static void PerfStop(void) {
  if (!g_perf) return;
  int slot = Debug_NextSlot("perf", DEBUG_DIR "/sm-perf-%02d.csv", PERF_SLOTS);
  FILE *f = OpenSlotFile(DEBUG_DIR "/sm-perf-%02d.csv", slot, "w");
  double sum = 0, max = 0;
  int shown = 0;
  if (f) {
    fprintf(f, "# Super Metroid 3DS %s\nframe,logic_ms,draw_ms,audio_ms,work_ms,shown,game_state,area,room,audio_lock_ms,audio_spc_ms,audio_dsp_ms,audio_resample_ms,"
                "gpu_build_ms,gpu_wait_ms,gpu_submit_ms,lines_ms,diff_ms,sprites_ms,bg_ms,tiles,quads,bands,tex_copy_ms,tex_flush_ms,tex_runs,tex_kb,gpu_draw_ms,gpu_proc_ms,cmdbuf,dp_ms,ui_ms,present_ms,present_wait_ms,tiles_reused,why_fresh,why_map,why_pal,why_char,why_plane,tiles_deferred,tiles_pending,gpu_test,eyes,slider,submit_tex_ms,submit_eyes_ms,submit_end_ms\n", g_version);
    for (int i = 0; i < g_perf_count; i++) {
      const PerfSample *s = &g_perf[i];
      const DebugPerfExtra *x = &s->x;
      fprintf(f, "%d,%.3f,%.3f,%.3f,%.3f,%u,%02X,%u,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%.3f,%.3f,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.2f,%.3f,%.3f,%.3f\n",
              i, s->logic, s->draw, s->audio, s->work, s->shown, s->state, s->area, s->room, s->a_lock, s->a_spc, s->a_dsp,
              s->a_resample, x->build_ms, x->wait_ms, x->submit_ms, x->lines_ms, x->diff_ms, x->sprites_ms, x->bg_ms, x->tiles,
              x->quads, x->bands, x->tex_copy_ms, x->tex_flush_ms, x->tex_runs, x->tex_kb, x->gpu_draw_ms, x->gpu_proc_ms, x->cmdbuf, x->draw_present_ms, x->ui_ms, x->present_ms, x->present_wait_ms, x->tiles_reused, x->why_fresh, x->why_map, x->why_pal, x->why_char, x->why_plane, x->tiles_deferred, x->tiles_pending, x->gpu_test, x->eyes, x->slider, x->submit_tex_ms, x->submit_eyes_ms, x->submit_end_ms);
      sum += s->work;
      if (s->work > max) max = s->work;
      shown += s->shown;
    }
    fclose(f);
  }
  int n = g_perf_count ? g_perf_count : 1;
  Debug_SetMessage("Perf %02d: %d fr, avg %.1f max %.1f ms", slot, g_perf_count, sum / n, max);
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
  Debug_SetMessage(g_perf ? "Perf recording..." : "Perf: out of memory");
}

void Debug_PerfFrame(float logic_ms, float draw_ms, float audio_ms, float work_ms, bool shown, const float audio[4],
                     const DebugPerfExtra *x) {
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
  s->x = *x;
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
  Debug_LogFlush();
  abort();
}
