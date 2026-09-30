#include "sm_warp.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/types.h"
#include "src/sm_rtl.h"
#include "src/funcs.h"
#include "src/variables.h"

typedef struct {
  uint16_t door_def;   // pointer of the door definition, bank $83
  uint16_t dest;       // header of the room it leads into
} DoorEntry;

enum { kMaxDoors = 1400 };

static DoorEntry g_doors[kMaxDoors];
static int g_door_count;
static bool g_indexed;

static uint16_t Word(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static bool IsRoom(uint16_t header) {
  int n;
  const SmRoom *rooms = SmMap_Rooms(&n);
  for (int i = 0; i < n; i++)
    if (rooms[i].header == header) return true;
  return false;
}

void SmWarp_Init(void) {
  if (g_indexed) return;
  g_indexed = true;
  g_door_count = 0;
  int n;
  const SmRoom *rooms = SmMap_Rooms(&n);
  for (int i = 0; i < n; i++) {
    // The door list is an array of u16 pointers to door definitions; it has no
    // length, so stop at the first entry that is not a door into a known room.
    for (int d = 0; d < 64; d++) {
      const uint16_t def = Word(RomPtr(0x8F0000 | (uint16_t)(rooms[i].door_list + 2 * d)));
      if (def < 0x8000) break;
      const uint16_t dest = Word(RomPtr(0x830000 | def));   // first field: destination room
      if (!IsRoom(dest)) break;
      // The same door definition can be listed by several rooms; keep one per pair.
      bool dup = false;
      for (int k = 0; k < g_door_count && !dup; k++) dup = g_doors[k].door_def == def;
      if (!dup && g_door_count < kMaxDoors) g_doors[g_door_count++] = (DoorEntry){ def, dest };
    }
  }
}

int SmWarp_DoorCount(const SmRoom *room) {
  SmWarp_Init();
  int n = 0;
  for (int i = 0; i < g_door_count; i++) n += g_doors[i].dest == room->header;
  return n;
}

bool SmWarp_Ready(void) {
  // Only between frames of normal gameplay. The game dispatcher remembers a state
  // function that is still running in coroutine_state_0; injecting a new state while
  // one is in progress would resume the wrong function.
  return game_state == 0x08 && coroutine_state_0 == 0;
}

// ---- Arrival check ----------------------------------------------------------
//
// Samus is loaded at a position that is merely inside the room, because the level data
// (needed to find a floor) does not exist until the room has loaded. Once the game is back
// in normal gameplay, stand her on the nearest floor in front of the door, and never leave
// her outside the room or inside a wall: the next frame's collision code would index the
// level data out of range (CalculateBlockAt's 0xFFFF sentinel), a data abort on the console.

static int g_pending_room = -1;   // header of the room we are warping into, or -1
#ifdef SM_WARP_DEBUG
static uint16_t g_def;   // the door definition of the warp in progress
#endif
static int g_target_x, g_target_y;   // where Samus should stand, pixels; -1 = unknown
static bool g_fixed_target;
static bool g_target_vertical;
static bool g_face_left;         // came in through a door on the right wall   // door in a ceiling or floor: she arrives in the air
static int g_pending_frames;
static bool g_left_gameplay;      // saw the transition start (game_state != 8)
static int g_fixups;

#define VX 32

typedef enum { kBlockAir = 0, kBlockSpecialAir = 3 /* water, sand... */, kBlockShootableAir = 4, kBlockSolid = 8 } BlockType;

// Blocks Samus can stand in without taking damage or being stuck.
static bool IsPassable(int type) { return type == kBlockAir || type == kBlockSpecialAir || type == kBlockShootableAir; }

// Blocks that hold her up: solid, slopes (door tunnels use them as flat floors),
// crumble, shot, grapple and bomb blocks. Spike blocks (0xA) and doors (9) do not count.
static bool IsFloor(int type) {
  return type == kBlockSolid || type == 1 || type == 0xB || type == 0xC || type == 0xE || type == 0xF;
}

static int BlockTypeAt(int bx, int by) {
  if (bx < 0 || by < 0 || bx >= room_width_in_blocks || by >= room_height_in_blocks) return kBlockSolid;
  return (level_data[by * room_width_in_blocks + bx] >> 12) & 0xF;
}

static bool SamusPositionOk(void) {
  const int w = room_width_in_blocks * 16, h = room_height_in_blocks * 16;
  if ((uint16_t)samus_x_pos >= w || (uint16_t)samus_y_pos >= h) return false;
  return BlockTypeAt(samus_x_pos >> 4, samus_y_pos >> 4) != kBlockSolid;
}

// Whether Samus (three blocks tall, centred on the point) fits there without touching
// anything solid.
static bool FitsInAir(int x_px, int y_px) {
  const int bx = x_px / 16, by = y_px / 16;
  if (x_px < 0 || y_px < 0) return false;
  return IsPassable(BlockTypeAt(bx, by - 1)) && IsPassable(BlockTypeAt(bx, by)) && IsPassable(BlockTypeAt(bx, by + 1));
}

// Nearest spot where Samus fits standing (three air blocks over a solid one), as a
// position for her centre. Falls back to the clamped current position.
static void RelocateSamus(int target_x_px, int target_y_px) {
  const int W = room_width_in_blocks, H = room_height_in_blocks;
  int tx = target_x_px / 16, ty = target_y_px / 16;
  if (tx < 0) tx = 0;
  if (tx >= W) tx = W - 1;
  if (ty < 0) ty = 0;
  if (ty >= H) ty = H - 1;
  int best_x = -1, best_y = -1, best_d = 1 << 30;
  for (int by = 3; by < H - 1; by++) {
    for (int bx = 0; bx < W; bx++) {
      if (!IsFloor(BlockTypeAt(bx, by + 1))) continue;
      if (!IsPassable(BlockTypeAt(bx, by)) || !IsPassable(BlockTypeAt(bx, by - 1)) || !IsPassable(BlockTypeAt(bx, by - 2)))
        continue;
      const int d = (bx - tx) * (bx - tx) + (by - ty) * (by - ty);
      if (d < best_d) { best_d = d; best_x = bx; best_y = by; }
    }
  }
  if (best_x < 0) { best_x = tx; best_y = ty; }   // no floor found: at least stay inside the room
  samus_x_pos = (uint16_t)(best_x * 16 + 8);
  samus_y_pos = (uint16_t)(best_y * 16 - 24 + 16);   // feet on the top of the block below
  samus_prev_x_pos = samus_x_pos;
  samus_prev_y_pos = samus_y_pos;
  samus_x_subpos = samus_y_subpos = 0;
  samus_y_speed = samus_y_subspeed = 0;
}

#ifdef SM_WARP_DEBUG
static int g_trace_frames;
int g_warp_trace_plm;
void __attribute__((noinline)) SmWarp_DebugArrived(void) { asm volatile(""); }   // gdb breakpoint target
#endif

void SmWarp_AfterFrame(void) {
#ifdef SM_WARP_DEBUG
  if (g_trace_frames > 0 && getenv("WARP_FOLLOW")) {
    printf("    follow %d: state %02x samus %u,%u layer1 %u,%u\n", 40 - g_trace_frames, (unsigned)game_state, samus_x_pos, samus_y_pos,
           (unsigned)layer1_x_pos, (unsigned)layer1_y_pos);
    g_trace_frames--;
  }
#endif
  if (g_pending_room < 0) return;
  if (++g_pending_frames > 900) { g_pending_room = -1; return; }   // never arrived: give up
  if (game_state != 0x08) { g_left_gameplay = true; return; }
  if (!g_left_gameplay) return;   // still in the frame that started the warp

  // The room was loaded like a saved game, which leaves Samus in the "just loaded" state:
  // standing facing the screen while Samus_Func16 plays the load fanfare for about six
  // seconds with the controls locked. End that state now, the way PlaySamusFanfare does when
  // it finishes: normal gameplay handlers, a standing pose facing into the room, and the
  // room's music (the fanfare would have queued it).
  frame_handler_alfa = FUNC16(Samus_FrameHandlerAlfa_Func11);
  frame_handler_beta = FUNC16(Samus_FrameHandlerBeta_Func17);
  substate = 0;
  samus_pose = g_face_left ? 2 /* kPose_02_FaceL_Normal */ : 1 /* kPose_01_FaceR_Normal */;
  samus_pose_x_dir = g_face_left ? 4 : 8;
  samus_prev_pose = samus_last_different_pose = samus_pose;
  samus_prev_pose_x_dir = samus_last_different_pose_x_dir = samus_pose_x_dir;
  PlayRoomMusicTrackAfterAFrames(16);
  const uint16_t arrive_x = samus_x_pos, arrive_y = samus_y_pos;
  (void)arrive_x; (void)arrive_y;   // only printed by the host test
  const bool ok = SamusPositionOk();
#ifdef SM_WARP_DEBUG
  const bool skip_fix = getenv("WARP_NOFIX") != NULL;   // host test: prove the fix is what prevents the crash
#else
  const bool skip_fix = false;
#endif
  if (!skip_fix) {
    // Put Samus just inside the door she arrives through: two blocks in front of its cap,
    // which is a position in this (destination) room, so she is clear of the door frame.
    if (g_fixed_target) {
      if (g_target_vertical && FitsInAir(g_target_x, g_target_y)) {
        // Through a floor or ceiling door Samus drops in or pops out; no ground needed.
        samus_x_pos = (uint16_t)g_target_x;
        samus_y_pos = (uint16_t)g_target_y;
        samus_prev_x_pos = samus_x_pos;
        samus_prev_y_pos = samus_y_pos;
        samus_x_subpos = samus_y_subpos = 0;
        samus_y_speed = samus_y_subspeed = 0;
      } else {
        RelocateSamus(g_target_x, g_target_y);
      }
      g_fixups++;
    } else if (!ok) {
      RelocateSamus((int16_t)samus_x_pos, (int16_t)samus_y_pos);
      g_fixups++;
    }
  }
#ifdef SM_WARP_DEBUG
  printf("  arrival: room %04x size %dx%d blocks, game put samus at %u,%u (%s) target %d,%d -> %u,%u\n", room_ptr,
         room_width_in_blocks, room_height_in_blocks, arrive_x, arrive_y, ok ? "ok" : "BAD", g_target_x, g_target_y,
         samus_x_pos, samus_y_pos);
  if (getenv("WARP_PLMS")) {
    const uint8_t *dd = RomPtr(0x830000 | g_def);
    printf("    door def %04x: dest %04x flags %02x orient %02x cap %d,%d  screen %d,%d  dist %04x\n", g_def, dd[0] | dd[1] << 8, dd[2], dd[3],
           dd[4], dd[5], dd[6], dd[7], dd[8] | dd[9] << 8);
    for (int i = 0; i < 40; i++) {
      const uint16_t hp = *(uint16_t *)(g_ram + 0x1C37 + 2 * i);
      if (!hp) continue;
      const uint16_t bi = *(uint16_t *)(g_ram + 0x1C87 + 2 * i) / 2;
      printf("    plm[%2d] header %04X at %d,%d\n", i, hp, bi % room_width_in_blocks, bi / room_width_in_blocks);
    }
  }
  if (getenv("WARP_AROUND")) {
    const int bx = g_target_x / 16, by = g_target_y / 16;
    for (int y = by - 6; y <= by + 4; y++) {
      printf("    %3d ", y);
      for (int x = bx - 10; x <= bx + 10; x++) printf(x == bx && y == by ? "[%X]" : " %X ", BlockTypeAt(x, y) & 0xF);
      printf("\n");
    }
  }
#endif
  g_pending_room = -1;
#ifdef SM_WARP_DEBUG
  g_trace_frames = 40;
  SmWarp_DebugArrived();
#endif
}

int SmWarp_FixupCount(void) { return g_fixups; }

bool SmWarp_LastTarget(int *x, int *y, bool *vertical) {
  if (x) *x = g_target_x;
  if (y) *y = g_target_y;
  if (vertical) *vertical = g_target_vertical;
  return g_fixed_target;
}

SmWarpResult SmWarp_ToRoom(const SmRoom *room, int which) {
  SmWarp_Init();
  const int count = SmWarp_DoorCount(room);
  if (count == 0) return kWarp_NoDoor;
  if (!SmWarp_Ready()) return kWarp_NotReady;

  int pick = ((which % count) + count) % count;
  uint16_t def = 0;
  for (int i = 0; i < g_door_count; i++) {
    if (g_doors[i].dest != room->header) continue;
    if (pick-- == 0) { def = g_doors[i].door_def; break; }
  }

  // DoorDef: +3 orientation, +4/+5 cap position in blocks (in the destination room),
  // +6/+7 the screen the camera goes to.
  const uint8_t *d = RomPtr(0x830000 | def);
  const int dir = d[3] & 3, cx = d[4], cy = d[5];
  const uint8_t *hdr = RomPtr(0x8F0000 | room->header);   // RoomDefHeader: +4/+5 size in screens
  const int room_w = hdr[4] * 256, room_h = hdr[5] * 256;

  // Where to stand: two blocks in front of the door, at its base. Door definitions whose
  // cap is (0,0) are scripted transitions with no real door; leave those to the generic
  // validity check.
  g_fixed_target = (cx != 0 || cy != 0);
  g_target_vertical = (dir & 2) != 0;
  g_face_left = dir == 1;
  switch (dir) {
  case 0: g_target_x = cx * 16 + 32;  g_target_y = cy * 16 + 40; break;   // came in going right: door on the left wall
  case 1: g_target_x = cx * 16 - 16;  g_target_y = cy * 16 + 40; break;   // going left: door on the right wall
  case 2: g_target_x = cx * 16 + 32;  g_target_y = cy * 16 + 48; break;   // going down: door in the ceiling
  default: g_target_x = cx * 16 + 32; g_target_y = cy * 16 - 24; break;   // going up: door in the floor
  }

  // Load the room the way "Continue" does (game_state 6), not with a door transition. A door
  // transition assumes Samus walked out of the room that leads here: it keeps the low byte of
  // her old position, continues the old room's scroll registers and redraws door caps, which
  // left a second, stale copy of the door on screen and Samus shut inside it. A full load
  // rebuilds everything for the new room from scratch. The initial position only has to be
  // inside the room; SmWarp_AfterFrame puts her on a floor once the level data exists.
  const int start_x = g_fixed_target ? g_target_x : 128, start_y = g_fixed_target ? g_target_y : 128;
  g_rtl_warp_load.active = true;
  g_rtl_warp_load.warp_room = room->header;
  g_rtl_warp_load.warp_door = def;
  g_rtl_warp_load.warp_screen_x = (uint16_t)(d[6] * 256);
  g_rtl_warp_load.warp_screen_y = (uint16_t)(d[7] * 256);
  g_rtl_warp_load.warp_samus_x = (uint16_t)(start_x < 16 ? 16 : start_x > room_w - 16 ? room_w - 16 : start_x);
  g_rtl_warp_load.warp_samus_y = (uint16_t)(start_y < 32 ? 32 : start_y > room_h - 32 ? room_h - 32 : start_y);

  SaveExploredMapTilesToSaved();   // of the room we are leaving; the load mirrors the new area's
  load_station_index = 0;          // LoadFromLoadStation indexes its table with it before we override
  loading_game_state = 5;          // kLoadingGameState_5_Main: no Ceres-specific start-up work

#ifdef SM_WARP_DEBUG
  g_def = def;
  g_warp_trace_plm = getenv("WARP_TRACEPLM") != NULL;
  if (getenv("WARP_FORCE_X")) g_rtl_warp_load.warp_samus_x = (uint16_t)atoi(getenv("WARP_FORCE_X"));
  if (getenv("WARP_FORCE_Y")) g_rtl_warp_load.warp_samus_y = (uint16_t)atoi(getenv("WARP_FORCE_Y"));
#endif
  g_pending_room = room->header;
  g_pending_frames = 0;
  g_left_gameplay = false;
  game_state = 0x06;   // InitAndLoadGameData_Async, then the fade-in, then gameplay
  return kWarp_Ok;
}

const char *SmWarp_ResultText(SmWarpResult r) {
  switch (r) {
  case kWarp_Ok:       return "Warping";
  case kWarp_NotReady: return "Not ready: be in a room, unpaused";
  case kWarp_NoDoor:   return "No door leads to that room";
  }
  return "?";
}
