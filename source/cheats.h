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
  bool invincible;   // GOD: energy and reserves are refilled around every frame
  bool max_mode;     // MAX: capacities at their maximum and ammo refilled every frame
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

// The actions below return false (and do nothing) outside of gameplay.

// Items and beams: missing -> collected and equipped -> missing. Spazer and Plasma
// are never equipped together (the game's beam tables have no such combination).
bool Cheats_ToggleItem(int index);
bool Cheats_ToggleBeam(int index);

// MAX on: remembers the current capacities, then 14 energy tanks, 4 reserve tanks,
// 230/50/50 ammo, kept full. MAX off: gives the remembered capacities back.
bool Cheats_SetMax(bool on);

// Every item and beam, collected and equipped (Plasma rather than Spazer). Capacities
// are MAX's job.
bool Cheats_GiveAll(void);
