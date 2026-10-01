#ifndef SM_CPU_INFRA_H_
#define SM_CPU_INFRA_H_

#include "types.h"
#include "snes/cpu.h"
#include "snes/snes.h"

typedef struct Snes Snes;
extern Snes *g_snes;
extern bool g_fail;

typedef struct Snes Snes;

Snes *SnesInit(const char *filename);

int RunAsmCode(uint32 pc, uint16 a, uint16 x, uint16 y, int flags);
bool ProcessHook(uint32 v);

void Call(uint32 addr);

void RunOneFrameOfGame();
void ClearUnusedOam();

void RunOneFrameOfGame_Both();

void SwapEmulatedNative(void);

// 3DS port: called in FULL_NATIVE frames after the game logic (and its NMI) and before
// the PPU draws the lines; null unless set. The WIDE view fills tilemap areas here.
extern void (*g_rtl_before_ppu_draw)(void);

#endif  // SM_CPU_INFRA_H_  
