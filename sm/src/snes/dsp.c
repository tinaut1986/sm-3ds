
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "dsp.h"
#include "apu.h"

#define MY_CHANGES 1

static const int rateValues[32] = {
  0, 2048, 1536, 1280, 1024, 768, 640, 512,
  384, 320, 256, 192, 160, 128, 96, 80,
  64, 48, 40, 32, 24, 20, 16, 12,
  10, 8, 6, 5, 4, 3, 2, 1
};

static const uint16_t gaussValues[512] = {
  0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
  0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x002, 0x002, 0x002, 0x002, 0x002,
  0x002, 0x002, 0x003, 0x003, 0x003, 0x003, 0x003, 0x004, 0x004, 0x004, 0x004, 0x004, 0x005, 0x005, 0x005, 0x005,
  0x006, 0x006, 0x006, 0x006, 0x007, 0x007, 0x007, 0x008, 0x008, 0x008, 0x009, 0x009, 0x009, 0x00A, 0x00A, 0x00A,
  0x00B, 0x00B, 0x00B, 0x00C, 0x00C, 0x00D, 0x00D, 0x00E, 0x00E, 0x00F, 0x00F, 0x00F, 0x010, 0x010, 0x011, 0x011,
  0x012, 0x013, 0x013, 0x014, 0x014, 0x015, 0x015, 0x016, 0x017, 0x017, 0x018, 0x018, 0x019, 0x01A, 0x01B, 0x01B,
  0x01C, 0x01D, 0x01D, 0x01E, 0x01F, 0x020, 0x020, 0x021, 0x022, 0x023, 0x024, 0x024, 0x025, 0x026, 0x027, 0x028,
  0x029, 0x02A, 0x02B, 0x02C, 0x02D, 0x02E, 0x02F, 0x030, 0x031, 0x032, 0x033, 0x034, 0x035, 0x036, 0x037, 0x038,
  0x03A, 0x03B, 0x03C, 0x03D, 0x03E, 0x040, 0x041, 0x042, 0x043, 0x045, 0x046, 0x047, 0x049, 0x04A, 0x04C, 0x04D,
  0x04E, 0x050, 0x051, 0x053, 0x054, 0x056, 0x057, 0x059, 0x05A, 0x05C, 0x05E, 0x05F, 0x061, 0x063, 0x064, 0x066,
  0x068, 0x06A, 0x06B, 0x06D, 0x06F, 0x071, 0x073, 0x075, 0x076, 0x078, 0x07A, 0x07C, 0x07E, 0x080, 0x082, 0x084,
  0x086, 0x089, 0x08B, 0x08D, 0x08F, 0x091, 0x093, 0x096, 0x098, 0x09A, 0x09C, 0x09F, 0x0A1, 0x0A3, 0x0A6, 0x0A8,
  0x0AB, 0x0AD, 0x0AF, 0x0B2, 0x0B4, 0x0B7, 0x0BA, 0x0BC, 0x0BF, 0x0C1, 0x0C4, 0x0C7, 0x0C9, 0x0CC, 0x0CF, 0x0D2,
  0x0D4, 0x0D7, 0x0DA, 0x0DD, 0x0E0, 0x0E3, 0x0E6, 0x0E9, 0x0EC, 0x0EF, 0x0F2, 0x0F5, 0x0F8, 0x0FB, 0x0FE, 0x101,
  0x104, 0x107, 0x10B, 0x10E, 0x111, 0x114, 0x118, 0x11B, 0x11E, 0x122, 0x125, 0x129, 0x12C, 0x130, 0x133, 0x137,
  0x13A, 0x13E, 0x141, 0x145, 0x148, 0x14C, 0x150, 0x153, 0x157, 0x15B, 0x15F, 0x162, 0x166, 0x16A, 0x16E, 0x172,
  0x176, 0x17A, 0x17D, 0x181, 0x185, 0x189, 0x18D, 0x191, 0x195, 0x19A, 0x19E, 0x1A2, 0x1A6, 0x1AA, 0x1AE, 0x1B2,
  0x1B7, 0x1BB, 0x1BF, 0x1C3, 0x1C8, 0x1CC, 0x1D0, 0x1D5, 0x1D9, 0x1DD, 0x1E2, 0x1E6, 0x1EB, 0x1EF, 0x1F3, 0x1F8,
  0x1FC, 0x201, 0x205, 0x20A, 0x20F, 0x213, 0x218, 0x21C, 0x221, 0x226, 0x22A, 0x22F, 0x233, 0x238, 0x23D, 0x241,
  0x246, 0x24B, 0x250, 0x254, 0x259, 0x25E, 0x263, 0x267, 0x26C, 0x271, 0x276, 0x27B, 0x280, 0x284, 0x289, 0x28E,
  0x293, 0x298, 0x29D, 0x2A2, 0x2A6, 0x2AB, 0x2B0, 0x2B5, 0x2BA, 0x2BF, 0x2C4, 0x2C9, 0x2CE, 0x2D3, 0x2D8, 0x2DC,
  0x2E1, 0x2E6, 0x2EB, 0x2F0, 0x2F5, 0x2FA, 0x2FF, 0x304, 0x309, 0x30E, 0x313, 0x318, 0x31D, 0x322, 0x326, 0x32B,
  0x330, 0x335, 0x33A, 0x33F, 0x344, 0x349, 0x34E, 0x353, 0x357, 0x35C, 0x361, 0x366, 0x36B, 0x370, 0x374, 0x379,
  0x37E, 0x383, 0x388, 0x38C, 0x391, 0x396, 0x39B, 0x39F, 0x3A4, 0x3A9, 0x3AD, 0x3B2, 0x3B7, 0x3BB, 0x3C0, 0x3C5,
  0x3C9, 0x3CE, 0x3D2, 0x3D7, 0x3DC, 0x3E0, 0x3E5, 0x3E9, 0x3ED, 0x3F2, 0x3F6, 0x3FB, 0x3FF, 0x403, 0x408, 0x40C,
  0x410, 0x415, 0x419, 0x41D, 0x421, 0x425, 0x42A, 0x42E, 0x432, 0x436, 0x43A, 0x43E, 0x442, 0x446, 0x44A, 0x44E,
  0x452, 0x455, 0x459, 0x45D, 0x461, 0x465, 0x468, 0x46C, 0x470, 0x473, 0x477, 0x47A, 0x47E, 0x481, 0x485, 0x488,
  0x48C, 0x48F, 0x492, 0x496, 0x499, 0x49C, 0x49F, 0x4A2, 0x4A6, 0x4A9, 0x4AC, 0x4AF, 0x4B2, 0x4B5, 0x4B7, 0x4BA,
  0x4BD, 0x4C0, 0x4C3, 0x4C5, 0x4C8, 0x4CB, 0x4CD, 0x4D0, 0x4D2, 0x4D5, 0x4D7, 0x4D9, 0x4DC, 0x4DE, 0x4E0, 0x4E3,
  0x4E5, 0x4E7, 0x4E9, 0x4EB, 0x4ED, 0x4EF, 0x4F1, 0x4F3, 0x4F5, 0x4F6, 0x4F8, 0x4FA, 0x4FB, 0x4FD, 0x4FF, 0x500,
  0x502, 0x503, 0x504, 0x506, 0x507, 0x508, 0x50A, 0x50B, 0x50C, 0x50D, 0x50E, 0x50F, 0x510, 0x511, 0x511, 0x512,
  0x513, 0x514, 0x514, 0x515, 0x516, 0x516, 0x517, 0x517, 0x517, 0x518, 0x518, 0x518, 0x518, 0x518, 0x519, 0x519
};

static int dsp_cycleChannel(Dsp* dsp, DspChannel* c, int ch, int noiseSample, int modulator);
static void dsp_handleEcho(Dsp* dsp, int* outputL, int* outputR, int inL, int inR, bool firOneTap);
static void dsp_handleGain(DspChannel* c);
static void dsp_decodeBrr(Dsp* dsp, DspChannel* c, int ch);
static int16_t dsp_getSample(const DspChannel* c, int sampleNum, int offset);
static void dsp_handleNoise(Dsp* dsp);

Dsp* dsp_init(uint8_t *ram) {
  Dsp* dsp = malloc(sizeof(Dsp));
  dsp->apu_ram = ram;
  return dsp;
}

void dsp_free(Dsp* dsp) {
  free(dsp);
}

void dsp_reset(Dsp* dsp) {
  memset(dsp->ram, 0, sizeof(dsp->ram));
  dsp->ram[0x7c] = 0xff; // set ENDx
  for(int i = 0; i < 8; i++) {
    dsp->channel[i].pitch = 0;
    dsp->channel[i].pitchCounter = 0;
    dsp->channel[i].pitchModulation = false;
    memset(dsp->channel[i].decodeBuffer, 0, sizeof(dsp->channel[i].decodeBuffer));
    dsp->channel[i].srcn = 0;
    dsp->channel[i].decodeOffset = 0;
    dsp->channel[i].previousFlags = 0;
    dsp->channel[i].old = 0;
    dsp->channel[i].older = 0;
    dsp->channel[i].useNoise = false;
    memset(dsp->channel[i].adsrRates, 0, sizeof(dsp->channel[i].adsrRates));
    dsp->channel[i].rateCounter = 0;
    dsp->channel[i].adsrState = 0;
    dsp->channel[i].sustainLevel = 0;
    dsp->channel[i].useGain = false;
    dsp->channel[i].gainMode = 0;
    dsp->channel[i].directGain = false;
    dsp->channel[i].gainValue = 0;
    dsp->channel[i].gain = 0;
    dsp->channel[i].keyOn = false;
    dsp->channel[i].keyOff = false;
    dsp->channel[i].sampleOut = 0;
    dsp->channel[i].volumeL = 0;
    dsp->channel[i].volumeR = 0;
    dsp->channel[i].echoEnable = false;
  }
  dsp->dirPage = 0;
  dsp->evenCycle = false;
  dsp->mute = true;
  dsp->reset = true;
  dsp->masterVolumeL = 0;
  dsp->masterVolumeR = 0;
  dsp->noiseSample = -0x4000;
  dsp->noiseRate = 0;
  dsp->noiseCounter = 0;
  dsp->echoWrites = false;
  dsp->echoVolumeL = 0;
  dsp->echoVolumeR = 0;
  dsp->feedbackVolume = 0;
  dsp->echoBufferAdr = 0;
  dsp->echoDelay = 1;
  dsp->echoRemain = 1;
  dsp->echoBufferIndex = 0;
  dsp->firBufferIndex = 0;
  memset(dsp->firValues, 0, sizeof(dsp->firValues));
  memset(dsp->firBufferL, 0, sizeof(dsp->firBufferL));
  memset(dsp->firBufferR, 0, sizeof(dsp->firBufferR));
  memset(dsp->sampleBuffer, 0, sizeof(dsp->sampleBuffer));
  dsp->sampleOffset = 0;
}

void dsp_saveload(Dsp *dsp, SaveLoadFunc *func, void *ctx) {
  func(ctx, &dsp->ram, sizeof(Dsp) - offsetof(Dsp, ram));
}

void dsp_cycle(Dsp* dsp) {
  dsp_cycleBlock(dsp, 1);
}

// 3DS port: runs `n` (<= 64) samples voice by voice instead of sample by sample. No
// register can change inside a block (the SPC driver runs between blocks), and the only
// links between voices within a sample are pitch modulation (voice ch reads voice ch-1's
// output of the same sample, kept in out[]) and the noise generator (stepped after the
// voices each sample, precomputed here). Each voice works on a local copy of its state,
// which the compiler can keep in registers: byte stores to dsp->ram or the APU RAM would
// otherwise force it to reload every field. Same output as dsp_cycle n times, except
// that the echo writes the APU RAM after each sample: if a voice could read sample data
// the echo is overwriting (a game bug, never seen in Super Metroid), the block is run one
// sample at a time.
static bool dsp_rangesOverlap(uint32_t a, uint32_t aLen, uint32_t b, uint32_t bLen) {
  // both ranges wrap at 64K
  return ((a - b) & 0xffff) < bLen || ((b - a) & 0xffff) < aLen;
}

static bool dsp_echoMayFeedVoices(Dsp* dsp, int n) {
  if(!dsp->echoWrites) return false;
  // the write position runs up to index + remain before wrapping to 0, then up to delay
  // (both differ only after the delay register changed)
  uint32_t echoEntries = dsp->echoBufferIndex + dsp->echoRemain;
  if(echoEntries < dsp->echoDelay) echoEntries = dsp->echoDelay;
  uint32_t echoStart = dsp->echoBufferAdr, echoLen = echoEntries * 4;
  for(int ch = 0; ch < 8; ch++) {
    // pitch is at most 0x3fff and a BRR block (9 bytes) lasts 0x10000, so n samples decode
    // at most n / 4 + 1 blocks, from the current position or, after an end flag, from the
    // loop point.
    uint32_t reach = 9 * (n / 4 + 2);
    uint16_t dirEntry = dsp->dirPage + 4 * dsp->channel[ch].srcn;
    uint16_t loop = dsp->apu_ram[(dirEntry + 2) & 0xffff] | dsp->apu_ram[(dirEntry + 3) & 0xffff] << 8;
    if(dsp_rangesOverlap(dirEntry, 4, echoStart, echoLen) ||
       dsp_rangesOverlap(dsp->channel[ch].decodeOffset, reach, echoStart, echoLen) ||
       dsp_rangesOverlap(loop, reach, echoStart, echoLen))
      return true;
  }
  return false;
}

void dsp_cycleBlock(Dsp* dsp, int n) {
  if(n > 1 && dsp_echoMayFeedVoices(dsp, n)) {
    for(int i = 0; i < n; i++) dsp_cycleBlock(dsp, 1);
    return;
  }
  int16_t noise[64];
  int16_t out[8][64];
  for(int i = 0; i < n; i++) {
    noise[i] = dsp->noiseSample;
    dsp_handleNoise(dsp);
  }
  for(int ch = 0; ch < 8; ch++) {
    DspChannel c = dsp->channel[ch];
    bool modulated = ch > 0 && c.pitchModulation;
    for(int i = 0; i < n; i++)
      out[ch][i] = dsp_cycleChannel(dsp, &c, ch, noise[i], modulated ? out[ch - 1][i] : 0);
    dsp->ram[(ch << 4) | 8] = c.gain >> 4;
    dsp->ram[(ch << 4) | 9] = c.sampleOut >> 7;
    dsp->channel[ch] = c;
  }
  // Super Metroid always uses the FIR filter 127,0,0,0,0,0,0,0 (checked over every room).
  bool firOneTap = true;
  for(int i = 1; i < 8; i++) firOneTap &= dsp->firValues[i] == 0;
  for(int i = 0; i < n; i++) {
    int totalL = 0;
    int totalR = 0;
    // the echo input (the same per-voice terms, summed over the voices with echo on) is
    // gathered here instead of in a second pass, and a silent voice is skipped: adding 0
    // to a total that is already clamped changes nothing.
    int echoL = 0;
    int echoR = 0;
    for(int ch = 0; ch < 8; ch++) {
      int sampleOut = out[ch][i];
      if(sampleOut == 0) continue;
      int l = (sampleOut * dsp->channel[ch].volumeL) >> 6;
      int r = (sampleOut * dsp->channel[ch].volumeR) >> 6;
      totalL += l;
      totalR += r;
      totalL = totalL < -0x8000 ? -0x8000 : (totalL > 0x7fff ? 0x7fff : totalL); // clamp 16-bit
      totalR = totalR < -0x8000 ? -0x8000 : (totalR > 0x7fff ? 0x7fff : totalR); // clamp 16-bit
      if(dsp->channel[ch].echoEnable) {
        echoL += l;
        echoR += r;
        echoL = echoL < -0x8000 ? -0x8000 : (echoL > 0x7fff ? 0x7fff : echoL); // clamp 16-bit
        echoR = echoR < -0x8000 ? -0x8000 : (echoR > 0x7fff ? 0x7fff : echoR); // clamp 16-bit
      }
    }
    totalL = (totalL * dsp->masterVolumeL) >> 7;
    totalR = (totalR * dsp->masterVolumeR) >> 7;
    totalL = totalL < -0x8000 ? -0x8000 : (totalL > 0x7fff ? 0x7fff : totalL); // clamp 16-bit
    totalR = totalR < -0x8000 ? -0x8000 : (totalR > 0x7fff ? 0x7fff : totalR); // clamp 16-bit
    dsp_handleEcho(dsp, &totalL, &totalR, echoL, echoR, firOneTap);
    if(dsp->mute) {
      totalL = 0;
      totalR = 0;
    }
    // put it in the samplebuffer
    if (dsp->sampleOffset < 534) {
      dsp->sampleBuffer[dsp->sampleOffset * 2] = totalL;
      dsp->sampleBuffer[dsp->sampleOffset * 2 + 1] = totalR;
      // prevent sampleOffset from going above 534-1 (out of sampleBuffer bounds)
      dsp->sampleOffset++;
    }
    dsp->evenCycle = !dsp->evenCycle;
  }
}

static void dsp_handleEcho(Dsp* dsp, int* outputL, int* outputR, int inL, int inR, bool firOneTap) {
  // get value out of ram
  uint16_t adr = dsp->echoBufferAdr + dsp->echoBufferIndex * 4;
  // 3DS port: adr is a multiple of 4 (page + 4 * index, wrapped at 16 bits), so the four
  // bytes never wrap past 0xffff and need no masking.
  uint8_t *echoRam = dsp->apu_ram + adr;
  dsp->firBufferL[dsp->firBufferIndex] = (int16_t)(echoRam[0] | echoRam[1] << 8) >> 1;
  dsp->firBufferR[dsp->firBufferIndex] = (int16_t)(echoRam[2] | echoRam[3] << 8) >> 1;
  // calculate FIR-sum
  int sumL = 0, sumR = 0;
  if(firOneTap) {
    // 3DS port: with taps 1-7 at 0 only the first term remains, then the same 16-bit clip
    // the loop applies before its last term (it matters for -0x4000 * -128).
    sumL = (int16_t)(((dsp->firBufferL[(dsp->firBufferIndex + 1) & 0x7] * dsp->firValues[0]) >> 6) & 0xffff);
    sumR = (int16_t)(((dsp->firBufferR[(dsp->firBufferIndex + 1) & 0x7] * dsp->firValues[0]) >> 6) & 0xffff);
  } else
  for(int i = 0; i < 8; i++) {
    sumL += (dsp->firBufferL[(dsp->firBufferIndex + i + 1) & 0x7] * dsp->firValues[i]) >> 6;
    sumR += (dsp->firBufferR[(dsp->firBufferIndex + i + 1) & 0x7] * dsp->firValues[i]) >> 6;
    if(i == 6) {
      // clip to 16-bit before last addition
      sumL = ((int16_t) (sumL & 0xffff)); // clip 16-bit
      sumR = ((int16_t) (sumR & 0xffff)); // clip 16-bit
    }
  }
  sumL = sumL < -0x8000 ? -0x8000 : (sumL > 0x7fff ? 0x7fff : sumL); // clamp 16-bit
  sumR = sumR < -0x8000 ? -0x8000 : (sumR > 0x7fff ? 0x7fff : sumR); // clamp 16-bit
  // modify output with sum
  int outL = *outputL + ((sumL * dsp->echoVolumeL) >> 7);
  int outR = *outputR + ((sumR * dsp->echoVolumeR) >> 7);
  *outputL = outL < -0x8000 ? -0x8000 : (outL > 0x7fff ? 0x7fff : outL); // clamp 16-bit
  *outputR = outR < -0x8000 ? -0x8000 : (outR > 0x7fff ? 0x7fff : outR); // clamp 16-bit
  // echo input (inL, inR) comes from dsp_cycleBlock
  // write this to ram
  inL += (sumL * dsp->feedbackVolume) >> 7;
  inR += (sumR * dsp->feedbackVolume) >> 7;
  inL = inL < -0x8000 ? -0x8000 : (inL > 0x7fff ? 0x7fff : inL); // clamp 16-bit
  inR = inR < -0x8000 ? -0x8000 : (inR > 0x7fff ? 0x7fff : inR); // clamp 16-bit
  inL &= 0xfffe;
  inR &= 0xfffe;
  if(dsp->echoWrites) {
    echoRam[0] = inL & 0xff;
    echoRam[1] = inL >> 8;
    echoRam[2] = inR & 0xff;
    echoRam[3] = inR >> 8;
  }
  // handle indexes
  dsp->firBufferIndex++;
  dsp->firBufferIndex &= 7;
  dsp->echoBufferIndex++;
  dsp->echoRemain--;
  if(dsp->echoRemain == 0) {
    dsp->echoRemain = dsp->echoDelay;
    dsp->echoBufferIndex = 0;
  }
}

// Returns the voice's output (also left in c->sampleOut). `modulator`: voice ch-1's
// output of the same sample, for pitch modulation.
static int dsp_cycleChannel(Dsp* dsp, DspChannel* c, int ch, int noiseSample, int modulator) {
  // handle pitch counter
  uint16_t pitch = c->pitch;
  if(ch > 0 && c->pitchModulation) {
    int factor = (modulator >> 4) + 0x400;
    pitch = (pitch * factor) >> 10;
    if(pitch > 0x3fff) pitch = 0x3fff;
  }
  int newCounter = c->pitchCounter + pitch;
  if(newCounter > 0xffff) {
    // next sample
    dsp_decodeBrr(dsp, c, ch);
  }
  c->pitchCounter = newCounter;
#if !MY_CHANGES
  if(dsp->evenCycle) {
    // handle keyon/off (every other cycle)
    if(c->keyOff) {
      // go to release
      c->adsrState = 4;
    } else if(c->keyOn) {
      c->keyOn = false;
      // restart current sample
      c->previousFlags = 0;
      uint16_t samplePointer = dsp->dirPage + 4 * c->srcn;
      c->decodeOffset = dsp->apu_ram[samplePointer];
      c->decodeOffset |= dsp->apu_ram[(samplePointer + 1) & 0xffff] << 8;
      memset(c->decodeBuffer, 0, sizeof(c->decodeBuffer));
      c->gain = 0;
      c->adsrState = c->useGain ? 3 : 0;
    }
  }
#endif
  // handle reset
  if(dsp->reset) {
    c->adsrState = 4;
    c->gain = 0;
  }
  // 3DS port: a voice released down to 0 stays at 0 (release wraps below 0 to 0) and is
  // silent; only its pitch counter and BRR decoding above have to keep running.
  if(c->adsrState == 4 && c->gain == 0) {
    c->sampleOut = 0;
    return 0;
  }
  // handle envelope/adsr
  bool doingDirectGain = c->adsrState != 4 && c->useGain && c->directGain;
  uint16_t rate = c->adsrState == 4 ? 0 : c->adsrRates[c->adsrState];
  if(c->adsrState != 4 && !doingDirectGain && rate != 0) {
    c->rateCounter++;
  }
  if(c->adsrState == 4 || (!doingDirectGain && c->rateCounter >= rate && rate != 0)) {
    if(c->adsrState != 4) c->rateCounter = 0;
    dsp_handleGain(c);
  }
  if(doingDirectGain) c->gain = c->gainValue;
  // set outputs (ENVX and OUTX are written by dsp_cycleBlock)
  // 3DS port: the sample is only needed when it is heard. Interpolating it after the
  // envelope (which never reads it) skips the work for silent voices, same output.
  int sample = 0;
  if(c->gain != 0) {
    if(c->useNoise) {
      sample = noiseSample;
    } else {
      sample = dsp_getSample(c, c->pitchCounter >> 12, (c->pitchCounter >> 4) & 0xff);
    }
  }
  sample = (int16_t)((sample * c->gain) >> 11);
  c->sampleOut = sample;
  return sample;
}

static void dsp_handleGain(DspChannel* c) {
  switch(c->adsrState) {
    case 0: { // attack
      uint16_t rate = c->adsrRates[c->adsrState];
      c->gain += rate == 1 ? 1024 : 32;
      if(c->gain >= 0x7e0) c->adsrState = 1;
      if(c->gain > 0x7ff) c->gain = 0x7ff;
      break;
    }
    case 1: { // decay
      c->gain -= ((c->gain - 1) >> 8) + 1;
      if(c->gain < c->sustainLevel) c->adsrState = 2;
      break;
    }
    case 2: { // sustain
      c->gain -= ((c->gain - 1) >> 8) + 1;
      break;
    }
    case 3: { // gain
      switch(c->gainMode) {
        case 0: { // linear decrease
          c->gain -= 32;
          // decreasing below 0 will underflow to above 0x7ff
          if(c->gain > 0x7ff) c->gain = 0;
          break;
        }
        case 1: { // exponential decrease
          c->gain -= ((c->gain - 1) >> 8) + 1;
          break;
        }
        case 2: { // linear increase
          c->gain += 32;
          if(c->gain > 0x7ff) c->gain = 0x7ff;
          break;
        }
        case 3: { // bent increase
          c->gain += c->gain < 0x600 ? 32 : 8;
          if(c->gain > 0x7ff) c->gain = 0x7ff;
          break;
        }
      }
      break;
    }
    case 4: { // release
      c->gain -= 8;
      // decreasing below 0 will underflow to above 0x7ff
      if(c->gain > 0x7ff) c->gain = 0;
      break;
    }
  }
}

static int16_t dsp_getSample(const DspChannel* c, int sampleNum, int offset) {
  int16_t news = c->decodeBuffer[sampleNum + 3];
  int16_t olds = c->decodeBuffer[sampleNum + 2];
  int16_t olders = c->decodeBuffer[sampleNum + 1];
  int16_t oldests = c->decodeBuffer[sampleNum];
  int out = (gaussValues[0xff - offset] * oldests) >> 10;
  out += (gaussValues[0x1ff - offset] * olders) >> 10;
  out += (gaussValues[0x100 + offset] * olds) >> 10;
  out = ((int16_t) (out & 0xffff)); // clip 16-bit
  out += (gaussValues[offset] * news) >> 10;
  out = out < -0x8000 ? -0x8000 : (out > 0x7fff ? 0x7fff : out); // clamp 16-bit
  return out >> 1;
}

static void dsp_decodeBrr(Dsp* dsp, DspChannel* c, int ch) {
  // copy last 3 samples (16-18) to first 3 for interpolation
  c->decodeBuffer[0] = c->decodeBuffer[16];
  c->decodeBuffer[1] = c->decodeBuffer[17];
  c->decodeBuffer[2] = c->decodeBuffer[18];
  // handle flags from previous block
  if(c->previousFlags == 1 || c->previousFlags == 3) {
    // loop sample
    uint16_t samplePointer = dsp->dirPage + 4 * c->srcn;
    c->decodeOffset = dsp->apu_ram[(samplePointer + 2) & 0xffff];
    c->decodeOffset |= (dsp->apu_ram[(samplePointer + 3) & 0xffff]) << 8;
    if(c->previousFlags == 1) {
      // also release and clear gain
      c->adsrState = 4;
      c->gain = 0;
    }
    dsp->ram[0x7c] |= 1 << ch; // set ENDx
  }
  uint8_t header = dsp->apu_ram[c->decodeOffset++];
  int shift = header >> 4;
  int filter = (header & 0xc) >> 2;
  c->previousFlags = header & 0x3;
  uint8_t curByte = 0;
  int old = c->old;
  int older = c->older;
  for(int i = 0; i < 16; i++) {
    int s = 0;
    if(i & 1) {
      s = curByte & 0xf;
    } else {
      curByte = dsp->apu_ram[c->decodeOffset++];
      s = curByte >> 4;
    }
    if(s > 7) s -= 16;
    if(shift <= 0xc) {
      s = (s << shift) >> 1;
    } else {
      s = (s >> 3) << 12;
    }
    switch(filter) {
      case 1: s += old + (-old >> 4); break;
      case 2: s += 2 * old + ((3 * -old) >> 5) - older + (older >> 4); break;
      case 3: s += 2 * old + ((13 * -old) >> 6) - older + ((3 * older) >> 4); break;
    }
    s = s < -0x8000 ? -0x8000 : (s > 0x7fff ? 0x7fff : s); // clamp 16-bit
    s = ((int16_t) ((s & 0x7fff) << 1)) >> 1; // clip 15-bit
    older = old;
    old = s;
    c->decodeBuffer[i + 3] = s;
  }
  c->older = older;
  c->old = old;
}

static void dsp_handleNoise(Dsp* dsp) {
  if(dsp->noiseRate != 0) {
    dsp->noiseCounter++;
  }
  if(dsp->noiseCounter >= dsp->noiseRate && dsp->noiseRate != 0) {
    int bit = (dsp->noiseSample & 1) ^ ((dsp->noiseSample >> 1) & 1);
    dsp->noiseSample = ((dsp->noiseSample >> 1) & 0x3fff) | (bit << 14);
    dsp->noiseSample = ((int16_t) ((dsp->noiseSample & 0x7fff) << 1)) >> 1;
    dsp->noiseCounter = 0;
  }
}

uint8_t dsp_read(Dsp* dsp, uint8_t adr) {
  return dsp->ram[adr];
}

void dsp_write(Dsp* dsp, uint8_t adr, uint8_t val) {
  int ch = adr >> 4;
  switch(adr) {
    case 0x00: case 0x10: case 0x20: case 0x30: case 0x40: case 0x50: case 0x60: case 0x70: {
      dsp->channel[ch].volumeL = val;
      break;
    }
    case 0x01: case 0x11: case 0x21: case 0x31: case 0x41: case 0x51: case 0x61: case 0x71: {
      dsp->channel[ch].volumeR = val;
      break;
    }
    case 0x02: case 0x12: case 0x22: case 0x32: case 0x42: case 0x52: case 0x62: case 0x72: {
      dsp->channel[ch].pitch = (dsp->channel[ch].pitch & 0x3f00) | val;
      break;
    }
    case 0x03: case 0x13: case 0x23: case 0x33: case 0x43: case 0x53: case 0x63: case 0x73: {
      dsp->channel[ch].pitch = ((dsp->channel[ch].pitch & 0x00ff) | (val << 8)) & 0x3fff;
      break;
    }
    case 0x04: case 0x14: case 0x24: case 0x34: case 0x44: case 0x54: case 0x64: case 0x74: {
      dsp->channel[ch].srcn = val;
      break;
    }
    case 0x05: case 0x15: case 0x25: case 0x35: case 0x45: case 0x55: case 0x65: case 0x75: {
      dsp->channel[ch].adsrRates[0] = rateValues[(val & 0xf) * 2 + 1];
      dsp->channel[ch].adsrRates[1] = rateValues[((val & 0x70) >> 4) * 2 + 16];
      dsp->channel[ch].useGain = (val & 0x80) == 0;
      break;
    }
    case 0x06: case 0x16: case 0x26: case 0x36: case 0x46: case 0x56: case 0x66: case 0x76: {
      dsp->channel[ch].adsrRates[2] = rateValues[val & 0x1f];
      dsp->channel[ch].sustainLevel = (((val & 0xe0) >> 5) + 1) * 0x100;
      break;
    }
    case 0x07: case 0x17: case 0x27: case 0x37: case 0x47: case 0x57: case 0x67: case 0x77: {
      dsp->channel[ch].directGain = (val & 0x80) == 0;
      if(val & 0x80) {
        dsp->channel[ch].gainMode = (val & 0x60) >> 5;
        dsp->channel[ch].adsrRates[3] = rateValues[val & 0x1f];
      } else {
        dsp->channel[ch].gainValue = (val & 0x7f) * 16;
      }
      break;
    }
    case 0x0c: {
      dsp->masterVolumeL = val;
      break;
    }
    case 0x1c: {
      dsp->masterVolumeR = val;
      break;
    }
    case 0x2c: {
      dsp->echoVolumeL = val;
      break;
    }
    case 0x3c: {
      dsp->echoVolumeR = val;
      break;
    }
    case 0x4c: {
      for(int ch = 0; ch < 8; ch++) {
        dsp->channel[ch].keyOn = val & (1 << ch);
#if MY_CHANGES
        if (dsp->channel[ch].keyOn) {
          dsp->channel[ch].keyOn = false;
          // restart current sample
          dsp->channel[ch].previousFlags = 0;
          uint16_t samplePointer = dsp->dirPage + 4 * dsp->channel[ch].srcn;
          dsp->channel[ch].decodeOffset = dsp->apu_ram[samplePointer];
          dsp->channel[ch].decodeOffset |= dsp->apu_ram[(samplePointer + 1) & 0xffff] << 8;
          memset(dsp->channel[ch].decodeBuffer, 0, sizeof(dsp->channel[ch].decodeBuffer));
          dsp->channel[ch].gain = 0;
          dsp->channel[ch].adsrState = dsp->channel[ch].useGain ? 3 : 0;
        }
#endif
      }
      break;
    }
    case 0x5c: {
      for(int ch = 0; ch < 8; ch++) {
        dsp->channel[ch].keyOff = val & (1 << ch);
#if MY_CHANGES
        if (dsp->channel[ch].keyOff) {
          // go to release
          dsp->channel[ch].adsrState = 4;
        }
#endif
      }
      break;
    }
    case 0x6c: {
      dsp->reset = val & 0x80;
      dsp->mute = val & 0x40;
      dsp->echoWrites = (val & 0x20) == 0;
      dsp->noiseRate = rateValues[val & 0x1f];
      break;
    }
    case 0x7c: {
      val = 0; // any write clears ENDx
      break;
    }
    case 0x0d: {
      dsp->feedbackVolume = val;
      break;
    }
    case 0x2d: {
      for(int i = 0; i < 8; i++) {
        dsp->channel[i].pitchModulation = val & (1 << i);
      }
      break;
    }
    case 0x3d: {
      for(int i = 0; i < 8; i++) {
        dsp->channel[i].useNoise = val & (1 << i);
      }
      break;
    }
    case 0x4d: {
      for(int i = 0; i < 8; i++) {
        dsp->channel[i].echoEnable = val & (1 << i);
      }
      break;
    }
    case 0x5d: {
      dsp->dirPage = val << 8;
      break;
    }
    case 0x6d: {
      dsp->echoBufferAdr = val << 8;
      break;
    }
    case 0x7d: {
      dsp->echoDelay = (val & 0xf) * 512; // 2048-byte steps, stereo sample is 4 bytes
      if(dsp->echoDelay == 0) dsp->echoDelay = 1;
      break;
    }
    case 0x0f: case 0x1f: case 0x2f: case 0x3f: case 0x4f: case 0x5f: case 0x6f: case 0x7f: {
      dsp->firValues[ch] = val;
      break;
    }
  }
  dsp->ram[adr] = val;
}

void dsp_getSamples(Dsp* dsp, int16_t* sampleData, int samplesPerFrame) {
  // resample from 534 samples per frame to wanted value
  if (samplesPerFrame == 534) {   // the DSP's own rate: nothing to resample
    memcpy(sampleData, dsp->sampleBuffer, 534 * 2 * sizeof(int16_t));
    dsp->sampleOffset = 0;
    return;
  }
  double adder = 534.0 / samplesPerFrame;
  double location = 0.0;
  for(int i = 0; i < samplesPerFrame; i++) {
    sampleData[i * 2] = dsp->sampleBuffer[((int) location) * 2];
    sampleData[i * 2 + 1] = dsp->sampleBuffer[((int) location) * 2 + 1];
    location += adder;
  }
  dsp->sampleOffset = 0;
}
