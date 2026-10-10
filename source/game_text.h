// The game's own texts in the UI language (docs/PLAN.md P4.8), without touching the ROM
// file or the game's RAM: only what is already in VRAM changes, so the game state, the
// save files and RetroAchievements are the same in every language.
//
// The message boxes (item pickups, map and energy stations, the save prompt), here:
// their tilemap is rewritten in VRAM once the game has put it there
// (g_rtl_message_box_hook, sm_85.c): titles with the box's own capitals, accents as
// marks in the blank cell above; the instruction line under an item with a lowercase
// font of ours, drawn into the characters of the English words it replaces, which are
// put back when the box closes. English leaves everything as the game draws it.
//
// The other screens (game_text_screens.c): option menus, file select, intro pages, HUD,
// pause screen, game over, credits. Each frame, before the PPU draws, the English is read
// from a copy of VRAM as the game wrote it (g_ppu_vram_shadow) and the translation
// written over it; what is no longer wanted is put back from that copy.
#pragma once

#include <stdbool.h>

void GameText_Init(void);

// A message box is on screen (from when the game draws it until it is gone).
bool GameText_MessageBoxShown(void);
// The item boxes' instruction lines for the modern controls (modern_controls.h): their
// "(modern line)" keys, English included, instead of the game's own lines.
void GameText_SetModernLines(bool on);

// The other screens (game_text_screens.c), set up by GameText_Init.
void GameTextScreens_Init(void);
void GameTextScreens_Forget(void);
// Puts the game's own VRAM back (the next frame translates again): before a save state, so
// the state holds the game's VRAM and not the translation.
void GameTextScreens_PutBack(void);

// VRAM was replaced wholesale (a loaded state, a reset): forget the characters to put back.
void GameText_Forget(void);

// Every key of the language files these texts look up (tools/lang-check writes the template from
// them and checks a file against them): `fn` is called with the section, the key (the English)
// and, for some, a note on its limits (NULL: none).
typedef void LangKeyFn(const char *section, const char *key, const char *note);
void GameText_ForEachKey(LangKeyFn *fn);
void GameTextScreens_ForEachKey(LangKeyFn *fn);
