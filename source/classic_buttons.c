#include "classic_buttons.h"

#include <stdio.h>
#include <string.h>

#include "src/types.h"
#include "src/variables.h"
#include "src/ida_types.h"

// The buttons the original lets the player assign: B Y SELECT A X L R (START and the D-pad are fixed).
enum { kAssignable = 0x8000 | 0x4000 | 0x2000 | 0x80 | 0x40 | 0x20 | 0x10 };

static uint16 g_setting[kClassicButtons];
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

bool ClassicButtons_Have(void) { return g_have; }

void ClassicButtons_Get(uint16_t out[kClassicButtons]) {
  for (int i = 0; i < kClassicButtons; i++) out[i] = g_setting[i];
}

bool ClassicButtons_Parse(const char *text) {
  unsigned v[kClassicButtons];
  if (sscanf(text, "%x,%x,%x,%x,%x,%x,%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]) != kClassicButtons) return false;
  uint16 b[kClassicButtons];
  for (int i = 0; i < kClassicButtons; i++) {
    if (v[i] > 0xFFFF) return false;
    b[i] = (uint16)v[i];
  }
  if (!Valid(b)) return false;
  memcpy(g_setting, b, sizeof(g_setting));
  g_have = true;
  return true;
}

void ClassicButtons_Adopt(void) {
  uint16 v[kClassicButtons];
  ReadGame(v);
  if (!Valid(v)) return;
  memcpy(g_setting, v, sizeof(g_setting));
  g_have = true;
}

void ClassicButtons_Sync(bool loaded) {
  // The intro sets the default buttons while Samus has its 900 missiles and puts the player's back
  // afterwards (CinematicFunction_Intro_Initial, RevertButtonConfig); the opening and the demos are
  // left alone too.
  if (game_state == kGameState_1_OpeningCinematic || game_state == kGameState_30_IntroCinematic ||
      (game_state >= kGameState_40_TransitionToDemo && game_state <= kGameState_44_TransitionFromDemo))
    return;
  // In the game's options menu the player changes them: that is the setting (a loaded state that
  // happens to be there still gets the setting put back first).
  if (game_state == kGameState_2_GameOptionsMenu && !loaded) {
    ClassicButtons_Adopt();
    return;
  }
  if (!g_have) {   // no setting yet (config.ini from before #70): the first ones played with become it
    if (game_state == kGameState_8_MainGameplay) ClassicButtons_Adopt();
    return;
  }
  for (int i = 0; i < kClassicButtons; i++)
    if (*GameButton(i) != g_setting[i]) *GameButton(i) = g_setting[i];
}
