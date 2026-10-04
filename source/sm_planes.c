#include "sm_planes.h"

#include <stdint.h>
#include <string.h>

#include "src/types.h"
#include "src/variables.h"
#include "stereo_depth.h"

typedef struct {
  uint16_t room;
  uint8_t layer, prio, plane;
} LayerRule;

typedef struct {
  uint16_t room, bx, by, block;
  uint8_t layer, corners, plane;
} TileFix;

// source/sm_plane_fixes.inc: SM_LAYER_PLANE is a rule for a whole layer, SM_PLANE_FIX one block's tiles (corners).
#define SM_PLANE_FIX(room, layer, bx, by, block, corners, plane)
#define SM_LAYER_PLANE(room, layer, prio, plane) { room, layer, prio, plane },
static const LayerRule kRules[] = {
#include "sm_plane_fixes.inc"
  { 0, 0, 0, 0 }   // never matches (room 0 is not a room), and keeps the array non-empty
};
#undef SM_PLANE_FIX
#undef SM_LAYER_PLANE

#define SM_PLANE_FIX(room, layer, bx, by, block, corners, plane) { room, bx, by, block, layer, corners, plane },
#define SM_LAYER_PLANE(room, layer, prio, plane)
static const TileFix kFixes[] = {
#include "sm_plane_fixes.inc"
  { 0, 0, 0, 0, 0, 0, 0 }
};
#undef SM_PLANE_FIX
#undef SM_LAYER_PLANE
enum { kFixCount = (int)(sizeof(kFixes) / sizeof(kFixes[0])) - 1 };

int SmPlanes_RuleCount(void) { return (int)(sizeof(kRules) / sizeof(kRules[0])) - 1; }

int SmPlanes_LayerRule(int layer, int prio) {
  const uint16_t room = room_ptr;
  for (int i = 0; i < SmPlanes_RuleCount(); i++)
    if (kRules[i].room == room && kRules[i].layer == layer && kRules[i].prio == prio) return kRules[i].plane;
  return -1;
}

// The fixes of the room the game is in, found again whenever the room changes.
static uint16_t g_fix_room;
static int g_fix_n;
static const TileFix *g_fix[kFixCount + 1];

static void FindRoomFixes(void) {
  g_fix_room = room_ptr;
  g_fix_n = 0;
  for (int i = 0; i < kFixCount; i++)
    if (kFixes[i].room == g_fix_room) g_fix[g_fix_n++] = &kFixes[i];
}

int SmPlanes_RoomHasRules(void) {
  const uint16_t room = room_ptr;
  for (int i = 0; i < SmPlanes_RuleCount(); i++)
    if (kRules[i].room == room) return 1;
  for (int i = 0; i < kFixCount; i++)
    if (kFixes[i].room == room) return 1;
  return 0;
}

int SmPlanes_SlotPlanes(int layer, int tw, int th, uint8_t *grid) {
  if (g_fix_room != room_ptr) FindRoomFixes();
  if (!g_fix_n || (layer != 1 && layer != 2)) return 0;
  const int w = room_width_in_blocks, h = room_height_in_blocks;
  // Level data: BG1's blocks from $7F:0002; BG2's from $7F:9602 only when it scrolls with the level.
  const uint16_t *level;
  int cam_x, cam_y;
  if (layer == 1) {
    level = (const uint16_t *)(g_ram + 0x10002), cam_x = layer1_x_pos, cam_y = layer1_y_pos;
  } else {
    if (layer2_scroll_x & 1) return 0;
    level = (const uint16_t *)(g_ram + 0x19602), cam_x = layer2_x_pos, cam_y = layer2_y_pos;
  }
  const int wb = tw >> 1, hb = th >> 1;   // blocks the tilemap holds
  // The window of blocks the tilemap shows: centred on the camera's middle (256x224 view).
  const int x0 = ((cam_x + 128) >> 4) - (wb >> 1), y0 = ((cam_y + 112) >> 4) - (hb >> 1);
  memset(grid, 0, (size_t)th * 64);
  int n = 0;
  for (int i = 0; i < g_fix_n; i++) {
    const TileFix *f = g_fix[i];
    if (f->layer != layer || f->bx < x0 || f->bx >= x0 + wb || f->by < y0 || f->by >= y0 + hb) continue;
    if (f->bx >= w || f->by >= h || level[f->by * w + f->bx] != f->block) continue;
    for (int c = 0; c < 4; c++) {
      if (!(f->corners >> c & 1)) continue;
      grid[((f->by * 2 + (c >> 1)) & (th - 1)) * 64 + ((f->bx * 2 + (c & 1)) & (tw - 1))] = (uint8_t)(f->plane + 1);
      n++;
    }
  }
  return n;
}
