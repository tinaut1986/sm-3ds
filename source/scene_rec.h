// Scene recorder (debug tools): the top screen, frame by frame, as it was shown.
//
// While recording, every Nth shown frame (60, 30 or 15 Hz) is copied into a RAM ring
// as RGB565 together with a few game facts. Nothing touches the SD card meanwhile, and
// it never stops by itself: the ring keeps the last seconds that fit, so the way to use
// it is to start, play until something goes wrong, and stop right after. Stopping writes
// debug/sm-rec-NN.bin (oldest frame first, run-length packed); tools/scene-rec/decode.py
// turns it into PNGs or a video. Format: see docs/debug-tools.md.
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum { kSceneRecW = 400, kSceneRecH = 240 };

typedef struct {
  uint32_t game_frame;   // frames run since boot
  uint16_t state, room, samus_x, samus_y;   // game_state, room_ptr, Samus position
  float logic_ms, draw_ms;
  bool gpu;              // drawn by the GPU renderer (else the CPU one)
  bool wide;             // the frame had WIDE margins
} SceneRecMeta;

bool SceneRec_Active(void);
// Start: allocates the ring (the biggest of 32..4 MB that leaves 4 MB free). Stop: writes the file
// (this takes a few seconds) and frees the ring.
void SceneRec_Toggle(void);
// Stops without writing anything and frees the ring (the report window's CANCEL).
void SceneRec_Discard(void);
// Rate presets, only while stopped.
void SceneRec_CycleRate(void);
const char *SceneRec_RateLabel(void);
// Frames held so far, and how many fit.
int SceneRec_Frames(void);
int SceneRec_Capacity(void);

// Called once per shown frame: true if this one is to be recorded (every Nth).
bool SceneRec_WantFrame(void);
// `fb` is the top screen in the framebuffer's layout: 240x400 words, column-major from
// the left, each column bottom to top, 0xRRGGBBAA.
void SceneRec_AddFrame(const uint32_t *fb, const SceneRecMeta *meta);
