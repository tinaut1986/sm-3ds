// The original control scheme's seven buttons as a player setting (config.ini `classic_buttons`, #70).
// The game keeps them in its own memory (button_config_*), so a save state and the save file carry
// the assignments they were made with: loading one brought those back. The setting is the player's
// choice wherever it is made (the bottom screen's CONTROLS window or the game's own options menu)
// and it is put back into the game's memory whenever the game holds another one.
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum { kClassicButtons = 7 };   // jump, run, shoot, item switch, item cancel, aim up, aim down

// Whether there is a setting yet (none until the game first reaches gameplay or the player changes one),
// and its seven SNES masks in the order above.
bool ClassicButtons_Have(void);
void ClassicButtons_Get(uint16_t out[kClassicButtons]);
// From config.ini's "XXXX,XXXX,..." (hex masks); false, and nothing changed, if the text is not valid.
bool ClassicButtons_Parse(const char *text);
// The game's current assignments become the setting (after the bottom screen changed them).
void ClassicButtons_Adopt(void);
// Once a game frame, before it runs; with `loaded` right after a save state was loaded or the game was
// reset. Takes what the game's options menu changed, puts the setting back otherwise.
void ClassicButtons_Sync(bool loaded);
