// 3DS port: OPTIONS -> PLAY -> REDUX (P4.26, off by default): Super Metroid Redux's quality-of-life
// changes to the game itself (ShadowOne333's hack, https://github.com/ShadowOne333/Super-Metroid-Redux),
// done again on the C game: Redux's values, the port's own code. What it covers is listed in sm_rtl.h.
// Part of it is ROM data the C game reads as it runs (the doors' instruction lists): those bytes are
// patched in the loaded ROM while the option is on and put back when it goes off (RtlRedux_SyncRom,
// every frame). The rest is code, behind RtlRedux() where the original's code is.
#include "ida_types.h"
#include "sm_rtl.h"
#include "variables.h"
#include "funcs.h"

bool g_rtl_redux;   // see sm_rtl.h

bool RtlRedux(void) {
  return g_rtl_redux && !(game_state >= kGameState_40_TransitionToDemo && game_state <= kGameState_44_TransitionFromDemo);
}

typedef struct { uint32 addr; uint8 original, redux; } ReduxRomByte;

static const ReduxRomByte kReduxRom[] = {
  // The red (missile) doors open with one missile, not five: the count of
  // PlmInstr_IncrementDoorHitCounterAndJGE in each of the four doors' lists.
  { 0x84C32C, 5, 1 }, { 0x84C38E, 5, 1 }, { 0x84C3F0, 5, 1 }, { 0x84C452, 5, 1 },
  // The doors' opening and closing animations (grey, yellow, green, red and blue, the four
  // directions): the frame counts of their instruction lists, Redux's.
{ 0x84BE59, 2, 8 }, { 0x84BE5D, 2, 1 }, { 0x84BE64, 2, 1 }, { 0x84BE68, 2, 1 }, { 0x84BEB0, 4, 1 },
  { 0x84BEB4, 4, 1 }, { 0x84BEB8, 4, 1 }, { 0x84BEC2, 2, 8 }, { 0x84BEC6, 2, 1 }, { 0x84BECD, 2, 1 },
  { 0x84BED1, 2, 1 }, { 0x84BF19, 4, 1 }, { 0x84BF1D, 4, 1 }, { 0x84BF21, 4, 1 }, { 0x84BF2B, 2, 8 },
  { 0x84BF2F, 2, 1 }, { 0x84BF36, 2, 1 }, { 0x84BF3A, 2, 1 }, { 0x84BF82, 4, 1 }, { 0x84BF86, 4, 1 },
  { 0x84BF8A, 4, 1 }, { 0x84BF94, 2, 8 }, { 0x84BF98, 2, 1 }, { 0x84BF9F, 2, 1 }, { 0x84BFA3, 2, 1 },
  { 0x84BFEB, 4, 1 }, { 0x84BFEF, 4, 1 }, { 0x84BFF3, 4, 1 }, { 0x84BFFD, 2, 8 }, { 0x84C001, 2, 1 },
  { 0x84C008, 2, 1 }, { 0x84C00C, 2, 1 }, { 0x84C04E, 4, 1 }, { 0x84C052, 4, 1 }, { 0x84C056, 4, 1 },
  { 0x84C060, 2, 1 }, { 0x84C064, 2, 1 }, { 0x84C06B, 2, 1 }, { 0x84C06F, 2, 1 }, { 0x84C0B1, 6, 1 },
  { 0x84C0B5, 6, 1 }, { 0x84C0B9, 6, 1 }, { 0x84C0C3, 2, 8 }, { 0x84C0C7, 2, 1 }, { 0x84C0CE, 2, 1 },
  { 0x84C0D2, 2, 1 }, { 0x84C110, 6, 1 }, { 0x84C114, 6, 1 }, { 0x84C118, 6, 1 }, { 0x84C122, 2, 8 },
  { 0x84C126, 2, 1 }, { 0x84C12D, 2, 1 }, { 0x84C131, 2, 1 }, { 0x84C173, 6, 1 }, { 0x84C177, 6, 1 },
  { 0x84C17B, 6, 1 }, { 0x84C185, 2, 8 }, { 0x84C189, 2, 1 }, { 0x84C190, 2, 1 }, { 0x84C194, 2, 1 },
  { 0x84C1D2, 6, 1 }, { 0x84C1D6, 6, 1 }, { 0x84C1DA, 6, 1 }, { 0x84C1E4, 2, 8 }, { 0x84C1E8, 2, 1 },
  { 0x84C1EF, 2, 1 }, { 0x84C1F3, 2, 1 }, { 0x84C231, 6, 1 }, { 0x84C235, 6, 1 }, { 0x84C239, 6, 1 },
  { 0x84C243, 2, 8 }, { 0x84C247, 2, 1 }, { 0x84C24E, 2, 1 }, { 0x84C252, 2, 1 }, { 0x84C290, 6, 1 },
  { 0x84C294, 6, 1 }, { 0x84C298, 6, 1 }, { 0x84C2A2, 2, 8 }, { 0x84C2A6, 2, 1 }, { 0x84C2AD, 2, 1 },
  { 0x84C2B1, 2, 1 }, { 0x84C2EF, 6, 1 }, { 0x84C2F3, 6, 1 }, { 0x84C2F7, 6, 1 }, { 0x84C301, 2, 8 },
  { 0x84C351, 6, 1 }, { 0x84C355, 6, 1 }, { 0x84C359, 6, 1 }, { 0x84C363, 2, 8 }, { 0x84C3B3, 6, 1 },
  { 0x84C3B7, 6, 1 }, { 0x84C3BB, 6, 1 }, { 0x84C415, 6, 1 }, { 0x84C419, 6, 1 }, { 0x84C41D, 6, 1 },
  { 0x84C427, 2, 8 }, { 0x84C477, 6, 1 }, { 0x84C47B, 6, 1 }, { 0x84C47F, 6, 1 }, { 0x84C48C, 6, 1 },
  { 0x84C490, 6, 1 }, { 0x84C494, 6, 1 }, { 0x84C49E, 2, 8 }, { 0x84C4BD, 6, 1 }, { 0x84C4C1, 6, 1 },
  { 0x84C4C5, 6, 1 }, { 0x84C4CF, 2, 8 }, { 0x84C4EE, 6, 1 }, { 0x84C4F2, 6, 1 }, { 0x84C4F6, 6, 1 },
  { 0x84C500, 2, 8 }, { 0x84C51F, 6, 1 }, { 0x84C523, 6, 1 }, { 0x84C527, 6, 1 }, { 0x84C531, 2, 8 },
};
enum { kReduxRomCount = sizeof(kReduxRom) / sizeof(kReduxRom[0]) };

static bool g_rom_patched;
static int8 g_rom_matches = -1;   // -1 not checked yet, 0 not the supported ROM (nothing is patched)

void RtlRedux_SyncRom(void) {
  bool want = RtlRedux();
  if (want == g_rom_patched || !g_rom)
    return;
  if (g_rom_matches < 0) {
    g_rom_matches = 1;
    for (int i = 0; i < kReduxRomCount; i++)
      if (*RomFixedPtr(kReduxRom[i].addr) != kReduxRom[i].original)
        g_rom_matches = 0;
  }
  if (!g_rom_matches)
    return;
  for (int i = 0; i < kReduxRomCount; i++)
    *(uint8 *)RomFixedPtr(kReduxRom[i].addr) = want ? kReduxRom[i].redux : kReduxRom[i].original;
  g_rom_patched = want;
}

// The expansions' item lists whose fanfare REDUX replaces with a sound (the chozo orbs' missile, super
// missile and power bomb, the first of each, keep it, as in Redux): the address of their
// PlmInstr_ClearMusicQueueAndQueueTrack(2).
static const uint16 kReduxExpansionFanfares[] = {
  0xE0B3, 0xE0D8, 0xE0FD, 0xE122,   // energy tank, missile, super missile, power bomb
  0xE43F, 0xE46F,                   // reserve tank, chozo orb energy tank
  0xE904, 0xE93A, 0xE972, 0xE9AA, 0xE9E2,   // in a scenery block: reserve, energy tank, missile, super, power bomb
  0xEE3E,                           // reserve tank in a shot block
};
static bool g_expansion_sound;

bool RtlRedux_ExpansionSound(const uint8 *plmp) {
  if (!RtlRedux() || plmp[0] != 2)
    return false;
  size_t off = (size_t)(plmp - 1 - g_rom);
  if ((off >> 15) != 4)   // bank $84
    return false;
  uint16 addr = 0x8000 | (off & 0x7fff);
  for (size_t i = 0; i < sizeof(kReduxExpansionFanfares) / sizeof(kReduxExpansionFanfares[0]); i++) {
    if (kReduxExpansionFanfares[i] == addr) {
      QueueSfx1_Max6(0x22);   // Redux's (sound library 1)
      g_expansion_sound = true;
      return true;
    }
  }
  return false;
}

bool RtlRedux_TakeExpansionSound(void) {
  bool r = g_expansion_sound;
  g_expansion_sound = false;
  return r;
}
