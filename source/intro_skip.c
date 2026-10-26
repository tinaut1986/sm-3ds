#include "intro_skip.h"

#include "src/types.h"
#include "src/sm_rtl.h"
#include "src/variables.h"

enum { kPadB = 1 << 0, kPadStart = 1 << 3, kPadUp = 1 << 4, kPadDown = 1 << 5, kPadA = 1 << 8 };
enum { kIntroState = 0x1E };

static bool g_open;
static int g_cursor;
static int g_prev;
static bool g_closed_now;   // the choice closed on this pass: its button stays away from the game

bool IntroSkip_Frame(int pad) {
  const int pressed = pad & ~g_prev;
  g_prev = pad;
  g_closed_now = false;
  if (game_state != kIntroState) {
    g_open = false;
    return false;
  }
  if (!g_open) {
    if (pressed & kPadStart) {
      g_open = true;
      g_cursor = 0;
    }
    return g_open;
  }
  if (pressed & (kPadUp | kPadDown)) g_cursor ^= 1;
  if (pressed & kPadA) {
    g_rtl_intro_skip = g_cursor ? kRtlIntroSkipZebes : kRtlIntroSkipCeres;
    g_open = false, g_closed_now = true;
  } else if (pressed & (kPadB | kPadStart)) {
    g_open = false, g_closed_now = true;
  }
  return g_open;
}

int IntroSkip_Filter(int pad) {
  if (game_state != kIntroState) return pad;
  return g_closed_now ? 0 : pad & ~kPadStart;
}

IntroSkipShown IntroSkip_Shown(void) {
  if (game_state != kIntroState || g_rtl_intro_skip) return kIntroSkipHidden;
  return g_open ? kIntroSkipMenu : kIntroSkipHint;
}

int IntroSkip_Cursor(void) { return g_cursor; }
