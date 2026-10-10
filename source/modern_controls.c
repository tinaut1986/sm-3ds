#include "modern_controls.h"

#include "cheats.h"
#include "game_text.h"
#include "src/types.h"
#include "src/sm_rtl.h"
#include "src/variables.h"

// The frontend's pad bits (main.c, RtlRunFrame): bit i is the SNES button 0x8000 >> i.
enum {
  kPadB = 1 << 0, kPadY = 1 << 1, kPadSelect = 1 << 2, kPadStart = 1 << 3, kPadUp = 1 << 4, kPadDown = 1 << 5,
  kPadLeft = 1 << 6, kPadRight = 1 << 7, kPadA = 1 << 8, kPadX = 1 << 9, kPadL = 1 << 10, kPadR = 1 << 11,
};
enum { kGrappleInactive = 0xC4F0 };   // grapple_beam_function: GrappleBeamFunc_Inactive

static bool g_on;
static int g_missile = kSmHudMissiles;
static int g_prev_inputs;
static bool g_aim_down;   // L held: aiming down since Down was pressed (until L is let go or Up pressed)
// Y alone rolls into the morph ball or out of it: Down (in) or Up (out, until standing) pressed for
// the player, one frame in two and never during a crouch transition, as a player would, until it is
// done or kMorphFrames have gone by (a low ceiling, a pose that cannot morph).
enum { kMorphFrames = 40 };
static int g_morph_goal;    // +1 into the ball, -1 out of it, 0 nothing to do
static int g_morph_left;
static bool g_morph_pressed;

bool ModernControls_On(void) { return g_on; }

void ModernControls_Set(bool on) {
  if (on == g_on) return;
  g_on = on;
  g_rtl_hud_quiet = on;
  g_rtl_hud_marked = 0;
  Hud_RequestSelect(kSmHudNone);   // nothing held: no weapon (false and harmless outside a room)
}

int ModernControls_Missile(void) { return g_missile; }

void ModernControls_SetMissile(int item) {
  if (item == kSmHudMissiles || item == kSmHudSupers) g_missile = item;
}

// A SNES button mask from the game's configuration (button_config_*) as pad bits.
static int Pad(uint16 snes) {
  int bits = 0;
  for (int i = 0; i < 12; i++)
    if (snes & (0x8000 >> i)) bits |= 1 << i;
  return bits;
}

static bool HasKind(int item) {
  return item == kSmHudMissiles ? samus_max_missiles != 0 : samus_max_super_missiles != 0;
}

// The missile kind R + X fires: the marked one, or the other one while Samus has none of it.
static int MissileKind(void) {
  const int other = g_missile == kSmHudMissiles ? kSmHudSupers : kSmHudMissiles;
  return !HasKind(g_missile) && HasKind(other) ? other : g_missile;
}

static bool InMorphBall(void) {
  switch (samus_movement_type) {
  case 0x04: case 0x08:                // morph ball on the ground, falling
  case 0x11: case 0x12: case 0x13:     // spring ball
    return true;
  default:
    return false;
  }
}

int ModernControls_Translate(int in) {
  const int pressed = in & ~g_prev_inputs;
  g_prev_inputs = in;
  if (!g_on) return in;
  // Gameplay only: the game's menus, the pause screen and its message boxes (B is "no") keep
  // the buttons as they are.
  if (game_state != 0x08 || GameText_MessageBoxShown()) {
    g_aim_down = false;
    g_morph_goal = 0;
    return in;
  }

  if ((pressed & kPadSelect) && HasKind(g_missile == kSmHudMissiles ? kSmHudSupers : kSmHudMissiles)) {
    g_missile = g_missile == kSmHudMissiles ? kSmHudSupers : kSmHudMissiles;
    g_rtl_hud_click = true;
  }
  const int missile = MissileKind();
  g_rtl_hud_marked = HasKind(missile) ? (uint8)(1 << missile) : 0;

  const bool r = in & kPadR, b = in & kPadB, y = in & kPadY;
  int out = in & (kPadUp | kPadDown | kPadLeft | kPadRight | kPadStart);
  if (in & kPadA) out |= Pad(button_config_jump_a);
  if (in & kPadX) out |= Pad(button_config_shoot_x);
  // L aims up; Down while L is held turns it down and it stays down (Down can be let go) until L is
  // let go or Up is pressed. Up and Down only steer the aim then: Samus does not crouch or look up.
  if (in & kPadL) {
    if (in & kPadDown) g_aim_down = true;
    if (pressed & kPadUp) g_aim_down = false;
    out &= ~(kPadUp | kPadDown);
    out |= Pad(g_aim_down ? button_config_aim_down_L : button_config_aim_up_R);
  } else {
    g_aim_down = false;
  }

  // Y alone: into the morph ball, or out of it (R + Y is the grapple; a hooked grapple keeps Y).
  if ((pressed & kPadY) && !r && hud_item_index != kSmHudGrapple && (equipped_items & 0x0004)) {
    g_morph_goal = InMorphBall() ? -1 : 1;
    g_morph_left = kMorphFrames;
    g_morph_pressed = false;
  }
  if (g_morph_goal) {
    const bool crouching = samus_movement_type == 0x05 || samus_movement_type == 0x0F;
    const bool done = g_morph_goal > 0 ? InMorphBall() : !InMorphBall() && !crouching;
    if (done || --g_morph_left < 0) {
      g_morph_goal = 0;
    } else {
      // Up and Down alone, Left and Right held back until it is done: in the air Down with a
      // direction is a diagonal aim, and a direction between two Downs undoes the first one.
      out &= ~(kPadUp | kPadDown | kPadLeft | kPadRight);
      g_morph_pressed = !g_morph_pressed && samus_movement_type != 0x0F;   // 0x0F: crouching or standing up
      if (g_morph_pressed) out |= g_morph_goal > 0 ? kPadDown : kPadUp;
    }
  }

  // The item the buttons want. While R is held: the power bomb in morph ball, else the grapple
  // with Y, the X-ray with B, the missile kind otherwise. A hooked grapple stays while Y is held.
  int want = kSmHudNone;
  if (r) want = InMorphBall() ? kSmHudPowerBombs : y ? kSmHudGrapple : b ? kSmHudXray : missile;
  else if (y && hud_item_index == kSmHudGrapple && grapple_beam_function != kGrappleInactive) want = kSmHudGrapple;
  if (want == kSmHudGrapple && y) out |= Pad(button_config_shoot_x);
  // Asked again every frame it differs: that also covers morphing with R held and coming back from
  // the pause screen. One the game skips (no ammo) leaves the selection as it was.
  if (want != hud_item_index) Hud_RequestSelect(want);

  // Samus always runs; B alone walks. The X-ray lasts while run is held, so while it is selected
  // run means R and B held, and letting go of either ends it.
  const bool run = hud_item_index == kSmHudXray ? r && b : !(b && !r);
  if (run) out |= Pad(button_config_run_b);
  return out;
}
