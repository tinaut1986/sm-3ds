// citro3d backend of the GPU PPU renderer (gpu_ppu.h): draws a GpuFrame and shows it
// on the top screen. citro3d presents the top screen itself (display transfer and
// swap); the caller swaps only the bottom screen on those frames.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gpu_ppu.h"

// Lazy: nothing of citro3d is touched until the GPU renderer is first switched on.
// Returns false if it could not start (then keep using the CPU renderer).
bool GpuPpu3ds_Init(void);
bool GpuPpu3ds_Ready(void);
void GpuPpu3ds_Exit(void);

// FPS overlay for the next frames: 64x64 column-major RGBA8 like the framebuffer (see
// BottomUi_DrawOverlayInto), drawn at the top-left corner; NULL hides it.
void GpuPpu3ds_SetOverlay(const uint32_t *px);

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
void GpuPpu3ds_DrawAndPresent(const GpuFrame *f, bool pixel_perfect, float slider, bool gameplay);

// Waits until the GPU has finished everything queued, so the CPU can write the top
// framebuffer again (when switching back to the CPU renderer).
void GpuPpu3ds_WaitIdle(void);

// Debug view: every quad is tinted with the colour of its stereo plane (StereoDepth_PlaneColor),
// blended over its own picture so the scene stays readable.
void GpuPpu3ds_SetPlaneTint(bool on);

// Debug: the last frame's GPU output as 256x224 XRGB rows like the CPU renderer's,
// for comparing against it on the console. Blocks until the GPU is done.
bool GpuPpu3ds_ReadBack(uint8_t *out, int pitch);

// Debug: the top screen as the GPU presented its last frame, 240x400 column-major
// RGBA8 exactly like the framebuffer (valid until the next call), or NULL. Blocks until
// the GPU is done.
const uint32_t *GpuPpu3ds_ReadTop(void);

// What the start-up calibration found, for the debug screens.
const char *GpuPpu3ds_CalibrationText(void);
