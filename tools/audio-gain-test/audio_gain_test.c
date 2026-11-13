// Host test of the master volume's gain and soft limit (source/audio_gain.c, #67). No ROM needed.
// Checks: 100 % changes nothing; under the knee a sample is only multiplied; the curve rises with the
// input, never passes full scale and has no step at the knee; it is symmetric; Apply equals Sample.
#include <stdio.h>
#include <stdlib.h>

#include "audio_gain.h"

static int g_failed, g_checks;

static void Check(int ok, const char *what, int a, int b) {
  g_checks++;
  if (!ok) {
    g_failed++;
    if (g_failed <= 20) printf("FAIL %s (%d, %d)\n", what, a, b);
  }
}

int main(void) {
  static const int kGains[] = { 0, 26, 128, 256, 384, 512, 768, 1024 };
  for (unsigned g = 0; g < sizeof(kGains) / sizeof(kGains[0]); g++) {
    const int gain = kGains[g];
    int prev = -40000;
    for (int s = -32768; s <= 32767; s++) {
      const int v = AudioGain_Sample(s, gain), lin = s < 0 ? -(-s * gain >> 8) : s * gain >> 8;
      Check(v >= -32767 && v <= 32767, "within full scale", s, gain);
      Check(v >= prev, "never falls as the input rises", s, gain);
      if (lin >= -kAudioGainKnee && lin <= kAudioGainKnee) Check(v == lin, "a multiply under the knee", s, gain);
      else Check(abs(v) <= abs(lin), "bent down above the knee", s, gain);
      if (s > -32768) Check(abs(v - prev) <= (gain >> 8) + 1, "no step (slope at most the gain's)", s, gain);
      if (s > -32768) Check(AudioGain_Sample(-s, gain) == -v, "symmetric", s, gain);
      prev = v;
    }
    int16_t buf[4096], ref[4096];
    for (int i = 0; i < 4096; i++) buf[i] = (int16_t)((i * 7919) % 65536 - 32768), ref[i] = buf[i];
    AudioGain_Apply(buf, 4096, gain);
    for (int i = 0; i < 4096; i++)
      Check(buf[i] == (gain == 256 ? ref[i] : AudioGain_Sample(ref[i], gain)), "Apply equals Sample", ref[i], gain);
  }
  Check(AudioGain_FromPercent(100) == 256 && AudioGain_FromPercent(400) == 1024, "percent to Q8", 0, 0);
  printf("audio gain: %d checks, %d failed\n", g_checks, g_failed);
  return g_failed != 0;
}
