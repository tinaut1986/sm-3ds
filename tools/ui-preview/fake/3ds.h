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
u64 svcGetSystemTick(void);
void gspWaitForVBlank(void);
#define SYSCLOCK_ARM11 268111856
size_t linearSpaceFree(void);
int APT_CheckNew3DS(bool *out);
void osSetSpeedupEnable(bool e);
typedef s32 Result;
#define R_SUCCEEDED(r) ((Result)(r) >= 0)
Result ptmuInit(void);
void ptmuExit(void);
Result PTMU_GetBatteryLevel(u8 *out);
Result PTMU_GetBatteryChargeState(u8 *out);
u8 osGetWifiStrength(void);

// Software keyboard: the preview never opens it; the stub answers "cancelled".
typedef struct { int unused; } SwkbdState;
typedef enum { SWKBD_TYPE_NORMAL } SwkbdType;
typedef enum { SWKBD_BUTTON_LEFT, SWKBD_BUTTON_MIDDLE, SWKBD_BUTTON_RIGHT, SWKBD_BUTTON_CONFIRM = SWKBD_BUTTON_RIGHT,
               SWKBD_BUTTON_NONE = -1 } SwkbdButton;
static inline void swkbdInit(SwkbdState *s, SwkbdType t, int buttons, int max) { (void)s; (void)t; (void)buttons; (void)max; }
static inline void swkbdSetHintText(SwkbdState *s, const char *t) { (void)s; (void)t; }
static inline SwkbdButton swkbdInputText(SwkbdState *s, char *buf, size_t n) { (void)s; (void)buf; (void)n; return SWKBD_BUTTON_NONE; }
