#pragma once
// Self-updater for the installed CIA, after ../mzm's (port_updater_3ds.c). Asks GitHub's
// releases endpoint for the newest build, downloads its .cia to the data folder and installs
// it with the system installer (am:net). Everything runs on a worker thread; the UI polls.
// The pure part (version comparison, release-list parsing) is updater_parse.c, host-tested
// in tools/updater-test.

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  UPD_IDLE = 0,
  UPD_CHECKING,
  UPD_UP_TO_DATE,
  UPD_AVAILABLE,     // a newer build was found, waiting for the player
  UPD_DOWNLOADING,   // downloading, then installing
  UPD_INSTALLED,     // installed; restart to run it
  UPD_ERROR
} UpdState;

// What the prompt drawn over any bottom-screen tab asks. Nothing installs by itself: a found
// update only raises ASK_INSTALL.
typedef enum {
  UPD_PROMPT_NONE = 0,
  UPD_PROMPT_ASK_INSTALL,    // a new version: install? YES/NO
  UPD_PROMPT_PROGRESS,       // installing, no buttons
  UPD_PROMPT_ASK_RESTART,    // installed: restart now? YES/NO
  UPD_PROMPT_ERROR           // the install failed; OK dismisses
} UpdPrompt;

// Called once the config is loaded. `auto_check`: look for an update now, in the background
// (a failure, as with no Wi-Fi, says nothing). `beta`: also offer pre-releases.
void Updater_Init(bool auto_check, bool beta);

// A manual check; a newer build raises the install prompt, the result is in the state.
void Updater_CheckNow(void);

UpdState Updater_State(void);
UpdPrompt Updater_Prompt(void);
void Updater_AnswerPrompt(bool yes);   // YES/NO of the current prompt (OK = either)
int Updater_Progress(void);            // 0..100 while DOWNLOADING
const char *Updater_RemoteTag(void);   // "" until a check succeeded
const char *Updater_Message(void);     // a short error text
bool Updater_KeptCia(void);            // a failed install left update/sm-update.cia for FBI
uint32_t Updater_Version(void);        // changes with every state, so the UI redraws
