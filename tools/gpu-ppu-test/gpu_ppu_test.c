// Host check of the GPU renderer's frame building (source/gpu_ppu.c): every tested
// frame is drawn three ways and compared.
//   A  the CPU renderer as the game normally runs it
//   B  the same frame captured line by line, then drawn by the CPU from the capture
//      (ppu_replayLines): must equal A, or the capture misses a register
//   C  the capture turned into a GpuFrame and drawn by gpu_ppu_ref.c: must equal B
// A failing frame is written as a PPM (B | C | difference).
//
// usage: gpu_ppu_test ROM state FILE.sav [FRAMES]      frames from a console state
//        gpu_ppu_test ROM rooms [FRAMES] [ROOM_HEX]   boot, warp into every room
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "src/types.h"
#include "src/sm_rtl.h"
#include "src/sm_cpu_infra.h"
#include "src/config.h"
#include "src/snes/ppu.h"
#include "src/snes/snes.h"
#include "src/spc_player.h"
#include "src/variables.h"
#include "src/funcs.h"
#include "src/ida_types.h"
#include "src/enemy_types.h"
#include "gpu_ppu.h"
#include "gpu_ppu_ref.h"
#include "sm_map.h"
#include "sm_warp.h"
#include "sm_wide.h"
#include "game_text.h"
#include "ui_lang.h"

bool g_debug_flag, g_is_turbo, g_want_dump_memmap_flags, g_new_ppu = true, g_other_image;
struct SpcPlayer *g_spc_player;
int g_got_mismatch_count;
extern Snes *g_snes;
void NORETURN Die(const char *e) { fprintf(stderr, "Die: %s\n", e); exit(3); }
void Warning(const char *e) {}
void RtlDrawPpuFrame(uint8 *b, size_t p, uint32 f) {}
void RtlApuLock(void) {}
void RtlApuUnlock(void) {}
void RtlApuQueueLock(void) {}
void RtlApuQueueUnlock(void) {}
int SDL_GetKeyFromName(const char *n) { return 0; }

bool GpuBackend_TexCreate(GpuTex *t, int w, int h) {
  t->px = calloc((size_t)w * h, 2);
  t->w = w;
  t->h = h;
  return t->px != NULL;
}
void GpuBackend_TexFree(GpuTex *t) { free(t->px); t->px = NULL; }
void GpuBackend_TexWritten(GpuTex *t, int y0, int y1) {}

enum { kPitch = 256 * 4 };
static uint8_t g_px[kPitch * 240], g_a[kPitch * 240], g_b[kPitch * 240], g_c[kPitch * 240];
static PpuLineCapture g_cap;
static int g_input;   // controller bits for TestFrame's frames
static GpuFrame g_frame;
static int g_frames, g_capture_bad, g_gpu_bad, g_refused, g_dumped;
static long g_tiles, g_composed, g_quads, g_max_quads, g_composed_frames;
static double g_build_us, g_build_max_us, g_replay_us;
static char g_refuse_reasons[16][64];
static int g_refuse_counts[16];

static void NoteRefusal(const char *why) {
  for (int i = 0; i < 16; i++) {
    if (!g_refuse_reasons[i][0]) snprintf(g_refuse_reasons[i], 64, "%s", why);
    if (!strcmp(g_refuse_reasons[i], why)) { g_refuse_counts[i]++; return; }
  }
}

// Pixels that differ in rows 0..223; also their bounding box.
static int Diff(const uint8_t *a, const uint8_t *b, int *bx0, int *by0, int *bx1, int *by1) {
  int n = 0;
  *bx0 = *by0 = 999, *bx1 = *by1 = -1;
  for (int y = 0; y < kGpuRows; y++)
    for (int x = 0; x < 256; x++) {
      const uint32_t pa = *(const uint32_t *)&a[y * kPitch + x * 4] & 0xffffff;
      const uint32_t pb = *(const uint32_t *)&b[y * kPitch + x * 4] & 0xffffff;
      if (pa == pb) continue;
      n++;
      if (x < *bx0) *bx0 = x;
      if (y < *by0) *by0 = y;
      if (x > *bx1) *bx1 = x;
      if (y > *by1) *by1 = y;
    }
  return n;
}

static void DumpTriptych(const char *name, const uint8_t *b, const uint8_t *c) {
  FILE *f = fopen(name, "wb");
  if (!f) return;
  fprintf(f, "P6\n768 224\n255\n");
  for (int y = 0; y < kGpuRows; y++)
    for (int x = 0; x < 768; x++) {
      const int k = x & 255;
      const uint8_t *pb = &b[y * kPitch + k * 4], *pc = &c[y * kPitch + k * 4];
      uint8_t rgb[3];
      if (x < 256) rgb[0] = pb[2], rgb[1] = pb[1], rgb[2] = pb[0];
      else if (x < 512) rgb[0] = pc[2], rgb[1] = pc[1], rgb[2] = pc[0];
      else {
        const bool d = memcmp(pb, pc, 3) != 0;
        rgb[0] = d ? 255 : pb[2] / 4, rgb[1] = d ? 0 : pb[1] / 4, rgb[2] = d ? 255 : pb[0] / 4;
      }
      fwrite(rgb, 1, 3, f);
    }
  fclose(f);
}

// WIDE=M: the frame just checked is built again with M pixels on each side. Its middle
// 256 columns must equal the normal build's (g_c); WIDE_DUMP=N writes the first N wide
// frames as wide-NNN.ppm for a look.
static uint8_t g_w[(256 + 2 * kGpuMaxMargin) * 4 * (kGpuRows + 2 * kGpuMaxExtraRows)], g_n[kPitch * 240];
static int g_wide_bad, g_wide_frames, g_wide_dumped, g_wide_tagdiff, g_wide_filled;
static uint64_t g_wide_hash = 1469598103934665603ull;   // every WIDE frame, margins included

static void TestWide(const char *label) {
  int ml, mr, hud_x, bg2_dx;   // this frame's margins, leaning off room edges (SmWide)
  SmWide_Margins(&ml, &mr, &hud_x, &bg2_dx);
  GpuPpu_SetLayerShiftX(1, bg2_dx);   // for the reference build too: it moves the middle
  int et, eb, hud_y;   // the extra rows, leaning off a room's top or bottom the same way
  SmWide_Rows(&et, &eb, &hud_y);
  if (getenv("WIDE_LEAN")) printf("  lean %d/%d rows %d/%d hud %d,%d bg2 %d\n", ml, mr, et, eb, hud_x, hud_y, bg2_dx);
  const int w = 256 + ml + mr, pitch = w * 4;
  const int ey = et, rows = kGpuRows + et + eb;   // ey: the image row of view row 0
  const char *why;
  // The reference for the middle: the normal frame, but with sprites placed by their full X
  // too (the WIDE game logic draws enemies whose pieces the 9-bit X would wrap into view).
  g_gpu_ppu_obj_x = g_rtl_oam_shown_x;
  g_gpu_ppu_obj_y = g_rtl_oam_shown_y;
  GpuPpu_SetNoSpriteWrap(true);   // as the WIDE frame (and the console with WIDE on)
  // So the mode 7 plane under the HUD is in the reference. WIDE_HUD_INBAND=1: not, so the
  // reference draws the HUD within its band, as without WIDE (the WIDE frame draws it over
  // everything, GpuFrame.hud_first): checks that both give the same image.
  GpuPpu_SetNarrowBg3Rows(getenv("WIDE_HUD_INBAND") ? 0 : kSmWideHudRows);
  GpuPpu_SetMode7UnderHud(SmWide_Mode7());
  // HUD sprites (the escape timer) moved with the HUD in the reference too (the HUD's own
  // rows are not compared when it moves).
  g_gpu_ppu_obj_hud = g_rtl_oam_shown_hud;
  GpuPpu_SetHudX(hud_x);
  if (!GpuPpu_BuildFrame(g_snes->ppu, &g_cap, &g_frame, &why)) {
    g_gpu_ppu_obj_x = g_gpu_ppu_obj_y = NULL;
    GpuPpu_SetLayerShiftX(1, 0);
    GpuPpu_SetNoSpriteWrap(false);
    GpuPpu_SetNarrowBg3Rows(0);
    GpuPpu_SetMode7UnderHud(false);
    g_gpu_ppu_obj_hud = NULL;
    GpuPpu_SetHudX(0);
    g_wide_bad++;
    printf("%s: normal build with full X refused (%s)\n", label, why);
    return;
  }
  GpuPpu_SetHudX(0);
  memset(g_n, 0, sizeof(g_n));
  GpuRef_DrawFrame(&g_frame, g_n, kPitch);
  // Alarm: rows below the HUD where the recorded positions changed what the SNES shows.
  // Legitimate when an enemy far off-screen would wrap into view; a lost Samus is not.
  int tagged = 0;   // (with a BG2 shift the reference differs anyway: not counted)
  for (int y = 32; y < kGpuRows && !bg2_dx; y++) tagged += memcmp(&g_n[y * kPitch], &g_c[y * kPitch], 256 * 4) != 0;
  if (tagged) {
    g_wide_tagdiff++;
    if (getenv("WIDE_TAGDIFF")) printf("%s: full positions change %d rows below the HUD\n", label, tagged);
  }
  GpuPpu_SetMargins(ml, mr);
  GpuPpu_SetHudX(hud_x);
  GpuPpu_SetHudY(hud_y);
  GpuPpu_SetExtraRows(et, eb);
  GpuPpu_SetNarrowBg3Rows(kSmWideHudRows);
  GpuPpu_SetNarrowBg3Map(kSmWideMessageBoxMap);
  GpuPpu_SetWindow2Extent(SmWide_Window2Extent());
  int cone_window;
  const int16_t (*cone)[2] = SmWide_WindowCone(&cone_window);
  GpuPpu_SetWindowCone(cone_window, getenv("WIDE_NO_CONE") ? NULL : cone);
  if (getenv("WIDE_CONE_CHECK") && cone) {   // the cone's columns against its window's registers
    const int16_t (*c)[2] = cone;
    int lines = 0, off = 0, worst = 0;
    for (int l = 1; l <= kGpuRows; l++) {
      const PpuLineState *st = &g_cap.line[l];
      const int wl = cone_window == 1 ? st->window1left : st->window2left;
      const int wr = cone_window == 1 ? st->window1right : st->window2right;
      const bool reg = wl <= wr, geo = c[l][0] != kGpuWinNone && c[l][1] > 0 && c[l][0] < 256;
      int d = 0;
      if (reg != geo) d = 99;
      else if (reg) {
        if (wl > 0) d = abs(c[l][0] - wl);
        if (wr < 255) d = d > abs(c[l][1] - 1 - wr) ? d : abs(c[l][1] - 1 - wr);
      }
      lines++, off += d > 1, worst = d > worst ? d : worst;
      if (d > 1 && getenv("WIDE_CONE_CHECK")[0] == '2')
        printf("  line %d: regs %d..%d (w2 %d..%d) cone %d..%d\n", l, st->window1left, st->window1right, st->window2left,
               st->window2right, c[l][0], c[l][1] - 1);
    }
    printf("%s: cone vs window %d: %d of %d lines off by more than 1 px (worst %d)\n", label, cone_window, off, lines, worst);
  }
  g_gpu_ppu_obj_x = getenv("WIDE_NO_FULLX") ? NULL : g_rtl_oam_shown_x;   // NO_FULLX: the 9-bit X (old bug)
  g_gpu_ppu_obj_y = getenv("WIDE_NO_FULLX") ? NULL : g_rtl_oam_shown_y;
  const bool built = GpuPpu_BuildFrame(g_snes->ppu, &g_cap, &g_frame, &why);
  GpuPpu_SetWindow2Extent(NULL);
  GpuPpu_SetWindowCone(0, NULL);
  GpuPpu_SetMode7UnderHud(false);
  g_gpu_ppu_obj_hud = NULL;
  GpuPpu_SetMargin(0);
  GpuPpu_SetHudX(0);
  GpuPpu_SetHudY(0);
  GpuPpu_SetLayerShiftX(1, 0);
  GpuPpu_SetExtraRows(0, 0);
  GpuPpu_SetNarrowBg3Rows(0);
  GpuPpu_SetNarrowBg3Map(-1);
  g_gpu_ppu_obj_x = g_gpu_ppu_obj_y = NULL;
  GpuPpu_SetNoSpriteWrap(false);
  if (built) SmWide_AddMasks(&g_frame, &g_cap);
  if (!built) {
    g_wide_bad++;
    printf("%s: WIDE build refused (%s)\n", label, why);
    return;
  }
  g_wide_frames++;
  if (getenv("WIDE_BG2")) {   // BG2 scroll per line against the camera
    printf("  l1 %d l2 %d scroll2 %02x bg2dx %d | BG2 h:", (int16)layer1_x_pos, (int16)layer2_x_pos, layer2_scroll_x, bg2_dx);
    for (int l = 10; l <= 220; l += 15) printf(" %d", g_cap.line[l].bgLayer[1].hScroll);
    printf(" | BG3 h:");
    for (int l = 40; l <= 220; l += 60) printf(" %d", g_cap.line[l].bgLayer[2].hScroll);
    printf(" | TM %02x TS %02x\n", g_cap.line[100].screenEnabled[0], g_cap.line[100].screenEnabled[1]);
  }
  if (getenv("WIDE_ANCHOR_CHECK")) {   // recorded positions far from the 9-bit/8-bit reading
    // Inside the view, a recorded position must be where the SNES shows the piece; one more
    // than 64 px off means a wrong anchor (a piece 256 px away from where it belongs).
    for (int i = 0; i < 128; i++) {
      if (g_rtl_oam_shown_x[i] == kRtlOamUnknown || g_rtl_oam_shown_y[i] >= kRtlOamHiddenY) continue;
      const uint16_t *o = &g_snes->ppu->oam[i * 2];
      int x = (o[0] & 0xff) | ((g_snes->ppu->highOam[i >> 2] >> ((i & 3) * 2)) & 1) << 8, y = o[0] >> 8;
      if (x >= 256 + 64) x -= 512;
      if (y >= 224 + 16) y -= 256;
      if (x < 32 || x > 224 || y < 48 || y > 192) continue;   // well inside: no ambiguity
      if (abs(g_rtl_oam_shown_x[i] - x) > 64 || abs(g_rtl_oam_shown_y[i] - y) > 64)
        printf("%s: OAM %d at %d,%d recorded as %d,%d\n", label, i, x, y, g_rtl_oam_shown_x[i], g_rtl_oam_shown_y[i]);
    }
  }
  if (getenv("WIDE_OAM")) {   // 9-bit OAM X against the recorded full X, first 24 entries (h: HUD)
    printf("  oam:");
    for (int i = 0; i < 24; i++) {
      const uint16_t *o = &g_snes->ppu->oam[i * 2];
      const int raw = (o[0] & 0xff) | ((g_snes->ppu->highOam[i >> 2] >> ((i & 3) * 2)) & 1) << 8;
      printf(" %d:%d/%d,y%d/%d%s", i, raw, g_rtl_oam_shown_x[i], o[0] >> 8, g_rtl_oam_shown_y[i], g_rtl_oam_shown_hud[i] ? "h" : "");
    }
    printf("\n");
  }
  if (getenv("WIDE_BANDS")) {   // the masks, the bands of the wide frame and their main-screen quads
    printf("  masks:");
    for (int i = 0; i < g_frame.mask_count; i++)
      printf(" [%d,%d %dx%d]", g_frame.mask[i].x, g_frame.mask[i].y, g_frame.mask[i].w, g_frame.mask[i].h);
    printf("\n");
    for (int i = 0; i < g_frame.band_count; i++) {
      const GpuBand *bd = &g_frame.bands[i];
      printf("  band %d rows %d..%d TM %02x quads:", i, bd->y0, bd->y1, g_cap.line[(bd->y0 < 0 ? 0 : bd->y0) + 1].screenEnabled[0]);
      for (int q = bd->main_first; q < bd->main_first + bd->main_count && q < bd->main_first + 12; q++)
        printf(" [x%d w%d y%d h%d L%d]", g_frame.quads[q].x, g_frame.quads[q].w, g_frame.quads[q].y, g_frame.quads[q].h,
               g_frame.quads[q].level);
      printf("\n");
    }
  }
  memset(g_w, 0, sizeof(g_w));
  GpuRef_DrawFrameColumns(&g_frame, g_w, pitch, -ml, 256 + mr);
  int n = 0;
  // With uneven margins the HUD is drawn moved (it keeps its place on the screen): compare
  // below its rows then.
  // (A message box is moved like the HUD: not compared then.)
  // Moved down, it covers rows up to 30 + hud_y. The HUD's rows also show the FX layer and
  // colour math of the rows below (gpu_ppu.c, SynthHudLine), which the SNES never drew there:
  // compared from under the HUD.
  for (int y = 31 + (hud_y > 0 ? hud_y : 0); y < (hud_x && gameplay_BG3SC == 0x58 ? 0 : kGpuRows); y++)
    n += memcmp(&g_w[(y + ey) * pitch + ml * 4], &g_n[y * kPitch], 256 * 4) != 0;
  if (n) {
    g_wide_bad++;
    printf("%s: WIDE middle differs from the normal frame on %d rows\n", label, n);
    for (int y = 0; y < kGpuRows && getenv("WIDE_DIFFROWS"); y++) {
      int c0 = -1, c1 = -1;
      for (int x = 0; x < 256; x++)
        if (memcmp(&g_w[(y + ey) * pitch + (ml + x) * 4], &g_n[y * kPitch + x * 4], 4)) { if (c0 < 0) c0 = x; c1 = x; }
      if (c0 >= 0) printf("  row %d columns %d..%d\n", y, c0, c1);
    }
  }
  // Without the room filled in (door transitions, fades) the margins must be black, apart
  // from the HUD's own columns on its rows and its sprites (it may sit in a margin when the
  // view leans):
  // their tilemap columns hold stale blocks (issue: garbage beside the HUD in a door).
  g_wide_filled += SmWide_Filled();
  for (int i = 0; i < rows * pitch; i++) g_wide_hash = (g_wide_hash ^ g_w[i]) * 1099511628211ull;
  if (!SmWide_Filled() && !SmWide_Mode7()) {   // (mode 7: the plane is the whole room)
    int dirty = 0;
    for (int y = 0; y < rows; y++) {
      const bool hud_row = y - ey - hud_y < 31 && y - ey - hud_y >= 0;
      for (int x = 0; x < w; x++) {
        const int vx = x - ml;
        if (vx >= 0 && vx < 256 && y >= ey && y - ey < kGpuRows) continue;   // the game's view
        if (hud_row && vx >= hud_x && vx < hud_x + 256) continue;
        bool hud_obj = false;   // a HUD sprite (the escape timer) keeps its place, in a margin too
        for (int q = g_frame.hud_first; q < g_frame.hud_first + g_frame.hud_count && !hud_obj; q++) {
          const GpuQuad *qd = &g_frame.quads[q];
          hud_obj = (qd->flags & kGpuQuadObj) && vx >= qd->x && vx < qd->x + qd->w && y - ey >= qd->y &&
                    y - ey < qd->y + qd->h;
        }
        if (hud_obj) continue;
        const uint8_t *p = &g_w[y * pitch + x * 4];
        if (p[0] | p[1] | p[2]) { dirty++; break; }
      }
    }
    if (dirty) {
      g_wide_bad++;
      printf("%s: WIDE margins not black on %d rows while the room is not filled in\n", label, dirty);
    }
  }
  if (getenv("WIDE_M7ROOMS") && SmWide_Mode7()) {   // which rooms drew the plane under WIDE
    static uint16_t seen[16];
    int i;
    for (i = 0; i < 16 && seen[i] && seen[i] != room_ptr; i++) {}
    if (i < 16 && !seen[i]) {
      seen[i] = room_ptr;
      char name[32];
      snprintf(name, sizeof(name), "m7-%04X.ppm", room_ptr);
      FILE *f = fopen(name, "wb");
      if (f) {
        fprintf(f, "P6\n%d %d\n255\n", w, rows);
        for (int k = 0; k < w * rows; k++) { const uint8_t rgb[3] = { g_w[k * 4 + 2], g_w[k * 4 + 1], g_w[k * 4] }; fwrite(rgb, 1, 3, f); }
        fclose(f);
      }
      printf("%s: mode 7 room under WIDE -> %s\n", label, name);
    }
  }
  const int dump = getenv("WIDE_DUMP") ? atoi(getenv("WIDE_DUMP")) : 0;
  // WIDE_DUMP_ROOM=hex: only frames in that room count; WIDE_DUMP_FROM=n: only from tested frame n on.
  if (g_wide_dumped < dump && (!getenv("WIDE_DUMP_FROM") || g_frames >= atoi(getenv("WIDE_DUMP_FROM"))) &&
      (!getenv("WIDE_DUMP_ROOM") || room_ptr == strtol(getenv("WIDE_DUMP_ROOM"), 0, 16))) {
    char name[64];
    snprintf(name, sizeof(name), "wide-%03d.ppm", g_wide_dumped++);
    FILE *f = fopen(name, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, rows);
    for (int y = 0; y < rows; y++)
      for (int x = 0; x < w; x++) {
        const uint8_t *p = &g_w[y * pitch + x * 4];
        const uint8_t rgb[3] = { p[2], p[1], p[0] };
        fwrite(rgb, 1, 3, f);
      }
    fclose(f);
    printf("%s -> %s\n", label, name);
  }
  // Last: the second build reuses the sprite atlas g_frame points into.
  if (getenv("WIDE_TOPCHECK")) {   // top rows (extra rows + HUD) with the 9-bit decode instead
    static uint8_t old[(256 + 2 * kGpuMaxMargin) * 4 * (kGpuRows + 2 * kGpuMaxExtraRows)];
    memset(g_w, 0, sizeof(g_w));
    GpuRef_DrawFrameColumns(&g_frame, g_w, pitch, -ml, 256 + mr);
    static GpuFrame f2;
    const char *why2;
    g_gpu_ppu_obj_x = g_gpu_ppu_obj_y = NULL;
    GpuPpu_SetMargins(ml, mr);
    GpuPpu_SetHudX(hud_x);
    GpuPpu_SetHudY(hud_y);
    GpuPpu_SetLayerShiftX(1, bg2_dx);
    GpuPpu_SetExtraRows(et, eb);
    GpuPpu_SetNarrowBg3Rows(kSmWideHudRows);
    GpuPpu_SetNarrowBg3Map(kSmWideMessageBoxMap);
    if (GpuPpu_BuildFrame(g_snes->ppu, &g_cap, &f2, &why2)) {
      SmWide_AddMasks(&f2, &g_cap);
      memset(old, 0, sizeof(old));
      GpuRef_DrawFrameColumns(&f2, old, pitch, -ml, 256 + mr);
      if (memcmp(old, g_w, (32 + ey) * pitch)) {
        static int shown;
        printf("%s: TOPCHECK top rows differ (9-bit vs full)%s\n", label, shown < 6 ? " -> dumped" : "");
        if (shown < 6) {
          char name[64];
          snprintf(name, sizeof(name), "top-%d.ppm", shown++);
          FILE *f = fopen(name, "wb");
          if (f) {
            fprintf(f, "P6\n%d %d\n255\n", w, 2 * (40 + ey));
            for (int y = 0; y < 2 * (40 + ey); y++)
              for (int x = 0; x < w; x++) {
                const uint8_t *p = y < 40 + ey ? &old[y * pitch + x * 4] : &g_w[(y - 40 - ey) * pitch + x * 4];
                const uint8_t rgb[3] = { p[2], p[1], p[0] };
                fwrite(rgb, 1, 3, f);
              }
            fclose(f);
          }
        }
      }
    }
    GpuPpu_SetMargin(0);
    GpuPpu_SetHudX(0);
    GpuPpu_SetHudY(0);
    GpuPpu_SetLayerShiftX(1, 0);
    GpuPpu_SetExtraRows(0, 0);
    GpuPpu_SetNarrowBg3Rows(0);
    GpuPpu_SetNarrowBg3Map(-1);
  }
}

// Runs one frame and checks it. `check_capture` also runs it a second time the
// normal way (from a save state) to compare the capture replay against it.
static void TestFrame(const char *label, bool check_capture) {
  if (check_capture) RtlSaveLoad(kSaveLoad_Save, 8);
  if (check_capture) {
    g_snes->disableRender = false;
    RtlRunFrame(g_input);
    memcpy(g_a, g_px, sizeof(g_a));
    RtlSaveLoad(kSaveLoad_Load, 8);
  }
  g_ppu_line_capture = &g_cap;
  g_snes->disableRender = false;
  RtlRunFrame(g_input);
  g_ppu_line_capture = NULL;
  if (getenv("WRAM_TRACE")) {   // WRAM after every tested frame, to find where two builds part
    static int n;
    char name[32];
    snprintf(name, sizeof(name), "wram-%04d.bin", n++);
    FILE *f = fopen(name, "wb");
    if (f) fwrite(g_ram, 1, 0x20000, f), fclose(f);
  }
  memset(g_px, 0, sizeof(g_px));
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  ppu_replayLines(g_snes->ppu, &g_cap, 1, g_cap.last_line);
  clock_gettime(CLOCK_MONOTONIC, &t1);
  g_replay_us += (t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3;
  memcpy(g_b, g_px, sizeof(g_b));
  g_frames++;
  // SHOTS=a-b: tested frames a..b (counted from 1) as shot-NNNN.ppm (CPU renderer, 256x224)
  // with VRAM as vram-NNNN.bin, e.g. to look at a message box (MSGBOX) and its font.
  int shot_a, shot_b;
  if (getenv("SHOTS") && sscanf(getenv("SHOTS"), "%d-%d", &shot_a, &shot_b) == 2 && g_frames >= shot_a &&
      g_frames <= shot_b) {
    char name[32];
    snprintf(name, sizeof(name), "shot-%04d.ppm", g_frames);
    FILE *f = fopen(name, "wb");
    if (f) {
      fprintf(f, "P6\n256 224\n255\n");
      for (int y = 0; y < 224; y++)
        for (int x = 0; x < 256; x++) {
          const uint8_t *q = &g_b[y * kPitch + x * 4];
          const uint8_t rgb[3] = { q[2], q[1], q[0] };
          fwrite(rgb, 1, 3, f);
        }
      fclose(f);
    }
    snprintf(name, sizeof(name), "vram-%04d.bin", g_frames);
    if ((f = fopen(name, "wb"))) fwrite(g_snes->ppu->vram, 2, 0x8000, f), fclose(f);
    snprintf(name, sizeof(name), "cgram-%04d.bin", g_frames);
    if ((f = fopen(name, "wb"))) fwrite(g_snes->ppu->cgram, 2, 256, f), fclose(f);
  }
  int x0, y0, x1, y1, n;
  if (check_capture && (n = Diff(g_a, g_b, &x0, &y0, &x1, &y1))) {
    g_capture_bad++;
    printf("%s: CAPTURE REPLAY differs from the normal render: %d px in %d,%d..%d,%d\n", label, n, x0, y0, x1, y1);
  }
  const char *why;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  const bool built = GpuPpu_BuildFrame(g_snes->ppu, &g_cap, &g_frame, &why);
  clock_gettime(CLOCK_MONOTONIC, &t1);
  const double us = (t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3;
  g_build_us += us;
  if (us > g_build_max_us) g_build_max_us = us;
  if (!built) {
    g_refused++;
    NoteRefusal(why);
    if (getenv("MODE7_SURVEY") && strstr(why, "mode")) {
      // One line per distinct Mode 7 setup: modes on the frame's lines, whether the
      // matrix changes between lines, field/fill/extbg, layers and colour math.
      static char seen7[128][160];
      int modes = 0, m7lines = 0, matrix_changes = 0, first = 0;
      for (int l = 1; l <= g_cap.last_line && l < kPpuCaptureLines; l++) {
        const PpuLineState *t = &g_cap.line[l];
        if (t->forcedBlank) continue;
        modes |= 1 << t->mode;
        if (t->mode != 7) continue;
        if (!first) first = l;
        if (m7lines && memcmp(t->m7matrix, g_cap.line[first].m7matrix, 8 * 2)) matrix_changes++;
        m7lines++;
      }
      if (first) {
        const PpuLineState *m = &g_cap.line[first];
        char key[160];
        snprintf(key, sizeof(key), "modes %02x m7 lines %d matrix varies on %d | large %d fill %d extbg %d flip %d%d | main %02x sub %02x math %d%d%d%d%d%d add %d",
                 modes, m7lines, matrix_changes ? 1 : 0, m->m7largeField, m->m7charFill, m->m7extBg, m->m7xFlip,
                 m->m7yFlip, m->screenEnabled[0], m->screenEnabled[1], m->mathEnabled[0], m->mathEnabled[1],
                 m->mathEnabled[2], m->mathEnabled[3], m->mathEnabled[4], m->mathEnabled[5], m->addSubscreen);
        int k;
        for (k = 0; k < 128 && seen7[k][0] && strcmp(seen7[k], key); k++) {}
        if (k < 128 && !seen7[k][0]) {
          strcpy(seen7[k], key);
          printf("%s: %s | m7 %d %d %d %d %d %d\n", label, key, m->m7matrix[0], m->m7matrix[1], m->m7matrix[2],
                 m->m7matrix[3], m->m7matrix[4], m->m7matrix[5]);
        }
      }
    }
    if (getenv("WINDOW_SURVEY") && strstr(why, "window")) {
      // One line per distinct window setup: which layers are windowed where, the
      // colour-window modes, and the window bounds on a sample of lines.
      static char seen[64][96];
      char key[96];
      const PpuLineState *m = &g_cap.line[112];
      snprintf(key, sizeof(key), "sel %08x mainW %02x subW %02x main %02x sub %02x clip %d prevent %d", m->windowsel,
               m->screenWindowed[0], m->screenWindowed[1], m->screenEnabled[0], m->screenEnabled[1], m->clipMode,
               m->preventMathMode);
      int k;
      for (k = 0; k < 64 && seen[k][0] && strcmp(seen[k], key); k++) {}
      if (k < 64 && !seen[k][0]) {
        strcpy(seen[k], key);
        printf("%s: %s: %s | w1 %d..%d w2 %d..%d at line 112\n", label, why, key, m->window1left, m->window1right,
               m->window2left, m->window2right);
      }
    }
    return;
  }
  const GpuPpuStats *st = GpuPpu_LastStats();
  g_tiles += st->tiles_decoded;
  g_composed += st->screen_rows_composed;
  g_composed_frames += st->screen_rows_composed > 0;
  if (getenv("SHOW_COMPOSE") && st->screen_rows_composed) printf("%s: composed %d rows\n", label, st->screen_rows_composed);
  g_quads += g_frame.quad_count;
  if (g_frame.quad_count > g_max_quads) g_max_quads = g_frame.quad_count;
  memset(g_c, 0, sizeof(g_c));
  GpuRef_DrawFrame(&g_frame, g_c, kPitch);
  if ((n = Diff(g_b, g_c, &x0, &y0, &x1, &y1))) {
    g_gpu_bad++;
    printf("%s: GPU differs: %d px in %d,%d..%d,%d (%d bands, %d quads)\n", label, n, x0, y0, x1, y1,
           g_frame.band_count, g_frame.quad_count);
    if (g_dumped < 20) {
      char name[64];
      snprintf(name, sizeof(name), "diff-%02d.ppm", g_dumped++);
      DumpTriptych(name, g_b, g_c);
      printf("  -> %s\n", name);
    }
  }
  if (getenv("FRAME_HASH")) {   // the normal 256 px frame, to compare runs (e.g. WIDE on/off)
    uint64_t h = 1469598103934665603ull;
    for (int i = 32 * kPitch; i < kGpuRows * kPitch; i++) h = (h ^ g_c[i]) * 1099511628211ull;   // below the HUD
    printf("FRAMEHASH %s %016llx\n", label, (unsigned long long)h);
  }
  if (getenv("WIDE")) TestWide(label);
}

static int g_music_rooms, g_music_stuck, g_music_wrong;

static void Report(void) {
  if (g_music_rooms)
    printf("MUSIC rooms %d, music queue stuck in %d, wrong music bank in %d\n", g_music_rooms, g_music_stuck,
           g_music_wrong);
  printf("RESULT frames %d, capture mismatches %d, GPU mismatches %d, refused %d\n", g_frames, g_capture_bad, g_gpu_bad,
         g_refused);
  if (getenv("WIDE"))
    printf("WIDE frames %d, bad %d, frames where full positions change the view below the HUD %d, room filled in %d\n",
           g_wide_frames, g_wide_bad, g_wide_tagdiff, g_wide_filled);
  if (getenv("WIDE")) printf("WIDE image hash %016llx\n", (unsigned long long)g_wide_hash);
  // Game state at the end, for tools/test: any change to the game logic changes it.
  uint64_t h = 1469598103934665603ull;
  for (int i = 0; i < 0x20000; i++) h = (h ^ g_ram[i]) * 1099511628211ull;
  printf("WRAM hash %016llx (game_state %02x room %04x)\n", (unsigned long long)h, (unsigned)game_state,
         (unsigned)room_ptr);
  const int drawn = g_frames - g_refused;
  if (drawn)
    printf("  per drawn frame: %.1f quads (max %ld), %.1f tiles decoded, %ld frames composed rows on the CPU (%.1f rows each)\n",
           (double)g_quads / drawn, g_max_quads, (double)g_tiles / drawn, g_composed_frames,
           g_composed_frames ? (double)g_composed / g_composed_frames : 0.0);
  if (g_frames)
    printf("  host time per frame: build %.0f us (max %.0f), CPU renderer %.0f us\n", g_build_us / g_frames,
           g_build_max_us, g_replay_us / g_frames);
  for (int i = 0; i < 16 && g_refuse_reasons[i][0]; i++) printf("  refused %d: %s\n", g_refuse_counts[i], g_refuse_reasons[i]);
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: gpu_ppu_test ROM state FILE.sav [FRAMES] | gpu_ppu_test ROM rooms [FRAMES] [ROOM]\n");
    return 1;
  }
  ParseConfigFile(NULL);
  Snes *snes = SnesInit(argv[1]);
  if (!snes) return 2;
  g_spc_player = SpcPlayer_Create();
  SpcPlayer_Initialize(g_spc_player);
  PpuBeginDrawing(snes->snes_ppu, g_px, kPitch, 0);
  // GAME_LANG=n: the game's message boxes in UI language n (ui_lang.h), as on the console.
  if (getenv("GAME_LANG")) {
    g_ui_lang = (UiLang)atoi(getenv("GAME_LANG"));
    GameText_Init();
  }
  // WIDE: the game side fills the margins' tilemap areas in every frame run from here on
  // (they are outside the normal view, so the normal checks are unaffected).
  if (getenv("WIDE"))
    SmWide_SetView(atoi(getenv("WIDE")), getenv("WIDE_Y") ? atoi(getenv("WIDE_Y")) : 0,
                   getenv("WIDE_Y") ? atoi(getenv("WIDE_Y")) : 0);
  if (!strcmp(argv[2], "state")) {
    // The state is copied to saves/save9.sav by run.sh.
    if (!RtlSaveLoad(kSaveLoad_Load, 9)) return 4;
    GpuPpu_Invalidate();
    const int frames = argc > 4 ? atoi(argv[4]) : 60;
    for (int i = 0; i < frames; i++) {
      // ROOM_SEQ=hex@frame,...: buttons from each frame on, as in the rooms mode.
      for (const char *q = getenv("ROOM_SEQ"); q && *q;) {
        int bits, at, used;
        if (sscanf(q, "%x@%d%n", &bits, &at, &used) != 2) break;
        if (i >= at) g_input = bits;
        q += used;
        if (*q == ',') q++;
      }
      if (getenv("AUTOFIRE") && (i & 8)) g_input &= ~0x200;
      char label[32];
      snprintf(label, sizeof(label), "frame %d", i);
      if (i % 50 == 0) printf("%s\n", label);
      if (getenv("TRACE_SAMUS") && i % 10 == 0)
        printf("  f%d in %03x: samus %d,%d camera %d,%d state %02x room %04x\n", i, g_input, samus_x_pos, samus_y_pos,
               layer1_x_pos, layer1_y_pos, (unsigned)game_state, (unsigned)room_ptr);
      TestFrame(label, i % 10 == 0);
    }
    Report();
    return g_capture_bad || g_gpu_bad || g_wide_bad;
  }
  if (!strcmp(argv[2], "pbomb")) {
    // A power bomb explodes where Samus stands; every frame is checked, and the normal
    // render is written as pb-NNN.ppm (CPU | GPU list | difference).
    if (!RtlSaveLoad(kSaveLoad_Load, 9)) return 4;
    GpuPpu_Invalidate();
    const int frames = argc > 4 ? atoi(argv[4]) : 240;
    for (int i = 0; i < frames; i++) {
      if (i == 2) {
        power_bomb_explosion_x_pos = samus_x_pos;
        power_bomb_explosion_y_pos = samus_y_pos;
        EnableHdmaObjects();
        SpawnPowerBombExplosion();
      }
      char label[32];
      snprintf(label, sizeof(label), "frame %d", i);
      TestFrame(label, true);
      char name[32];
      snprintf(name, sizeof(name), "pb-%03d.ppm", i);
      DumpTriptych(name, g_a, g_c);
    }
    Report();
    return g_capture_bad || g_gpu_bad || g_wide_bad;
  }
  // rooms: boot with scripted inputs until gameplay, then warp into every room.
  // boot: the same boot, every frame tested from power-on: title, file select (with
  // saves/sm.srm the first file is continued), loading the game and what follows.
  RtlReadSram();
  enum { A = 0x100, START = 0x08 };
  if (!strcmp(argv[2], "boot")) {
    const int frames = argc > 3 ? atoi(argv[3]) : 1500;
    int soft_resets = 0;
    for (int i = 0; i < frames; i++) {
      char label[48];
      snprintf(label, sizeof(label), "boot frame %d (state %02x room %04x)", i, (unsigned)game_state, (unsigned)room_ptr);
      if (i % 100 == 0) printf("%s\n", label);
      g_input = (i % 60 < 6) ? (i % 120 < 60 ? START : A) : 0;
      if (game_state == 8) {
        // BOOT_INPUT=hex[,hex2@frames]: buttons held in gameplay; the second set after that
        // many gameplay frames (e.g. walk right, then left).
        static int played;
        g_input = 0;
        const char *bi = getenv("BOOT_INPUT");
        if (bi) {
          int a = 0, b = 0, at = 0;
          sscanf(bi, "%x,%x@%d", &a, &b, &at);
          g_input = (at && played >= at) ? b : a;
        }
        played++;
      }
      // CERES_BOOM: once in the Ceres elevator room (DF45), jump to "made it to the
      // elevator" so the escape cutscene (Ceres explodes) plays.
      if (getenv("CERES_BOOM") && game_state == 8 && room_ptr == 0xDF45) {
        printf("frame %d: game_state 8 -> 0x20 in DF45\n", i);
        game_state = 0x20;
      }
      // MASH_B=N: from the file-select map on, B on every other frame for N frames.
      static int mash_left = -1;
      // MASH_B_STATE: the game state to start at (default 5, the file-select map)
      const int mash_state = getenv("MASH_B_STATE") ? (int)strtol(getenv("MASH_B_STATE"), 0, 16) : 5;
      if (getenv("MASH_B") && mash_left < 0 && game_state == mash_state) mash_left = atoi(getenv("MASH_B"));
      if (mash_left > 0) {
        mash_left--;
        g_input = (i & 1) ? 0x01 : 0;
        if (i % 20 == 0) printf("mash: frame %d state %02x coroutine %04x\n", i, (unsigned)game_state, (unsigned)coroutine_state_0);
      }
      TestFrame(label, i % 10 == 0);
      soft_resets += game_state == 0xffff;
    }
    printf("soft resets seen: %d\n", soft_resets);
    Report();
    return g_capture_bad || g_gpu_bad || g_wide_bad;
  }
  for (int i = 0; i < 20000; i++) {
    g_snes->disableRender = true;
    RtlRunFrame((i % 60 < 6) ? (i % 120 < 60 ? START : A) : 0);
    if (game_state == 8 && i > 100) break;
  }
  const int frames = argc > 3 ? atoi(argv[3]) : 20;
  const int only = argc > 4 ? (int)strtol(argv[4], 0, 16) : 0;
  int n;
  const SmRoom *rooms = SmMap_Rooms(&n);
  SmWarp_Init();
  RtlSaveLoad(kSaveLoad_Save, 7);
  for (int r = 0; r < n; r++) {
    if (only && rooms[r].header != only) continue;
    if (SmWarp_DoorCount(&rooms[r]) == 0) continue;
    if (!getenv("MUSIC_CHAIN")) {   // MUSIC_CHAIN: warp from wherever the last warp left
      RtlSaveLoad(kSaveLoad_Load, 7);
      GpuPpu_Invalidate();
    }
    for (int k = 0; k < 5; k++) { g_snes->disableRender = true; RtlRunFrame(0); }
    // ROOM_DOOR=n: arrive through door n instead of 0.
    const int door = getenv("ROOM_DOOR") ? atoi(getenv("ROOM_DOOR")) : 0;
    if (SmWarp_ToRoom(&rooms[r], door) != kWarp_Ok) continue;
    // WARP_AT=camera_x,camera_y,samus_x,samus_y: arrive there instead (e.g. a console
    // screen dump's position).
    if (getenv("WARP_AT")) {
      int cx, cy, sx, sy;
      if (sscanf(getenv("WARP_AT"), "%d,%d,%d,%d", &cx, &cy, &sx, &sy) == 4) {
        g_rtl_warp_load.warp_screen_x = (uint16)cx, g_rtl_warp_load.warp_screen_y = (uint16)cy;
        g_rtl_warp_load.warp_samus_x = (uint16)sx, g_rtl_warp_load.warp_samus_y = (uint16)sy;
      }
    }
    for (int k = 0; k < 400 && !(game_state == 8 && room_ptr == rooms[r].header); k++) {
      samus_health = 99;
      g_snes->disableRender = true;
      RtlRunFrame(0);
      SmWarp_AfterFrame();
    }
    if (getenv("MUSIC_CHECK")) {
      // The music queue after a warp: pending delays with read == write and no timer is
      // the state that hangs the next door (DoorTransition_WaitForMusicToClear).
      int stuck_at = -1;
      const int settle = getenv("MUSIC_SETTLE") ? atoi(getenv("MUSIC_SETTLE")) : 600;
      for (int k = 0; k < settle && stuck_at < 0; k++) {
        samus_health = 99;
        g_snes->disableRender = true;
        RtlRunFrame(0);
        SmWarp_AfterFrame();
        if (getenv("MUSIC_TRACE"))
          printf("  %04X f%d state %02x: r%d w%d timer %d entry %04x | %d:%d %d:%d %d:%d %d:%d %d:%d %d:%d %d:%d %d:%d\n",
                 rooms[r].header, k, (unsigned)game_state, music_queue_read_pos, music_queue_write_pos, music_timer,
                 music_entry, music_queue_track[0], music_queue_delay[0], music_queue_track[1], music_queue_delay[1],
                 music_queue_track[2], music_queue_delay[2], music_queue_track[3], music_queue_delay[3],
                 music_queue_track[4], music_queue_delay[4], music_queue_track[5], music_queue_delay[5],
                 music_queue_track[6], music_queue_delay[6], music_queue_track[7], music_queue_delay[7]);
        if (HasQueuedMusic() && music_queue_read_pos == music_queue_write_pos && music_timer == 0) stuck_at = k;
      }
      g_music_rooms++;
      // After settling, the area's music bank must be the one uploaded.
      if (settle >= 600 && room_music_data_index && music_data_index != room_music_data_index) {
        g_music_wrong++;
        printf("room %04X: music bank %02x, room wants %02x\n", rooms[r].header, music_data_index, room_music_data_index);
      }
      if (stuck_at >= 0) {
        g_music_stuck++;
        printf("room %04X: music queue stuck %d frames after arrival (delays %d %d %d %d %d %d %d %d, pos %d)\n",
               rooms[r].header, stuck_at, music_queue_delay[0], music_queue_delay[1], music_queue_delay[2],
               music_queue_delay[3], music_queue_delay[4], music_queue_delay[5], music_queue_delay[6],
               music_queue_delay[7], music_queue_read_pos);
      }
      continue;
    }
    // FIREFLEA_DARK=n: only fireflea rooms (fx type 0x24), darkness forced to level n.
    if (getenv("FIREFLEA_DARK") && fx_type != 0x24) continue;
    if (getenv("FIREFLEA_DARK")) printf("fireflea room %04X\n", rooms[r].header);
    // ROOM_INPUT=hex: buttons held in the tested frames (frontend bits: right 0x80, left
    // 0x40, B jump 0x01, Y run 0x02), e.g. to scroll while WIDE is checked.
    g_input = getenv("ROOM_INPUT") ? (int)strtol(getenv("ROOM_INPUT"), 0, 16) : 0;
    for (int k = 0; k < frames; k++) {
      samus_health = getenv("SAMUS_HEALTH") ? (uint16)atoi(getenv("SAMUS_HEALTH")) : 99;
      // EARTHQUAKE=type: the room shakes (HandleRoomShaking) for every tested frame.
      if (getenv("EARTHQUAKE")) earthquake_type = (uint16)atoi(getenv("EARTHQUAKE")), earthquake_timer = 30;
      // CERES_ESCAPE: the escape is on (ceres_status bit 15): the elevator shaft (DF45) tilts,
      // and the escape timer starts (ProcessTimer_CeresStart).
      if (getenv("CERES_ESCAPE")) {
        ceres_status |= 0x8000;
        if (!timer_status) timer_status = 0x8001, frame_handler_gamma = (uint16)fnSamus_Func3;
      }
      // MSGBOX=n: queue message box n (as an item pickup does) on frame 5.
      if (getenv("MSGBOX") && k == 5) queued_message_box_index = (uint16)atoi(getenv("MSGBOX"));
      // SAMUS_AT=x,y: put Samus there on frame 1 (a console dump's place; the camera follows).
      int sx, sy;
      if (getenv("SAMUS_AT") && k == 1 && sscanf(getenv("SAMUS_AT"), "%d,%d", &sx, &sy) == 2)
        samus_x_pos = samus_prev_x_pos = (uint16)sx, samus_y_pos = samus_prev_y_pos = (uint16)sy;
      // ITEMS=hex: these items collected and equipped from frame 1 (4 = morph ball: the eyes).
      if (getenv("ITEMS") && k == 1) {
        const int it = (int)strtol(getenv("ITEMS"), 0, 16);
        collected_items |= it, equipped_items |= it;
      }
      // XRAY=1: X-ray scope collected, equipped and selected from frame 1 (hold Y, 0x02, to use it).
      if (getenv("XRAY") && k == 1) {
        collected_items |= 0x8000, equipped_items |= 0x8000;
        hud_item_index = 5;
      }
      // SCROLLS_OPEN=1: every scroll screen blue on frame 1, so the camera can follow her there.
      if (getenv("SCROLLS_OPEN") && k == 1)
        for (int i = 0; i < room_width_in_scrolls * room_height_in_scrolls && i < 50; i++) scrolls[i] = 1;
      if (getenv("FIREFLEA_DARK")) fireflea_darkness_level = (uint16)atoi(getenv("FIREFLEA_DARK"));
      // ROOM_INPUT2=hex@frame: other buttons from that frame on (e.g. come back through a door).
      if (getenv("ROOM_INPUT2")) {
        int bits = 0, at = 0;
        if (sscanf(getenv("ROOM_INPUT2"), "%x@%d", &bits, &at) == 2 && k >= at) g_input = bits;
      }
      // ROOM_SEQ=hex@frame,hex@frame,...: buttons from each frame on (a scripted route).
      if (getenv("ROOM_SEQ")) {
        const char *q = getenv("ROOM_SEQ");
        int bits, at, used;
        while (sscanf(q, "%x@%d%n", &bits, &at, &used) == 2) {
          if (k >= at) g_input = bits;
          q += used;
          if (*q == ',') q++;
        }
      }
      if (getenv("TRACE_SAMUS") && k % 10 == 0)
        printf("  k%d in %03x: samus %d,%d pose %02x camera %d,%d state %02x room %04x\n", k, g_input, samus_x_pos,
             samus_y_pos, (unsigned)samus_pose, layer1_x_pos, layer1_y_pos, (unsigned)game_state, (unsigned)room_ptr);
      // AUTOFIRE: the shot button (0x200) released every other 8 frames, so held fire keeps
      // shooting (a door that closed behind Samus needs a new shot).
      if (getenv("AUTOFIRE") && (k & 8)) g_input &= ~0x200;
      if (getenv("PBOMB") && k == 2) {   // a power bomb where Samus stands
        power_bomb_explosion_x_pos = samus_x_pos;
        power_bomb_explosion_y_pos = samus_y_pos;
        EnableHdmaObjects();
        SpawnPowerBombExplosion();
      }
      char label[64];
      snprintf(label, sizeof(label), "room %04X frame %d (state %02x)", rooms[r].header, k, (unsigned)game_state);
      TestFrame(label, k == 0);
      SmWarp_AfterFrame();
    }
    if (getenv("BLOCKS_AT")) {   // BLOCKS_AT=x,y,w,h: block type/BTS of a block rectangle
      int bx, by, bw, bh;
      if (sscanf(getenv("BLOCKS_AT"), "%d,%d,%d,%d", &bx, &by, &bw, &bh) == 4)
        for (int y = by; y < by + bh; y++) {
          printf("  %d:", y);
          for (int x = bx; x < bx + bw; x++)
            printf(" %x/%02x", level_data[y * room_width_in_blocks + x] >> 12, BTS[y * room_width_in_blocks + x]);
          printf("\n");
        }
    }
    if (getenv("LAYER2_INFO"))
      printf("room %04X layer2 scroll x %02x y %02x BG2SC %02x\n", rooms[r].header, layer2_scroll_x, layer2_scroll_y,
             reg_BG2SC);
    if (getenv("EXT_ENEMIES")) {   // enemies drawn with extended spritemaps (bosses), by room
      for (int i = 0; i < 32; i++) {
        EnemyData *E = gEnemyData(i * 64);
        if (E->enemy_ptr && (E->extra_properties & 4)) {
          printf("room %04X has extended-spritemap enemies\n", rooms[r].header);
          break;
        }
      }
    }
    if (getenv("WIDE_INFO")) {   // room size, camera and scroll colours (0 red, 1 blue, 2 green)
      printf("room %04X: %dx%d screens, camera %d,%d, scrolls:", rooms[r].header, room_width_in_scrolls,
             room_height_in_scrolls, layer1_x_pos, layer1_y_pos);
      for (int i = 0; i < room_width_in_scrolls * room_height_in_scrolls && i < 64; i++)
        printf("%s%d", i % room_width_in_scrolls ? "" : " ", scrolls[i]);
      printf("\n  distinct level words per screen (BG1 / BTS-ignored):");
      for (int sy = 0; sy < room_height_in_scrolls; sy++) {
        printf(" |");
        for (int sx = 0; sx < room_width_in_scrolls; sx++) {
          uint16_t seen[256];
          int n = 0;
          for (int by = sy * 16; by < sy * 16 + 16; by++)
            for (int bx = sx * 16; bx < sx * 16 + 16; bx++) {
              const uint16_t v = level_data[by * room_width_in_blocks + bx];
              int k;
              for (k = 0; k < n && seen[k] != v; k++) {}
              if (k == n && n < 256) seen[n++] = v;
            }
          printf(" %d", n);
        }
      }
      printf("\n");
    }
  }
  Report();
  return g_capture_bad || g_gpu_bad || g_wide_bad;
}
