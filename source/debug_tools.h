// On-device diagnostics: log to SD, state dumps, frame-time recorder, crash note
// (the scene recorder is scene_rec.h).
//
// Everything lands in `debug/` inside the data folder (the game's working
// directory), so it can be fetched over FTP. Captures rotate over a fixed
// number of slots (Debug_NextSlot): the first free one, else the one after the
// slot written last, which debug/sm-<kind>-last.txt names.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Creates debug/ if needed. Safe to call more than once.
void Debug_Init(const char *version);

// --- Log -------------------------------------------------------------------
// Off until enabled. Buffered by default: lines reach the SD card in 16 KB blocks
// (and on a mark, an exit step, an assert). Direct mode writes each line at once,
// for hangs that would swallow what is still in RAM.
void Debug_LogSetEnabled(bool on);
void Debug_LogSetBuffered(bool on);
bool Debug_LogBuffered(void);
void Debug_LogFlush(void);
bool Debug_LogEnabled(void);
const char *Debug_LogName(void);   // current log file name, "" if none
void Debug_Log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void Debug_LogMark(void);          // "USER MARK" line with frame and room

// --- Dumps -----------------------------------------------------------------
// Writes one rotating set: top screen RGB, VRAM, CGRAM, OAM, PPU registers,
// WRAM and a game-state text. `bgra` is the 256x240 PPU output. Returns the
// set number (0-based), or -1 on failure.
int Debug_DumpScreen(const uint8_t *bgra);

// Adds sm-dump-NN-<suffix>.rgb (256x240 like -top.rgb) to dump set `slot`.
void Debug_DumpExtraImage(int slot, const char *suffix, const uint8_t *bgra);

// Frame capture: a dump set plus debug/sm-dump-NN-frame.txt, which lists every PPU
// register write of one frame with its scanline, the HDMA channel setup and a
// per-register summary. Begin right before RtlRunFrame (the frame must be drawn),
// End right after it with the new PPU output.
bool Debug_FrameCaptureBegin(void);
int Debug_FrameCaptureEnd(const uint8_t *bgra);

// --- Frame-time recorder ----------------------------------------------------
// Records per-frame timings in memory, writes debug/sm-perf-NN.csv on stop
// (or when the buffer fills, ~60 s).
void Debug_PerfToggle(void);
bool Debug_PerfRecording(void);
// audio[] = lock wait, SPC driver loop, DSP cycles, resample (ms, last block).
void Debug_PerfFrame(float logic_ms, float draw_ms, float audio_ms, float work_ms, bool shown, const float audio[4]);

// Last status line for the UI ("Dump 03 saved", "Perf: 1234 frames", ...).
const char *Debug_LastMessage(void);
void Debug_SetMessage(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
const char *Debug_Version(void);

// Slot for a new capture of `kind` ("dump", "rec", ...): the first of `slots` whose
// marker file (`marker_fmt` with the slot number) does not exist, else the one after
// the slot this kind wrote last (remembered in debug/sm-<kind>-last.txt).
int Debug_NextSlot(const char *kind, const char *marker_fmt, int slots);
