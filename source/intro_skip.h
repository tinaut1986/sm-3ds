// Skipping the intro (issue #54): while the intro plays (game state 0x1E) a hint at the bottom of the
// top screen says START skips it; START opens a choice there, the intro held meanwhile: Ceres station
// (only the story is skipped) or planet Zebes (Ceres too, with its escape). The game takes the choice
// through g_rtl_intro_skip (sm/src/sm_rtl.h). Always available, no option.
#pragma once

#include <stdbool.h>

typedef enum { kIntroSkipHidden, kIntroSkipHint, kIntroSkipMenu } IntroSkipShown;

// Once a main-loop pass, with the frontend's pad bits (modern_controls.h order: B Y SELECT START Up
// Down Left Right A X L R). True while the choice is open: the game must not run a frame then.
bool IntroSkip_Frame(int pad);
// The buttons the game may see this frame: the intro never gets START (it opens the choice), and no
// button on the frame the choice closes.
int IntroSkip_Filter(int pad);
// What the top screen shows, and in the choice the row picked (0 Ceres, 1 Zebes).
IntroSkipShown IntroSkip_Shown(void);
int IntroSkip_Cursor(void);
