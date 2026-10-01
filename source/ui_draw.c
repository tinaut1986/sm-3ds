#include "ui_draw.h"

#include "ui_font.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool UiDraw_Init(void) { return true; }   // the font is compiled in (ui_font.c)

Surface UiDraw_Screen(gfxScreen_t screen) {
  u16 w, h;   // libctru reports the rotated size: w = 240, h = width in pixels
  u8 *fb = gfxGetFramebuffer(screen, GFX_LEFT, &w, &h);
  return (Surface){ (uint32_t *)fb, h, w };
}

void UiFillRect(Surface s, int x, int y, int w, int h, uint32_t c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > s.w) w = s.w - x;
  if (y + h > s.h) h = s.h - y;
  for (int xx = x; xx < x + w; xx++) {
    uint32_t *col = s.px + xx * s.h + (s.h - 1 - y);
    for (int yy = 0; yy < h; yy++) col[-yy] = c;
  }
}

void UiFrameRect(Surface s, int x, int y, int w, int h, uint32_t c) {
  UiFillRect(s, x, y, w, 1, c);
  UiFillRect(s, x, y + h - 1, w, 1, c);
  UiFillRect(s, x, y, 1, h, c);
  UiFillRect(s, x + w - 1, y, 1, h, c);
}

int UiTextWidth(const char *str, int scale) { return (int)strlen(str) * kUiAdvance * scale; }

void UiDrawText(Surface s, int x, int y, int scale, uint32_t c, const char *str) {
  for (; *str; str++, x += kUiAdvance * scale) {
    const uint8_t *glyph = UiFont_Glyph((unsigned char)*str);
    if (!glyph) continue;
    if (scale == 1) {
      // Common case: write pixels directly, no per-pixel rectangle clipping.
      if (x < 0 || x + kUiGlyphW > s.w || y < 0 || y + kUiGlyphH > s.h) continue;
      for (int col = 0; col < kUiGlyphW; col++) {
        uint32_t *px = s.px + (x + col) * s.h + (s.h - 1 - y);
        for (int row = 0; row < kUiGlyphH; row++)
          if (glyph[row] & (1u << (kUiGlyphW - 1 - col))) px[-row] = c;
      }
      continue;
    }
    for (int row = 0; row < kUiGlyphH; row++)
      for (int col = 0; col < kUiGlyphW; col++)
        if (glyph[row] & (1u << (kUiGlyphW - 1 - col)))
          UiFillRect(s, x + col * scale, y + row * scale, scale, scale, c);
  }
}

void UiDrawTextf(Surface s, int x, int y, uint32_t c, const char *fmt, ...) {
  char buf[64];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  UiDrawText(s, x, y, 1, c, buf);
}

void UiDrawButton(Surface s, Rect r, uint32_t bg, const char *label) {
  UiFillRect(s, r.x, r.y, r.w, r.h, bg);
  UiDrawText(s, r.x + (r.w - UiTextWidth(label, 1)) / 2, r.y + (r.h - kUiGlyphH) / 2, 1, COL_TEXT, label);
}

void UiDrawBox(Surface s, Rect r, uint32_t body, uint32_t border, bool pressed) {
  if (pressed) {
    body = COL_PRESSED;
    border = RGB(210, 235, 255);
  }
  UiFillRect(s, r.x, r.y, r.w, r.h, border);
  UiFillRect(s, r.x + 1, r.y + 1, r.w - 2, r.h - 2, body);
  UiFillRect(s, r.x + 1, r.y + 1, r.w - 2, 1, pressed ? border : RGB(90, 130, 180));
}

void UiDrawBoxLabel(Surface s, Rect r, uint32_t body, uint32_t border, uint32_t text, bool pressed, const char *label) {
  UiDrawBox(s, r, body, border, pressed);
  UiDrawText(s, r.x + (r.w - UiTextWidth(label, 1)) / 2, r.y + (r.h - kUiGlyphH) / 2 + (pressed ? 1 : 0), 1, text, label);
}

void UiDrawTextCentered(Surface s, int cx, int y, uint32_t c, const char *str) {
  UiDrawText(s, cx - UiTextWidth(str, 1) / 2, y, 1, c, str);
}

void UiDrawBar(Surface s, int x, int y, int w, int h, int value, int max, uint32_t fill) {
  UiFillRect(s, x, y, w, h, COL_BG);
  if (max > 0 && value > 0) {
    if (value > max) value = max;
    UiFillRect(s, x + 1, y + 1, (w - 2) * value / max, h - 2, fill);
  }
  UiFrameRect(s, x, y, w, h, COL_BORDER);
}
