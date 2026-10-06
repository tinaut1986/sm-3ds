// updater.h for the UI preview: the state and prompt are set by the preview itself, no network.
#include "updater.h"

#include <string.h>

UpdState g_preview_upd_state = UPD_IDLE;
UpdPrompt g_preview_upd_prompt = UPD_PROMPT_NONE;
int g_preview_upd_progress;
bool g_preview_upd_kept;
const char *g_preview_upd_message = "";

void Updater_Init(bool auto_check, bool beta) { (void)auto_check, (void)beta; }
void Updater_SetBeta(bool beta) { (void)beta; }
void Updater_CheckNow(void) { g_preview_upd_state = UPD_CHECKING; }
UpdState Updater_State(void) { return g_preview_upd_state; }
UpdPrompt Updater_Prompt(void) { return g_preview_upd_prompt; }
void Updater_AnswerPrompt(bool yes) { (void)yes; g_preview_upd_prompt = UPD_PROMPT_NONE; }
int Updater_Progress(void) { return g_preview_upd_progress; }
const char *Updater_RemoteTag(void) { return "v0.2.4"; }
const char *Updater_Message(void) { return g_preview_upd_message; }
bool Updater_KeptCia(void) { return g_preview_upd_kept; }
uint32_t Updater_Version(void) { return ((uint32_t)g_preview_upd_state << 24) | ((uint32_t)g_preview_upd_prompt << 16) | (uint32_t)g_preview_upd_progress; }
