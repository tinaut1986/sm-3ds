#include "scene_rec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "debug_tools.h"

#define REC_SLOTS 4

// One frame in the ring: its facts, then the pixels in the framebuffer's order.
typedef struct {
  uint32_t game_frame;
  uint16_t state, room, samus_x, samus_y;
  uint16_t logic_x100, draw_x100;   // ms x 100
  uint8_t flags;                    // bit 0 GPU renderer, bit 1 WIDE
  uint8_t pad[3];
} FrameHead;

enum { kPixels = kSceneRecW * kSceneRecH, kSlotBytes = sizeof(FrameHead) + kPixels * 2 };

static const struct { const char *label; int every; } kRates[] = {
  { "60 HZ", 1 }, { "30 HZ", 2 }, { "15 HZ", 4 },
};
enum { kRateCount = sizeof(kRates) / sizeof(kRates[0]) };

static int g_rate;
static uint8_t *g_ring;
static int g_capacity, g_head, g_count;   // slots, next slot to write, slots filled
static uint32_t g_tick;

bool SceneRec_Active(void) { return g_ring != NULL; }
int SceneRec_Frames(void) { return g_count; }
int SceneRec_Capacity(void) { return g_capacity; }
const char *SceneRec_RateLabel(void) { return kRates[g_rate].label; }

void SceneRec_CycleRate(void) {
  if (!g_ring) g_rate = (g_rate + 1) % kRateCount;
}

bool SceneRec_WantFrame(void) {
  return g_ring && (g_tick++ % kRates[g_rate].every) == 0;
}

static uint16_t ToMsX100(float ms) {
  const float v = ms * 100.0f;
  return v <= 0 ? 0 : v >= 65535.0f ? 65535 : (uint16_t)v;
}

void SceneRec_AddFrame(const uint32_t *fb, const SceneRecMeta *m) {
  if (!g_ring) return;
  uint8_t *slot = g_ring + (size_t)g_head * kSlotBytes;
  FrameHead h = {
    m->game_frame, m->state, m->room, m->samus_x, m->samus_y,
    ToMsX100(m->logic_ms), ToMsX100(m->draw_ms), (uint8_t)((m->gpu ? 1 : 0) | (m->wide ? 2 : 0)), { 0 },
  };
  memcpy(slot, &h, sizeof(h));
  uint16_t *px = (uint16_t *)(slot + sizeof(h));
  for (int i = 0; i < kPixels; i++) {
    const uint32_t w = fb[i];
    px[i] = (uint16_t)(((w >> 16) & 0xF800) | ((w >> 13) & 0x07E0) | ((w >> 11) & 0x001F));
  }
  g_head = (g_head + 1) % g_capacity;
  if (g_count < g_capacity) g_count++;
}

// Run-length packing of one frame, in 16-bit words: a word with the top bit set is a
// run (low 15 bits = length) followed by the pixel to repeat; otherwise it is the
// number of literal pixels that follow. Returns the number of words written to `out`
// (at most kPixels + kPixels / 0x7FFF + 1).
static size_t Pack(const uint16_t *px, uint16_t *out) {
  size_t o = 0;
  int i = 0;
  while (i < kPixels) {
    int run = 1;
    while (i + run < kPixels && run < 0x7FFF && px[i + run] == px[i]) run++;
    if (run >= 3) {
      out[o++] = (uint16_t)(0x8000 | run);
      out[o++] = px[i];
      i += run;
      continue;
    }
    // Literals up to the next run of 3 or more.
    int n = 0;
    while (i + n < kPixels && n < 0x7FFF) {
      if (i + n + 2 < kPixels && px[i + n] == px[i + n + 1] && px[i + n] == px[i + n + 2]) break;
      n++;
    }
    out[o++] = (uint16_t)n;
    memcpy(out + o, px + i, n * 2);
    o += n;
    i += n;
  }
  return o;
}

// File: a 64-byte header, then per frame its FrameHead, a uint32 count of packed words
// and the words. Little endian.
static void Save(void) {
  mkdir("debug", 0777);
  const int slot = Debug_NextSlot("rec", "debug/sm-rec-%02d.bin", REC_SLOTS);
  char path[64];
  snprintf(path, sizeof(path), "debug/sm-rec-%02d.bin", slot);
  FILE *f = fopen(path, "wb");
  if (f) setvbuf(f, NULL, _IOFBF, 64 * 1024);
  uint16_t *packed = (uint16_t *)malloc((kPixels + kPixels / 0x7FFF + 2) * 2);
  if (!f || !packed) {
    if (f) fclose(f);
    free(packed);
    Debug_SetMessage("Scene rec: cannot write %s", path);
    return;
  }
  struct {
    char magic[8];   // "SMREC1\0\0"
    uint16_t width, height;
    uint32_t frames, every;
    char version[44];
  } head;
  memset(&head, 0, sizeof(head));
  memcpy(head.magic, "SMREC1", 6);
  head.width = kSceneRecW;
  head.height = kSceneRecH;
  head.frames = (uint32_t)g_count;
  head.every = (uint32_t)kRates[g_rate].every;
  snprintf(head.version, sizeof(head.version), "%s", Debug_Version());
  fwrite(&head, 1, sizeof(head), f);
  size_t bytes = sizeof(head);
  const int first = g_count < g_capacity ? 0 : g_head;
  for (int k = 0; k < g_count; k++) {
    const uint8_t *s = g_ring + (size_t)((first + k) % g_capacity) * kSlotBytes;
    const uint32_t words = (uint32_t)Pack((const uint16_t *)(s + sizeof(FrameHead)), packed);
    fwrite(s, 1, sizeof(FrameHead), f);
    fwrite(&words, 1, 4, f);
    fwrite(packed, 2, words, f);
    bytes += sizeof(FrameHead) + 4 + words * 2;
  }
  fclose(f);
  free(packed);
  Debug_SetMessage("Scene rec %02d: %d frames, %u KB", slot, g_count, (unsigned)(bytes / 1024));
  Debug_Log("scene recording -> sm-rec-%02d.bin: %d frames every %d, %u KB", slot, g_count, kRates[g_rate].every,
            (unsigned)(bytes / 1024));
}

void SceneRec_Toggle(void) {
  if (g_ring) {
    if (g_count) Save();
    else Debug_SetMessage("Scene rec: nothing recorded");
    free(g_ring);
    g_ring = NULL;
    g_capacity = g_count = 0;
    return;
  }
  // The biggest ring that still leaves the game 4 MB of heap.
  static const int kMegs[] = { 32, 24, 16, 8, 4 };
  for (size_t i = 0; i < sizeof(kMegs) / sizeof(kMegs[0]) && !g_ring; i++) {
    g_capacity = kMegs[i] * 1024 * 1024 / kSlotBytes;
    g_ring = (uint8_t *)malloc((size_t)g_capacity * kSlotBytes);
    void *spare = g_ring ? malloc(4 * 1024 * 1024) : NULL;
    if (g_ring && !spare) {
      free(g_ring);
      g_ring = NULL;
    }
    free(spare);
  }
  if (!g_ring) {
    g_capacity = 0;
    Debug_SetMessage("Scene rec: out of memory");
    return;
  }
  g_head = g_count = 0;
  g_tick = 0;
  const float secs = (float)g_capacity * kRates[g_rate].every / 60.0f;
  Debug_SetMessage("Scene rec: last %.1f s kept", secs);
  Debug_Log("scene recording started: %d frames of ring, every %d", g_capacity, kRates[g_rate].every);
}
