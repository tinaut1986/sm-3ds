// RetroAchievements (docs/PLAN.md P4.4), on top of rcheevos (third_party/rcheevos), after
// ../mzm/platform/3ds/source/port_retroachievements_3ds.c.
//
// The game keeps its state in g_ram with the SNES layout (that is how snesrev checks it
// against the ROM), so the existing Super Metroid set runs unchanged: rcheevos reads
// "System RAM" from g_ram and "Cartridge RAM" from g_sram. Softcore only: RA does not
// sanction unofficial ports. Once a cheat or the teleport has been used (DEBUG_TOOLS
// builds) nothing more is evaluated until the app restarts.
//
// Every rc_client call is made from the main thread; HTTP runs on a worker thread and
// its responses are handed back in RetroAch_Update. Login: the system keyboard asks for
// the user name and password once; the token the server returns is kept in
// retroachievements.ini in the data folder. Log: debug/retroachievements.log.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  kRaOff,          // switched off
  kRaNoAccount,    // on, but nothing to log in with
  kRaConnecting,
  kRaOnline,
  kRaOffline,      // the server could not be reached (unlocks wait and are retried)
  kRaLoginError,   // the server rejected the login
} RaStatus;

typedef struct {
  uint32_t id;
  char title[64];
  char description[128];
  uint32_t points;
  bool unlocked;
  uint32_t unlock_time;   // seconds since 1970, 0 when locked
} RaAchievement;

// How the list is ordered (mzm's choices): as rcheevos groups it (locked first), by title,
// by points, or by unlock time (still locked ones last).
typedef enum { kRaSortDefault, kRaSortTitle, kRaSortPoints, kRaSortRecent, kRaSortCount } RaSort;

// Init reads retroachievements.ini and logs in if it is on and has a token.
void RetroAch_Init(void);
void RetroAch_Shutdown(void);
// Every main loop iteration (network responses, keep-alive).
void RetroAch_Update(void);
// After each game frame.
void RetroAch_DoFrame(void);
// The game was reset, or a save state was saved or loaded (slot 0..9): the progress
// of each achievement goes with the state, in saves/saveN.rap.
void RetroAch_GameReset(void);
void RetroAch_StateSaved(int slot);
void RetroAch_StateLoaded(int slot);
// A cheat or the teleport changed the game: stop evaluating until the app restarts.
void RetroAch_NoteCheat(void);
bool RetroAch_CheatsUsed(void);

bool RetroAch_Enabled(void);
void RetroAch_SetEnabled(bool on);
// Asks for the user name and password with the system keyboard (blocks while it shows).
void RetroAch_PromptLogin(void);
void RetroAch_Logout(void);

RaStatus RetroAch_Status(void);
const char *RetroAch_User(void);
// Last thing worth telling the player (server error, "ROM not recognised"...), or "".
const char *RetroAch_Message(void);
bool RetroAch_GameLoaded(void);

int RetroAch_Count(void);
const RaAchievement *RetroAch_Get(int i);   // in the list's order (RetroAch_SetSort)
int RetroAch_UnlockedCount(void);
uint32_t RetroAch_Points(bool unlocked_only);

// Bumped whenever anything above changes, so the UI knows to redraw.
uint32_t RetroAch_Version(void);

// The last unlock, while its notice should show (3 s); NULL otherwise.
const RaAchievement *RetroAch_Toast(void);

// Settings, kept in retroachievements.ini like mzm's: the notice on the top screen
// (otherwise the bottom one), the unlock sound, the list's order.
bool RetroAch_NotifyTop(void);
void RetroAch_SetNotifyTop(bool top);
bool RetroAch_Sound(void);
void RetroAch_SetSound(bool on);
RaSort RetroAch_Sort(void);
bool RetroAch_Descending(void);
void RetroAch_SetSort(RaSort sort, bool descending);
// A sample notice (and the sound), to see where it shows.
void RetroAch_ShowPreview(void);
// Adds the unlock sound, when one is playing, to `frames` frames of 16-bit stereo audio at
// 44100 Hz. Called from the audio thread.
void RetroAch_MixAudio(int16_t *out, int frames);
