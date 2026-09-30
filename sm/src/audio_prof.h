// Optional timing of the audio path, used by the 3DS port's debug tooling.
// Compiles to nothing off-console. Values are in system ticks (268 MHz) for the
// last rendered block.
#pragma once

#include <stdint.h>

enum { kAudioProf_Total, kAudioProf_LockWait, kAudioProf_SpcLoop, kAudioProf_DspCycles, kAudioProf_Resample, kAudioProf_Count };

#ifdef __3DS__
// <3ds.h> cannot be included from game code (its identifiers clash with the
// game's variable macros), so the clock read lives in the frontend.
uint64_t AudioProf_Now(void);
extern volatile uint64_t g_audio_prof_last[kAudioProf_Count];
extern uint64_t g_audio_prof_cur[kAudioProf_Count];
#define AUDIO_PROF_NOW() AudioProf_Now()
#define AUDIO_PROF_ADD(i, t0) (g_audio_prof_cur[i] += AUDIO_PROF_NOW() - (t0))
#else
#define AUDIO_PROF_NOW() 0
#define AUDIO_PROF_ADD(i, t0) ((void)(t0))
#endif
