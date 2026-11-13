// The game's own settings as player settings of the port (#70): the original control scheme's seven
// buttons (config.ini `classic_buttons`) and OPTION MODE's special settings, ICON CANCEL and MOON WALK
// (`special_settings`). The game keeps them in its own memory (button_config_*, hud_auto_cancel_flag,
// moonwalk_flag), so a save state and the save file carry the ones they were made with: loading one
// brought those back. The setting is the player's choice wherever it is made (the bottom screen's
// CONTROLS window or the game's own options menu) and it is put back into the game's memory whenever
// the game holds another one.
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum { kClassicButtons = 7 };   // jump, run, shoot, item switch, item cancel, aim up, aim down

// Whether there is a setting yet (none until the game first reaches gameplay or the player changes one).
bool GameSettings_Have(void);
// The seven buttons' SNES masks in the order above.
void GameSettings_Buttons(uint16_t out[kClassicButtons]);
// The special settings: ICON CANCEL automatic, MOON WALK on.
bool GameSettings_IconCancel(void);
bool GameSettings_MoonWalk(void);
// From config.ini: the buttons' "XXXX,XXXX,..." (hex masks) and the special settings' "I,M" (0 or 1).
// False, and nothing changed, if the text is not valid. The buttons line makes the setting exist; the
// special settings are kept off until it does.
bool GameSettings_ParseButtons(const char *text);
bool GameSettings_ParseSpecial(const char *text);
// The game's current settings become the player's (after the bottom screen changed the buttons).
void GameSettings_Adopt(void);
// Once a game frame, before it runs; with `loaded` right after a save state was loaded or the game was
// reset. Takes what the game's options menu changed, puts the setting back otherwise.
void GameSettings_Sync(bool loaded);
