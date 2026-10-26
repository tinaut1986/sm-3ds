// The start-up check of the DSP firmware (dsp_firm.h) with this port's own look: panels and touch
// buttons drawn like the rest of the UI, the texts in the player's language.
#pragma once

// Call after BottomUi_Init (the language is set) and before the audio starts. A good firmware shows
// nothing. Missing or damaged: a dialog says the sound needs it and offers to extract it (A or tap
// EXTRACT) or to go on without sound (B or tap NO SOUND). Returns a line for the debug log.
const char *DspFirmUi_Ensure(void);
