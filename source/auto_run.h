// Automatic running (OPTIONS -> PLAY -> AUTO RUN, config.ini `auto_run`, #60): Samus runs as if the
// run button were held, but only once a direction has been held for a moment (kRunDelay frames), so
// she starts walking and then runs, as in Zero Mission; a short press is a step. Holding the run
// button walks. Both control schemes. It only ever adds or removes the game's run button.
#pragma once

#include <stdbool.h>

bool AutoRun_On(void);
void AutoRun_Set(bool on);

// `inputs` (the frontend's pad bits, after ModernControls_Translate) as the game must see them
// this frame. Called once a game frame, before it runs.
int AutoRun_Translate(int inputs);
