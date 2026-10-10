#include "ui_lang.h"

#include <stddef.h>
#include <string.h>

#include "lang_file.h"

#ifdef __3DS__
#include <3ds.h>
#endif

UiLang g_ui_lang = kLangEn;
void (*g_ui_lang_on_change)(void);
static bool g_translated;   // g_ui_lang has a file of strings (Lang_Load)

// The English of each string, also its key in the [ui] section of the language files (but
// kStrUpdsLabel, "UPDATES (short)": the same English as kStrUpdates, shorter there). The comments
// go with the strings into romfs/lang/*.txt; a translation keeps the English one's conversions
// (%d, %s) in the same order.
static const char *const kText[kStrCount] = {
  [kStrOn] = "ON",
  [kStrOff] = "OFF",
  // Map tab: the follow button ("FOLLOW: ON"), the area line ("CRATERIA  12/80 CELLS  MAP").
  [kStrFollow] = "FOLLOW",
  [kStrCells] = "CELLS",
  [kStrMapMark] = "MAP",
  // Status tab.
  [kStrEnergy] = "ENERGY",
  [kStrMax] = "MAX",
  [kStrReserve] = "RESERVE",
  [kStrAuto] = "AUTO",
  [kStrManual] = "MANUAL",
  [kStrItems] = "ITEMS",
  [kStrBeams] = "BEAMS",
  [kStrMapStations] = "MAP STATIONS",
  [kStrRoom] = "ROOM",
  // States tab.
  [kStrSaveStates] = "SAVE STATES",
  [kStrSavedNoDetails] = "SAVED (NO DETAILS)",
  [kStrSavedSlot] = "Saved to slot %d",
  [kStrSaveFailed] = "Could not save slot %d",
  [kStrLoadedSlot] = "Loaded slot %d",
  [kStrLoadFailed] = "Slot %d: cannot load",
  [kStrGameReset] = "Game reset",
  // Options tab (a cell's label fits 23 characters).
  [kStrPacing] = "FRAMES",
  [kStrAudio] = "AUDIO",
  [kStrDisplay] = "DISPLAY",
  [kStrPixelP] = "PIXEL P.",
  [kStrScaled] = "SCALED",
  [kStrView] = "VIEW",
  [kStrLanguage] = "LANGUAGE",
  [kStrResetGame] = "RESET GAME",
  // A toast: one line of at most 52 characters.
  [kStrFrameSkipOffToast] = "FRAME SKIP OFF: heavy rooms may slow down",
  // The RESET GAME window.
  [kStrResetQuestion] = "RESET THE GAME?",
  [kStrResetLost1] = "PROGRESS SINCE THE LAST SAVE",
  [kStrResetLost2] = "IS LOST",
  [kStrReset] = "RESET",
  [kStrCancel] = "CANCEL",
  // The achievements tab (RetroAchievements; titles and descriptions come from its server).
  [kStrRaAchievements] = "ACHIEVEMENTS",
  [kStrRaLogin] = "LOG IN",
  [kStrRaLogout] = "LOG OUT",
  [kStrRaDisabled] = "SWITCHED OFF",
  [kStrRaNoAccount] = "NOT LOGGED IN",
  [kStrRaConnecting] = "CONNECTING...",
  [kStrRaOnline] = "ONLINE AS %s",
  [kStrRaOffline] = "OFFLINE (UNLOCKS WAIT)",
  [kStrRaLoginError] = "LOGIN REJECTED",
  [kStrRaSummary] = "%d/%d  %u/%u PTS",
  [kStrRaLoading] = "LOADING THE ACHIEVEMENT LIST...",
  [kStrRaNoList] = "LOG IN TO SEE THE ACHIEVEMENTS",
  [kStrRaUnlocked] = "ACHIEVEMENT UNLOCKED!",
  [kStrRaNotify] = "NOTICE",
  [kStrRaTop] = "TOP",
  [kStrRaBottom] = "BOTTOM",
  [kStrRaSound] = "SOUND",
  [kStrRaSortDefault] = "LOCKED FIRST",
  [kStrRaSortTitle] = "NAME",
  [kStrRaSortPoints] = "POINTS",
  [kStrRaSortRecent] = "RECENT",
  [kStrRaPoints] = "%u POINTS",
  [kStrRaLockedState] = "LOCKED",
  [kStrRaUnlockedState] = "UNLOCKED",
  [kStrRaMissable] = "MISSABLE",
  [kStrRaProgression] = "PROGRESSION",
  [kStrRaWin] = "WIN CONDITION",
  [kStrClose] = "CLOSE",
  [kStrUpdate] = "UPDATE",
  [kStrUpdates] = "UPDATES",
  [kStrUpdTap] = "TAP TO CHECK",
  [kStrUpdChecking] = "CHECKING...",
  [kStrUpdUpToDate] = "UP TO DATE",
  [kStrUpdNew] = "NEW: %s",
  [kStrUpdInstalled] = "RESTART TO USE",
  [kStrUpdError] = "ERROR: TAP TO RETRY",
  [kStrUpdAsk] = "NEW VERSION %s",
  [kStrUpdAsk2] = "INSTALL IT NOW?",
  [kStrUpdInstalling] = "INSTALLING...",
  [kStrUpdRestart] = "UPDATED. RESTART NOW?",
  [kStrUpdFailed] = "THE UPDATE FAILED",
  [kStrUpdKept] = "CIA KEPT IN UPDATE/ FOR FBI",
  [kStrYes] = "YES",
  [kStrNo] = "NO",
  [kStrOk] = "OK",
  [kStrNewState] = "+ NEW",
  [kStrStatesNone] = "NO STATES YET: TAP + NEW",
  [kStrNoImage] = "NO IMAGE",
  [kStrMark] = "MARK",
  [kStrTime] = "TIME",
  [kStrLoad] = "LOAD",
  [kStrSaveOver] = "SAVE OVER",
  [kStrDelete] = "DELETE",
  [kStrStateDeleted] = "STATE %d DELETED",
  [kStrWait] = "PLEASE WAIT...",
  [kStrPaceLock] = "LOCK 30",
  [kStrPaceNoSkip] = "NO SKIP",
  [kStrViewOriginal] = "ORIGINAL",
  [kStrViewWide] = "WIDE",
  [kStrChannel] = "CHANNEL",
  [kStrChanStable] = "STABLE",
  [kStrChanBeta] = "+ BETAS",
  [kStrHud] = "HUD",
  [kStrHudHidden] = "HIDDEN WITH ITS TAB",
  [kStrHudShown] = "ALWAYS SHOWN",
  [kStrWhatsNew] = "WHAT'S NEW",
  [kStrNotesEmpty] = "NOTHING YET. CHECK NOW FIRST.",
  // Short forms for the OPTIONS half button (about 11 characters).
  [kStrUpdsLabel] = "UPDATES",
  [kStrUpdsCheck] = "CHECK",
  [kStrUpdsChecking] = "CHECKING",
  [kStrUpdsNew] = "NEW",
  [kStrUpdsInstalling] = "INSTALLING",
  [kStrUpdsRestart] = "RESTART",
  [kStrUpdsError] = "ERROR",
  [kStrUpdFrom] = "%s > %s",
  // OPTIONS -> SPOILERS (ON / OFF): off hides the names of what Samus has not found and the item totals.
  [kStrSpoilers] = "SPOILERS",
  // The items window (map tab): column heads of its table (energy and reserve tanks are "E" and "R"),
  // and the unique items of the area picked under it.
  [kStrArea] = "AREA",
  [kStrMajorShort] = "ITEM",
  [kStrTotalShort] = "TOT",
  [kStrUniqueItems] = "UNIQUE ITEMS",
  [kStrNone] = "NONE",
  // OPTIONS -> CONTROLS: the original buttons, or the modern scheme (docs/modern-controls.md).
  [kStrControls] = "CONTROLS",
  [kStrClassic] = "CLASSIC",
  [kStrModern] = "MODERN",
  // OPTIONS -> CONTROLS -> "?": the window with what each button does (the keys are drawn, the text beside them
  // has about 34 characters of room).
  [kStrCtlMove] = "MOVE",
  [kStrCtlMoveRun] = "MOVE, ALWAYS RUNNING",
  [kStrCtlJump] = "JUMP",
  [kStrCtlRun] = "RUN",
  [kStrCtlWalk] = "WALK",
  [kStrCtlFire] = "FIRE",
  [kStrCtlFireBomb] = "FIRE / BOMB IN BALL",
  [kStrCtlChoose] = "CHOOSE WEAPON",
  [kStrCtlCancel] = "CANCEL WEAPON",
  [kStrCtlAimUp] = "AIM UP",
  [kStrCtlAimDown] = "AIM DOWN",
  [kStrCtlAimDownLock] = "AIM DOWN, UNTIL UP",
  [kStrCtlPause] = "PAUSE",
  [kStrCtlMorph] = "MORPH BALL IN / OUT",
  [kStrCtlMissile] = "MISSILE / POWER BOMB IN BALL",
  [kStrCtlGrapple] = "GRAPPLING BEAM",
  [kStrCtlXray] = "X-RAY SCOPE",
  [kStrCtlMissileKind] = "MISSILE OR SUPER MISSILE",
  [kStrCtlConfigNote] = "BUTTONS FROM THE GAME'S CONTROLLER SETTING",
};

static const char *g_tr[kStrCount];   // the current language's, resolved by UiLang_Set

static const char *UiKey(int id) { return id == kStrUpdsLabel ? "UPDATES (short)" : kText[id]; }

// The same %-conversions in the same order: the strings go through snprintf, and a file from the
// SD card with a %s where the English has %d would crash it.
static bool SameConversions(const char *a, const char *b) {
  for (;;) {
    a = strchr(a, '%');
    b = strchr(b, '%');
    if (!a || !b) return !a && !b;
    if (a[1] != b[1]) return false;
    if (!a[1]) return true;
    a += 2;
    b += 2;
  }
}

const char *Tr(UiStr id) {
  if ((unsigned)id >= kStrCount) return "";
  return g_tr[id] ? g_tr[id] : kText[id];
}

const char *UiLang_Get(const char *section, const char *key) { return g_translated ? Lang_Get(section, key) : NULL; }

int UiLang_Count(void) { return Lang_Count(); }

void UiLang_Set(UiLang lang) {
  if (lang < 0 || lang >= Lang_Count()) lang = kLangEn;
  if (!Lang_Load(lang)) lang = kLangEn;   // unreadable: English (its strings are the code's)
  g_ui_lang = lang;
  const LangInfo *l = Lang_Info(lang);
  g_translated = l->path[0] != 0;
  for (int id = 0; id < kStrCount; id++) {
    const char *t = g_translated ? Lang_Get("ui", UiKey(id)) : NULL;
    g_tr[id] = t && SameConversions(kText[id], t) ? t : NULL;
  }
}

bool UiLang_Translated(void) { return g_translated; }
bool UiLang_IsJapanese(UiLang lang) { return lang >= 0 && lang < Lang_Count() && Lang_Info(lang)->japanese; }
const char *UiLang_Code(UiLang lang) { return Lang_Info(lang)->code; }
UiLang UiLang_FromCode(const char *code) { return Lang_Find(code); }

// The UI's font has no kana or kanji: Japanese is named in English here (the game's OPTION MODE
// shows 日本語 with its own letters).
const char *UiLang_Name(UiLang lang) { return Lang_Info(lang)->name; }

UiLang UiLang_FromSystem(void) {
#ifdef __3DS__
  static const char *const kCodes[] = {
    [CFG_LANGUAGE_JP] = "ja", [CFG_LANGUAGE_EN] = "en", [CFG_LANGUAGE_FR] = "fr", [CFG_LANGUAGE_DE] = "de",
    [CFG_LANGUAGE_IT] = "it", [CFG_LANGUAGE_ES] = "es", [CFG_LANGUAGE_ZH] = "zh", [CFG_LANGUAGE_KO] = "ko",
    [CFG_LANGUAGE_NL] = "nl", [CFG_LANGUAGE_PT] = "pt", [CFG_LANGUAGE_RU] = "ru", [CFG_LANGUAGE_TW] = "tw",
  };
  u8 lang = CFG_LANGUAGE_EN;
  if (R_SUCCEEDED(cfguInit())) {
    if (R_FAILED(CFGU_GetSystemLanguage(&lang))) lang = CFG_LANGUAGE_EN;
    cfguExit();
  }
  const UiLang l = lang < sizeof(kCodes) / sizeof(kCodes[0]) && kCodes[lang] ? Lang_Find(kCodes[lang]) : -1;
  return l >= 0 ? l : kLangEn;
#else
  return kLangEn;
#endif
}

// ---- The game's names -----------------------------------------------------------------
// The English, also their keys in the language files ([items], [beams], [ammo], [areas],
// [areas short]).

static const char *const kItems[11] = { "VARIA", "GRAV", "MORPH", "BOMB", "HIJUMP", "SPACE", "SPEED", "SCREW",
                                        "SPRING", "GRAPPL", "XRAY" };
static const char *const kBeams[5] = { "CHARGE", "ICE", "WAVE", "SPAZER", "PLASMA" };
static const char *const kAmmo[3] = { "MSL", "SUPER", "PB" };
static const char *const kAreas[8] = { "Crateria", "Brinstar", "Norfair", "Wrecked Ship", "Maridia", "Tourian", "Ceres",
                                       "Debug" };
static const char *const kAreasShort[8] = { "CRA", "BRI", "NOR", "WRE", "MAR", "TOU", "CER", "DBG" };

static const char *Name(const char *section, const char *en) {
  const char *t = UiLang_Get(section, en);
  return t ? t : en;
}

const char *TrItem(int i) { return (unsigned)i < 11 ? Name("items", kItems[i]) : ""; }
const char *TrBeam(int i) { return (unsigned)i < 5 ? Name("beams", kBeams[i]) : ""; }
const char *TrAmmo(int i) { return (unsigned)i < 3 ? Name("ammo", kAmmo[i]) : ""; }
const char *TrArea(int area) { return Name("areas", kAreas[(unsigned)area < 8 ? area : 7]); }
const char *TrAreaShort(int area) { return Name("areas short", kAreasShort[(unsigned)area < 8 ? area : 7]); }

void UiLang_ForEachKey(void (*fn)(const char *section, const char *key, const char *note)) {
  fn("language", "name", "the language in itself, as the menus show it");
  for (int id = 0; id < kStrCount; id++) fn("ui", UiKey(id), NULL);
  for (int i = 0; i < 11; i++) fn("items", kItems[i], "11 characters at most");
  for (int i = 0; i < 5; i++) fn("beams", kBeams[i], "8 characters at most");
  for (int i = 0; i < 3; i++) fn("ammo", kAmmo[i], "7 characters at most");
  for (int i = 0; i < 8; i++) fn("areas", kAreas[i], NULL);
  for (int i = 0; i < 8; i++) fn("areas short", kAreasShort[i], "3 characters at most");
}
