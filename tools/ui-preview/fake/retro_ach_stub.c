// retro_ach.h for the UI preview: a logged-in player with a few achievements, no network.
#include "retro_ach.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static RaAchievement g_list[] = {
  { 1, "Ceres Station Escape", "Escape Ceres Station before it explodes", "1", 5, kRaTypeProgression, true, 1759400000 },
  { 2, "Bomb Torizo", "Defeat Bomb Torizo", "2", 10, kRaTypeProgression, true, 1759480000 },
  { 3, "Kraid's Lair Cleared Out and Then Some More Words", "Defeat Kraid in his lair deep in Brinstar, without taking any damage from his spikes", "3", 25, kRaTypeMissable, false },
  { 4, "Phantoon", "Defeat Phantoon", "4", 10, kRaTypeProgression, false },
  { 5, "Draygon", "Defeat Draygon", "5", 10, kRaTypeProgression, false },
  { 6, "Ridley", "Defeat Ridley", "6", 25, kRaTypeProgression, false },
  { 7, "Mother Brain", "Defeat Mother Brain", "7", 50, kRaTypeWin, false },
  { 8, "Spore Spawn", "Defeat Spore Spawn", "8", 5, kRaTypeStandard, false },
  { 9, "Crocomire", "Defeat Crocomire", "9", 10, kRaTypeStandard, false },
  { 10, "Botwoon", "Defeat Botwoon", "10", 5, kRaTypeStandard, false },
  { 11, "Golden Torizo", "Defeat Golden Torizo", "11", 10, kRaTypeStandard, false },
  { 12, "100%", "Collect every item", "12", 50, kRaTypeStandard, false },
};
static bool g_on = true;
bool g_preview_ra_toast;

bool RetroAch_Enabled(void) { return g_on; }
void RetroAch_SetEnabled(bool on) { g_on = on; }
void RetroAch_PromptLogin(void) {}
void RetroAch_Logout(void) {}
RaStatus RetroAch_Status(void) { return g_on ? kRaOnline : kRaOff; }
const char *RetroAch_User(void) { return "SamusFan"; }
const char *RetroAch_Message(void) { return ""; }
bool RetroAch_GameLoaded(void) { return true; }
int RetroAch_Count(void) { return g_on ? (int)(sizeof(g_list) / sizeof(g_list[0])) : 0; }
const RaAchievement *RetroAch_Get(int i) { return i >= 0 && i < RetroAch_Count() ? &g_list[i] : NULL; }
int RetroAch_UnlockedCount(void) { return 2; }
uint32_t RetroAch_Points(bool unlocked_only) { return unlocked_only ? 15 : 215; }
uint32_t RetroAch_Version(void) { return g_preview_ra_toast; }
const RaAchievement *RetroAch_Toast(void) { return g_preview_ra_toast ? &g_list[1] : NULL; }
static bool g_top, g_snd = true, g_desc;
static RaSort g_sort;
bool RetroAch_NotifyTop(void) { return g_top; }
void RetroAch_SetNotifyTop(bool top) { g_top = top; }
bool RetroAch_Sound(void) { return g_snd; }
void RetroAch_SetSound(bool on) { g_snd = on; }
RaSort RetroAch_Sort(void) { return g_sort; }
bool RetroAch_Descending(void) { return g_desc; }
void RetroAch_SetSort(RaSort sort, bool descending) { g_sort = sort, g_desc = descending; }
void RetroAch_ShowPreview(void) {}
void RetroAch_MixAudio(int16_t *out, int frames, int rate) { (void)out, (void)frames, (void)rate; }

// Made-up badges: a ring in a colour per achievement on a dark square. Badge "4" never
// loads, to show the placeholder.
const uint32_t *RetroAch_Badge(const char *badge, int size) {
  static uint32_t pixels[13][2][kRaBadgeBig * kRaBadgeBig];
  const int id = atoi(badge);
  if (id <= 0 || id > 12 || id == 4) return NULL;
  uint32_t *px = pixels[id][size == kRaBadgeBig];
  const unsigned r = 60 + id * 37 % 190, g = 60 + id * 71 % 190, b = 60 + id * 113 % 190;
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) {
      const int dx = 2 * x - size + 1, dy = 2 * y - size + 1, d = dx * dx + dy * dy, q = size * size;
      const bool ring = d < q * 7 / 10 && d > q * 3 / 10;
      px[y * size + x] = ring ? r << 24 | g << 16 | b << 8 | 0xFF : d < q * 3 / 10 ? 0xF0E0A0FFu : 0x202838FFu;
    }
  return px;
}
