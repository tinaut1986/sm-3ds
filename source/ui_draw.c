#include "ui_draw.h"

#include "ui_font.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool UiDraw_Init(void) { return true; }   // the font is compiled in (ui_font.c)

// A swap takes effect at the next vblank; until then the buffer gfxGetFramebuffer hands
// out is still being scanned out, and drawing into it shows as flashes and torn panels.
// That happens when a slow frame (no vblank wait) is followed by a quick one, e.g. a
// skipped frame that redraws the UI.
static u64 g_swap_tick;   // 0 = the last swap has been shown

void UiDraw_Swapped(void) { g_swap_tick = svcGetSystemTick(); }

void UiDraw_VBlankSeen(void) { g_swap_tick = 0; }

static u64 g_wait_ticks;   // spent blocked in UiDraw_WaitSwapShown since the last UiDraw_TakeWaitMs

void UiDraw_WaitSwapShown(void) {
  if (!g_swap_tick) return;
  // Some vblank has certainly happened if a whole refresh (59.8 Hz) has passed.
  if (svcGetSystemTick() - g_swap_tick < SYSCLOCK_ARM11 / 59) {
    const u64 t0 = svcGetSystemTick();
    gspWaitForVBlank();
    g_wait_ticks += svcGetSystemTick() - t0;
  }
  g_swap_tick = 0;
}

float UiDraw_TakeWaitMs(void) {
  const float ms = (float)((double)g_wait_ticks * 1000.0 / SYSCLOCK_ARM11);
  g_wait_ticks = 0;
  return ms;
}

// The screens' framebuffers are 24-bit (BGR8): HOME draws the background of a suspended
// application from them and cannot read 32-bit ones (it showed black, #20). The CPU drawing
// code works in 32 bits (0xRRGGBBAA), so it draws into these two buffers, and UiDraw_Present
// converts one to the real framebuffer when it is about to be swapped.
static uint32_t g_shadow_top[400 * 240], g_shadow_bottom[320 * 240];

Surface UiDraw_Screen(gfxScreen_t screen) {
  return screen == GFX_TOP ? (Surface){ g_shadow_top, 400, 240 } : (Surface){ g_shadow_bottom, 320, 240 };
}

void UiDraw_Present(gfxScreen_t screen, bool both_eyes) {
  UiDraw_WaitSwapShown();
  const uint32_t *src = screen == GFX_TOP ? g_shadow_top : g_shadow_bottom;
  const int n = screen == GFX_TOP ? 400 * 240 : 320 * 240;
  uint8_t *dst = gfxGetFramebuffer(screen, GFX_LEFT, NULL, NULL);
  for (int i = 0; i < n; i++) {   // column-major, origin bottom-left, in both
    const uint32_t c = src[i];
    dst[0] = (uint8_t)(c >> 8);    // B
    dst[1] = (uint8_t)(c >> 16);   // G
    dst[2] = (uint8_t)(c >> 24);   // R
    dst += 3;
  }
  if (both_eyes && screen == GFX_TOP)
    memcpy(gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL), gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), n * 3);
}

static int g_clip_y0, g_clip_y1 = 1 << 30;

void UiClipY(int y0, int y1) { g_clip_y0 = y0, g_clip_y1 = y1; }
void UiNoClip(void) { UiClipY(0, 1 << 30); }

void UiFillRect(Surface s, int x, int y, int w, int h, uint32_t c) {
  const int top = g_clip_y0 > 0 ? g_clip_y0 : 0, bottom = g_clip_y1 < s.h ? g_clip_y1 : s.h;
  if (x < 0) { w += x; x = 0; }
  if (y < top) { h -= top - y; y = top; }
  if (x + w > s.w) w = s.w - x;
  if (y + h > bottom) h = bottom - y;
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

// The next character of a UTF-8 string, as a code point (Latin-1 for the font), moving
// *str past it. A malformed byte reads as itself.
static unsigned NextChar(const char **str) {
  const unsigned char *p = (const unsigned char *)*str;
  if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
    *str += 2;
    return (p[0] & 0x1F) << 6 | (p[1] & 0x3F);
  }
  if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
    *str += 3;
    return 0xFFFF;   // beyond the font
  }
  *str += 1;
  return p[0];
}

int UiTextWidth(const char *str, int scale) {
  int n = 0;
  while (*str) NextChar(&str), n++;
  return n * kUiAdvance * scale;
}

void UiDrawText(Surface s, int x, int y, int scale, uint32_t c, const char *str) {
  for (; *str; x += kUiAdvance * scale) {
    const unsigned ch = NextChar(&str);
    const uint8_t *glyph = ch <= 0xFF ? UiFont_Glyph((unsigned char)ch) : NULL;
    if (!glyph) continue;
    const uint8_t *mark = UiFont_Mark((unsigned char)ch);   // rare: drawn the slow way
    for (int row = 0; mark && row < 2; row++)
      for (int col = 0; col < kUiGlyphW; col++)
        if (mark[row] & (1u << (kUiGlyphW - 1 - col)))
          UiFillRect(s, x + col * scale, y - (kUiMarkRise - row) * scale, scale, scale, c);
    if (scale == 1) {
      // Common case: write pixels directly, no per-pixel rectangle clipping.
      if (x < 0 || x + kUiGlyphW > s.w || y < 0 || y + kUiGlyphH > s.h) continue;
      const int r0 = g_clip_y0 > y ? g_clip_y0 - y : 0;
      const int r1 = g_clip_y1 < y + kUiGlyphH ? g_clip_y1 - y : kUiGlyphH;
      for (int col = 0; col < kUiGlyphW; col++) {
        uint32_t *px = s.px + (x + col) * s.h + (s.h - 1 - y);
        for (int row = r0; row < r1; row++)
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

void UiBlit(Surface s, int x, int y, int size, const uint32_t *px, bool gray) {
  const int top = g_clip_y0 > 0 ? g_clip_y0 : 0, bottom = g_clip_y1 < s.h ? g_clip_y1 : s.h;
  for (int xx = 0; xx < size; xx++) {
    if (x + xx < 0 || x + xx >= s.w) continue;
    uint32_t *col = s.px + (x + xx) * s.h + (s.h - 1 - y);
    for (int yy = 0; yy < size; yy++) {
      if (y + yy < top || y + yy >= bottom) continue;
      uint32_t c = px[yy * size + xx];
      if (gray) {   // dimmed grey, as RA shows a locked badge
        const uint32_t l = ((c >> 24) * 30 + (c >> 16 & 0xFF) * 59 + (c >> 8 & 0xFF) * 11) / 160;
        c = l << 24 | l << 16 | l << 8 | 0xFF;
      }
      col[-yy] = c;
    }
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
