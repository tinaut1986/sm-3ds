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

// Timing of the last DrawAndPresent: waiting for the GPU to finish the frame before, and
// building plus submitting the command list.
void GpuPpu3ds_LastTimes(float *wait_ms, float *submit_ms);

// Draws `f` and queues it for the top screen.
void GpuPpu3ds_DrawAndPresent(const GpuFrame *f);

// Waits until the GPU has finished everything queued, so the CPU can write the top
// framebuffer again (when switching back to the CPU renderer).
void GpuPpu3ds_WaitIdle(void);

// Debug: the last frame's GPU output as 256x224 XRGB rows like the CPU renderer's,
// for comparing against it on the console. Blocks until the GPU is done.
bool GpuPpu3ds_ReadBack(uint8_t *out, int pitch);

// What the start-up calibration found, for the debug screens.
const char *GpuPpu3ds_CalibrationText(void);
