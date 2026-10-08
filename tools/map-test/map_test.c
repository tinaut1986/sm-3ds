// Host check of the map's room scan (source/sm_map.c): the level data of every room it finds must
// fit the buffer ScreenIsReal decompresses into (kSmMapLevelBytes), and asking every cell of every
// area for its room must not crash. The decompressor has no size limit, so a room that does not
// fit corrupts whatever follows the buffer: the console died in free() on the map of Norfair.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/types.h"
#include "src/sm_rtl.h"
#include "src/funcs.h"
#include "src/variables.h"
#include "src/sm_cpu_infra.h"
#include "sm_map.h"

bool g_debug_flag, g_is_turbo, g_want_dump_memmap_flags, g_new_ppu = true, g_other_image;
struct SpcPlayer *g_spc_player;
int g_got_mismatch_count;
void NORETURN Die(const char *e) { fprintf(stderr, "Die: %s\n", e); exit(3); }
void Warning(const char *e) {}
void RtlDrawPpuFrame(uint8 *b, size_t p, uint32 f) {}
void RtlApuLock(void) {} void RtlApuUnlock(void) {} void RtlApuQueueLock(void) {} void RtlApuQueueUnlock(void) {}
int SDL_GetKeyFromName(const char *n) { return 0; }

static inline uint16_t Wd(const uint8_t *p) { return p[0] | p[1] << 8; }

int main(int argc, char **argv) {
  if (argc < 2 || !SnesInit(argv[1])) { fprintf(stderr, "cannot load the ROM\n"); return 1; }
  int n, bad = 0, max_len = 0, max_room = 0;
  const SmRoom *rooms = SmMap_Rooms(&n);
  static uint8_t a[1 << 20], b[1 << 20];
  for (int i = 0; i < n; i++) {
    const SmRoom *r = &rooms[i];
    const uint8_t *h = RomPtr(0x8F0000 | r->header) + 11;
    int k = 0;
    while (k < 80 && Wd(h + k) != 0xE5E6) k++;
    if (k >= 80) continue;   // the map treats such a room as unknown
    const uint32_t src = h[k + 2] | h[k + 3] << 8 | h[k + 4] << 16;
    memset(a, 0xA5, sizeof(a)), memset(b, 0x5A, sizeof(b));
    DecompressToMem(src, a), DecompressToMem(src, b);   // two fills tell where the output ends
    int len = 0;
    for (int j = (int)sizeof(a) - 1; j >= 0; j--)
      if (a[j] != 0xA5 || b[j] != 0x5A) { len = j + 1; break; }
    if (len > max_len) max_len = len, max_room = r->header;
    if (len > kSmMapLevelBytes) {
      bad++;
      printf("FAIL room %04X area %d %dx%d: the level data is %d bytes, the buffer %d\n", r->header, r->area, r->w, r->h, len,
             kSmMapLevelBytes);
    }
  }
  int found = 0;
  for (int area = 0; area < kSmAreaCount; area++)
    for (int row = 0; row < kSmMapRows; row++)
      for (int col = 0; col < kSmMapCols; col++) found += SmMap_RoomAt(area, col, row) != NULL;
  printf("%d rooms, the largest level data %d bytes (room %04X) of %d; %d cells with a room\n", n, max_len, max_room, kSmMapLevelBytes,
         found);
  if (bad) return 1;
  printf("map_test: OK\n");
  return 0;
}
