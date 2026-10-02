// WIDE view, game side (docs/PLAN.md P4.5): what the game does not do for the pixels
// outside its 256x224 view. The renderer side is GpuPpu_SetMargin (gpu_ppu.h).
//
// The game keeps 17 block columns of the BG1/BG2 tilemaps up to date (the view plus
// one) and rows 1..15 of 16; the tilemaps are 32 blocks wide. Before the PPU draws a
// frame (g_rtl_before_ppu_draw), the margin columns, and the rows the game leaves stale
// that the view now shows, are written straight into VRAM from the level data, exactly as
// the game's own column/row uploads would write them.
#pragma once

#include <stdbool.h>

#include "gpu_ppu.h"

// View for the next frames, in screen pixels: margin on each side, rows above and below
// the 224. All 0 = off (the game runs untouched).
void SmWide_SetView(int margin_x, int extra_top, int extra_bottom);

// The last frame's margins: 2 * margin_x in all, but leaning away from a room edge that
// would otherwise show (GpuPpu_SetMargins), where to draw the HUD so that it stays put on
// the screen (GpuPpu_SetHudX), and how far to move BG2 so its parallax follows the leaned
// view rather than the game's camera (GpuPpu_SetLayerShiftX).
void SmWide_Margins(int *left, int *right, int *hud_x, int *bg2_dx);

// BG3 that must stay within the 256 px view, as it would repeat into the margins: the
// HUD's rows (0-30) and the message boxes' tilemap (item, save, map; BG3SC 0x58).
enum { kSmWideHudRows = 31, kSmWideMessageBoxMap = 0x5800 };

// Whether the last frame had its margins filled (gameplay, not a door transition). When
// false the margins hold whatever VRAM had and should be masked.
bool SmWide_Filled(void);

// Whether the last frame showed a mode 7 room (Ceres): nothing filled nor masked, and the
// plane goes on under the HUD (GpuPpu_SetMode7UnderHud).
bool SmWide_Mode7(void);

// The room's rectangle in screen pixels for the last frame ([x0, x1) x [y0, y1)); the
// level data does not go beyond it, so outside it should be masked.
void SmWide_RoomRect(int *x0, int *y0, int *x1, int *y1);

// The power bomb's (and crystal flash's) real outline per captured line, for
// GpuPpu_SetWindow2Extent; NULL when there is no explosion.
const int16_t (*SmWide_Window2Extent(void))[2];

// Black masks for a frame built with margins or extra rows: their parts outside the room
// or in a red scroll screen made of one block (filler), and all of them when they were
// not filled. (The HUD's BG3 stays out of the margins through GpuPpu_SetNarrowBg3Rows.)
void SmWide_AddMasks(GpuFrame *f);
