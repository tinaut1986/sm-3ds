// Minimal stand-in for libctru, just enough to compile the bottom UI on the host.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64; typedef int32_t s32;
typedef enum { GFX_TOP, GFX_BOTTOM } gfxScreen_t;
typedef enum { GFX_LEFT, GFX_RIGHT } gfx3dSide_t;
u8 *gfxGetFramebuffer(gfxScreen_t screen, gfx3dSide_t side, u16 *width, u16 *height);
u64 osGetTime(void);
size_t linearSpaceFree(void);
int APT_CheckNew3DS(bool *out);
void osSetSpeedupEnable(bool e);
