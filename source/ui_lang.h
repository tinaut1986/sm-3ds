// Languages of the bottom-screen UI (OPTIONS -> LANGUAGE, `language` in config.ini).
// Only what a player sees is translated: the DEBUG tab, the debug tools and the
// DEBUG_TOOLS-only parts of the other tabs stay in English, as do the game's own names
// (areas, items, beams, ammo).
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
  kStrEnergy, kStrMax, kStrReserve, kStrAuto, kStrManual, kStrItems, kStrBeams, kStrMapStations,
  kStrSaveStates, kStrEmpty, kStrSavedNoDetails, kStrTapTwice,
  kStrSavedSlot, kStrSaveFailed, kStrLoadedSlot, kStrLoadFailed, kStrGameReset,
  kStrPause, kStrTurbo, kStrFrameSkip, kStrAudio, kStrFpsOverlay, kStrDisplay, kStrPixelPerfect,
  kStrScaled, kStrWideView, kStrLanguage, kStrResetGame, kStrFrameSkipOffToast,
  kStrResetQuestion, kStrResetLost1, kStrResetLost2, kStrReset, kStrCancel,
  kStrCount
} UiStr;

extern UiLang g_ui_lang;

// The text in the current language (English when a translation is missing).
const char *Tr(UiStr id);

// A language's name in itself (ESPAÑOL, FRANÇAIS...).
const char *UiLang_Name(UiLang lang);

// The console's language when the UI has it, else English.
UiLang UiLang_FromSystem(void);
