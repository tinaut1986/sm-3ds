// Languages of the bottom-screen UI (OPTIONS -> LANGUAGE, `language` in config.ini); the
// game's own message boxes follow it too (game_text.c). Only what a player sees is translated: the DEBUG tab, the debug tools and the
// DEBUG_TOOLS-only parts of the other tabs stay in English. The game's own names (items,
// beams, ammo, areas) follow the official Spanish and French ones where Nintendo has them
// (Zero Mission, the Prime and later games), see TrItem.
//
// Strings are UTF-8; the 5x7 font has the accented capitals they need (ui_font.c).
// Width: the UI has no wrapping, so a translation must fit where the English one is
// drawn (check with tools/ui-preview, which renders the tabs in every language).
#pragma once

// The order is what config.ini stores: append only. kLangJa is the game's Japanese text (the
// intro's subtitles, japanese_text_flag); everything else, this UI too, is in English with it:
// only the first kTextLangCount have strings of their own.
typedef enum { kLangEn, kLangEs, kLangCa, kLangFr, kLangPt, kLangJa, kLangCount } UiLang;
enum { kTextLangCount = kLangJa };

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
  kStrCount
} UiStr;

extern UiLang g_ui_lang;

// The language of the strings: g_ui_lang, or English for a language without its own (kLangJa).
UiLang UiLang_Text(void);

// Called after the game's OPTION MODE picked a language (game_text_screens.c), to save it.
extern void (*g_ui_lang_on_change)(void);

// The text in the current language (English when a translation is missing).
const char *Tr(UiStr id);

// A language's name in itself (ESPAÑOL, FRANÇAIS...).
const char *UiLang_Name(UiLang lang);

// The console's language when the port has it (Japanese: the game's Japanese text), else English.
UiLang UiLang_FromSystem(void);

// The game's names for the Status tab: an item or beam in the order of kSmItems / kSmBeams
// (cheats.c), an ammo kind (missiles, supers, power bombs), an area by area_index (0-6, then
// Debug). Short forms: an item fits 11 characters, a beam 8, ammo 7, an area code 3.
const char *TrItem(int i);
const char *TrBeam(int i);
const char *TrAmmo(int i);
const char *TrArea(int area);
const char *TrAreaShort(int area);
