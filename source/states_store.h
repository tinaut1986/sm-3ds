#pragma once
// The save states on the SD card, any number of them: saves/save<id>.sav is the game's state
// (written by RtlSaveLoadFile), saves/save<id>.txt what the list shows (this file), .rap the
// achievements' progress (retro_ach.c) and .img the screenshot of the top screen. Ids 0-9 are
// the ones the first versions had.

#include <stdbool.h>
#include <stdint.h>

enum { kStateShotW = 200, kStateShotH = 120, kStateMarks = 8 };

typedef struct {
  int id;
  bool has_info, has_shot;
  long long saved_at;
  unsigned area, room, health, max_health, reserve, max_reserve, missiles, max_missiles, supers, max_supers, pbs, max_pbs;
  unsigned hours, minutes;
  int mark;               // 0 = none, 1..kStateMarks-1 a colour
  char version[32];       // the build that saved it
} StateInfo;

// Scans saves/: fills `out` (up to `max`) newest first, returns how many states there are in all.
int States_Scan(StateInfo *out, int max);

// The id a new state takes: one past the largest in use.
int States_NextId(void);

// What a state's .txt holds (a state without one has has_info false). The id is filled in.
bool States_ReadInfo(int id, StateInfo *info);
bool States_WriteInfo(const StateInfo *info);

// The screenshot, kStateShotW x kStateShotH in RGB565 (row-major, top row first).
bool States_WriteShot(int id, const uint16_t *rgb565);
bool States_ReadShot(int id, uint16_t *rgb565);

// Removes the state's every file.
void States_Delete(int id);

// Shrinks a 400x240 column-major 0xRRGGBBAA image (the framebuffer's layout) to the screenshot's
// size and format, averaging 2x2 pixels.
void States_MakeShot(const uint32_t *top_400x240, uint16_t *rgb565);
