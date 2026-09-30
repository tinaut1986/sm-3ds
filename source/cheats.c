#include "cheats.h"

#include "src/types.h"
#include "src/sm_rtl.h"
#include "src/variables.h"

Cheats g_cheats;

const SmFlag kSmItems[kSmItemCount] = {
  { 0x0001, "VARIA" },  { 0x0020, "GRAV" },   { 0x0004, "MORPH" }, { 0x1000, "BOMB" },
  { 0x0100, "HIJUMP" }, { 0x0200, "SPACE" },  { 0x2000, "SPEED" }, { 0x0008, "SCREW" },
  { 0x0002, "SPRING" }, { 0x4000, "GRAPPL" }, { 0x8000, "XRAY" },
};

const SmFlag kSmBeams[kSmBeamCount] = {
  { 0x1000, "CHARGE" }, { 0x0002, "ICE" }, { 0x0001, "WAVE" }, { 0x0004, "SPAZER" }, { 0x0008, "PLASMA" },
};

const char *const kSmAreaNames[8] = {
  "Crateria", "Brinstar", "Norfair", "Wrecked Ship", "Maridia", "Tourian", "Ceres", "Debug",
};

enum {
  kMaxMissiles = 230,
  kMaxSupers = 50,
  kMaxPowerBombs = 50,
  kMaxEnergy = 1499,    // 99 + 14 tanks * 100
  kMaxReserve = 400,    // 4 reserve tanks
};

bool Cheats_InGameplay(void) {
  // 0x08 main gameplay, 0x09-0x0B door transitions, 0x0C-0x12 pause menu.
  return game_state >= 0x08 && game_state <= 0x12;
}

static void RefillEnergy(void) {
  if (samus_max_health) samus_health = samus_max_health;
  if (samus_max_reserve_health) samus_reserve_health = samus_max_reserve_health;
}

static void RefillAmmo(void) {
  samus_missiles = samus_max_missiles;
  samus_super_missiles = samus_max_super_missiles;
  samus_power_bombs = samus_max_power_bombs;
}

// Damage can kill within the same frame that dealt it, so refill both before
// the frame (covers anything that would leave us at or below zero) and after it.
void Cheats_BeforeFrame(void) {
  if (!Cheats_InGameplay()) return;
  if (g_cheats.invincible) RefillEnergy();
  if (g_cheats.infinite_ammo) RefillAmmo();
}

void Cheats_AfterFrame(void) {
  Cheats_BeforeFrame();
}

bool Cheats_FullHeal(void) {
  if (!Cheats_InGameplay()) return false;
  RefillEnergy();
  RefillAmmo();
  return true;
}

bool Cheats_GiveAllItems(void) {
  if (!Cheats_InGameplay()) return false;
  for (int i = 0; i < kSmItemCount; i++) {
    collected_items |= kSmItems[i].mask;
    equipped_items |= kSmItems[i].mask;
  }
  return true;
}

bool Cheats_GiveAllBeams(void) {
  if (!Cheats_InGameplay()) return false;
  for (int i = 0; i < kSmBeamCount; i++)
    collected_beams |= kSmBeams[i].mask;
  // Spazer and Plasma are never equipped together: the game's beam tables have no
  // entry for that combination and it ends in Unreachable(). Equip Plasma.
  equipped_beams = (equipped_beams & ~0x0004) | 0x1000 | 0x0002 | 0x0001 | 0x0008;
  return true;
}

bool Cheats_MaxAmmo(void) {
  if (!Cheats_InGameplay()) return false;
  samus_max_missiles = kMaxMissiles;
  samus_max_super_missiles = kMaxSupers;
  samus_max_power_bombs = kMaxPowerBombs;
  RefillAmmo();
  return true;
}

bool Cheats_MaxEnergy(void) {
  if (!Cheats_InGameplay()) return false;
  samus_max_health = kMaxEnergy;
  samus_max_reserve_health = kMaxReserve;
  if (!reserve_health_mode) reserve_health_mode = 1;   // auto-use, as a normal pickup sets it
  RefillEnergy();
  return true;
}
