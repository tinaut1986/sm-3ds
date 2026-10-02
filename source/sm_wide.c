#include "sm_wide.h"

#include <stdint.h>

#include "src/types.h"
#include "src/variables.h"
#include "src/ida_types.h"
#include "src/sm_cpu_infra.h"
#include "src/sm_rtl.h"
#include "src/funcs.h"
#include "src/snes/ppu.h"
#include "src/snes/snes.h"

static int g_margin_x, g_extra_top, g_extra_bottom;
static int g_left, g_right;   // this frame's margins: 2 * g_margin_x in all, leaning off a room edge
static int g_top, g_bottom;   // this frame's extra rows, the same way: g_extra_top + g_extra_bottom in all
static int g_lean_from, g_lean_from_y;   // the leans (-g_left, -g_top) when a door transition started
static bool g_in_door, g_door_scrolling;
static int g_door_last_count;
static int g_bg2_dx;          // BG2 shift that keeps the parallax with the leaned view
static bool g_filled;
static bool g_mode7;   // the last frame showed a mode 7 room (Ceres): the plane fills it all
static int g_room[4];        // the room in screen pixels, as drawn (screen shake included)
static int g_room_still[4];  // the same without the shake: what the lean follows

// Whether the frame shows the room as it is in the level data. In a door transition only
// while the old room fades out (its level data still loaded, the camera where it was) and
// while the new one fades in (loaded, the camera at its destination); in between the
// level data already belongs to the next room while the old one is on screen. Masking the
// margins during the fades made them go black and come back at once, beside a room that
// fades.
static bool RoomShown(void) {
  switch (game_state) {
  case kGameState_7_MainGameplayFadeIn: case kGameState_8_MainGameplay: case kGameState_12_Pausing:
  case kGameState_18_Unpausing: case kGameState_27_ReserveTanksAuto: case kGameState_42_PlayingDemo:
  case kGameState_32_MadeItToCeresElevator: case kGameState_33_BlackoutFromCeres:   // Ceres escape's end
  case kGameState_9_HitDoorBlock: case kGameState_10_LoadingNextRoom:
    return true;
  case kGameState_11_LoadingNextRoom:
    switch (door_transition_function | 0x820000) {
    case fnDoorTransitionFunction_WaitForSoundsToFinish: case fnDoorTransitionFunction_FadeOutScreen:
    case fnDoorTransition_FadeInScreenAndFinish:
      return true;
    }
    return false;
  default:
    return false;
  }
}

static inline int FloorDiv16(int v) { return v >> 4; }   // arithmetic shift: floor

static inline void VramPut(Ppu *ppu, uint16_t adr, uint16_t v) {
  adr &= 0x7fff;
  if (ppu->vram[adr] == v) return;
  ppu->vram[adr] = v;
  if (g_ppu_vram_dirty) g_ppu_vram_dirty[adr >> 3] = 1;
}

// One block's four tilemap entries, as UpdateLevelOrBackgroundDataColumn (sm_80.c)
// derives them from a level data word: tile table entry, bit 10 flips X, bit 11 flips Y.
static void PutBlock(Ppu *ppu, uint16_t base, int vx, int vy, uint16_t block) {
  const TileTable *t = &tile_table.tables[block & 0x3ff];
  uint16_t tl = t->top_left, tr = t->top_right, bl = t->bottom_left, br = t->bottom_right;
  switch (block & 0xc00) {
  case 0x400: {   // X flip: left and right swap
    const uint16_t a = tl, b = bl;
    tl = tr ^ 0x4000, tr = a ^ 0x4000, bl = br ^ 0x4000, br = b ^ 0x4000;
    break;
  }
  case 0x800: {   // Y flip: top and bottom swap
    const uint16_t a = tl, b = tr;
    tl = bl ^ 0x8000, tr = br ^ 0x8000, bl = a ^ 0x8000, br = b ^ 0x8000;
    break;
  }
  case 0xc00: {   // both: diagonal swap
    const uint16_t a = tl, b = tr;
    tl = br ^ 0xc000, tr = bl ^ 0xc000, bl = b ^ 0xc000, br = a ^ 0xc000;
    break;
  }
  }
  // 64x32 tilemap: two 32x32 screens; a block is 2x2 tiles.
  const uint16_t adr = base + (vy & 15) * 64 + (vx & 15) * 2 + ((vx & 16) ? 0x400 : 0);
  VramPut(ppu, adr, tl);
  VramPut(ppu, adr + 1, tr);
  VramPut(ppu, adr + 32, bl);
  VramPut(ppu, adr + 33, br);
}

// Fills one BG layer: `data` is its level data (room_width_in_blocks per row), `sc` its
// BGnSC register, `hofs`/`vofs` its scroll and `lx`/`ly` the level position the game
// scrolls it by (layerN_x_pos / y_pos).
// `all_columns`: write the game's own columns too (the view is not where the game keeps
// them).
// `hofs`/`vofs` are the scroll the PPU draws with; `base_h`/`base_v` the one the game maps
// level blocks to tilemap blocks by, before any screen shake (HandleRoomShaking and the
// room shakes add to the BG scroll registers after the game has placed its columns: mapping
// by the shaken scroll put blocks one column off whenever the shake crossed a block edge,
// and they showed once the camera brought those columns into view; Ceres escape).
static void FillLayer(Ppu *ppu, const uint16 *data, uint8 sc, uint16 hofs, uint16 vofs, uint16 base_h,
                      uint16 base_v, uint16 lx, uint16 ly, bool all_columns) {
  if (!(sc & 1)) return;   // not a 64-column tilemap: not the layout the game streams
  const uint16_t base = (uint16_t)((sc & 0xfc) << 8);
  const int w = room_width_in_blocks, h = room_height_in_blocks;
  const int vx0 = base_h >> 4, vy0 = base_v >> 4;
  const int lx0 = FloorDiv16((int16)lx), ly0 = FloorDiv16((int16)ly);
  // Block columns k (relative to the game's first) that screen x in [-left, 256 + right)
  // shows; rows j for output rows [-top, 224 + bottom), output row r showing BG row
  // vofs + r + 1.
  const int fx = (base_h & 15) + (int16)(hofs - base_h), fy = (base_v & 15) + (int16)(vofs - base_v);
  const int k0 = FloorDiv16(fx - g_left), k1 = FloorDiv16(fx + 255 + g_right);
  const int j0 = FloorDiv16(fy + 1 - g_top), j1 = FloorDiv16(fy + 224 + g_bottom);
  for (int k = k0; k <= k1; k++) {
    const int bx = lx0 + k;
    if (bx < 0 || bx >= w) continue;
    const bool game_column = !all_columns && k >= 0 && k <= 16;
    for (int j = j0; j <= j1; j++) {
      // The game keeps rows 1..14 of its 17 columns (row 15 too, but the view's extra
      // rows below can need that tilemap row for row -1).
      if (game_column && j >= 1 && j <= 14) continue;
      const int by = ly0 + j;
      if (by < 0 || by >= h) continue;
      PutBlock(ppu, base, vx0 + k, vy0 + j, data[by * w + bx]);
    }
  }
}

// ---- HUD over the room --------------------------------------------------------------
// The HUD's empty cells are tilemap entry 0x2C0F: char 0x0F in colour 3 of palette 3,
// opaque black. While the room shows under the HUD they become a fully transparent BG3
// char instead (found in VRAM), and go back to 0x2C0F afterwards. The HUD tilemap is BG3SC
// 0x5A (VRAM word 0x5800); its 32 lines are tile rows 0-3.
enum { kHudMap = 0x5800, kHudCells = 4 * 32, kHudBlank = 0x2C0F };
static int g_clear_char = -1;      // a BG3 char whose 8 words are zero
static uint16_t g_clear_entry;      // 0x2C00 | that char, written in place of kHudBlank

static uint16_t Bg3Chars(void) { return (uint16_t)((reg_BG34NBA & 0x0f) << 12); }

static bool CharClear(const Ppu *ppu, int c) {
  const uint16_t base = Bg3Chars() + c * 8;
  for (int i = 0; i < 8; i++)
    if (ppu->vram[(base + i) & 0x7fff]) return false;
  return true;
}

static void HudRestore(Ppu *ppu) {
  if (!g_clear_entry) return;
  for (int i = 0; i < kHudCells; i++)
    if (ppu->vram[kHudMap + i] == g_clear_entry) VramPut(ppu, kHudMap + i, kHudBlank);
  g_clear_entry = 0;
}

static void HudSeeThrough(Ppu *ppu) {
  if (g_clear_char < 0 || !CharClear(ppu, g_clear_char)) {
    g_clear_char = -1;
    // From the top down (the HUD uses the low chars), but only chars below the BG1
    // tilemap (VRAM 0x5000): higher "chars" are tilemap words that change as the game
    // scrolls, this file's own fills included.
    for (int c = (0x5000 - Bg3Chars()) / 8 - 1; c > 0 && g_clear_char < 0; c--)
      if (CharClear(ppu, c)) g_clear_char = c;
  }
  if (g_clear_char < 0) return;
  const uint16_t entry = (uint16_t)(0x2C00 | g_clear_char);
  if (g_clear_entry && g_clear_entry != entry) HudRestore(ppu);
  g_clear_entry = entry;
  for (int i = 0; i < kHudCells; i++)
    if (ppu->vram[kHudMap + i] == kHudBlank) VramPut(ppu, kHudMap + i, entry);
}

// Where the margins go: evenly, unless that shows beyond a room edge while the other side
// has room to spare. Then the view leans away from the edge, which ends up on the screen's
// border, as if the camera stopped there (the game's camera is left alone: door
// transitions rely on it). Narrower rooms are centred. The extra rows above and below
// (PIXEL PERFECT) lean the same way (issue #7); the HUD keeps its place on the screen.
// Returns the first column (row) shown, from -total (all of it before the view) to 0 (all
// after it), for a room whose edges are at screen columns (rows) `lo_edge` and `hi_edge`,
// a view `size` wide and `before` of the `total` evenly.
static int LeanFor(int lo_edge, int hi_edge, int before, int total, int size) {
  const int lo = lo_edge, hi = hi_edge - (size + total);
  int v0 = lo <= hi ? (-before < lo ? lo : -before > hi ? hi : -before) : (lo + hi) / 2;
  if (v0 > 0) v0 = 0;
  if (v0 < -total) v0 = -total;
  return v0;
}

static int LeanX(int lo_edge, int hi_edge) { return LeanFor(lo_edge, hi_edge, g_margin_x, 2 * g_margin_x, 256); }

static int LeanY(int lo_edge, int hi_edge) {
  return LeanFor(lo_edge, hi_edge, g_extra_top, g_extra_top + g_extra_bottom, 224);
}

static void SetLean(int x0) {
  g_left = -x0;
  g_right = 2 * g_margin_x - g_left;
}

static void SetLeanY(int y0) {
  g_top = -y0;
  g_bottom = g_extra_top + g_extra_bottom - g_top;
}

// What the next frame's game logic treats as on screen: the margins and rows just chosen.
static void PublishView(void) {
  g_rtl_wide_margin_left = (uint16)g_left;
  g_rtl_wide_margin_right = (uint16)g_right;
  g_rtl_wide_extra_top = (uint16)g_top;
  g_rtl_wide_extra_bottom = (uint16)g_bottom;
}

// During a door transition the lean moves from the old room's to the new room's along with
// the scrolling (64 frames of 4 px across, 57 down or up), the new one taken at the
// destination camera: the door then travels to where the new room shows it, rather than
// jumping there when the transition ends. Before the scrolling starts the old lean stays:
// the counter still holds the previous transition's end (64) until FixDoorsMovingUp /
// DoorTransitionFunction_SetupScrolling set it to 0, and the room size and destination may
// still be the old ones. That reset is seen as the counter going down (or 0): the first
// scrolling step runs in the same frame, so the hook may never see the 0 itself.
static void LeanDoor(void) {
  if (!g_in_door) {
    g_in_door = true;
    g_door_scrolling = false;
    g_door_last_count = door_transition_frame_counter;
    g_lean_from = -g_left;
    g_lean_from_y = -g_top;
  }
  const bool across = !(door_direction & 2);
  const int frames = across ? 64 : 57, n = door_transition_frame_counter;
  if (n == 0 || n < g_door_last_count) g_door_scrolling = true;
  g_door_last_count = n;
  if (!g_door_scrolling || n <= 0) {
    SetLean(g_lean_from);
    SetLeanY(g_lean_from_y);
    return;
  }
  // Across, the camera ends at door_destination_x_pos; up or down it keeps its x. Its y
  // ends at door_destination_y_pos either way (output row r shows level row y + r + 1).
  const int cam = across ? (int16)door_destination_x_pos : (int16)layer1_x_pos;
  const int cam_y = (int16)door_destination_y_pos;
  const int to = LeanX(-cam, room_width_in_blocks * 16 - cam);
  const int to_y = LeanY(-cam_y - 1, room_height_in_blocks * 16 - cam_y - 1);
  const int k = n > frames ? frames : n;
  SetLean(g_lean_from + (to - g_lean_from) * k / frames);
  SetLeanY(g_lean_from_y + (to_y - g_lean_from_y) * k / frames);
}

// BG2's X as the game derives it from layer 1 (CalculateLayer2Xpos). The scrolling-sky
// rooms (Landing Site, the ocean) instead scroll BG2 by bands with HDMA, drifting with time
// and never with the camera's X (HdmaobjPreInstr_SkyLandBG2XscrollInner): fixed.
static int Layer2X(int l1) {
  switch (room_main_code_ptr) {
  case (uint16)fnRoomCode_ScrollingSkyLand_: case (uint16)fnRoomCode_ScrollingSkyOcean_:
  case (uint16)fnRoomCode_ScrollingSkyLand_Shakes:
    return 0;
  }
  if (!layer2_scroll_x) return l1;
  if (layer2_scroll_x == 1) return (int16)layer2_x_pos;   // fixed
  const int t = layer2_scroll_x & 0xfe;
  return t * ((l1 >> 8) & 0xff) + ((t * (l1 & 0xff)) >> 8);
}

// The leaned view stands for a camera (l1 + m - left) that the game does not have: BG1 and
// sprites follow it by construction, BG2 must be moved to where its parallax would put it.
static void Bg2Shift(void) {
  const int l1 = (uint16)layer1_x_pos, d = g_margin_x - g_left;
  g_bg2_dx = d ? Layer2X(l1 + d) - Layer2X(l1) - d : 0;
}

// The explosion's window per captured line y (screen line y - 1): table entry k is the
// distance from the centre line, the lines below it one entry behind (the indirect HDMA
// table, found by matching the captured WH2/WH3 against the tables), and the half-width the
// game computed before cutting it to the screen (g_rtl_pb_half_width).
static int16_t g_win2[kPpuCaptureLines][2];
static bool g_win2_on;

static void ExplosionExtent(void) {
  g_win2_on = (power_bomb_explosion_status & 0x8000) != 0;
  if (!g_win2_on) return;
  const int cx = (int16)(power_bomb_explosion_x_pos - layer1_x_pos);
  const int cy = (int16)(power_bomb_explosion_y_pos - layer1_y_pos);
  for (int y = 0; y < kPpuCaptureLines; y++) {
    const int d = y - 1 - cy, k = d <= 0 ? -d : d - 1;
    if (y == 0 || k > 255) {
      g_win2[y][0] = kGpuWinNone;
      continue;
    }
    const int w = g_rtl_pb_half_width[k];
    g_win2[y][0] = (int16_t)(cx - w);
    g_win2[y][1] = (int16_t)(cx + w + 1);
  }
}

const int16_t (*SmWide_Window2Extent(void))[2] { return g_win2_on ? (const int16_t (*)[2])g_win2 : NULL; }

static void BeforePpuDraw(void) {
  g_filled = false;
  ExplosionExtent();
  // A screen shake moves the room on the screen for a frame or two; the lean must not
  // follow it, or the whole view jitters against the sprites (Ceres escape).
  const uint16 base_h = bg1_x_offset + layer1_x_pos, base_v = bg1_y_offset + layer1_y_pos;
  const int shake_x = (int16)(reg_BG1HOFS - base_h), shake_y = (int16)(reg_BG1VOFS - base_v);
  const int fx = base_h & 15, fy = base_v & 15;
  const int lx0 = FloorDiv16((int16)layer1_x_pos), ly0 = FloorDiv16((int16)layer1_y_pos);
  // Screen column c shows level x lx0*16 + fx + shake_x + c; output row r shows
  // ly0*16 + fy + shake_y + r + 1.
  g_room_still[0] = -(lx0 * 16 + fx);
  g_room_still[1] = -(ly0 * 16 + fy + 1);
  g_room_still[2] = g_room_still[0] + room_width_in_blocks * 16;
  g_room_still[3] = g_room_still[1] + room_height_in_blocks * 16;
  for (int i = 0; i < 4; i++) g_room[i] = g_room_still[i] - (i & 1 ? shake_y : shake_x);
  Ppu *ppu = g_snes->ppu;
  const bool door = game_state == kGameState_9_HitDoorBlock || game_state == kGameState_10_LoadingNextRoom ||
                    game_state == kGameState_11_LoadingNextRoom;
  const bool shown = RoomShown() && !irq_enable_mode7;
  if (door && !shown) LeanDoor();
  // A mode 7 room: the plane holds the whole room (outside it, transparent), so nothing is
  // filled or masked; only the HUD's blank cells let it show under the HUD.
  g_mode7 = RoomShown() && irq_enable_mode7;
  if (g_mode7) {
    g_in_door = false;
    SetLean(-g_margin_x);
    SetLeanY(-g_extra_top);
    g_bg2_dx = 0;
    PublishView();
    if (g_rtl_wide_hud_over_room) HudSeeThrough(ppu);
    return;
  }
  if (!shown) {
    HudRestore(ppu);   // margins masked: a door transition's rooms disagree
    if (door) Bg2Shift();
    PublishView();
    return;
  }
  g_in_door = false;
  SetLean(LeanX(g_room_still[0], g_room_still[2]));
  SetLeanY(LeanY(g_room_still[1], g_room_still[3]));
  Bg2Shift();
  PublishView();
  FillLayer(ppu, level_data, reg_BG1SC, reg_BG1HOFS, reg_BG1VOFS, bg1_x_offset + layer1_x_pos,
            bg1_y_offset + layer1_y_pos, layer1_x_pos, layer1_y_pos, false);
  // BG2 from the level's background data, when the game streams it like BG1 (otherwise it
  // is a fixed background already loaded whole), moved with the parallax shift: then the
  // game's columns are not where the view is, so every column is written.
  // Not when an enemy draws its body in BG2 here (Spore Spawn): the fill wrote the room's
  // background over the boss's tiles in the rows and columns it keeps up to date.
  if (!(layer2_scroll_x & 1) && !(layer2_scroll_y & 1) && g_rtl_enemy_bg2_room != room_ptr)
    FillLayer(ppu, custom_background, reg_BG2SC, reg_BG2HOFS + g_bg2_dx, reg_BG2VOFS,
              bg2_x_scroll + layer2_x_pos + g_bg2_dx, bg2_y_scroll + layer2_y_pos, layer2_x_pos + g_bg2_dx,
              layer2_y_pos, g_bg2_dx != 0);
  if (g_rtl_wide_hud_over_room) HudSeeThrough(ppu);
  g_filled = true;
}

void SmWide_SetView(int margin_x, int extra_top, int extra_bottom) {
  if (margin_x != g_margin_x) {   // even until the next gameplay frame works out the lean
    g_left = g_right = margin_x;
    g_bg2_dx = 0;
    g_in_door = false;
    g_rtl_wide_margin_left = g_rtl_wide_margin_right = (uint16)margin_x;
  }
  if (extra_top != g_extra_top || extra_bottom != g_extra_bottom) {
    g_top = extra_top;
    g_bottom = extra_bottom;
    g_in_door = false;
    g_rtl_wide_extra_top = (uint16)extra_top;
    g_rtl_wide_extra_bottom = (uint16)extra_bottom;
  }
  g_margin_x = margin_x;
  g_extra_top = extra_top;
  g_extra_bottom = extra_bottom;
  g_rtl_wide_hud_over_room = margin_x || extra_top || extra_bottom;
  const bool on = margin_x || extra_top || extra_bottom;
  g_rtl_before_ppu_draw = on ? BeforePpuDraw : NULL;
  if (!on) {
    g_filled = false;
    HudRestore(g_snes->ppu);
  }
}

void SmWide_Rows(int *top, int *bottom, int *hud_y) {
  *top = g_top;
  *bottom = g_bottom;
  // The HUD keeps its place on the screen: centred in the frame's rows, like the 224.
  *hud_y = (g_bottom - g_top) / 2;
}

bool SmWide_Filled(void) { return g_filled; }

bool SmWide_Mode7(void) { return g_mode7; }

void SmWide_Margins(int *left, int *right, int *hud_x, int *bg2_dx) {
  *left = g_left;
  *right = g_right;
  *bg2_dx = g_bg2_dx;
  // The HUD keeps its place on the screen: centred in the frame.
  *hud_x = (g_right - g_left) / 2;
}

static inline int FloorDiv256(int v) { return v >> 8; }

// Whether screen (sx, sy) of the room is one block repeated: unfinished filler (CF80 has
// a screen of "X" blocks behind a door), or plain sky or rock.
static bool ScreenUniform(int sx, int sy) {
  const int w = room_width_in_blocks;
  const uint16 first = level_data[sy * 16 * w + sx * 16];
  for (int by = sy * 16; by < sy * 16 + 16; by++)
    for (int bx = sx * 16; bx < sx * 16 + 16; bx++)
      if (level_data[by * w + bx] != first) return false;
  return true;
}

// Whether the margins may show screen (sx, sy) of the room: inside it, and either its
// scroll is not red (scrolls[], 0 = red) or it is real room. Red screens are often rooms
// the camera may not enter yet (a shaft's side, 92FD) and showing them is the point;
// masking every red screen made those appear only once the game turned them blue.
static bool ScreenShown(int sx, int sy) {
  if (sx < 0 || sy < 0 || sx >= room_width_in_scrolls || sy >= room_height_in_scrolls) return false;
  return scrolls[sy * room_width_in_scrolls + sx] != 0 || !ScreenUniform(sx, sy);
}

void SmWide_AddMasks(GpuFrame *f) {
  if (g_mode7) return;
  // What the frame shows beyond the game's own 256x224 view: the side margins (full
  // height) and the extra rows above and below it.
  const int regions[4][4] = {
    { f->x0, f->y0, 0, f->y1 }, { 256, f->y0, f->x1, f->y1 },
    { 0, f->y0, 256, 0 }, { 0, kGpuRows, 256, f->y1 },
  };
  for (int k = 0; k < 4; k++) {
    const int c0 = regions[k][0], r0 = regions[k][1], c1 = regions[k][2], r1 = regions[k][3];
    if (c0 >= c1 || r0 >= r1) continue;
    if (!g_filled) {
      // All of it: the margins' tilemap columns hold whatever the last fill left (a door
      // transition showed stale BG1 blocks beside the HUD). The HUD is drawn over the masks
      // (GpuFrame.hud_first), so it shows even where it sits in a margin.
      GpuPpu_AddMask(f, c0, r0, c1 - c0, r1 - r0);
      continue;
    }
    // The parts outside the room or in a red scroll screen made of one block (filler).
    // Screen column c shows level x c - g_room[0]; row r level y r - g_room[1].
    for (int sx = FloorDiv256(c0 - g_room[0]); sx <= FloorDiv256(c1 - 1 - g_room[0]); sx++) {
      for (int sy = FloorDiv256(r0 - g_room[1]); sy <= FloorDiv256(r1 - 1 - g_room[1]); sy++) {
        if (ScreenShown(sx, sy)) continue;
        int x0 = g_room[0] + sx * 256, x1 = x0 + 256, y0 = g_room[1] + sy * 256, y1 = y0 + 256;
        if (x0 < c0) x0 = c0;
        if (x1 > c1) x1 = c1;
        if (y0 < r0) y0 = r0;
        if (y1 > r1) y1 = r1;
        GpuPpu_AddMask(f, x0, y0, x1 - x0, y1 - y0);
      }
    }
  }
}

void SmWide_RoomRect(int *x0, int *y0, int *x1, int *y1) {
  *x0 = g_room[0], *y0 = g_room[1], *x1 = g_room[2], *y1 = g_room[3];
}
