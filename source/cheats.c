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
  kBeamSpazer = 0x0004,
  kBeamPlasma = 0x0008,
};

// Capacities before MAX was switched on.
static struct { uint16_t health, reserve, missiles, supers, power_bombs, reserve_mode; } g_before_max;

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
  if (g_cheats.max_mode) RefillAmmo();
}

void Cheats_AfterFrame(void) {
  Cheats_BeforeFrame();
}

bool Cheats_ToggleItem(int index) {
  if (!Cheats_InGameplay() || index < 0 || index >= kSmItemCount) return false;
  const uint16_t m = kSmItems[index].mask;
  if (equipped_items & m) {
    collected_items &= ~m;
    equipped_items &= ~m;
  } else {
    collected_items |= m;
    equipped_items |= m;
  }
  return true;
}

bool Cheats_ToggleBeam(int index) {
  if (!Cheats_InGameplay() || index < 0 || index >= kSmBeamCount) return false;
  const uint16_t m = kSmBeams[index].mask;
  if (equipped_beams & m) {
    collected_beams &= ~m;
    equipped_beams &= ~m;
    return true;
  }
  collected_beams |= m;
  if (m == kBeamSpazer) equipped_beams &= ~kBeamPlasma;
  if (m == kBeamPlasma) equipped_beams &= ~kBeamSpazer;
  equipped_beams |= m;
  return true;
}

static void Clamp(uint16_t *cur, uint16_t max) {
  if (*cur > max) *cur = max;
}

bool Cheats_SetMax(bool on) {
  if (!Cheats_InGameplay() || on == g_cheats.max_mode) return false;
  if (on) {
    g_before_max.health = samus_max_health;
    g_before_max.reserve = samus_max_reserve_health;
    g_before_max.missiles = samus_max_missiles;
    g_before_max.supers = samus_max_super_missiles;
    g_before_max.power_bombs = samus_max_power_bombs;
    g_before_max.reserve_mode = reserve_health_mode;
    samus_max_health = kMaxEnergy;
    samus_max_reserve_health = kMaxReserve;
    samus_max_missiles = kMaxMissiles;
    samus_max_super_missiles = kMaxSupers;
    samus_max_power_bombs = kMaxPowerBombs;
    if (!reserve_health_mode) reserve_health_mode = 1;   // auto-use, as a normal pickup sets it
    RefillEnergy();
    RefillAmmo();
  } else {
    samus_max_health = g_before_max.health;
    samus_max_reserve_health = g_before_max.reserve;
    samus_max_missiles = g_before_max.missiles;
    samus_max_super_missiles = g_before_max.supers;
    samus_max_power_bombs = g_before_max.power_bombs;
    reserve_health_mode = g_before_max.reserve_mode;
    Clamp(&samus_health, samus_max_health);
    Clamp(&samus_reserve_health, samus_max_reserve_health);
    Clamp(&samus_missiles, samus_max_missiles);
    Clamp(&samus_super_missiles, samus_max_super_missiles);
    Clamp(&samus_power_bombs, samus_max_power_bombs);
  }
  g_cheats.max_mode = on;
  return true;
}

bool Cheats_GiveAll(void) {
  if (!Cheats_InGameplay()) return false;
  for (int i = 0; i < kSmItemCount; i++) {
    collected_items |= kSmItems[i].mask;
    equipped_items |= kSmItems[i].mask;
  }
  for (int i = 0; i < kSmBeamCount; i++)
    collected_beams |= kSmBeams[i].mask;
  equipped_beams = (equipped_beams & ~kBeamSpazer) | 0x1000 | 0x0002 | 0x0001 | kBeamPlasma;
  return true;
}

bool Hud_RequestSelect(int item) {
  if (game_state != 0x08 || item < kSmHudNone || item > kSmHudXray) return false;
  g_rtl_hud_select.frame = nmi_frame_counter_word;
  g_rtl_hud_select.item = (int8)item;
  return true;
}
