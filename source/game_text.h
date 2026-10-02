// The game's own texts in the UI language (docs/PLAN.md P4.8), without touching the ROM
// file or the game's RAM: only what is already in VRAM changes, so the game state, the
// save files and RetroAchievements are the same in every language.
//
// Today: the message boxes (item pickups, map and energy stations, the save prompt).
// Their tilemap is rewritten in VRAM once the game has put it there
// (g_rtl_message_box_hook, sm_85.c): titles with the box's own capitals, accents as
// marks in the blank cell above; the instruction line under an item with a lowercase
// font of ours, drawn into the characters of the English words it replaces, which are
// put back when the box closes. English leaves everything as the game draws it.
#pragma once

void GameText_Init(void);

// VRAM was replaced wholesale (a loaded state, a reset): forget the characters to put back.
void GameText_Forget(void);
