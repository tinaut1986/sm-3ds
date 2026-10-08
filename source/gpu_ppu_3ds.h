// citro3d backend of the GPU PPU renderer (gpu_ppu.h): draws a GpuFrame and shows it
// on the top screen. citro3d presents the top screen itself (display transfer and
// swap); the caller swaps only the bottom screen on those frames.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gpu_ppu.h"
#include "stereo_depth.h"

// Lazy: nothing of citro3d is touched until the GPU renderer is first switched on.
// Returns false if it could not start (then keep using the CPU renderer).
bool GpuPpu3ds_Init(void);
bool GpuPpu3ds_Ready(void);
void GpuPpu3ds_Exit(void);

// FPS overlay for the next frames: 64x64 column-major RGBA8 like the framebuffer (see
// BottomUi_DrawOverlayInto), drawn at a corner of the top screen (1 top-left, 2 top-right,
// 3 bottom-left, 4 bottom-right: the box is already at that corner of the texture); NULL hides it.
void GpuPpu3ds_SetOverlay(const uint32_t *px, int corner);

// Achievement notice for the next frames: 512x64 column-major RGBA8 with the 300x36 box in
// its top-left corner (BottomUi_DrawTopToastInto), drawn centred at the top; NULL hides it.
void GpuPpu3ds_SetToast(const uint32_t *px);

// Timing of the last DrawAndPresent: waiting for the GPU to finish the frame before, and
// building plus submitting the command list.
void GpuPpu3ds_LastTimes(float *wait_ms, float *submit_ms);

// Draws `f` and queues it for the top screen, centred: scaled to 274x240, or 1:1 with
// `pixel_perfect`.
// `slider`: the 3D slider (0 = one flat eye); `gameplay`: the room is on screen (stereo
// planes; otherwise the frame is drawn flat, its HUD list and text excepted).
void GpuPpu3ds_DrawAndPresent(const GpuFrame *f, bool pixel_perfect, float slider, bool gameplay, StereoScreen screen);

// Waits until the GPU has finished everything queued, so the CPU can write the top
// framebuffer again (when switching back to the CPU renderer).
void GpuPpu3ds_WaitIdle(void);

// Debug views: every quad is tinted, blended over its own picture so the scene stays readable.
//   kPlaneTintPlanes: the colour of its stereo plane (StereoDepth_PlaneColor)
//   kPlaneTintOrder:  a one-colour ramp by its drawing order, the compositor level (back dark .. front bright)
//   kPlaneTintDepth:  the same ramp by its stereoscopic depth, the plane's shift (far dark .. near bright)
// Where the two ramps differ is what does not fit between how the game draws it and where 3D puts it.
enum { kPlaneTintOff, kPlaneTintPlanes, kPlaneTintOrder, kPlaneTintDepth, kPlaneTintModes };
void GpuPpu3ds_SetPlaneTint(int mode);

// Debug: draw the second eye too when the console has no 3D screen (2DS), into a target that is
// not shown, with the slider at half: the CPU cost an Old 3DS pays with the slider up, to measure
// on a 2DS. The left eye is what is shown, shifted like the real 3D's.
void GpuPpu3ds_SetForceTwoEyes(bool on);
// Debug: leave one pass out (or draw the BG without reading its textures) to measure it on the console.
enum { kGpuTestOff, kGpuTestBgFlat, kGpuTestNoBg, kGpuTestNoSprites, kGpuTestNoTop, kGpuTestNoMath, kGpuTestNoClears, kGpuTestCount };
void GpuPpu3ds_SetGpuTest(int mode);
// Eyes the last GpuPpu3ds_DrawAndPresent drew: 2 with the 3D on (slider up, or FORCE 3D), 1 otherwise.
int GpuPpu3ds_LastEyes(void);

// The last frame's texture upload: ms copying, ms flushing the cache, flush calls, KB copied.
void GpuPpu3ds_LastTexStats(float *copy_ms, float *flush_ms, int *runs, int *kb);

// The GPU's own time for the last frame it finished: drawing, command processing (ms), command buffer use (0..1).
void GpuPpu3ds_LastGpuTimes(float *draw_ms, float *proc_ms, float *cmdbuf);

// Debug: the last frame's GPU output as 256x224 XRGB rows like the CPU renderer's,
// for comparing against it on the console. Blocks until the GPU is done.
bool GpuPpu3ds_ReadBack(uint8_t *out, int pitch);

// Debug: the top screen as the GPU presented its last frame, 240x400 column-major
// RGBA8 exactly like the framebuffer (valid until the next call), or NULL. Blocks until
// the GPU is done.
const uint32_t *GpuPpu3ds_ReadTop(void);

// What the start-up calibration found, for the debug screens.
const char *GpuPpu3ds_CalibrationText(void);
