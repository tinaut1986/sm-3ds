// Host tool: dumps what the layer workbench needs of every room, from the game itself.
//
// Boots headless to gameplay, then warps into each room (source/sm_warp.c, as tools/warp-test
// does), lets it settle and writes <OUT>/<room>.room: the room's level data (bank $7F: BG1's
// blocks, their BTS, and BG2's map), the tile table, the palette and VRAM, which is all that is
// needed to draw the blocks without the emulator. It needs the ROM (never committed); the
// output is not committed either. Format: README.md.
//
// Usage: export_rooms ROM OUTDIR [room_hex]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "src/types.h"
#include "src/sm_rtl.h"
#include "src/sm_cpu_infra.h"
#include "src/config.h"
#include "src/snes/ppu.h"
#include "src/spc_player.h"
#include "src/variables.h"
#include "sm_map.h"
#include "sm_warp.h"

bool g_debug_flag, g_is_turbo, g_want_dump_memmap_flags, g_new_ppu = true, g_other_image;
struct SpcPlayer *g_spc_player;
int g_got_mismatch_count;
extern Snes *g_snes;
static uint8_t px[256 * 4 * 240];
void NORETURN Die(const char *e) { fprintf(stderr, "Die: %s\n", e); exit(3); }
void Warning(const char *e) {}
void RtlDrawPpuFrame(uint8 *b, size_t p, uint32 f) {}
void RtlApuLock(void) {} void RtlApuUnlock(void) {} void RtlApuQueueLock(void) {} void RtlApuQueueUnlock(void) {}
int SDL_GetKeyFromName(const char *n) { return 0; }

enum { A = 0x100, START = 0x08 };

static void Step(int in) {
  samus_health = 99;
  g_snes->disableRender = 1;
  RtlRunFrame(in);
  SmWarp_AfterFrame();
}

// File layout, little endian:
//   RoomFileHead (34 bytes): magic "SMRM1\0\0\0", u16 room header pointer, u8 area, u8 reserved, u16 width and height
//                    (blocks), u16 layer2_scroll_x, u16 BG1SC and BG2SC (registers), u16 BG1 and BG2 tilemap
//                    (VRAM word addresses), u16 layers on the main screen and u16 on the sub screen (bit n =
//                    BG1..BG4 = 0..3, sprites = 4), u16 mode | BG3 priority << 4, u16 reserved
//   u8 bank7f[65536]    $7F:0000; word 0 = size of the level data, layer 1 words from $7F:0002
//                       (bits 0-9 tile table index, 10 hflip, 11 vflip, 12-15 block type), its BTS
//                       after them, BG2's words from $7F:9602 (only if layer2_scroll_x bit 0 is 0)
//   u16 tile_table[1024][4]   top-left, top-right, bottom-left, bottom-right (SNES tilemap words)
//   u16 cgram[256]
//   u16 vram[32768]
typedef struct __attribute__((packed)) {
  char magic[8];
  uint16_t room_hdr;
  uint8_t area, reserved;
  uint16_t width, height, l2_scroll_x, bg1_sc, bg2_sc, bg1_map_adr, bg2_map_adr, spare[4];
} RoomFileHead;

static void WriteRoom(const char *dir, const SmRoom *r) {
  char path[256];
  snprintf(path, sizeof(path), "%s/%04X.room", dir, r->header);
  FILE *f = fopen(path, "wb");
  if (!f) { perror(path); return; }
  const Ppu *p = g_snes->ppu;
  RoomFileHead h;
  memset(&h, 0, sizeof(h));
  memcpy(h.magic, "SMRM1", 5);
  h.room_hdr = r->header, h.area = (uint8_t)r->area;
  h.width = (uint16_t)room_width_in_blocks, h.height = (uint16_t)room_height_in_blocks;
  h.l2_scroll_x = (uint16_t)layer2_scroll_x, h.bg1_sc = (uint16_t)reg_BG1SC, h.bg2_sc = (uint16_t)reg_BG2SC;
  h.bg1_map_adr = (uint16_t)p->bgLayer[0].tilemapAdr, h.bg2_map_adr = (uint16_t)p->bgLayer[1].tilemapAdr;
  for (int i = 0; i < 5; i++)
    h.spare[0] |= (uint16_t)(p->layer[i].mainScreenEnabled << i), h.spare[1] |= (uint16_t)(p->layer[i].subScreenEnabled << i);
  h.spare[2] = (uint16_t)(p->mode | p->bg3priority << 4);
  fwrite(&h, sizeof(h), 1, f);
  fwrite(g_ram + 0x10000, 1, 0x10000, f);
  fwrite(g_ram + 0xA000, 1, 0x2000, f);
  fwrite(p->cgram, 2, 256, f);
  fwrite(p->vram, 2, 0x8000, f);
  fclose(f);
}

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: export_rooms ROM OUTDIR [room_hex]\n"); return 2; }
  ParseConfigFile(NULL);
  Snes *snes = SnesInit(argv[1]);
  g_spc_player = SpcPlayer_Create(); SpcPlayer_Initialize(g_spc_player);
  PpuBeginDrawing(snes->snes_ppu, px, 256 * 4, 0);
  RtlReadSram();
  mkdir(argv[2], 0777);
  for (int i = 0; i < 20000; i++) {
    Step((i % 60 < 6) ? (i % 120 < 60 ? START : A) : 0);
    if (game_state == 8 && i > 100) break;
  }
  for (int i = 0; i < 120; i++) Step(0);
  RtlSaveLoad(kSaveLoad_Save, 1);
  int n;
  const SmRoom *rooms = SmMap_Rooms(&n);
  SmWarp_Init();
  const int only = argc > 3 ? (int)strtol(argv[3], 0, 16) : 0;
  int ok = 0, skipped = 0;
  for (int i = 0; i < n; i++) {
    const SmRoom *r = &rooms[i];
    if (only && r->header != only) continue;
    if (SmWarp_DoorCount(r) == 0) { skipped++; continue; }
    RtlSaveLoad(kSaveLoad_Load, 1);
    for (int k = 0; k < 5; k++) Step(0);
    if (SmWarp_ToRoom(r, 0) != kWarp_Ok) { skipped++; continue; }
    for (int k = 0; k < 400 && !(game_state == 8 && room_ptr == r->header); k++) Step(0);
    if (!(game_state == 8 && room_ptr == r->header)) { printf("room %04X: did not settle\n", r->header); skipped++; continue; }
    for (int k = 0; k < 30; k++) Step(0);
    WriteRoom(argv[2], r);
    ok++;
  }
  printf("export_rooms: %d rooms written, %d skipped\n", ok, skipped);
  return 0;
}
