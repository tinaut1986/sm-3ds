#include "modern_controls.h"

#include <stdio.h>
#include <string.h>

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

enum { kPadGame = (1 << 12) - 1 };   // the twelve bits the game may see

static const int kFacePad[kWeaponButtons] = { kPadA, kPadB, kPadX, kPadY, kModernPadZL, kModernPadZR };
static ModernFace g_face[kModernActions] = { kFaceA, kFaceB, kFaceX, kFaceY };   // action -> button
// Where each weapon is: the action it rides on (R + that action's button), or alone on a trigger.
enum { kOnZL = kModernActions, kOnZR };
static int g_weapon_place[kModernWeapons] = { kModernShoot, kModernRun, kModernMorph };
static int g_trigger_pressed = -1;

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

ModernFace ModernControls_ButtonOf(ModernAction a) { return g_face[a]; }
bool ModernControls_WeaponWithR(ModernWeapon w) { return g_weapon_place[w] < kModernActions; }
int ModernControls_TriggerPressed(void) { return g_trigger_pressed; }

ModernFace ModernControls_WeaponButton(ModernWeapon w) {
  const int p = g_weapon_place[w];
  return p == kOnZL ? kFaceZL : p == kOnZR ? kFaceZR : g_face[p];
}

int ModernControls_ActionWeapon(ModernAction a) {
  for (int w = 0; w < kModernWeapons; w++)
    if (g_weapon_place[w] == (int)a) return w;
  return -1;
}

void ModernControls_SetButton(ModernAction a, ModernFace face) {
  if (face >= kFaceButtons) return;
  for (int o = 0; o < kModernActions; o++) {
    if (o != (int)a && g_face[o] == face) {
      g_face[o] = g_face[a];
      break;
    }
  }
  g_face[a] = face;
}

bool ModernControls_SetWeaponButton(ModernWeapon w, ModernFace face) {
  int place = face == kFaceZL ? kOnZL : face == kFaceZR ? kOnZR : kModernJump;
  for (int i = 0; i < kModernActions && face < kFaceButtons; i++)
    if (g_face[i] == face) place = i;
  if (g_weapon_place[w] == place) return true;
  if (place == kModernJump || (w == kModernMissile && place == kModernRun)) return false;
  for (int o = 0; o < kModernWeapons; o++) {
    if (o != (int)w && g_weapon_place[o] == place) {
      if (o == kModernMissile && g_weapon_place[w] == kModernRun) return false;
      g_weapon_place[o] = g_weapon_place[w];
      break;
    }
  }
  g_weapon_place[w] = place;
  return true;
}

void ModernControls_ResetMapping(void) {
  for (int i = 0; i < kModernActions; i++) g_face[i] = (ModernFace)i;
  g_weapon_place[kModernMissile] = kModernShoot;
  g_weapon_place[kModernXray] = kModernRun;
  g_weapon_place[kModernGrapple] = kModernMorph;
}

unsigned ModernControls_MappingKey(void) {
  unsigned k = 0;
  for (int i = 0; i < kModernActions; i++) k = k << 2 | g_face[i];
  for (int i = 0; i < kModernWeapons; i++) k = k << 3 | (unsigned)g_weapon_place[i];
  return k;
}

static const char *const kFaceNames[kWeaponButtons] = { "A", "B", "X", "Y", "ZL", "ZR" };
static const char *const kActionNames[kModernActions] = { "jump", "run", "shoot", "morph" };

const char *ModernControls_FaceName(ModernFace f) { return kFaceNames[f]; }
const char *ModernControls_ActionName(ModernAction a) { return kActionNames[a]; }

const char *ModernControls_WeaponPlaceName(ModernWeapon w) {
  const int p = g_weapon_place[w];
  return p < kModernActions ? kActionNames[p] : kFaceNames[p == kOnZL ? kFaceZL : kFaceZR];
}

const char *ModernControls_WeaponKeys(ModernWeapon w, const char *sep) {
  static char buf[kModernWeapons][16];
  const char *face = kFaceNames[ModernControls_WeaponButton(w)];
  if (ModernControls_WeaponWithR(w)) snprintf(buf[w], sizeof(buf[w]), "R%s%s", sep, face);
  else snprintf(buf[w], sizeof(buf[w]), "%s", face);
  return buf[w];
}

bool ModernControls_FaceFromName(const char *name, ModernFace *out) {
  for (int i = 0; i < kWeaponButtons; i++)
    if (!strcmp(name, kFaceNames[i])) { *out = (ModernFace)i; return true; }
  return false;
}

bool ModernControls_ActionFromName(const char *name, ModernAction *out) {
  for (int i = 0; i < kModernActions; i++)
    if (!strcmp(name, kActionNames[i])) { *out = (ModernAction)i; return true; }
  return false;
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

// Whether a weapon's button is down: R and its action's button, or its trigger alone.
static bool WeaponHeld(int in, ModernWeapon w) {
  return (!ModernControls_WeaponWithR(w) || (in & kPadR)) && (in & kFacePad[ModernControls_WeaponButton(w)]);
}

int ModernControls_Translate(int in) {
  const int pressed = in & ~g_prev_inputs;
  g_prev_inputs = in;
  g_trigger_pressed = (pressed & kModernPadZL) ? kFaceZL : (pressed & kModernPadZR) ? kFaceZR : -1;
  if (!g_on) return in & kPadGame;
  // Gameplay only: the game's menus, the pause screen and its message boxes (B is "no") keep
  // the buttons as they are.
  if (game_state != 0x08 || GameText_MessageBoxShown()) {
    g_aim_down = false;
    g_morph_goal = 0;
    return in & kPadGame;
  }

  if ((pressed & kPadSelect) && HasKind(g_missile == kSmHudMissiles ? kSmHudSupers : kSmHudMissiles)) {
    g_missile = g_missile == kSmHudMissiles ? kSmHudSupers : kSmHudMissiles;
    g_rtl_hud_click = true;
  }
  const int missile = MissileKind();
  g_rtl_hud_marked = HasKind(missile) ? (uint8)(1 << missile) : 0;

  const bool r = in & kPadR;
  const int shoot_pad = kFacePad[g_face[kModernShoot]], run_pad = kFacePad[g_face[kModernRun]];
  const int morph_pad = kFacePad[g_face[kModernMorph]], jump_pad = kFacePad[g_face[kModernJump]];
  // Whether each weapon's button is down (R + it, or its trigger).
  const bool missile_held = WeaponHeld(in, kModernMissile);
  const bool xray_held = WeaponHeld(in, kModernXray);
  const bool grapple_held = WeaponHeld(in, kModernGrapple);

  int out = in & (kPadUp | kPadDown | kPadLeft | kPadRight | kPadStart);
  // A button carrying a weapon does that weapon while R is held, not its action (below). Jump never
  // carries one.
  const bool weapon_on[kModernActions] = {
    false, r && ModernControls_ActionWeapon(kModernRun) >= 0,
    r && ModernControls_ActionWeapon(kModernShoot) >= 0, r && ModernControls_ActionWeapon(kModernMorph) >= 0 };
  if ((in & jump_pad) && !weapon_on[kModernJump]) out |= Pad(button_config_jump_a);
  if ((in & shoot_pad) && !weapon_on[kModernShoot]) out |= Pad(button_config_shoot_x);
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

  // The morph button alone: into the morph ball, or out of it (with R held it is the grapple when
  // that is what it carries; a hooked grapple keeps it).
  if ((pressed & morph_pad) && !weapon_on[kModernMorph] && hud_item_index != kSmHudGrapple && (equipped_items & 0x0004)) {
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

  // The item the buttons want. In morph ball, the power bomb while R or the missile's button is held;
  // else the grapple, the X-ray or the missile kind, by the button held. R alone also asks for the
  // missile when that is held with R (so R and its button pressed on the same frame fire it). A hooked
  // grapple stays while its button is held. A weapon Samus does not have leaves the item as none (not
  // the missile selected by holding R): its button then fires nothing.
  const bool has_grapple = equipped_items & 0x4000, has_xray = equipped_items & 0x8000;
  const int grapple_pad = kFacePad[ModernControls_WeaponButton(kModernGrapple)];
  int want = kSmHudNone;
  if (InMorphBall()) {
    if (r || missile_held) want = kSmHudPowerBombs;
  } else if (grapple_held) {
    want = has_grapple ? kSmHudGrapple : kSmHudNone;
  } else if (xray_held) {
    want = has_xray ? kSmHudXray : kSmHudNone;
  } else if (missile_held || (r && ModernControls_WeaponWithR(kModernMissile))) {
    want = missile;
  } else if (!r && (in & grapple_pad) && hud_item_index == kSmHudGrapple && grapple_beam_function != kGrappleInactive) {
    want = kSmHudGrapple;
  }
  // Firing: the missile's button, and the grapple's once it is the item.
  if (missile_held || (want == kSmHudGrapple && (in & grapple_pad)))
    out |= Pad(button_config_shoot_x);
  // Asked again every frame it differs: that also covers morphing with R held and coming back from
  // the pause screen. One the game skips (no ammo) leaves the selection as it was.
  if (want != hud_item_index) Hud_RequestSelect(want);

  // The run button runs, as in the original (unless R makes it carry the grapple or the missile).
  // The X-ray lasts while run is held, so while it is selected run means R and the X-ray's button
  // held, whichever button that is, and letting go of either ends it.
  const bool run_weapon_other = weapon_on[kModernRun] && ModernControls_ActionWeapon(kModernRun) != kModernXray;
  const bool run = hud_item_index == kSmHudXray ? xray_held : (in & run_pad) && !run_weapon_other;
  if (run) out |= Pad(button_config_run_b);
  return out;
}
