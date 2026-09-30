// Touch UI for the bottom screen: status, options and debug tabs.
//
// Software-drawn straight into the bottom framebuffer (RGBA8, like the game
// on the top screen) with the 8x8 font from romfs. It only redraws when
// something changed or every REFRESH_FRAMES frames, so it costs next to nothing.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  float fps;          // frames actually shown per second, averaged over ~1 s
  float game_fps;     // game logic frames per second: 60 means full speed
  float frame_ms;     // wall time of one full main-loop iteration
  float logic_ms;     // RtlRunFrame (game logic + PPU rendering into g_pixels)
  float draw_ms;      // copy of the PPU output to the top screen
  float audio_ms;     // last audio block generation (audio thread)
  uint32_t frames;    // frames since boot
  bool is_new3ds;
} UiPerf;

typedef struct {
  const char *rom_name;
  const char *rom_sha1;
  bool rom_had_header;
  const char *version;
} UiRomInfo;

// Options the main loop reads every frame.
typedef struct {
  bool paused;
  bool turbo;            // skip 15 of 16 renders while on (fast-forward)
  bool frameskip;        // when late, drop rendering (not logic) to hold game speed
  bool audio_on;
  bool fps_overlay;      // small FPS counter on the top screen
  bool render_on;        // false: skip PPU drawing, to measure its cost
  bool new3ds_speedup;   // 804 MHz + L2 cache on New 3DS
  int save_slot;         // 0..9, for save states
  // One-shot requests, consumed by the main loop.
  bool req_reset;
  bool req_save_state;
  bool req_load_state;
} UiOptions;

extern UiOptions g_ui;

bool BottomUi_Init(const UiRomInfo *rom);   // loads romfs:/font.bmp
void BottomUi_Exit(void);

// Touch: x,y in bottom-screen pixels (0..319, 0..239).
void BottomUi_TouchDown(int x, int y);
void BottomUi_TouchMove(int x, int y);
void BottomUi_TouchUp(void);

// Call once per frame after the top screen has been drawn and before
// gfxSwapBuffers.
void BottomUi_Frame(const UiPerf *perf);

// Small FPS/timing overlay on the top framebuffer (if g_ui.fps_overlay).
void BottomUi_DrawTopOverlay(const UiPerf *perf);
