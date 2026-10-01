// 2D drawing primitives for the bottom-screen UI and the top-screen overlay.
//
// Software-drawn straight into the framebuffer (RGBA8, column-major). Every
// screen-drawing call in bottom_ui.c goes through this header, so moving the UI
// to citro2d later only means reimplementing these few functions.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <3ds.h>

// Memory order in the RGBA8 framebuffer is A,B,G,R, i.e. R is the top byte.
#define RGB(r, g, b) (((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | 0xFFu)

#define COL_BG      RGB(14, 16, 28)
#define COL_PANEL   RGB(24, 28, 48)
#define COL_BORDER  RGB(58, 66, 104)
#define COL_TAB     RGB(34, 38, 64)
#define COL_TAB_ON  RGB(70, 96, 190)
#define COL_BTN     RGB(44, 50, 82)
#define COL_ON      RGB(30, 140, 84)
#define COL_OFF     RGB(120, 52, 52)
#define COL_TEXT    RGB(230, 232, 240)
#define COL_DIM     RGB(140, 148, 170)
#define COL_FAINT   RGB(70, 76, 100)
#define COL_GOOD    RGB(110, 220, 130)
#define COL_WARN    RGB(240, 200, 80)
#define COL_BAD     RGB(240, 100, 100)
#define COL_ENERGY  RGB(240, 190, 60)
#define COL_MISSILE RGB(220, 90, 70)
#define COL_SUPER   RGB(90, 200, 110)
#define COL_PBOMB   RGB(230, 210, 80)
#define COL_RESERVE RGB(110, 160, 240)
#define COL_TITLE   RGB(255, 215, 0)
#define COL_ACCENT  RGB(100, 220, 255)
#define COL_BOX     RGB(20, 70, 130)      // default button body
#define COL_BOX_EDGE RGB(70, 130, 210)    // default button border
#define COL_PRESSED RGB(90, 150, 220)     // body of a button just tapped
#define COL_MODAL   RGB(10, 14, 24)
#define COL_MODAL_EDGE RGB(40, 70, 120)

typedef struct { uint32_t *px; int w, h; } Surface;   // column-major, h px per column
typedef struct { int x, y, w, h; } Rect;

// Kept for symmetry with a future citro2d backend; the 5x7 font is compiled in.
bool UiDraw_Init(void);

Surface UiDraw_Screen(gfxScreen_t screen);

void UiFillRect(Surface s, int x, int y, int w, int h, uint32_t c);
void UiFrameRect(Surface s, int x, int y, int w, int h, uint32_t c);   // 1 px outline
void UiDrawText(Surface s, int x, int y, int scale, uint32_t c, const char *str);
void UiDrawTextf(Surface s, int x, int y, uint32_t c, const char *fmt, ...) __attribute__((format(printf, 5, 6)));
int UiTextWidth(const char *str, int scale);
void UiDrawButton(Surface s, Rect r, uint32_t bg, const char *label);
// mzm-style box: 1 px border, body, 1 px bevel on top. `pressed` draws the tap flash.
void UiDrawBox(Surface s, Rect r, uint32_t body, uint32_t border, bool pressed);
// Box plus a centred label (nudged down 1 px while pressed).
void UiDrawBoxLabel(Surface s, Rect r, uint32_t body, uint32_t border, uint32_t text, bool pressed, const char *label);
void UiDrawTextCentered(Surface s, int cx, int y, uint32_t c, const char *str);
// Horizontal bar: `value` of `max` filled, with a dark track and outline.
void UiDrawBar(Surface s, int x, int y, int w, int h, int value, int max, uint32_t fill);

static inline bool UiIn(Rect r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }
