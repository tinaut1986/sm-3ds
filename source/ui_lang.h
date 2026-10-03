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

// The order is what config.ini stores: append only.
typedef enum { kLangEn, kLangEs, kLangCa, kLangFr, kLangPt, kLangCount } UiLang;

typedef enum {
  kStrOn, kStrOff,
  kStrFollow, kStrCells, kStrMapMark,
  kStrEnergy, kStrMax, kStrReserve, kStrAuto, kStrManual, kStrItems, kStrBeams, kStrMapStations, kStrRoom,
  kStrSaveStates, kStrEmpty, kStrSavedNoDetails, kStrTapTwice,
  kStrSavedSlot, kStrSaveFailed, kStrLoadedSlot, kStrLoadFailed, kStrGameReset,
  kStrPause, kStrTurbo, kStrFrameSkip, kStrAudio, kStrFpsOverlay, kStrDisplay, kStrPixelPerfect,
  kStrScaled, kStrWideView, kStrLanguage, kStrResetGame, kStrFrameSkipOffToast,
  kStrResetQuestion, kStrResetLost1, kStrResetLost2, kStrReset, kStrCancel,
  kStrRaAchievements, kStrRaLogin, kStrRaLogout, kStrRaDisabled, kStrRaNoAccount, kStrRaConnecting, kStrRaOnline,
  kStrRaOffline, kStrRaLoginError, kStrRaSummary, kStrRaCheats, kStrRaLoading, kStrRaNoList, kStrRaUnlocked,
  kStrRaNotify, kStrRaTop, kStrRaBottom, kStrRaSound, kStrRaSortDefault, kStrRaSortTitle, kStrRaSortPoints,
  kStrRaSortRecent, kStrRaPoints, kStrRaLockedState, kStrRaUnlockedState, kStrRaMissable, kStrRaProgression,
  kStrRaWin, kStrClose,
  kStrCount
} UiStr;

extern UiLang g_ui_lang;

// The text in the current language (English when a translation is missing).
const char *Tr(UiStr id);

// A language's name in itself (ESPAÑOL, FRANÇAIS...).
const char *UiLang_Name(UiLang lang);

// The console's language when the UI has it, else English.
UiLang UiLang_FromSystem(void);

// The game's names for the Status tab: an item or beam in the order of kSmItems / kSmBeams
// (cheats.c), an ammo kind (missiles, supers, power bombs), an area by area_index (0-6, then
// Debug). Short forms: an item fits 11 characters, a beam 8, ammo 7, an area code 3.
const char *TrItem(int i);
const char *TrBeam(int i);
const char *TrAmmo(int i);
const char *TrArea(int area);
const char *TrAreaShort(int area);
