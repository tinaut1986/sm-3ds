// On-device diagnostics: log to SD, state dumps, frame-time recorder, crash note.
//
// Everything lands in `debug/` inside the data folder (the game's working
// directory), so it can be fetched over FTP. Captures rotate over a fixed
// number of slots; the newest slot is whichever was free or least recently
// written, so order a fetched set by modification time, not by name.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Creates debug/ if needed. Safe to call more than once.
void Debug_Init(const char *version);

// --- Log -------------------------------------------------------------------
// Off until enabled. Lines are flushed to the SD card as they are written, so
// the last line before a crash is on disk.
void Debug_LogSetEnabled(bool on);
bool Debug_LogEnabled(void);
const char *Debug_LogName(void);   // current log file name, "" if none
void Debug_Log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void Debug_LogMark(void);          // "USER MARK" line with frame and room

// --- Dumps -----------------------------------------------------------------
// Writes one rotating set: top screen RGB, VRAM, CGRAM, OAM, PPU registers,
// WRAM and a game-state text. `bgra` is the 256x240 PPU output. Returns the
// set number (0-based), or -1 on failure.
int Debug_DumpScreen(const uint8_t *bgra);

// --- Frame-time recorder ----------------------------------------------------
// Records per-frame timings in memory, writes debug/sm-perf-NN.csv on stop
// (or when the buffer fills, ~60 s).
void Debug_PerfToggle(void);
bool Debug_PerfRecording(void);
void Debug_PerfFrame(float logic_ms, float draw_ms, float audio_ms, float work_ms, bool shown);

// Last status line for the UI ("Dump 03 saved", "Perf: 1234 frames", ...).
const char *Debug_LastMessage(void);
