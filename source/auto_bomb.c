#include "auto_bomb.h"

#include <stdlib.h>
#include "src/types.h"
#include "src/variables.h"
#include "src/sm_rtl.h"

// How the game lifts the morph ball (sm_90.c, sm_a0.c), as measured on the host:
// - a bomb is laid at Samus's position and its counter (projectile_variables) starts at 60; it
//   is looked at against Samus once, on the frame the counter reads 8 (SamusProjectileInteraction-
//   Handler), with the bomb's box 4 px and the ball's 5 x 7: within 9 px across and 11 down or up
//   she is thrown;
// - the throw starts on the next frame: 2.C000 px a frame up, slowed by samus_y_accel every frame
//   (0x1C00 out of liquids), then the same acceleration down to a top speed of 5.
// So where Samus will be, and which bombs will catch her, can be worked out in advance. Each frame
// a bomb can be laid, the next kLook frames are tried as the moment to lay it, and it is laid now
// when now is the moment whose throw starts highest (a catch near the box's edge counting as lower).
// What comes out is the usual climb: a bomb just after each throw, to catch her as she comes back
// down, and one at the top, ~36 px a climb of ~78 frames (tools/auto-bomb-test).
// With HEAVY PHYSICS (#65) the counter starts at 50 and the throw at 3.C000 px a frame (RtlHeavyPhysics).
enum {
  kCheckDelay = 51,   // frames from laying a bomb to the frame it is looked at (CheckDelay: 41 with heavy physics)
  kHitX = 9, kHitY = 11,
  kLook = 16,         // the bomb cooldown: a later moment than this is another bomb's business
  kHorizon = kLook + kCheckDelay + 3,
  kMaxBombs = 5 + 1,
  kRedundant = 6,     // a throw this soon after another one adds nothing
  kMargin = 3,        // px from the bomb a catch can be planned at without cost (the prediction is ~3 px off)
  kEdgeCost = 4,      // px of height each px of catch beyond kMargin costs
};

// y and v in 1/65536 px; dir 0 still, 1 up, 2 down; turn: frames left of the stop at the top.
typedef struct { int32_t y, v; int dir, turn; } Body;
typedef struct { int y, x, at; } Bomb;   // at: frames from now to the frame it is looked at

static int CheckDelay(void) { return RtlHeavyPhysics() ? kCheckDelay - 10 : kCheckDelay; }
static int32_t ThrowSpeed(void) { return RtlHeavyPhysics() ? 0x3C000 : 0x2C000; }

static int32_t Gravity(void) { return (int32_t)((uint32_t)samus_y_accel << 16 | samus_y_subaccel); }

// One frame, as the game moves the ball after a throw: up by the speed, then the speed falls by
// the acceleration; once it is below 0, three frames still at the top; then down by the speed, which
// grows by the acceleration up to 5 (as measured: sm_90.c Samus_BombJumpRisingYMovement_ and
// Samus_BombJumpFallingYMovement_).
static void Step(Body *b, int32_t g) {
  if (b->turn) {
    b->turn--;   // (the frame after this one adds the first bit of speed without moving)
    if (!b->turn) b->v += g;
  } else if (b->dir == 1) {
    if (b->v < 0) {
      b->v = 0, b->dir = 2, b->turn = 2;
      return;
    }
    b->y -= b->v;
    b->v -= g;
  } else if (b->dir == 2) {
    b->y += b->v;
    if (b->v < (5 << 16)) b->v += g;
  }
}

// Plays `bombs` forward from Samus as she is now. Returns the height (px) of the throw that bomb
// `which` gives, or -1 if it gives none (it misses her, or comes right after another throw).
// `path`, when given, gets her height for the first kLook frames.
static int Simulate(const Bomb *bombs, int n, int which, int *path) {
  const int32_t g = Gravity();
  Body b = { (int32_t)((uint32_t)samus_y_pos << 16 | samus_y_subpos), (int32_t)((uint32_t)samus_y_speed << 16 | samus_y_subspeed),
             samus_y_dir == 1 ? 1 : samus_y_dir == 2 ? 2 : 0, 0 };
  if (b.dir == 1 && (samus_y_speed & 0x8000)) b.v = -1;
  int throw_at = -1, thrown_by = -1, last_throw = -100, result = -1, edge = 0;
  for (int s = 0; s < kHorizon; s++) {
    if (s) Step(&b, g);
    if (s == throw_at) {
      b.v = ThrowSpeed(), b.dir = 1;
      // How good the throw is: its height, less for a catch near the box's edge (it might miss).
      if (thrown_by == which && s - last_throw > kRedundant) result = (b.y >> 16) + kEdgeCost * edge;
      last_throw = s;
      throw_at = -1;
    }
    if (path && s < kLook) path[s] = b.y >> 16;
    for (int i = 0; i < n; i++) {
      if (bombs[i].at != s) continue;
      const int dy = abs(bombs[i].y - (b.y >> 16));
      if (dy < (i == which ? kHitY - 2 : kHitY) && abs(bombs[i].x - samus_x_pos) < kHitX) {   // a planned one with 2 px to spare
        throw_at = s + 1, thrown_by = i;
        if (i == which) edge = dy > kMargin ? dy - kMargin : 0;
      }
    }
  }
  return result;
}

static bool WantBomb(void) {
  Bomb bombs[kMaxBombs];
  int n = 0;
  for (int i = 5; i < 10 && n < kMaxBombs - 1; i++) {
    if ((projectile_type[i] & 0x0F00) == 0x0500 && projectile_variables[i] > 8)
      bombs[n++] = (Bomb){ projectile_y_pos[i], projectile_x_pos[i], projectile_variables[i] - 8 };
  }
  int path[kLook];
  Simulate(bombs, n, -1, path);
  // A bomb can only be laid as HudSelectionHandler_MorphBall_Helper2 allows.
  if (bomb_counter && (bomb_counter >= 5 || (uint8)cooldown_timer)) return false;
  int best = -1, best_y = 0;
  for (int k = 0; k < kLook; k++) {
    bombs[n] = (Bomb){ path[k], samus_x_pos, k + 1 + CheckDelay() };   // laid in the next frame (s = k + 1)
    const int y = Simulate(bombs, n + 1, n, NULL);
    if (y >= 0 && (best < 0 || y < best_y)) best = k, best_y = y;
  }
  return best == 0;
}


// ---- In front of the game ----------------------------------------------------------------------

static bool g_on = true;
static int g_held;        // frames the bomb button has been held in a row
static bool g_sent;       // the bomb button went to the game last frame

bool AutoBomb_On(void) { return g_on; }
void AutoBomb_Set(bool on) { g_on = on; }

static bool InMorphBall(void) {
  switch (samus_movement_type) {
  case 0x04: case 0x08:                // morph ball on the ground, falling
  case 0x11: case 0x12: case 0x13:     // spring ball
    return true;
  default:
    return false;
  }
}

// In a liquid without the gravity suit a bomb barely lifts the ball (Samus_InitBombJump): no
// climbing there.
static bool WeakThrow(void) {
  if (equipped_items & 0x20) return false;
  const int bottom = samus_y_pos + samus_y_radius - 1;
  if (fx_y_pos & 0x8000) return !(lava_acid_y_pos & 0x8000) && (int16)lava_acid_y_pos < bottom;
  return (int16)fx_y_pos < bottom && !(fx_liquid_options & 4);
}

// A charged beam carried into the ball becomes the bomb spread while the button stays held
// (HudSelectionHandler_MorphBall_Helper; Down held puts it off to throw it further): the button
// must reach the game as it is until then.
static bool SpreadDue(void) {
  return (equipped_items & 0x1000) && (int16)(flare_counter - 60) >= 0 && !bomb_counter;
}

int AutoBomb_Translate(int in) {
  // The frontend's pad bits: bit i is the SNES button 0x8000 >> i (main.c, RtlRunFrame).
  int bomb = 0;
  for (int i = 0; i < 12; i++)
    if (button_config_shoot_x & (0x8000 >> i)) bomb |= 1 << i;
  g_held = in & bomb ? g_held + 1 : 0;
  // The first frame of a press is the player's own bomb; from the next one on, while the button is
  // held, the bombs are ours. A selected item (a power bomb) keeps the button as it is.
  if (!g_on || g_held < 2 || game_state != 0x08 || !InMorphBall() || hud_item_index != 0 || WeakThrow() || SpreadDue()) {
    g_sent = in & bomb;
    return in;
  }
  // A bomb needs a fresh press: after a frame with the button down, one with it up.
  g_sent = !g_sent && WantBomb();
  return g_sent ? in : in & ~bomb;
}
