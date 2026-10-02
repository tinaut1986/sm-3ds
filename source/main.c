// 3DS-specific main file for Super Metroid port
// Based on snesrev/sm with minimal modifications for 3DS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>
#include "SDL2/SDL.h"
#include <3ds.h>

#include "src/snes/ppu.h"
#include "src/types.h"
#include "src/sm_rtl.h"
#include "src/sm_cpu_infra.h"
#include "src/config.h"
#include "src/util.h"
#include "src/spc_player.h"
#include "src/audio_prof.h"
#include "src/variables.h"
#include "src/ida_types.h"

#include "bottom_ui.h"
#include "cheats.h"
#include "sm_warp.h"
#include "sm_wide.h"
#include "debug_tools.h"
#include "scene_rec.h"
#include "gpu_ppu.h"
#include "gpu_ppu_3ds.h"
#include "rom_loader.h"
#include "ui_draw.h"
#include "retro_ach.h"
#include "game_text.h"
#include "version.h"
#include "build_config.h"

enum Button {
  BTN_A = 0,
  BTN_B = 1,
  BTN_SELECT = 2,
  BTN_START = 3,
  BTN_DPAD_R = 4,
  BTN_DPAD_L = 5,
  BTN_DPAD_U = 6,
  BTN_DPAD_D = 7,
  BTN_R = 8,
  BTN_L = 9,
  BTN_X = 10,
  BTN_Y = 11,
  BTN_ZL = 14,
  BTN_ZR = 15,
};

static void SDLCALL AudioCallback(void *userdata, Uint8 *stream, int len);
static void HandleInput(int keyCode, int keyMod, bool pressed);
static void HandleCommand(uint32 j, bool pressed);

bool g_debug_flag;
bool g_is_turbo;
bool g_want_dump_memmap_flags;
bool g_new_ppu = true;
bool g_other_image;
struct SpcPlayer *g_spc_player;

static uint8_t g_pixels[256 * 4 * 240] __attribute__((aligned(16)));
static uint8_t g_my_pixels[256 * 4 * 240];

int g_got_mismatch_count;

static const char kWindowTitle[] = "Super Metroid 3DS";
static SDL_Window *g_window;
static SDL_Renderer *g_renderer;
static SDL_Texture *g_texture;

static uint8 g_turbo, g_replay_turbo = true;
static uint8 g_gamepad_buttons;
static int g_input1_state;
static bool g_display_perf;
static int g_curr_fps;
static int g_ppu_render_flags = 0;
static int g_snes_width = 256, g_snes_height = 240;
static int g_sdl_audio_mixer_volume = SDL_MIX_MAXVOLUME;
static volatile float g_audio_ms;   // last audio block, written by the audio thread

extern Snes *g_snes;

void NORETURN Die(const char *error) {
  fprintf(stderr, "Error: %s\n", error);
  Debug_Log("FATAL: %s", error);
  exit(1);
}

void Warning(const char *error) {
  fprintf(stderr, "Warning: %s\n", error);
  Debug_Log("warning: %s", error);
}

void RtlDrawPpuFrame(uint8 *pixel_buffer, size_t pitch, uint32 render_flags) {
  uint8 *ppu_pixels = g_pixels; //g_other_image ? g_my_pixels : g_pixels;
  for (size_t y = 0; y < 240; y++)
    memcpy((uint8_t *)pixel_buffer + y * pitch, ppu_pixels + y * 256 * 4, 256 * 4);
}

// Copies the 256x224 PPU output to the top screen, centred, and rotated: the 3DS
// framebuffer is column-major with the origin at the bottom-left. SCALED stretches it
// to 274x240 (nearest, aspect-correct), PIXEL PERFECT copies it 1:1 with 8 black rows
// above and below. Source pixels are 0x00RRGGBB, the framebuffer wants R,G,B,A from
// the high byte down, so one shift does the conversion.
// `clear_sides`: the screen has something outside this layout (a WIDE frame from the GPU).
static void DrawPpuFrame(bool pixel_perfect, bool clear_sides) {
    enum { SRC_W = 256, SRC_H = 224, FB_W = 400, FB_H = 240, SCALED_W = 274 };
    static uint16_t xmap[2][SCALED_W];
    static const uint32_t *rows[2][FB_H];   // source row for each destination row
    static const uint32_t black_row[SRC_W];
    static bool init;
    if (!init) {
        const float scale = (float)FB_H / (float)SRC_H;
        for (int dx = 0; dx < SCALED_W; dx++) {
            xmap[0][dx] = (uint16_t)((int)(dx / scale) % SRC_W);
            xmap[1][dx] = (uint16_t)(dx < SRC_W ? dx : 0);
        }
        for (int dy = 0; dy < FB_H; dy++) {
            int sy = (int)(dy / scale);
            rows[0][dy] = (const uint32_t *)g_pixels + (sy < SRC_H ? sy : SRC_H - 1) * SRC_W;
            sy = dy - (FB_H - SRC_H) / 2;
            rows[1][dy] = sy >= 0 && sy < SRC_H ? (const uint32_t *)g_pixels + sy * SRC_W : black_row;
        }
        init = true;
    }

    // The sides are only drawn by the layout that covers them: clear the whole screen,
    // in both buffers, after a switch (the GPU path clears its own frames).
    static int clear_frames = 2, last_layout = -1;
    const int layout = pixel_perfect;
    if (layout != last_layout || clear_sides) clear_frames = 2;
    last_layout = layout;

    UiDraw_WaitSwapShown();
    uint32_t *fb = (uint32_t *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    if (clear_frames > 0) {
        clear_frames--;
        for (int i = 0; i < FB_W * FB_H; i++) fb[i] = 0xFFu;
    }
    const int dst_w = pixel_perfect ? SRC_W : SCALED_W;
    const int x_off = (FB_W - dst_w) / 2;
    for (int dx = 0; dx < dst_w; dx++) {
        uint32_t *col = fb + (x_off + dx) * FB_H + (FB_H - 1);   // dy = 0 is the last word
        const int sx = xmap[layout][dx];
        const uint32_t *const *r = rows[layout];
        for (int dy = 0; dy < FB_H; dy++)
            col[-dy] = (r[dy][sx] << 8) | 0xFFu;
    }
}

// GPU renderer state: the line capture it draws from, its draw list, and whether the
// top screen is currently being presented by citro3d rather than by DrawPpuFrame.
static PpuLineCapture g_line_capture;
static GpuFrame g_gpu_frame;
static bool g_top_by_gpu;
static bool g_top_wide;   // the last frame shown had WIDE margins

// WIDE view margin for the next frame: gameplay only, and the fades into and out of it;
// title, menus, the pause map and cutscenes stay 4:3. 60 px when SCALED: 376 px at x1.07
// fill the 400 px screen.
static int WideMargin(void) {
  if (!g_ui.wide) return 0;
  switch (game_state) {
  case kGameState_7_MainGameplayFadeIn: case kGameState_8_MainGameplay: case kGameState_9_HitDoorBlock:
  case kGameState_10_LoadingNextRoom: case kGameState_11_LoadingNextRoom: case kGameState_12_Pausing:
  case kGameState_18_Unpausing: case kGameState_27_ReserveTanksAuto: case kGameState_42_PlayingDemo:
  case kGameState_32_MadeItToCeresElevator:   // the elevator rising up the shaft, still the room,
  case kGameState_33_BlackoutFromCeres:       // and its fade to black
    return g_ui.pixel_perfect ? 72 : 60;
  default:
    return 0;
  }
}



// Debug tools -> GPU CHECK: draws the frame just shown by the GPU again with the CPU
// renderer (from the same capture) and compares them. Writes a dump set with the CPU
// image as -top.rgb and the GPU's as -gpu.rgb.
static void GpuCheck(void) {
  static uint8_t gpu_px[256 * 4 * 240];
  memset(gpu_px, 0, sizeof(gpu_px));
  ppu_replayLines(g_snes->ppu, &g_line_capture, 1, g_line_capture.last_line);
  if (!GpuPpu3ds_ReadBack(gpu_px, 256 * 4)) {
    BottomUi_Toast("GPU check: no GPU frame");
    return;
  }
  int differ = 0, far = 0;
  for (int i = 0; i < 256 * 224; i++) {
    int d = 0;
    for (int c = 0; c < 3; c++) {
      const int e = abs((int)g_pixels[i * 4 + c] - (int)gpu_px[i * 4 + c]);
      if (e > d) d = e;
    }
    differ += d > 0;
    far += d > 8;
  }
  const int slot = Debug_DumpScreen(g_pixels);
  if (slot >= 0) Debug_DumpExtraImage(slot, "gpu", gpu_px);
  Debug_Log("GPU check -> set %02d: %d px differ, %d by more than 8; %s", slot, differ, far,
            GpuPpu3ds_CalibrationText());
  char msg[48];
  snprintf(msg, sizeof(msg), "GPU check %02d: %d px off, %d >8", slot, differ, far);
  BottomUi_Toast(msg);
}

uint64_t AudioProf_Now(void) {
  return svcGetSystemTick();
}

static float TicksToMs(u64 ticks) {
  return (float)((double)ticks * 1000.0 / SYSCLOCK_ARM11);
}

// Scene recorder: one shown frame, with what the game was doing.
static void RecordTop(const uint32_t *top, uint32_t frame, u64 t_logic, u64 t_draw) {
  if (!top) return;
  const SceneRecMeta meta = {
    frame, game_state, room_ptr, samus_x_pos, samus_y_pos, TicksToMs(t_logic), TicksToMs(t_draw),
    g_top_by_gpu, g_top_wide,
  };
  SceneRec_AddFrame(top, &meta);
}

static SDL_mutex *g_audio_mutex;
static uint8 *g_audiobuffer, *g_audiobuffer_cur, *g_audiobuffer_end;
static int g_frames_per_block;
static uint8 g_audio_channels;
static SDL_AudioDeviceID g_audio_device;

void RtlApuLock(void) {
  SDL_LockMutex(g_audio_mutex);
}

void RtlApuUnlock(void) {
  SDL_UnlockMutex(g_audio_mutex);
}

// Separate from the audio mutex, which the audio callback holds for a whole
// block: see RtlPushApuState.
static SDL_mutex *g_apu_queue_mutex;

void RtlApuQueueLock(void) {
  SDL_LockMutex(g_apu_queue_mutex);
}

void RtlApuQueueUnlock(void) {
  SDL_UnlockMutex(g_apu_queue_mutex);
}

// Audio thread health, for the periodic log line: how long a callback took against the
// time its buffer lasts (a callback slower than that starves the DSP: broken sound), and
// gaps between callbacks (the thread did not get the CPU in time). Reset by the reader.
static volatile float g_cb_max_ms;
static volatile int g_cb_count, g_cb_slow, g_cb_gaps;
static volatile u64 g_cb_last;

static void SDLCALL AudioCallback(void *userdata, Uint8 *stream, int len) {
  const u64 cb_start = svcGetSystemTick();
  Uint8 *const stream_start = stream;
  const int stream_len = len;
  const float buffer_ms = len * 1000.0f / (44100 * 4);   // stereo s16
  if (g_cb_last && TicksToMs(cb_start - g_cb_last) > buffer_ms * 1.5f) g_cb_gaps++;
  g_cb_last = cb_start;
  if (SDL_LockMutex(g_audio_mutex)) Die("Mutex lock failed!");
  while (len != 0) {
    if (g_audiobuffer_end - g_audiobuffer_cur == 0) {
      u64 t0 = svcGetSystemTick();
      RtlRenderAudio((int16 *)g_audiobuffer, g_frames_per_block, g_audio_channels);
      g_audio_ms = TicksToMs(svcGetSystemTick() - t0);
      g_audiobuffer_cur = g_audiobuffer;
      g_audiobuffer_end = g_audiobuffer + g_frames_per_block * g_audio_channels * sizeof(int16);
    }
    int n = IntMin(len, g_audiobuffer_end - g_audiobuffer_cur);
    if (g_sdl_audio_mixer_volume == SDL_MIX_MAXVOLUME) {
      memcpy(stream, g_audiobuffer_cur, n);
    } else {
      SDL_memset(stream, 0, n);
      SDL_MixAudioFormat(stream, g_audiobuffer_cur, AUDIO_S16, n, g_sdl_audio_mixer_volume);
    }
    g_audiobuffer_cur += n;
    stream += n;
    len -= n;
  }
  SDL_UnlockMutex(g_audio_mutex);
  RetroAch_MixAudio((int16_t *)stream_start, stream_len / 4);   // the achievement sound, if playing
  const float cb_ms = TicksToMs(svcGetSystemTick() - cb_start);
  if (cb_ms > g_cb_max_ms) g_cb_max_ms = cb_ms;
  g_cb_slow += cb_ms > buffer_ms;
  g_cb_count++;
}

int idx_of_btn(enum Button b) {
  switch(b) {
    case BTN_DPAD_U:
      return 0;
    case BTN_DPAD_D:
      return 1;
    case BTN_DPAD_L:
      return 2;
    case BTN_DPAD_R:
      return 3;
    case BTN_SELECT:
      return 4;
    case BTN_START:
      return 5;
    case BTN_A:
      return 6;
    case BTN_B:
      return 7;
    case BTN_X:
      return 8;
    case BTN_Y:
      return 9;
    case BTN_L:
      return 10;
    case BTN_R:
      return 11;
    default:
      // SDL numbers the joystick buttons after the HID key bits, so touching the
      // screen (KEY_TOUCH, bit 20), ZL/ZR and others arrive here too. They are not
      // game buttons: falling off the end of this function used to return garbage
      // that ended up as "D-pad up" on every tap.
      return -1;
  }
}

static void HandleCommand(uint32 j, bool pressed) {
  int idx = idx_of_btn(j);
  if (idx < 0) return;
  j = 1 + idx;
  if (j <= kKeys_Controls_Last) {
    static const uint8 kKbdRemap[] = { 0, 4, 5, 6, 7, 2, 3, 8, 0, 9, 1, 10, 11 };
    if (pressed)
      g_input1_state |= 1 << kKbdRemap[j];
    else
      g_input1_state &= ~(1 << kKbdRemap[j]);
    return;
  }

  // if (j == kKeys_Turbo) {
  //   g_turbo = pressed;
  //   return;
  // }

  // if (!pressed)
  //   return;

  // if (j <= kKeys_Load_Last) {
  //   RtlSaveLoad(kSaveLoad_Load, j - kKeys_Load);
  // } else if (j <= kKeys_Save_Last) {
  //   RtlSaveLoad(kSaveLoad_Save, j - kKeys_Save);
  // } else if (j <= kKeys_Replay_Last) {
  //   RtlSaveLoad(kSaveLoad_Replay, j - kKeys_Replay);
  // } else {
  //   switch (j) {
  //   case kKeys_Reset: RtlReset(1); break;
  //   case kKeys_Pause: g_paused = !g_paused; break;
  //   case kKeys_ReplayTurbo: g_replay_turbo = !g_replay_turbo; break;
  //   default: break;
  //   }
  // }
}

enum {
  kDefaultFullscreen = 0,
  kMaxWindowScale = 10,
  kDefaultFreq = 44100,
  kDefaultChannels = 2,
  kDefaultSamples = 2048,
};

// The ROM is missing or wrong: say so on screen and wait for START.
static void ShowRomError(const RomInfo *info) {
  gfxInitDefault();
  consoleInit(GFX_TOP, NULL);
  printf("Super Metroid 3DS %s\n\n", APP_VERSION);
  printf("%s\n\n", RomLoader_StatusText(info->status));
  printf("Put your ROM in:\n  %s\n\n", ROM_DATA_DIR);
  printf("Needed: Super Metroid (Japan, USA)\n  .smc or .sfc, sha1:\n  %s\n", kRomExpectedSha1);
  if (info->status == ROM_BAD_HASH) {
    printf("\nFound: %s\n  sha1: %s\n", info->name, info->sha1);
    if (info->rejected > 1) printf("  (+%d more rejected)\n", info->rejected - 1);
  }
  printf("\nPress START to exit.\n");
  while (aptMainLoop()) {
    hidScanInput();
    if (hidKeysDown() & KEY_START) break;
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
  }
  gfxExit();
}

// Notes exit progress (see the cleanup at the end of main). Kept open so a step costs
// one write and a flush.
static FILE *g_exit_file;

static void ExitStep(const char *what) {
  if (!g_exit_file) {
    mkdir("debug", 0777);
    g_exit_file = fopen("debug/sm-exit.txt", "w");
    if (!g_exit_file) return;
  }
  fprintf(g_exit_file, "%llu ms: %s\n", (unsigned long long)osGetTime(), what);
  fflush(g_exit_file);
  Debug_Log("exit: %s", what);
  Debug_LogFlush();
}

// Debug log, once a second: settings that changed, and every 5 s the timings and the
// audio thread's health (see AudioCallback).
static void LogPeriodic(const UiPerf *p) {
  static int seconds;
  static int last_speedup = -1, last_gpu = -1, last_audio = -1, last_paused = -1;
  if (g_ui.new3ds_speedup != last_speedup || g_ui.gpu_render != last_gpu || g_ui.audio_on != last_audio ||
      g_ui.paused != last_paused) {
    last_speedup = g_ui.new3ds_speedup, last_gpu = g_ui.gpu_render, last_audio = g_ui.audio_on;
    last_paused = g_ui.paused;
    Debug_Log("settings: cpu %s, renderer %s, audio %s%s", g_ui.new3ds_speedup ? "804" : "268",
              g_ui.gpu_render ? "GPU" : "CPU", g_ui.audio_on ? "on" : "off", g_ui.paused ? ", PAUSED" : "");
  }
  if (++seconds % 5) return;
  Debug_Log("stats: speed %.1f shown %.1f | work %.1f logic %.1f draw %.1f ms | frameskip %s | room %04X",
            p->game_fps, p->fps, p->frame_ms, p->logic_ms, p->draw_ms, g_ui.frameskip ? "on" : "off",
            (unsigned)room_ptr);
  if (g_ui.gpu_render)
  {
    const GpuPpuStats *st = GpuPpu_LastStats();
    Debug_Log("gpu: frames %lu fallback %lu (%s) | build %.1f, wait for GPU %.1f, submit %.1f ms | last frame: "
              "%d surfaces, %d tiles decoded, %d sprites, %d rows composed, %d mode 7 cells decoded",
              (unsigned long)p->gpu_frames, (unsigned long)p->gpu_fallbacks, p->gpu_reason ? p->gpu_reason : "-",
              p->gpu_build_ms, p->gpu_wait_ms, p->gpu_submit_ms, st->surfaces, st->tiles_decoded, st->sprites,
              st->screen_rows_composed, st->m7_cells_decoded);
    Debug_Log("gpu build, last frame: lines+bands %.2f, vram diff %.2f, sprites %.2f, bg %.2f, shadow copy %.2f ms | "
              "%d bands, %d quads",
              TicksToMs(st->t_lines), TicksToMs(st->t_diff), TicksToMs(st->t_sprites), TicksToMs(st->t_bg),
              TicksToMs(st->t_shadow), g_gpu_frame.band_count, g_gpu_frame.quad_count);
  }
  Debug_Log("audio: block %.1f ms (dsp %.1f spc %.1f lock %.1f) | callbacks %d, slowest %.1f ms, slower than "
            "their buffer %d, late starts %d",
            p->audio_ms, p->audio_part_ms[2], p->audio_part_ms[1], p->audio_part_ms[0], g_cb_count, g_cb_max_ms,
            g_cb_slow, g_cb_gaps);
  g_cb_count = g_cb_slow = g_cb_gaps = 0;
  g_cb_max_ms = 0;
}

// #undef main
int main(int argc, char** argv) {
  // Use default config - no config file on 3DS
  ParseConfigFile(NULL);

  g_ppu_render_flags = kPpuRenderFlags_Height240 
                     | kPpuRenderFlags_NewRenderer
                     | kPpuRenderFlags_4x4Mode7;
  g_config.audio_freq = kDefaultFreq;
  g_config.audio_channels = kDefaultChannels;
  g_config.audio_samples = kDefaultSamples;

  RomInfo rom;
  if (RomLoader_Find(&rom) != ROM_OK) {
    ShowRomError(&rom);
    return 1;
  }
  // Saves, save states and dumps use paths relative to the data folder.
  chdir(ROM_DATA_DIR);

  // Initialize SDL
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
    printf("Failed to init SDL: %s\n", SDL_GetError());
    return 1;
  }

  SDL_JoystickEventState(SDL_ENABLE);
  SDL_GameControllerEventState(SDL_ENABLE);

  if (SDL_NumJoysticks() > 0) {
      SDL_GameControllerOpen(0);
  }

  Result rc = romfsInit();
  if (rc)
    while(true);

  Snes *snes = SnesInit(rom.path);

  if(snes == NULL) {
    char buf[600];
    snprintf(buf, sizeof(buf), "Unable to load ROM: %s", rom.path);
    Die(buf);
    return 1;
  }

  // Create window - 3DS top screen
  SDL_Window *window = SDL_CreateWindow(
    kWindowTitle,
    SDL_WINDOWPOS_CENTERED_DISPLAY(0),
    SDL_WINDOWPOS_CENTERED_DISPLAY(0),
    400, 240,
    SDL_WINDOW_SHOWN
  );
  if(window == NULL) {
    printf("Failed to create window: %s\n", SDL_GetError());
    return 1;
  }
  g_window = window;

  // Create renderer - SOFTWARE for 3DS
  g_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  if (g_renderer == NULL) {
    printf("Failed to create renderer: %s\n", SDL_GetError());
    return 1;
  }

  // Create texture
  g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING,
                                g_snes_width, g_snes_height);
  if (g_texture == NULL) {
    printf("Failed to create texture: %s\n", SDL_GetError());
    return 1;
  }

  UiRomInfo ui_rom = { rom.name, rom.sha1, rom.had_header, APP_VERSION };
  BottomUi_Init(&ui_rom);
  RetroAch_Init();
  GameText_Init();

  // Setup audio
  g_audio_mutex = SDL_CreateMutex();
  g_apu_queue_mutex = SDL_CreateMutex();
  if (!g_audio_mutex) Die("No mutex");

  g_spc_player = SpcPlayer_Create();
  SpcPlayer_Initialize(g_spc_player);

  SDL_AudioSpec want = { 0 }, have = { 0 };
  want.freq = 44100;
  want.format = AUDIO_S16;
  want.channels = 2;
  want.samples = 2048;
  want.callback = &AudioCallback;
  g_audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (g_audio_device == 0) {
    printf("Failed to open audio device: %s\n", SDL_GetError());
  } else {
    g_audio_channels = 2;
    g_frames_per_block = (534 * have.freq) / 32000;
    g_audiobuffer = (uint8 *)malloc(g_frames_per_block * have.channels * sizeof(int16));
  }
  // SDL put the audio thread on the system core with a 30 % time limit. The DSP needs
  // about 1.5 ms of CPU per block at 804 MHz, so at 268 MHz (Old 3DS, or the speedup
  // off) it sits right at that limit and the sound breaks up. Ask for more, like mzm:
  // the largest share the system grants.
  static const u32 kCore1Limits[] = { 80, 70, 50 };
  u32 core1_limit = 0;
  Result core1_rc[3] = { 0 };
  for (size_t i = 0; i < sizeof(kCore1Limits) / sizeof(kCore1Limits[0]); i++)
    if (R_SUCCEEDED(core1_rc[i] = APT_SetAppCpuTimeLimit(kCore1Limits[i]))) break;
  APT_GetAppCpuTimeLimit(&core1_limit);

  mkdir("saves", 0755);
  Debug_Init(APP_VERSION);
#if DEBUG_TOOLS
  // Debug builds log from boot, so a playtest always leaves a log behind.
  Debug_LogSetEnabled(true);
#endif
  {
    bool n3ds = false;
    APT_CheckNew3DS(&n3ds);
    Debug_Log("console %s, audio device %s (%d Hz, %d samples)", n3ds ? "New 3DS" : "Old 3DS/2DS",
              g_audio_device ? "open" : "FAILED", have.freq, have.samples);
    Debug_Log("core 1 time limit: asked 80 -> %08lX, 70 -> %08lX, 50 -> %08lX; granted %lu %%",
              (unsigned long)core1_rc[0], (unsigned long)core1_rc[1], (unsigned long)core1_rc[2],
              (unsigned long)core1_limit);
  }

  PpuBeginDrawing(snes->snes_ppu, g_pixels, 256 * 4, 0);
  // PpuBeginDrawing(snes->my_ppu, g_my_pixels, 256 * 4, 0);

  RtlReadSram();

  bool running = true;
  uint32 lastTick = SDL_GetTicks();
  uint32 frameCtr = 0;
  bool audio_running = false;

  UiPerf perf = { .is_new3ds = false, .core1_limit = core1_limit };
  g_gpu_ppu_clock = svcGetSystemTick;
  APT_CheckNew3DS(&perf.is_new3ds);
  u64 fps_window_start = svcGetSystemTick(), frame_start = fps_window_start;
  uint32 logic_window = 0, shown_window = 0;
  uint8 is_replay = 0;
  bool skip_render = false;      // this frame is late: run the logic, draw nothing
  int skipped_in_a_row = 0;
  enum { kMaxSkipInARow = 2 };   // never show fewer than 20 fps

  printf("Super Metroid starting...\n");

  while (running) {
    SDL_Event event;
    RetroAch_Update();

    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_JOYBUTTONDOWN:
        HandleCommand(event.jbutton.button, true);
        break;
      case SDL_JOYBUTTONUP:
        HandleCommand(event.jbutton.button, false);
        break;
      // Touch coordinates arrive normalised to the bottom screen (0..1).
      case SDL_FINGERDOWN:
        BottomUi_TouchDown((int)(event.tfinger.x * 320), (int)(event.tfinger.y * 240));
        break;
      case SDL_FINGERMOTION:
        BottomUi_TouchMove((int)(event.tfinger.x * 320), (int)(event.tfinger.y * 240));
        break;
      case SDL_FINGERUP:
        BottomUi_TouchUp();
        break;
      case SDL_QUIT:
        ExitStep("quit event");
        running = false;
        break;
      }
    }

    bool want_audio = g_ui.audio_on && !g_ui.paused;
    if (want_audio != audio_running) {
      audio_running = want_audio;
      if (g_audio_device)
        SDL_PauseAudioDevice(g_audio_device, !audio_running);
    }
    g_turbo = g_ui.turbo;

    if (g_ui.req_reset) {
      RtlReset(1);
      BottomUi_GameReset();
      RetroAch_GameReset();
    }
    if (g_ui.req_save_state) {
      const bool ok = RtlSaveLoad(kSaveLoad_Save, g_ui.save_slot);
      BottomUi_StateSaved(g_ui.save_slot, ok);
      if (ok) RetroAch_StateSaved(g_ui.save_slot);
    }
    if (g_ui.req_load_state) {
      const bool ok = RtlSaveLoad(kSaveLoad_Load, g_ui.save_slot);
      BottomUi_StateLoaded(g_ui.save_slot, ok);
      if (ok) RetroAch_StateLoaded(g_ui.save_slot);
    }
    // A reset or a loaded state replaces VRAM without going through the PPU's data port,
    // which is how the GPU renderer learns what changed.
    if (g_ui.req_reset || g_ui.req_load_state) {
      GpuPpu_Invalidate();
      GameText_Forget();
    }
    g_ui.req_reset = g_ui.req_save_state = g_ui.req_load_state = false;
    u64 t_logic = 0, t_draw = 0;
    bool presented = false, gpu_presented = false;
    if (!g_ui.paused) {
      // PPU drawing happens inside RtlRunFrame, so decide before running it.
      bool turbo_skip = (g_turbo ^ (is_replay & g_replay_turbo)) && (frameCtr & 0xf) != 0;
      bool draw = !turbo_skip && !(g_ui.frameskip && skip_render);
      // Dumps and frame captures need this frame's CPU-rendered pixels, so the frame is
      // drawn, and drawn by the CPU (the GPU check draws it both ways).
      bool capture = false, dump = g_ui.req_dump, gpu_check = g_ui.req_gpu_check && g_ui.gpu_render;
      g_ui.req_dump = g_ui.req_gpu_check = false;
      if (g_ui.req_frame_dump) {
        g_ui.req_frame_dump = false;
        capture = Debug_FrameCaptureBegin();
        if (!capture) BottomUi_Toast(Debug_LastMessage());
      }
      draw |= capture || dump || gpu_check;
      bool gpu = draw && g_ui.gpu_render && !capture && !dump;
      if (gpu && !GpuPpu3ds_Init()) {
        gpu = g_ui.gpu_render = false;
        BottomUi_Toast("GPU renderer failed to start");
      }
      g_snes->disableRender = !draw;
      g_ppu_line_capture = gpu ? &g_line_capture : NULL;
      // Decided before the frame runs, like the game state the margin depends on. Not
      // from `gpu`: the margin changes game logic (which enemies run), and that must not
      // depend on whether this frame happens to be drawn.
      const int margin = g_ui.gpu_render ? WideMargin() : 0;
      // PIXEL PERFECT also has 8 rows above and 8 below the 224 (SCALED fills the height).
      const int extra = margin && g_ui.pixel_perfect ? 8 : 0;

      g_gpu_ppu_obj_x = margin ? g_rtl_oam_x : NULL;
      g_gpu_ppu_obj_y = margin ? g_rtl_oam_y : NULL;
      g_gpu_ppu_obj_hud = margin ? g_rtl_oam_hud : NULL;
      SmWide_SetView(margin, extra, extra);

      u64 t0 = svcGetSystemTick();
      int inputs = g_input1_state | g_gamepad_buttons;
      Cheats_BeforeFrame();
      is_replay = RtlRunFrame(inputs);
      RetroAch_DoFrame();
      g_ppu_line_capture = NULL;
      if (capture) {
        Debug_FrameCaptureEnd(g_pixels);
        BottomUi_Toast(Debug_LastMessage());
      }
      if (dump) {
        Debug_DumpScreen(g_pixels);
        BottomUi_Toast(Debug_LastMessage());
      }
      Cheats_AfterFrame();
      SmWarp_AfterFrame();
      t_logic = svcGetSystemTick() - t0;
      frameCtr++;
      logic_window++;

      if (draw) {
        t0 = svcGetSystemTick();
        const char *why = NULL;
        const u64 t_build = svcGetSystemTick();
        // The margins this frame ended up with (SmWide leans them off room edges).
        int margin_l, margin_r, hud_x, bg2_dx, rows_top, rows_bottom, hud_y;
        SmWide_Margins(&margin_l, &margin_r, &hud_x, &bg2_dx);
        SmWide_Rows(&rows_top, &rows_bottom, &hud_y);
        GpuPpu_SetMargins(margin_l, margin_r);
        GpuPpu_SetExtraRows(rows_top, rows_bottom);
        GpuPpu_SetHudX(hud_x);
        GpuPpu_SetHudY(hud_y);
        GpuPpu_SetLayerShiftX(1, bg2_dx);
        GpuPpu_SetNarrowBg3Rows(margin_l || margin_r ? kSmWideHudRows : 0);
        GpuPpu_SetNoSpriteWrap(margin_l || margin_r);
        GpuPpu_SetNarrowBg3Map(margin_l || margin_r ? kSmWideMessageBoxMap : -1);
        GpuPpu_SetWindow2Extent(margin_l || margin_r ? SmWide_Window2Extent() : NULL);
        GpuPpu_SetMode7UnderHud((margin_l || margin_r) && SmWide_Mode7());
        const bool built = gpu && GpuPpu_BuildFrame(g_snes->ppu, &g_line_capture, &g_gpu_frame, &why);
        if (built) {
          SmWide_AddMasks(&g_gpu_frame);
          perf.gpu_build_ms += (TicksToMs(svcGetSystemTick() - t_build) - perf.gpu_build_ms) * 0.1f;
          static uint32_t overlay_px[64 * 64];
          GpuPpu3ds_SetOverlay(BottomUi_DrawOverlayInto(overlay_px, 64, 64, &perf) ? overlay_px : NULL);
          static uint32_t toast_px[512 * 64];
          GpuPpu3ds_SetToast(BottomUi_DrawTopToastInto(toast_px) ? toast_px : NULL);
          GpuPpu3ds_DrawAndPresent(&g_gpu_frame, g_ui.pixel_perfect);
          float wait_ms, submit_ms;
          GpuPpu3ds_LastTimes(&wait_ms, &submit_ms);
          perf.gpu_wait_ms += (wait_ms - perf.gpu_wait_ms) * 0.1f;
          perf.gpu_submit_ms += (submit_ms - perf.gpu_submit_ms) * 0.1f;
          gpu_presented = true;
          perf.gpu_frames++;
          if (gpu_check) GpuCheck();
        } else {
          if (gpu) {
            // The GPU path refused this frame: draw it with the CPU from the capture.
            perf.gpu_fallbacks++;
            perf.gpu_reason = why;
            ppu_replayLines(g_snes->ppu, &g_line_capture, 1, g_line_capture.last_line);
            if (gpu_check) BottomUi_Toast(why);
          }
          // The GPU may still be presenting its last frame into the framebuffer.
          if (g_top_by_gpu) GpuPpu3ds_WaitIdle();
          DrawPpuFrame(g_ui.pixel_perfect, g_top_wide);
        }
        g_top_by_gpu = gpu_presented;
        g_top_wide = built && g_gpu_frame.x0 < 0;
        t_draw = svcGetSystemTick() - t0;
        presented = true;
      }
    } else {
      presented = true;   // keep the bottom screen alive while paused
    }

    perf.frames = frameCtr;
    perf.audio_ms = g_audio_ms;
    perf.audio_part_ms[0] = TicksToMs(g_audio_prof_last[kAudioProf_LockWait]);
    perf.audio_part_ms[1] = TicksToMs(g_audio_prof_last[kAudioProf_SpcLoop]);
    perf.audio_part_ms[2] = TicksToMs(g_audio_prof_last[kAudioProf_DspCycles]);
    perf.audio_part_ms[3] = TicksToMs(g_audio_prof_last[kAudioProf_Resample]);
    perf.gpu_calibration = GpuPpu3ds_CalibrationText();
    if (!g_ui.paused) {
      // Light smoothing so the numbers are readable.
      perf.logic_ms += (TicksToMs(t_logic) - perf.logic_ms) * 0.1f;
      if (t_draw) perf.draw_ms += (TicksToMs(t_draw) - perf.draw_ms) * 0.1f;
    }

    // A skipped frame must not touch or swap the (double buffered) screens:
    // swapping without drawing would show the frame before last. While the GPU
    // renderer owns the top screen, citro3d swaps it and only the bottom is ours.
    bool swapped = false;
    // Scene recorder: the frame as shown, from the framebuffer before the swap or from
    // the GPU's top target.
    const bool record = presented && !g_ui.paused && SceneRec_WantFrame();
    if (presented && !g_top_by_gpu) {
      swapped = true;
      BottomUi_DrawTopOverlay(&perf);
      if (record) RecordTop((const uint32_t *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), frameCtr, t_logic, t_draw);
      BottomUi_Frame(&perf);
      gfxFlushBuffers();
      gfxSwapBuffers();
      UiDraw_Swapped();
      shown_window++;
    } else {
      if (presented) {
        swapped = true;
        shown_window++;
      }
      if (record) RecordTop(GpuPpu3ds_ReadTop(), frameCtr, t_logic, t_draw);
      if (BottomUi_Frame(&perf)) {
        // Nothing new on the top screen from us (skipped frame, or the
        // GPU presents it), but the UI changed: swap the bottom screen only.
        gfxFlushBuffers();
        gfxScreenSwapBuffers(GFX_BOTTOM, false);
        UiDraw_Swapped();
        swapped = true;
      }
    }

    // Measure how long the whole iteration took, before the pacing delay.
    u64 now = svcGetSystemTick();
    float work_ms = TicksToMs(now - frame_start);
    perf.frame_ms += (work_ms - perf.frame_ms) * 0.1f;
    if (!g_ui.paused)
      Debug_PerfFrame(TicksToMs(t_logic), TicksToMs(t_draw), perf.audio_ms, work_ms, presented, perf.audio_part_ms);
    float window_ms = TicksToMs(now - fps_window_start);
    if (window_ms >= 1000.0f) {
      perf.fps = shown_window * 1000.0f / window_ms;
      perf.game_fps = logic_window * 1000.0f / window_ms;
      fps_window_start = now;
      logic_window = shown_window = 0;
      LogPeriodic(&perf);
    }

    if (g_ui.paused) {
      if (swapped) {
        gspWaitForVBlank();
        UiDraw_VBlankSeen();
      } else {
        SDL_Delay(16);
      }
      frame_start = svcGetSystemTick();
      continue;
    }

    // With time to spare, let the display pace us. Two swaps inside one vblank
    // leave the next draw in the buffer that is being scanned out, which shows
    // as torn, half-painted screens (seen first on the bottom UI).
    if (swapped && work_ms < 15.0f) {
      gspWaitForVBlank();
      UiDraw_VBlankSeen();
      lastTick = SDL_GetTicks();   // locked to the display: drop accumulated drift
      skip_render = false;
      skipped_in_a_row = 0;
      frame_start = svcGetSystemTick();
      continue;
    }

    // Frame delay for 60 fps
    static const uint8 delays[3] = { 17, 17, 16 };
    lastTick += delays[frameCtr % 3];
    uint32 curTick = SDL_GetTicks();

    if (lastTick > curTick) {
      uint32 delta = lastTick - curTick;
      if (delta > 500) {
        lastTick = curTick - 500;
        delta = 500;
      }
      SDL_Delay(delta);
      skip_render = false;
      skipped_in_a_row = 0;
    } else {
      // Already late for the next frame: skip drawing it, but not forever.
      skip_render = (curTick - lastTick) >= 4 && skipped_in_a_row < kMaxSkipInARow;
      skipped_in_a_row = skip_render ? skipped_in_a_row + 1 : 0;
      if (curTick - lastTick > 500) lastTick = curTick;
    }
    frame_start = svcGetSystemTick();
  }

  // Cleanup. Each step is noted in debug/sm-exit.txt first: closing from the HOME menu
  // has been seen to hang on "Closing software", and the file shows the step it hung in.
  ExitStep("loop left");
  RetroAch_Shutdown();
  ExitStep("achievements done");
  GpuPpu3ds_Exit();
  ExitStep("gpu done");
  BottomUi_Exit();
  ExitStep("ui done");
  SDL_PauseAudioDevice(g_audio_device, 1);
  ExitStep("audio paused");
  SDL_CloseAudioDevice(g_audio_device);
  ExitStep("audio closed");
  SDL_DestroyMutex(g_audio_mutex);
  SDL_DestroyMutex(g_apu_queue_mutex);
  free(g_audiobuffer);
  SDL_DestroyTexture(g_texture);
  SDL_DestroyRenderer(g_renderer);
  SDL_DestroyWindow(window);
  ExitStep("window destroyed");
  SDL_Quit();
  ExitStep("SDL_Quit done, returning");
  if (g_exit_file) fclose(g_exit_file);

  return 0;
}
