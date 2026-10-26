#include "dsp_firm_ui.h"

#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "dsp_firm.h"
#include "ui_draw.h"
#include "ui_lang.h"
#include "version.h"

enum { kBottomW = 320, kTopW = 400, kScreenH = 240 };

typedef enum { kAsk, kWorking, kResult, kStarting } Page;

static Rect ExtractRect(void) { return (Rect){ 24, 176, 128, 28 }; }
static Rect SkipRect(void) { return (Rect){ 168, 176, 128, 28 }; }
static Rect OkRect(void) { return (Rect){ 96, 176, 128, 28 }; }

// A button with the pad's key inside it, in a ring at its left: a round badge 15 px across.
static void KeyButton(Surface s, Rect r, uint32_t body, uint32_t border, uint32_t text, char key, bool pressed, const char *label) {
  UiDrawBox(s, r, body, border, pressed);
  const int cx = r.x + 16, cy = r.y + r.h / 2 + (pressed ? 1 : 0), rad = 7;
  for (int dy = -rad; dy <= rad; dy++) {
    int dx = 0;
    while ((dx + 1) * (dx + 1) + dy * dy <= rad * rad + rad) dx++;
    UiFillRect(s, cx - dx, cy + dy, 2 * dx + 1, 1, text);
  }
  const char k[2] = { key, 0 };
  UiDrawText(s, cx - 2, cy - 3, 1, body, k);
  UiDrawTextCentered(s, r.x + 30 + (r.w - 30) / 2, r.y + (r.h - 7) / 2 + (pressed ? 1 : 0), text, label);
}

// Both screens are ours until the game starts: the top one carries the name, the bottom one the dialog.
static void Draw(Page page, DspFirmStatus st, const char *failure, bool held, int tx, int ty, bool ok) {
  Surface top = UiDraw_Screen(GFX_TOP);
  UiFillRect(top, 0, 0, kTopW, kScreenH, COL_BG);
  UiFillRect(top, 0, 96, kTopW, 2, COL_BORDER);
  UiFillRect(top, 0, 142, kTopW, 2, COL_BORDER);
  const char *name = "SUPER METROID 3DS";
  UiDrawText(top, (kTopW - UiTextWidth(name, 3)) / 2, 108, 3, COL_TITLE, name);
  UiDrawTextCentered(top, kTopW / 2, 150, COL_DIM, APP_VERSION_LABEL);

  Surface s = UiDraw_Screen(GFX_BOTTOM);
  UiFillRect(s, 0, 0, kBottomW, kScreenH, COL_BG);
  const Rect panel = { 10, 24, 300, 192 };
  UiFillRect(s, panel.x, panel.y, panel.w, panel.h, COL_MODAL_EDGE);
  UiFillRect(s, panel.x + 1, panel.y + 1, panel.w - 2, panel.h - 2, COL_MODAL);
  const int cx = kBottomW / 2;
  if (page == kAsk) {
    UiDrawTextCentered(s, cx, 38, COL_TITLE, Tr(kStrDspTitle));
    UiDrawTextCentered(s, cx, 62, COL_TEXT, Tr(st == DSPFIRM_MISSING ? kStrDspMissing : kStrDspDamaged));
    UiDrawTextCentered(s, cx, 92, COL_WARN, Tr(kStrDspWhy1));
    UiDrawTextCentered(s, cx, 112, COL_TEXT, Tr(kStrDspWhy2));
    const Rect e = ExtractRect(), k = SkipRect();
    KeyButton(s, e, RGB(22, 44, 30), RGB(60, 150, 90), RGB(150, 255, 180), 'A', held && UiIn(e, tx, ty), Tr(kStrDspExtract));
    KeyButton(s, k, COL_BOX, COL_BOX_EDGE, COL_TEXT, 'B', held && UiIn(k, tx, ty), Tr(kStrDspSkip));
  } else if (page == kWorking || page == kStarting) {   // starting the game takes a while: say the key was heard
    UiDrawTextCentered(s, cx, 112, COL_TITLE, Tr(page == kWorking ? kStrDspWorking : kStrWait));
  } else {
    UiDrawTextCentered(s, cx, 70, ok ? COL_GOOD : COL_BAD, Tr(ok ? kStrDspDone : kStrDspFailed));
    if (!ok) {
      UiDrawTextCentered(s, cx, 94, COL_DIM, failure);
      UiDrawTextCentered(s, cx, 118, COL_WARN, Tr(kStrDspWhy1));
    }
    const Rect o = OkRect();
    UiDrawBoxLabel(s, o, COL_BOX, COL_BOX_EDGE, COL_TEXT, held && UiIn(o, tx, ty), Tr(kStrOk));
  }
}

static void Show(void) {
  UiDraw_Present(GFX_TOP, false);
  UiDraw_Present(GFX_BOTTOM, false);
  gfxFlushBuffers();
  gfxScreenSwapBuffers(GFX_TOP, false);
  gfxScreenSwapBuffers(GFX_BOTTOM, false);
  UiDraw_Swapped();
  gspWaitForVBlank();
  UiDraw_VBlankSeen();
}

typedef enum { kNone, kYes, kNo } Answer;

// One frame of input: A / the left button is a yes, B / the right one a no (an OK button takes either).
static Answer Poll(Page page, int *tx, int *ty, bool *held) {
  hidScanInput();
  const u32 down = hidKeysDown();
  touchPosition t;
  hidTouchRead(&t);
  *held = (hidKeysHeld() & KEY_TOUCH) != 0;
  if (*held) *tx = t.px, *ty = t.py;
  if (down & KEY_A) return kYes;
  if (down & KEY_B) return page == kResult ? kYes : kNo;
  static bool was_held;
  Answer a = kNone;
  if (was_held && !*held) {   // a tap counts when it is lifted over a button
    if (UiIn(page == kResult ? OkRect() : ExtractRect(), *tx, *ty)) a = kYes;
    else if (page == kAsk && UiIn(SkipRect(), *tx, *ty)) a = kNo;
  }
  was_held = *held;
  return a;
}

static void ShowStarting(void) {
  Draw(kStarting, DSPFIRM_OK, NULL, false, 0, 0, false);
  Show();
}

const char *DspFirmUi_Ensure(void) {
  const DspFirmStatus st = DspFirm_Check(DSPFIRM_PATH);
  if (st == DSPFIRM_OK) return "ok";
  if (st == DSPFIRM_MISSING && R_SUCCEEDED(ndspInit())) {   // an emulator brings its own firmware
    ndspExit();
    return "no file, but ndspInit works";
  }

  int tx = 0, ty = 0;
  bool held = false;
  Answer a = kNone;
  while (aptMainLoop() && a == kNone) {
    a = Poll(kAsk, &tx, &ty, &held);
    Draw(kAsk, st, NULL, held, tx, ty, false);
    Show();
  }
  if (a != kYes) {
    ShowStarting();
    return "asked, refused";
  }

  Draw(kWorking, st, NULL, false, 0, 0, false);
  Show();
  int32_t detail = 0;
  const DspFirmExtractResult r = DspFirm_Extract(DSPFIRM_PATH, &detail);
  static char note[560];
  snprintf(note, sizeof(note), "extraction result %d (%s), detail %08lX | %s", (int)r, DspFirm_ExtractText(r), (unsigned long)detail,
           g_dspfirm_trace);
  char failure[48];
  snprintf(failure, sizeof(failure), "ERROR %d / %08lX", (int)r, (unsigned long)detail);

  a = kNone;
  while (aptMainLoop() && a == kNone) {
    a = Poll(kResult, &tx, &ty, &held);
    Draw(kResult, st, failure, held, tx, ty, r == DSPFIRM_EX_OK);
    Show();
  }
  ShowStarting();
  return note;
}
