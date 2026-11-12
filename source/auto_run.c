#include "auto_run.h"

#include "cheats.h"
#include "game_text.h"
#include "src/types.h"
#include "src/variables.h"

// The frontend's pad bits (main.c, RtlRunFrame): bit i is the SNES button 0x8000 >> i.
enum { kPadLeft = 1 << 6, kPadRight = 1 << 7 };

// Frames a direction is held before the run button is added. With the run held from the first
// frame (the original's B always down) Samus is at running speed at once and every tap of the
// D-pad is a long step: hard to place her. Walking first, then running, is how Zero Mission feels
// (its Samus reaches her top speed in 12 frames, from a walk).
enum { kRunDelay = 20 };

static bool g_on;
static int g_dir;    // the direction held (kPadLeft or kPadRight), 0 none or both
static int g_held;   // frames it has been held, up to kRunDelay

bool AutoRun_On(void) { return g_on; }

void AutoRun_Set(bool on) {
  g_on = on;
  g_dir = g_held = 0;
}

// A SNES button mask from the game's configuration (button_config_*) as pad bits.
static int Pad(uint16 snes) {
  int bits = 0;
  for (int i = 0; i < 12; i++)
    if (snes & (0x8000 >> i)) bits |= 1 << i;
  return bits;
}

int AutoRun_Translate(int in) {
  // Gameplay only: the menus, the pause screen and the message boxes keep the buttons as they are.
  if (!g_on || game_state != 0x08 || GameText_MessageBoxShown()) {
    g_dir = g_held = 0;
    return in;
  }
  const int dir = in & (kPadLeft | kPadRight);
  if (dir != kPadLeft && dir != kPadRight) {
    g_dir = g_held = 0;
  } else if (dir != g_dir) {
    g_dir = dir;
    g_held = 0;
  } else if (g_held < kRunDelay) {
    g_held++;
  }
  // The X-ray lasts while the run button is held (the original's own use of it): left alone.
  if (hud_item_index == kSmHudXray) return in;
  const int run = Pad(button_config_run_b);
  if (in & run) return in & ~run;   // the run button held: walk
  if (g_dir && g_held >= kRunDelay) in |= run;
  return in;
}
