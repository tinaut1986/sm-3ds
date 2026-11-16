// The port's master volume (OPTIONS -> SOUND, #67): a gain on the game's whole output, after the
// S-DSP and before the achievement sound is mixed in. The game's music peaks around a fifth of full
// scale (tools/audio-bench, "levels"), so it can be raised a lot before it clips; what goes over is
// bent smoothly under full scale instead of being cut flat. Pure, host-testable (tools/audio-gain-test).
#pragma once

#include <stdint.h>

enum {
  kAudioGainKnee = 24576,   // up to here a sample is only multiplied; above, bent towards 32767
  kAudioVolumeMax = 400,    // OPTIONS: master 0..400 %
  kAudioVolumeDefault = 200,
  kAudioMixMax = 200,       // OPTIONS: music and effects 0..200 %
};

// One sample times `gain` (Q8: 256 = 100 %), within -32767..32767.
int16_t AudioGain_Sample(int s, int gain);
// `count` samples in place.
void AudioGain_Apply(int16_t *samples, int count, int gain);
// A percentage as a Q8 gain.
static inline int AudioGain_FromPercent(int pct) { return pct * 256 / 100; }
