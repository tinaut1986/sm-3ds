// The modern control scheme (OPTIONS -> CONTROLS, issue #32, docs/modern-controls.md): Samus
// runs with B, L aims, R + a button uses a weapon, SELECT only switches missiles / super missiles.
// It lives here, in front of the game: the game only ever sees SNES pad bits (through its own
// button configuration) and item selections asked through g_rtl_hud_select, so its RAM holds what
// a SNES pad could produce and RetroAchievements, saves and states are not affected.
#pragma once

#include <stdbool.h>

// The scheme in use (config.ini `controls`); switching asks the game for no item.
bool ModernControls_On(void);
void ModernControls_Set(bool on);

// The missile kind R + X fires: kSmHudMissiles or kSmHudSupers (cheats.h), config.ini `modern_missile`.
int ModernControls_Missile(void);
void ModernControls_SetMissile(int item);

// The remappable part (docs/modern-controls.md, "Remapping"). Four actions sit on the four face
// buttons, a permutation; three weapons each ride on one action's button, held with R, or sit alone
// on ZL or ZR (New 3DS: R + ZR cannot be pressed, so a trigger replaces the combination and frees that
// button). No weapon rides on jump: R held must still jump. The missile is never on the run action:
// R + run is the shortcut of another weapon, so missiles are not fired while running (R + run + shoot
// is not practical to press). Weapons follow their action when the actions move. The defaults are the
// scheme as it always was: A jump, B run, X shoot, Y morph ball, and R + X missile, R + B X-ray, R + Y
// grapple.
typedef enum { kModernJump, kModernRun, kModernShoot, kModernMorph, kModernActions } ModernAction;
typedef enum { kModernMissile, kModernXray, kModernGrapple, kModernWeapons } ModernWeapon;
// The four face buttons the actions use, then the two triggers only a weapon can have.
typedef enum { kFaceA, kFaceB, kFaceX, kFaceY, kFaceButtons, kFaceZL = kFaceButtons, kFaceZR, kWeaponButtons } ModernFace;

// The frontend's pad bits for ZL and ZR (above the twelve SNES ones of ModernControls_Translate):
// they never reach the game.
enum { kModernPadZL = 1 << 12, kModernPadZR = 1 << 13 };

// The face button an action is on, the weapon an action carries (-1 for none, also for a weapon on
// a trigger), the button a weapon is used with (its action's, held with R, or kFaceZL / kFaceZR alone)
// and whether that needs R.
ModernFace ModernControls_ButtonOf(ModernAction action);
int ModernControls_ActionWeapon(ModernAction action);
ModernFace ModernControls_WeaponButton(ModernWeapon weapon);
bool ModernControls_WeaponWithR(ModernWeapon weapon);
// "R+X" or "ZR": what a weapon is pressed with, as the bottom screen writes it (`sep` between R and the
// button, "+" or " + ").
const char *ModernControls_WeaponKeys(ModernWeapon weapon, const char *sep);

// Puts an action on a face button; the action that was there takes its old one. The weapons stay with
// their actions, so moving actions never breaks the rules above. A trigger is ignored.
void ModernControls_SetButton(ModernAction action, ModernFace face);
// Puts a weapon on the action that has `face` (R + it) or on a trigger; a weapon already there trades
// places with it. Returns false (nothing changes) for any weapon on jump or the missile on run.
bool ModernControls_SetWeaponButton(ModernWeapon weapon, ModernFace face);
void ModernControls_ResetMapping(void);
// The whole table in one number (to tell whether it changed), and from the config.ini names.
unsigned ModernControls_MappingKey(void);
const char *ModernControls_FaceName(ModernFace face);         // "A", "B", "X", "Y", "ZL", "ZR"
const char *ModernControls_ActionName(ModernAction action);   // "jump", "run", "shoot", "morph"
// config.ini's `modern_with_*` value: the action a weapon rides on, or "ZL" / "ZR".
const char *ModernControls_WeaponPlaceName(ModernWeapon weapon);
bool ModernControls_FaceFromName(const char *name, ModernFace *out);
bool ModernControls_ActionFromName(const char *name, ModernAction *out);
// The trigger (kFaceZL / kFaceZR) pressed in the last translated frame, or -1: the game's CONTROLLER
// SETTING MODE takes it for a weapon row (the triggers never reach the game).
int ModernControls_TriggerPressed(void);

// `inputs` (the frontend's pad bits, SNES order from bit 0: B Y SELECT START Up Down Left Right
// A X L R) as the game must see them this frame. With the original controls it returns them as
// they are; with the modern ones, during gameplay, it translates them and asks the game for the
// item the buttons want. Called once a game frame, before it runs. kModernPadZL / kModernPadZR in
// `inputs` are only read by the modern scheme and never returned.
int ModernControls_Translate(int inputs);
