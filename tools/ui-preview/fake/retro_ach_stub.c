// retro_ach.h for the UI preview: a logged-in player with a few achievements, no network.
#include "retro_ach.h"

#include <stddef.h>

static RaAchievement g_list[] = {
  { 1, "Ceres Station Escape", "Escape Ceres Station before it explodes", 5, true },
  { 2, "Bomb Torizo", "Defeat Bomb Torizo", 10, true },
  { 3, "Kraid's Lair Cleared Out and Then Some More Words", "Defeat Kraid in his lair deep in Brinstar, without taking any damage from his spikes", 25, false },
  { 4, "Phantoon", "Defeat Phantoon", 10, false },
  { 5, "Draygon", "Defeat Draygon", 10, false },
  { 6, "Ridley", "Defeat Ridley", 25, false },
  { 7, "Mother Brain", "Defeat Mother Brain", 50, false },
  { 8, "Spore Spawn", "Defeat Spore Spawn", 5, false },
  { 9, "Crocomire", "Defeat Crocomire", 10, false },
  { 10, "Botwoon", "Defeat Botwoon", 5, false },
  { 11, "Golden Torizo", "Defeat Golden Torizo", 10, false },
  { 12, "100%", "Collect every item", 50, false },
};
static bool g_on = true;
bool g_preview_ra_toast;

void RetroAch_NoteCheat(void) {}
bool RetroAch_CheatsUsed(void) { return false; }
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
void RetroAch_MixAudio(int16_t *out, int frames) { (void)out, (void)frames; }
