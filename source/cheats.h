// Debug cheats that write straight into the game's RAM.
//
// They act on the native game state (g_ram), so they need no ROM patching and
// work the same on the console as on the PC build. All of them do nothing
// outside of gameplay, so a stray tap at the title screen cannot corrupt the
// save-file loading.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  bool invincible;      // energy and reserves are refilled around every frame
  bool infinite_ammo;   // missiles, supers and power bombs refilled around every frame
} Cheats;

extern Cheats g_cheats;

typedef struct {
  uint16_t mask;
  const char *name;   // up to 6 characters, to fit the status grid
} SmFlag;

// Bits of collected_items / equipped_items and collected_beams / equipped_beams.
enum { kSmItemCount = 11, kSmBeamCount = 5 };
extern const SmFlag kSmItems[kSmItemCount];
extern const SmFlag kSmBeams[kSmBeamCount];
extern const char *const kSmAreaNames[8];

// True while the game is running a room (including doors and the pause menu),
// false at the title, file select, death and ending.
bool Cheats_InGameplay(void);

// Called by the main loop around RtlRunFrame.
void Cheats_BeforeFrame(void);
void Cheats_AfterFrame(void);

// One-shot actions. Return false (and do nothing) outside of gameplay.
bool Cheats_FullHeal(void);       // energy, reserves and ammo up to their maximum
bool Cheats_GiveAllItems(void);   // suits, boots, morph ball and the rest
bool Cheats_GiveAllBeams(void);
bool Cheats_MaxAmmo(void);        // 230 missiles, 50 super missiles, 50 power bombs
bool Cheats_MaxEnergy(void);      // 14 energy tanks + 4 reserve tanks
