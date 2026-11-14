#include "game_settings.h"

#include <stdio.h>
#include <string.h>

#include "src/types.h"
#include "src/variables.h"
#include "src/ida_types.h"
#include "src/sm_rtl.h"

// The buttons the original lets the player assign: B Y SELECT A X L R (START and the D-pad are fixed).
enum { kAssignable = 0x8000 | 0x4000 | 0x2000 | 0x80 | 0x40 | 0x20 | 0x10 };

static uint16 g_buttons[kClassicButtons];
static bool g_icon_cancel, g_moon_walk;
static bool g_have;

static uint16 *GameButton(int i) {
  switch (i) {
  case 0: return &button_config_jump_a;
  case 1: return &button_config_run_b;
  case 2: return &button_config_shoot_x;
  case 3: return &button_config_itemswitch;
  case 4: return &button_config_itemcancel_y;
  case 5: return &button_config_aim_up_R;
  default: return &button_config_aim_down_L;
  }
}

// One assignable button each, none twice (what both menus leave).
static bool Valid(const uint16 v[kClassicButtons]) {
  for (int i = 0; i < kClassicButtons; i++) {
    if (!v[i] || (v[i] & ~kAssignable) || (v[i] & (v[i] - 1))) return false;
    for (int j = 0; j < i; j++)
      if (v[j] == v[i]) return false;
  }
  return true;
}

static void ReadGame(uint16 v[kClassicButtons]) {
  for (int i = 0; i < kClassicButtons; i++) v[i] = *GameButton(i);
}

bool GameSettings_Have(void) { return g_have; }
bool GameSettings_IconCancel(void) { return g_icon_cancel; }
bool GameSettings_MoonWalk(void) { return g_moon_walk; }

void GameSettings_Buttons(uint16_t out[kClassicButtons]) {
  for (int i = 0; i < kClassicButtons; i++) out[i] = g_buttons[i];
}

bool GameSettings_ParseButtons(const char *text) {
  unsigned v[kClassicButtons];
  if (sscanf(text, "%x,%x,%x,%x,%x,%x,%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]) != kClassicButtons) return false;
  uint16 b[kClassicButtons];
  for (int i = 0; i < kClassicButtons; i++) {
    if (v[i] > 0xFFFF) return false;
    b[i] = (uint16)v[i];
  }
  if (!Valid(b)) return false;
  memcpy(g_buttons, b, sizeof(g_buttons));
  g_have = true;
  return true;
}

bool GameSettings_ParseSpecial(const char *text) {
  int icon, moon;
  if (sscanf(text, "%d,%d", &icon, &moon) != 2 || (icon & ~1) || (moon & ~1)) return false;
  g_icon_cancel = icon, g_moon_walk = moon;
  return true;
}

void GameSettings_Adopt(void) {
  uint16 v[kClassicButtons];
  ReadGame(v);
  if (!Valid(v)) return;
  memcpy(g_buttons, v, sizeof(g_buttons));
  g_icon_cancel = hud_auto_cancel_flag != 0;
  g_moon_walk = moonwalk_flag != 0;
  g_have = true;
}

void GameSettings_Sync(bool loaded) {
  // The intro sets the default buttons while Samus has its 900 missiles and puts the player's back
  // afterwards (CinematicFunction_Intro_Initial, RevertButtonConfig); the opening and the demos (which
  // set their own buttons and turn MOON WALK off) are left alone too.
  if (game_state == kGameState_1_OpeningCinematic || game_state == kGameState_30_IntroCinematic ||
      (game_state >= kGameState_40_TransitionToDemo && game_state <= kGameState_44_TransitionFromDemo))
    return;
  // What the player changes in the game's options menu reaches GameSettings_Adopt through
  // g_rtl_game_settings_changed (sm_rtl.h). Anything else that changes them is not the player's choice:
  // the save file the file select loads just before the options menu, a save state, the game's reset.
  if (!g_have) {   // no setting yet (config.ini from before #70): the first ones played with become it
    if (game_state == kGameState_8_MainGameplay) GameSettings_Adopt();
    return;
  }
  for (int i = 0; i < kClassicButtons; i++)
    if (*GameButton(i) != g_buttons[i]) *GameButton(i) = g_buttons[i];
  if ((hud_auto_cancel_flag != 0) != g_icon_cancel) hud_auto_cancel_flag = g_icon_cancel;
  if ((moonwalk_flag != 0) != g_moon_walk) moonwalk_flag = g_moon_walk;
  // A state made on the CONTROLLER SETTING screen brings that screen's own copy of the buttons, which
  // it writes back when left: rebuilt from the setting.
  if (loaded && game_state == kGameState_2_GameOptionsMenu &&
      (game_options_screen_index == 7 || game_options_screen_index == 9 || game_options_screen_index == 10))
    RtlReloadControllerOptions();
}

void GameSettings_Init(void) {
  g_rtl_game_settings_changed = GameSettings_Adopt;
}
