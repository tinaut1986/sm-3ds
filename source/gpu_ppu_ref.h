// Software execution of a GpuFrame with the CPU renderer's exact rules. Writes rows
// 0..223 of a 256-wide XRGB8888 image laid out like the CPU renderer's output.
#pragma once

#include "gpu_ppu.h"

void GpuRef_DrawFrame(const GpuFrame *f, uint8_t *out, int pitch);
