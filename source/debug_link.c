#include "debug_link.h"

#include <3ds.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "src/types.h"
#include "src/variables.h"

#include "bottom_ui.h"
#include "debug_tools.h"
#include "ftp_server.h"
#include "states_store.h"

// The main loop hands over what is on the top screen (a 400x240 column-major 0xRRGGBBAA image,
// the framebuffer's layout), or NULL when there is none.
extern const uint32_t *Main_TopScreenPixels(void);

enum {
  kPeekMax = 256,
  kReplyMax = 600,
  kTopW = 400, kBottomW = 320, kShotH = 240,
  kMaxStates = 64,
};

// A request the main thread never takes (game hung or paused with the window closed).
#define LINK_WAIT_NS 20000000000LL
#define LINK_WAIT_SLICE_NS 50000000LL

typedef enum {
  OP_NONE, OP_TOUCH, OP_SHOT, OP_STATE_SAVE, OP_STATE_LOAD, OP_STATE_DELETE, OP_STATES, OP_DUMP, OP_SWITCH,
  OP_STATE_LINE, OP_POKE
} LinkOp;

// One request at a time: the FTP server serves one client and one command.
static LightLock g_lock = 1;   // LightLock's unlocked value
static LightEvent g_done;
static bool g_done_ready;
static volatile LinkOp g_op;
static LinkOp g_active;        // a request that takes more than one frame
static int g_arg_a, g_arg_b;
static uintptr_t g_poke_address;
static uint8_t g_poke_bytes[kPeekMax];
static unsigned g_poke_length;
static char g_result[kReplyMax];
static uint64_t g_frame;
static volatile bool g_state_job_done, g_state_job_ok;

// Injected buttons: written by the FTP thread, read by the main thread.
static volatile uint32_t g_keys_mask;
static volatile uint32_t g_keys_frames;
static volatile bool g_keys_new;
static uint64_t g_keys_until;
static uint32_t g_keys_now;   // what is injected as held right now

static const struct { const char *name; uint32_t mask; } kKeys[] = {
  { "A", KEY_A },         { "B", KEY_B },           { "X", KEY_X },        { "Y", KEY_Y },
  { "L", KEY_L },         { "R", KEY_R },           { "START", KEY_START }, { "SELECT", KEY_SELECT },
  { "UP", KEY_DUP },      { "DOWN", KEY_DDOWN },    { "LEFT", KEY_DLEFT }, { "RIGHT", KEY_DRIGHT },
  { "ZL", KEY_ZL },       { "ZR", KEY_ZR },
};

static bool ParseKeys(const char *text, uint32_t *mask) {
  *mask = 0;
  char word[16];
  while (*text && *text != ' ') {
    unsigned n = 0;
    while (*text && *text != '+' && *text != ' ' && n + 1 < sizeof(word)) word[n++] = (char)toupper((unsigned char)*text++);
    word[n] = 0;
    if (*text == '+') text++;
    if (!strcmp(word, "NONE")) continue;
    bool found = false;
    for (unsigned i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); i++)
      if (!strcmp(word, kKeys[i].name)) {
        *mask |= kKeys[i].mask;
        found = true;
      }
    if (!found) return false;
  }
  return true;
}

// Readable (or writable) for `length` bytes, page by page: a bad address from the PC must not
// crash the game.
static bool MemoryOk(uintptr_t address, unsigned length, bool write) {
  uintptr_t at = address;
  const uintptr_t end = address + length;
  while (at < end) {
    MemInfo info;
    PageInfo page;
    if (R_FAILED(svcQueryMemory(&info, &page, (u32)at))) return false;
    if (info.state == MEMSTATE_FREE) return false;
    if (!(info.perm & (write ? MEMPERM_WRITE : MEMPERM_READ))) return false;
    at = info.base_addr + info.size;
  }
  return true;
}

static int ParseHex(const char *text, uint8_t *out, unsigned cap) {
  unsigned n = 0;
  while (text[0] && text[1] && n < cap) {
    char pair[3] = { text[0], text[1], 0 };
    char *end;
    const long v = strtol(pair, &end, 16);
    if (*end) return -1;
    out[n++] = (uint8_t)v;
    text += 2;
  }
  return text[0] ? -1 : (int)n;
}

// Hands the request to the main thread and waits for its result line.
static void RunOnMainThread(LinkOp op, char *reply, unsigned cap) {
  LightLock_Lock(&g_lock);
  if (!g_done_ready) {
    LightEvent_Init(&g_done, RESET_ONESHOT);
    g_done_ready = true;
  }
  LightEvent_Clear(&g_done);
  g_result[0] = 0;
  __atomic_store_n(&g_op, op, __ATOMIC_RELEASE);
  // In slices, giving up as soon as the server stops: FtpServer_Stop runs on the main thread,
  // so a request waiting for it could only time out, long after the stop's 3 s join.
  bool done = false;
  for (long long waited = 0; waited < LINK_WAIT_NS && FtpServer_Running(); waited += LINK_WAIT_SLICE_NS) {
    if (LightEvent_WaitTimeout(&g_done, LINK_WAIT_SLICE_NS) == 0) {
      done = true;
      break;
    }
  }
  if (!done) {
    __atomic_store_n(&g_op, OP_NONE, __ATOMIC_RELEASE);
    snprintf(reply, cap, "451 The game did not take the request (stopped?).");
  } else {
    snprintf(reply, cap, "%s", g_result);
  }
  LightLock_Unlock(&g_lock);
}

// ---- SWITCH: the values the owner changes by hand on the console ---------------------------

typedef struct {
  const char *name;
  int *i;        // an int field of g_ui, or
  bool *b;       // a bool one
  int count;     // values of an int field (it cycles 0..count-1)
  const char *const *labels;   // optional names of those values
} Switch;

static const char *const kPacing[] = { "AUTO", "LOCK 30", "NO SKIP" };
static const char *const kTint[] = { "OFF", "PLANES", "DRAW ORDER", "STEREO DEPTH" };
static const char *const kGpuTest[] = { "OFF", "BG NOT TEXTURED", "NO BG", "NO SPRITES", "NO TOP PASS", "NO COLOUR MATH",
                                        "NO CLEARS", "NO STRIP RUNS" };
static const char *const kFps[] = { "OFF", "TOP-LEFT", "TOP-RIGHT", "BOTTOM-LEFT", "BOTTOM-RIGHT" };

static const Switch *Switches(int *count) {
  static Switch table[11];
  static bool ready;
  if (!ready) {
    ready = true;
    const Switch t[] = {
      { "RENDERER", NULL, &g_ui.gpu_render, 0, NULL },
      { "FORCE 3D", NULL, &g_ui.force_3d, 0, NULL },
      { "PLANE TINT", &g_ui.plane_tint, NULL, 4, kTint },
      { "GPU TEST", &g_ui.gpu_test, NULL, 8, kGpuTest },
      { "WIDE", NULL, &g_ui.wide, 0, NULL },
      { "PIXEL PERFECT", NULL, &g_ui.pixel_perfect, 0, NULL },
      { "FRAMES", &g_ui.pacing, NULL, 3, kPacing },
      { "AUDIO", NULL, &g_ui.audio_on, 0, NULL },
      { "FPS", &g_ui.fps_overlay, NULL, 5, kFps },
      { "PAUSE", NULL, &g_ui.paused, 0, NULL },
    };
    memcpy(table, t, sizeof(t));
  }
  *count = 10;
  return table;
}

static bool NameMatches(const char *name, const char *arg) {
  // "PLANE TINT" matches PLANE_TINT, plane-tint and plane tint.
  unsigned i = 0;
  while (name[i] && arg[i] &&
         (toupper((unsigned char)arg[i]) == name[i] || (name[i] == ' ' && (arg[i] == '_' || arg[i] == '-'))))
    i++;
  return !name[i] && !arg[i];
}

static void SwitchValue(const Switch *s, char *out, unsigned cap) {
  if (s->b) snprintf(out, cap, "%s", *s->b ? "ON" : "OFF");
  else snprintf(out, cap, "%s", s->labels ? s->labels[*s->i] : "?");
}

// ---- screenshots ----------------------------------------------------------------------------

// A 24-bit BMP of a column-major 0xRRGGBBAA image (row 0 is the last word of each column),
// built in RAM and written at once: row by row it kept the game frozen for seconds.
static bool WriteBmp(const char *path, const uint32_t *px, int w, int h) {
  const int stride = (w * 3 + 3) & ~3;
  const size_t size = 54 + (size_t)stride * h;
  uint8_t *buf = calloc(1, size);
  if (!buf) return false;
  const uint32_t fsize = (uint32_t)size, offset = 54, info = 40, planes_bits = 1 | (24 << 16), image = (uint32_t)(stride * h);
  buf[0] = 'B';
  buf[1] = 'M';
  memcpy(buf + 2, &fsize, 4);
  memcpy(buf + 10, &offset, 4);
  memcpy(buf + 14, &info, 4);
  const int32_t sw = w, sh = h;
  memcpy(buf + 18, &sw, 4);
  memcpy(buf + 22, &sh, 4);
  memcpy(buf + 26, &planes_bits, 4);
  memcpy(buf + 34, &image, 4);
  for (int y = 0; y < h; y++) {
    uint8_t *row = buf + 54 + (size_t)(h - 1 - y) * stride;   // bottom-up
    for (int x = 0; x < w; x++) {
      const uint32_t c = px[x * h + (h - 1 - y)];
      row[x * 3 + 0] = (uint8_t)(c >> 8);    // B
      row[x * 3 + 1] = (uint8_t)(c >> 16);   // G
      row[x * 3 + 2] = (uint8_t)(c >> 24);   // R
    }
  }
  FILE *f = fopen(path, "wb");
  bool ok = f && fwrite(buf, 1, size, f) == size;
  if (f && fclose(f) != 0) ok = false;
  free(buf);
  return ok;
}

// ---- the FTP thread's side --------------------------------------------------------------------

// States are numbered from 1, newest first, as the STATES tab lists them.
static int StatesList(StateInfo *out) {
  const int total = States_Scan(out, kMaxStates);
  return total < kMaxStates ? total : kMaxStates;
}

bool DebugLink_Command(const char *verb, const char *args, char *reply, unsigned cap) {
  if (!args) args = "";
  if (!strcmp(verb, "KEYS")) {
    uint32_t mask;
    if (!ParseKeys(args, &mask)) {
      snprintf(reply, cap, "501 KEYS <A+B+LEFT...|NONE> [frames]");
      return true;
    }
    const char *frames = strchr(args, ' ');
    long n = frames ? strtol(frames + 1, NULL, 10) : 1;
    if (n < 1) n = 1;
    g_keys_mask = mask;
    g_keys_frames = (uint32_t)n;
    __atomic_store_n(&g_keys_new, true, __ATOMIC_RELEASE);
    snprintf(reply, cap, "200 Holding %s for %ld frames.", args, n);
    return true;
  }
  if (!strcmp(verb, "TOUCH")) {
    if (sscanf(args, "%d %d", &g_arg_a, &g_arg_b) != 2 || g_arg_a < 0 || g_arg_a >= 320 || g_arg_b < 0 || g_arg_b >= 240) {
      snprintf(reply, cap, "501 TOUCH <x 0-319> <y 0-239>");
      return true;
    }
    RunOnMainThread(OP_TOUCH, reply, cap);
    return true;
  }
  if (!strcmp(verb, "SHOT")) {
    RunOnMainThread(OP_SHOT, reply, cap);
    return true;
  }
  if (!strcmp(verb, "DUMP")) {
    RunOnMainThread(OP_DUMP, reply, cap);
    return true;
  }
  if (!strcmp(verb, "STATE")) {
    RunOnMainThread(OP_STATE_LINE, reply, cap);
    return true;
  }
  if (!strcmp(verb, "STATES")) {
    RunOnMainThread(OP_STATES, reply, cap);
    return true;
  }
  if (!strcmp(verb, "SAVESTATE") || !strcmp(verb, "LOADSTATE") || !strcmp(verb, "DELSTATE")) {
    const long n = strtol(args, NULL, 10);
    if (!strcmp(verb, "DELSTATE") && n <= 0) {
      snprintf(reply, cap, "501 DELSTATE <number from SITE STATES>");
      return true;
    }
    g_arg_a = (int)n;   // 0: the default (a new state for SAVESTATE, the newest otherwise)
    RunOnMainThread(!strcmp(verb, "SAVESTATE") ? OP_STATE_SAVE : !strcmp(verb, "LOADSTATE") ? OP_STATE_LOAD : OP_STATE_DELETE,
                    reply, cap);
    return true;
  }
  if (!strcmp(verb, "SWITCH")) {
    int count;
    const Switch *t = Switches(&count);
    g_arg_a = -1;
    for (int i = 0; i < count; i++)
      if (NameMatches(t[i].name, args)) g_arg_a = i;
    if (g_arg_a < 0) {
      int n = snprintf(reply, cap, "501 SWITCH <name>: ");
      for (int i = 0; i < count && (unsigned)n + 20 < cap; i++) n += snprintf(reply + n, cap - (unsigned)n, "%s%s", i ? ", " : "", t[i].name);
      return true;
    }
    RunOnMainThread(OP_SWITCH, reply, cap);
    return true;
  }
  if (!strcmp(verb, "PEEK")) {
    unsigned long address = 0, length = 0;
    if (sscanf(args, "%lx %lu", &address, &length) != 2 || length == 0 || length > kPeekMax) {
      snprintf(reply, cap, "501 PEEK <hex address> <length 1-%d>", kPeekMax);
      return true;
    }
    if (!MemoryOk((uintptr_t)address, (unsigned)length, false)) {
      snprintf(reply, cap, "550 Not readable.");
      return true;
    }
    int n = snprintf(reply, cap, "200 ");
    const uint8_t *p = (const uint8_t *)(uintptr_t)address;
    for (unsigned long i = 0; i < length && (unsigned)n + 3 < cap; i++) n += snprintf(reply + n, cap - (unsigned)n, "%02x", p[i]);
    return true;
  }
  if (!strcmp(verb, "POKE")) {
    unsigned long address = 0;
    char hex[2 * kPeekMax + 1];
    int length;
    if (sscanf(args, "%lx %512s", &address, hex) != 2 || (length = ParseHex(hex, g_poke_bytes, kPeekMax)) <= 0) {
      snprintf(reply, cap, "501 POKE <hex address> <hex bytes>");
      return true;
    }
    if (!MemoryOk((uintptr_t)address, (unsigned)length, true)) {
      snprintf(reply, cap, "550 Not writable.");
      return true;
    }
    g_poke_address = (uintptr_t)address;
    g_poke_length = (unsigned)length;
    RunOnMainThread(OP_POKE, reply, cap);
    return true;
  }
  return false;
}

// ---- the main thread's side -------------------------------------------------------------------

static int StateNumber(int id, const StateInfo *list, int n) {
  for (int i = 0; i < n; i++)
    if (list[i].id == id) return i + 1;
  return 0;
}

// Starts a request. True when it is complete (g_result set); false when it needs another frame
// (Continue finishes it).
static bool Start(LinkOp op) {
  static StateInfo list[kMaxStates];
  switch (op) {
  case OP_TOUCH:
    BottomUi_TouchDown(g_arg_a, g_arg_b);
    return false;   // released next frame
  case OP_SHOT: {
    mkdir("debug", 0777);
    const uint32_t *top = Main_TopScreenPixels();
    const bool ok = top && WriteBmp("debug/shot-top.bmp", top, kTopW, kShotH) &&
                    WriteBmp("debug/shot-bottom.bmp", UiDraw_Screen(GFX_BOTTOM).px, kBottomW, kShotH);
    snprintf(g_result, sizeof(g_result), ok ? "200 debug/shot-top.bmp debug/shot-bottom.bmp"
                                            : "550 Could not take the screenshots (no frame drawn yet?).");
    return true;
  }
  case OP_DUMP:
    if (g_ui.paused) {
      snprintf(g_result, sizeof(g_result), "451 The game is paused: a dump needs a frame.");
      return true;
    }
    g_ui.req_dump = true;
    return false;
  case OP_STATES: {
    const int n = StatesList(list);
    int len = snprintf(g_result, sizeof(g_result), "200 %d:", n);
    for (int i = 0; i < n && len + 24 < (int)sizeof(g_result); i++)
      len += snprintf(g_result + len, sizeof(g_result) - (unsigned)len, " %d=%u/%04X", i + 1, list[i].area, list[i].room);
    return true;
  }
  case OP_STATE_SAVE: case OP_STATE_LOAD: case OP_STATE_DELETE: {
    const int n = StatesList(list);
    const int number = g_arg_a > 0 ? g_arg_a : 1;
    if (op == OP_STATE_SAVE && g_arg_a <= 0) {
      g_ui.save_slot = States_NextId();
    } else if (number > n) {
      snprintf(g_result, sizeof(g_result), "550 No state number %d (%d saved).", number, n);
      return true;
    } else {
      g_ui.save_slot = list[number - 1].id;
    }
    if (op == OP_STATE_DELETE) {
      BottomUi_Busy();
      States_Delete(g_ui.save_slot);
      BottomUi_StatesChanged();
      snprintf(g_result, sizeof(g_result), "200 Deleted state number %d.", number);
      return true;
    }
    g_state_job_done = false;
    if (op == OP_STATE_SAVE) g_ui.req_save_state = true;
    else g_ui.req_load_state = true;
    return false;
  }
  case OP_SWITCH: {
    int count;
    const Switch *s = &Switches(&count)[g_arg_a];
    if (s->b) *s->b = !*s->b;
    else *s->i = (*s->i + 1) % s->count;
    char value[24];
    SwitchValue(s, value, sizeof(value));
    Debug_Log("switch %s -> %s (link)", s->name, value);
    snprintf(g_result, sizeof(g_result), "200 %s = %s", s->name, value);
    return true;
  }
  case OP_STATE_LINE:
    snprintf(g_result, sizeof(g_result),
             "200 frame=%llu paused=%d state=%02X area=%u room=%04X x=%u y=%u pose=%02X health=%u/%u reserve=%u "
             "missiles=%u supers=%u pbs=%u",
             (unsigned long long)g_frame, g_ui.paused ? 1 : 0, (unsigned)game_state, (unsigned)area_index,
             (unsigned)room_ptr, (unsigned)samus_x_pos, (unsigned)samus_y_pos, (unsigned)samus_pose,
             (unsigned)samus_health, (unsigned)samus_max_health, (unsigned)samus_reserve_health,
             (unsigned)samus_missiles, (unsigned)samus_super_missiles, (unsigned)samus_power_bombs);
    return true;
  case OP_POKE:
    memcpy((void *)g_poke_address, g_poke_bytes, g_poke_length);
    snprintf(g_result, sizeof(g_result), "200 Wrote %u bytes.", g_poke_length);
    return true;
  default: return true;
  }
}

static bool Continue(LinkOp op) {
  static StateInfo list[kMaxStates];
  switch (op) {
  case OP_TOUCH:
    BottomUi_TouchUp();
    snprintf(g_result, sizeof(g_result), "200 Tapped %d,%d.", g_arg_a, g_arg_b);
    return true;
  case OP_DUMP:
    snprintf(g_result, sizeof(g_result), "200 %s", Debug_LastMessage());
    return true;
  case OP_STATE_SAVE: case OP_STATE_LOAD: {
    if (!g_state_job_done) return false;   // the loop has not run it yet
    const int n = StatesList(list);
    snprintf(g_result, sizeof(g_result), "%s %s state number %d.", g_state_job_ok ? "200" : "550",
             op == OP_STATE_SAVE ? (g_state_job_ok ? "Saved" : "Could not save") : (g_state_job_ok ? "Loaded" : "Could not load"),
             StateNumber(g_ui.save_slot, list, n));
    return true;
  }
  default: return true;
  }
}

void DebugLink_StateDone(bool ok) {
  g_state_job_ok = ok;
  g_state_job_done = true;
}

void DebugLink_Poll(uint32_t *down, uint32_t *up, uint64_t frame) {
  g_frame = frame;
  // Buttons. A new KEYS first releases what is still held (one frame), so that the same
  // button pressed again is a new press for the game.
  if (g_keys_new && g_keys_now) {
    *up |= g_keys_now;
    g_keys_now = 0;
  } else {
    if (__atomic_exchange_n(&g_keys_new, false, __ATOMIC_ACQ_REL)) g_keys_until = frame + g_keys_frames;
    const uint32_t want = frame < g_keys_until ? g_keys_mask : 0;
    *down |= want & ~g_keys_now;
    *up |= g_keys_now & ~want;
    g_keys_now = want;
  }

  if (g_active != OP_NONE) {
    if (Continue(g_active)) {
      g_active = OP_NONE;
      LightEvent_Signal(&g_done);
    }
    return;
  }
  const LinkOp op = __atomic_exchange_n(&g_op, OP_NONE, __ATOMIC_ACQ_REL);
  if (op == OP_NONE) return;
  if (Start(op)) LightEvent_Signal(&g_done);
  else g_active = op;
}
