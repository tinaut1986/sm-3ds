// The modern control scheme (OPTIONS -> CONTROLS, issue #32, docs/modern-controls.md): Samus
// always runs, L aims, R + a button uses a weapon, SELECT only switches missiles / super missiles.
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

// `inputs` (the frontend's pad bits, SNES order from bit 0: B Y SELECT START Up Down Left Right
// A X L R) as the game must see them this frame. With the original controls it returns them as
// they are; with the modern ones, during gameplay, it translates them and asks the game for the
// item the buttons want. Called once a game frame, before it runs.
int ModernControls_Translate(int inputs);
