// Random register writes into the original S-DSP (sm/src/snes/dsp.c as vendored, before
// the 3DS port's optimisations) and the current one; every output sample, every DSP
// register and the whole APU RAM must stay identical. Built by dsp_fuzz.sh.
// usage: dsp_fuzz [SEED] [full]   (full: echo delay over its whole range, not just 0-3)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes/dsp.h"
Dsp *old_dsp_init(uint8_t *ram); void old_dsp_reset(Dsp *d); void old_dsp_cycle(Dsp *d);
void old_dsp_write(Dsp *d, uint8_t a, uint8_t v);
static uint8_t ram_a[0x10000], ram_b[0x10000];
int main(int argc, char **argv) {
  srand(argc > 1 ? atoi(argv[1]) : 1234);
  for (int i = 0; i < 0x10000; i++) ram_a[i] = ram_b[i] = rand();
  Dsp *a = old_dsp_init(ram_a), *b = dsp_init(ram_b);
  old_dsp_reset(a); dsp_reset(b);
  long samples = 0;
  for (int round = 0; round < 200000; round++) {
    int writes = rand() % 4;
    for (int w = 0; w < writes; w++) {
      uint8_t adr = rand() & 0x7f, val = rand();
      if (adr == 0x6c && (rand() % 8)) val &= ~0xc0;   // mostly not reset/mute
      if (adr == 0x7d && argc < 3) val &= 3;                      // keep echo small
      old_dsp_write(a, adr, val); dsp_write(b, adr, val);
    }
    int n = 1 + rand() % 64;
    for (int i = 0; i < n; i++) old_dsp_cycle(a);
    dsp_cycleBlock(b, n);
    if (memcmp(a->sampleBuffer, b->sampleBuffer, sizeof(a->sampleBuffer)) || memcmp(a->ram, b->ram, 0x80) ||
        memcmp(ram_a, ram_b, sizeof(ram_a))) {
      printf("MISMATCH at round %d (%ld samples), n=%d\n", round, samples, n);
      for (int k = 0; k < 534 * 2; k++) if (a->sampleBuffer[k] != b->sampleBuffer[k]) { printf(" sample %d: %d vs %d\n", k, a->sampleBuffer[k], b->sampleBuffer[k]); break; }
      for (int k = 0; k < 0x80; k++) if (a->ram[k] != b->ram[k]) printf(" reg %02x: %02x vs %02x\n", k, a->ram[k], b->ram[k]);
      for (int k = 0; k < 0x10000; k++) if (ram_a[k] != ram_b[k]) { printf(" apu ram %04x\n", k); break; }
      printf(" pmod %02x non %02x eon %02x flg %02x fir %d %d %d %d %d %d %d %d\n", a->ram[0x2d], a->ram[0x3d], a->ram[0x4d], a->ram[0x6c], a->firValues[0], a->firValues[1], a->firValues[2], a->firValues[3], a->firValues[4], a->firValues[5], a->firValues[6], a->firValues[7]);
      for (int c = 0; c < 8; c++) printf(" ch%d gain %d/%d state %d/%d out %d/%d pc %d/%d\n", c, a->channel[c].gain, b->channel[c].gain, a->channel[c].adsrState, b->channel[c].adsrState, a->channel[c].sampleOut, b->channel[c].sampleOut, a->channel[c].pitchCounter, b->channel[c].pitchCounter);
      return 1;
    }
    samples += n;
    if (a->sampleOffset >= 534) { a->sampleOffset = 0; b->sampleOffset = 0; }
  }
  printf("OK: %ld samples identical\n", samples);
  return 0;
}
