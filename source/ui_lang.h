// Languages of the bottom-screen UI and of the game's own texts (OPTIONS -> LANGUAGE, the
// game's OPTION MODE -> LANGUAGE, `language` in config.ini). The strings are in files, one per
// language (lang_file.h, docs/translations.md): this file keeps the English, which is also each
// string's key in the file. Only what a player sees is translated: the DEBUG tab, the debug tools
// and the DEBUG_TOOLS-only parts of the other tabs stay in English. The game's own names (items,
// beams, ammo, areas) follow the official Spanish and French ones where Nintendo has them
// (Zero Mission, the Prime and later games), see the [items] notes in romfs/lang/es.txt.
//
// Strings are UTF-8; the 5x7 font has the accented capitals they need (ui_font.c).
// Width: the UI has no wrapping, so a translation must fit where the English one is
// drawn (check with tools/ui-preview, which renders the tabs in every language).
#pragma once

#include <stdbool.h>

// A language: its index in the list of those offered (lang_file.h: English first, then the files,
// Japanese last). config.ini stores its code, not the index.
typedef int UiLang;
enum { kLangEn = 0 };

typedef enum {
  kStrOn, kStrOff,
  kStrFollow, kStrCells, kStrMapMark,
  kStrEnergy, kStrMax, kStrReserve, kStrAuto, kStrManual, kStrItems, kStrBeams, kStrMapStations, kStrRoom,
  kStrSaveStates, kStrSavedNoDetails,
  kStrSavedSlot, kStrSaveFailed, kStrLoadedSlot, kStrLoadFailed, kStrGameReset,
  kStrPacing, kStrAudio, kStrDisplay, kStrPixelP,
  kStrScaled, kStrView, kStrLanguage, kStrResetGame, kStrFrameSkipOffToast,
  kStrResetQuestion, kStrResetLost1, kStrResetLost2, kStrReset, kStrCancel,
  kStrRaAchievements, kStrRaLogin, kStrRaLogout, kStrRaDisabled, kStrRaNoAccount, kStrRaConnecting, kStrRaOnline,
  kStrRaOffline, kStrRaLoginError, kStrRaSummary, kStrRaLoading, kStrRaNoList, kStrRaUnlocked,
  kStrRaNotify, kStrRaTop, kStrRaBottom, kStrRaSound, kStrRaSortDefault, kStrRaSortTitle, kStrRaSortPoints,
  kStrRaSortRecent, kStrRaPoints, kStrRaLockedState, kStrRaUnlockedState, kStrRaMissable, kStrRaProgression,
  kStrRaWin, kStrClose,
  kStrUpdate, kStrUpdates, kStrUpdTap, kStrUpdChecking, kStrUpdUpToDate, kStrUpdNew, kStrUpdInstalled,
  kStrUpdError, kStrUpdAsk, kStrUpdAsk2, kStrUpdInstalling, kStrUpdRestart, kStrUpdFailed, kStrUpdKept,
  kStrYes, kStrNo, kStrOk,
  kStrNewState, kStrStatesNone, kStrNoImage, kStrMark, kStrTime, kStrLoad, kStrSaveOver, kStrDelete, kStrStateDeleted, kStrWait,
  kStrPaceLock, kStrPaceNoSkip, kStrViewOriginal, kStrViewWide, kStrChannel, kStrChanStable, kStrChanBeta,
  kStrHud, kStrHudHidden, kStrHudShown,
  kStrWhatsNew, kStrNotesEmpty, kStrUpdFrom,
  kStrUpdsLabel, kStrUpdsCheck, kStrUpdsChecking, kStrUpdsNew, kStrUpdsInstalling, kStrUpdsRestart, kStrUpdsError,
  kStrSpoilers, kStrArea, kStrMajorShort, kStrTotalShort, kStrUniqueItems, kStrNone,
  kStrControls, kStrClassic, kStrModern,
  kStrCtlMove, kStrCtlMoveRun, kStrCtlJump, kStrCtlRun, kStrCtlWalk, kStrCtlFire, kStrCtlFireBomb, kStrCtlChoose, kStrCtlCancel, kStrCtlAimUp, kStrCtlAimDown, kStrCtlAimDownLock, kStrCtlPause, kStrCtlMorph, kStrCtlMissile, kStrCtlGrapple, kStrCtlXray, kStrCtlMissileKind, kStrCtlConfigNote,
  kStrCount
} UiStr;

extern UiLang g_ui_lang;   // set it with UiLang_Set

int UiLang_Count(void);
// Makes `lang` the language and reads its strings (English if it cannot).
void UiLang_Set(UiLang lang);
// The language has strings of its own (not English, not Japanese).
bool UiLang_Translated(void);
// The game's Japanese text (japanese_text_flag) goes with this language.
bool UiLang_IsJapanese(UiLang lang);
// A language's code ("es"), and the language with a code (-1: none).
const char *UiLang_Code(UiLang lang);
UiLang UiLang_FromCode(const char *code);

// The text in the current language (English when a translation is missing).
const char *Tr(UiStr id);

// The current language's text for the English `key` in a section of its file (the game's
// screens and message boxes); NULL when it has none, so the English stays.
const char *UiLang_Get(const char *section, const char *key);

// A language's name in itself (ESPAÑOL, FRANÇAIS...).
const char *UiLang_Name(UiLang lang);

// The console's language when the port has it (Japanese: the game's Japanese text), else English.
UiLang UiLang_FromSystem(void);

// Called after the game's OPTION MODE picked a language (game_text_screens.c), to save it.
extern void (*g_ui_lang_on_change)(void);

// The game's names for the Status tab: an item or beam in the order of kSmItems / kSmBeams
// (cheats.c), an ammo kind (missiles, supers, power bombs), an area by area_index (0-6, then
// Debug). Short forms: an item fits 11 characters, a beam 8, ammo 7, an area code 3.
const char *TrItem(int i);
const char *TrBeam(int i);
const char *TrAmmo(int i);
const char *TrArea(int area);
const char *TrAreaShort(int area);

// Every key of the language files the UI looks up ([ui] and the game's names), as
// GameText_ForEachKey (game_text.h).
void UiLang_ForEachKey(void (*fn)(const char *section, const char *key, const char *note));
