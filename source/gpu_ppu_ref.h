// Software execution of a GpuFrame with the CPU renderer's exact rules. Writes rows
// 0..223 of a 256-wide XRGB8888 image laid out like the CPU renderer's output.
#pragma once

#include "gpu_ppu.h"

void GpuRef_DrawFrame(const GpuFrame *f, uint8_t *out, int pitch);

// Same for columns [vx0, vx1) (within the frame's x0..x1, e.g. the WIDE view): output
// column 0 is vx0, output row 0 the frame's row y0 (negative with extra rows). Masks
// included.
void GpuRef_DrawFrameColumns(const GpuFrame *f, uint8_t *out, int pitch, int vx0, int vx1);
