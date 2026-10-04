#include "sm_planes.h"

#include <stdint.h>

#include "src/types.h"
#include "src/variables.h"
#include "stereo_depth.h"

typedef struct {
  uint16_t room;
  uint8_t layer, prio, plane;
} LayerRule;

// source/sm_plane_fixes.inc: SM_LAYER_PLANE is a rule; SM_PLANE_FIX (one block) is for the renderer's tile
// split, not wired yet.
#define SM_PLANE_FIX(room, layer, bx, by, block, plane)
#define SM_LAYER_PLANE(room, layer, prio, plane) { room, layer, prio, plane },
static const LayerRule kRules[] = {
#include "sm_plane_fixes.inc"
  { 0, 0, 0, 0 }   // never matches (room 0 is not a room), and keeps the array non-empty
};
#undef SM_PLANE_FIX
#undef SM_LAYER_PLANE

int SmPlanes_RuleCount(void) { return (int)(sizeof(kRules) / sizeof(kRules[0])) - 1; }

int SmPlanes_LayerRule(int layer, int prio) {
  const uint16_t room = room_ptr;
  for (int i = 0; i < SmPlanes_RuleCount(); i++)
    if (kRules[i].room == room && kRules[i].layer == layer && kRules[i].prio == prio) return kRules[i].plane;
  return -1;
}

int SmPlanes_RoomHasRules(void) {
  const uint16_t room = room_ptr;
  for (int i = 0; i < SmPlanes_RuleCount(); i++)
    if (kRules[i].room == room) return 1;
  return 0;
}
