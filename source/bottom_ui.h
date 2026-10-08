// Touch UI for the bottom screen, laid out like ../mzm's: icon tabs for the map,
// status, debug (DEBUG_TOOLS builds only), save states and options, with modal
// windows over them.
//
// Software-drawn straight into the bottom framebuffer (RGBA8, like the game on the
// top screen) with the 5x7 font. It only redraws when something changed or every
// REFRESH_FRAMES frames, so it costs next to nothing.
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
  float audio_part_ms[4];  // of that: lock wait, SPC driver loop, DSP cycles, resample
  uint32_t core1_limit;    // % of the system core granted to the app (audio thread)
  uint32_t frames;    // frames since boot
  bool is_new3ds;
  // GPU renderer: frames it drew, frames it handed to the CPU and the last reason.
  uint32_t gpu_frames, gpu_fallbacks;
  float gpu_build_ms, gpu_wait_ms, gpu_submit_ms;   // last GPU frame: GpuPpu_BuildFrame, GPU wait, submit
  const char *gpu_reason;
  const char *gpu_calibration;
} UiPerf;

typedef struct {
  const char *rom_name;
  const char *rom_sha1;
  bool rom_had_header;
  const char *version;
} UiRomInfo;

// Frame pacing (OPTIONS -> FRAMES). The game logic always runs at 60 Hz; this is about drawing.
// AUTO drops the drawing of a frame that is late (the default), LOCK 30 draws one frame in two,
// NO SKIP draws every frame whatever it costs (debugging).
enum { kPaceAuto, kPaceLock30, kPaceNoSkip, kPaceCount };

// Options the main loop reads every frame.
typedef struct {
  bool paused;
  int pacing;            // kPaceAuto, kPaceLock30 or kPaceNoSkip (OPTIONS -> FRAMES)
  bool audio_on;
  int fps_overlay;       // small FPS counter on the top screen: 0 off, 1 top-left, 2 top-right, 3 bottom-left, 4 bottom-right
  bool gpu_render;       // draw frames with the GPU renderer (gpu_ppu.c) when it can; on by default
  bool new3ds_speedup;   // 804 MHz + L2 cache on New 3DS
  bool pixel_perfect;    // top screen 256x224 at 1:1; otherwise scaled to 274x240
  int gpu_test;          // debug: GPU TEST, a pass of the GPU renderer left out to measure it (kGpuTest*, not saved)
  bool force_3d;         // debug: draw the second eye too on a console without the 3D screen, to measure what an Old 3DS pays
  int plane_tint;        // debug tint (GPU renderer): 0 off, 1 by plane, 2 by drawing order, 3 by stereo depth
  bool auto_update;      // look for a newer build at boot (config.ini); the check only asks
  bool hud_auto_hide;    // hide, on the top screen, the HUD half the open bottom tab shows (STATUS: energy and weapons, MAP: minimap)
  bool update_beta;      // the updates it looks for: only releases, or the betas too
  bool wide;             // WIDE view: more of the room on the sides in gameplay (GPU renderer)
  int save_slot;        // 0..9, for save states
  // One-shot requests, consumed by the main loop.
  bool req_reset;
  bool req_save_state;
  bool req_load_state;
  bool req_dump;         // Debug_DumpScreen, needs the PPU output owned by main
  bool req_frame_dump;   // Debug_FrameCapture* around the next drawn frame
  bool repause;          // pause again once the dump a report asked for has been taken
  bool req_gpu_check;    // draw the next frame both ways and compare (GPU renderer on)
} UiOptions;

extern UiOptions g_ui;

bool BottomUi_Init(const UiRomInfo *rom);
void BottomUi_Exit(void);

// Touch: x,y in bottom-screen pixels (0..319, 0..239).
// Which halves of the game's HUD the top screen should not draw now: bit 0 the status half
// (energy, reserve, ammunition, selected weapon), bit 1 the minimap. Not 0 only when the option
// is on and the open bottom tab shows that information.
int BottomUi_HudHidden(void);

// Before something that blocks the main loop for a while (writing a state or a dump to the SD
// card): shows PLEASE WAIT on the bottom screen at once, since nothing draws until it is done.
void BottomUi_Busy(void);

void BottomUi_TouchDown(int x, int y);
void BottomUi_TouchMove(int x, int y);
void BottomUi_TouchUp(void);

// Call once per frame after the top screen has been drawn and before
// gfxSwapBuffers.
// Returns true if it drew this frame, i.e. the bottom screen needs a swap.
bool BottomUi_Frame(const UiPerf *perf);

// Short message at the bottom of the screen for ~1.5 s.
void BottomUi_Toast(const char *msg);

// The main loop reports what it did with req_save_state / req_load_state /
// req_reset, so the States tab can describe the slot and forget stale debug state.
void BottomUi_StateSaved(int slot, bool ok, const uint16_t *shot);   // `shot`: the top screen, 200x120 RGB565, or NULL
void BottomUi_StateLoaded(int slot, bool ok);
void BottomUi_GameReset(void);

// Small FPS/timing overlay on the top framebuffer (if g_ui.fps_overlay).
void BottomUi_DrawTopOverlay(const UiPerf *perf);

// Same overlay into a column-major RGBA8 buffer of `w` x `h` (at least 60 x 52), for
// when the GPU renderer presents the top screen. Returns false (buffer untouched) if
// the overlay is off.
bool BottomUi_DrawOverlayInto(uint32_t *px, int w, int h, const UiPerf *perf);

// The achievement notice for the top screen (when set to show there) into a 512x64
// column-major RGBA8 buffer, box in its top-left corner; false when there is none
// (GpuPpu3ds_SetToast).
bool BottomUi_DrawTopToastInto(uint32_t *px);
