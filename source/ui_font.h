#pragma once

#include <stdint.h>

// 7 rows of 5 bits (bit 4 = leftmost), or NULL for characters without a glyph
// (space and anything unknown: the caller just advances).
const uint8_t *UiFont_Glyph(unsigned char c);

enum { kUiGlyphW = 5, kUiGlyphH = 7, kUiAdvance = 6 };
