#include "audio_gain.h"

// On the magnitude, so a negative sample comes out as the mirror of its positive (a shift of a negative
// number rounds the other way). Above the knee the curve is knee + d * R / (d + R), d the distance over
// the knee and R the room left to full scale: slope 1 at the knee (no corner to hear), never 32767.
int16_t AudioGain_Sample(int s, int gain) {
  const int neg = s < 0;
  int v = (neg ? -s : s) * gain >> 8;   // |s| <= 32768, gain <= 1024 (400 %): fits
  if (v > kAudioGainKnee) {
    const int room = 32767 - kAudioGainKnee, d = v - kAudioGainKnee;
    v = kAudioGainKnee + (int)((int64_t)d * room / (d + room));
  }
  return (int16_t)(neg ? -v : v);
}

void AudioGain_Apply(int16_t *samples, int count, int gain) {
  if (gain == 256) return;
  for (int i = 0; i < count; i++) samples[i] = AudioGain_Sample(samples[i], gain);
}
