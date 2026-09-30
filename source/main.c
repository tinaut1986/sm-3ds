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

#include "bottom_ui.h"
#include "debug_tools.h"
#include "rom_loader.h"
#include "version.h"

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

// Copies the 256x224 PPU output to the top screen, scaled to 274x240 (nearest,
// aspect-correct) and rotated: the 3DS framebuffer is column-major with the
// origin at the bottom-left. Source pixels are 0x00RRGGBB, the framebuffer
// wants R,G,B,A from the high byte down, so one shift does the conversion.
static void DrawPpuFrame(void) {
    enum { SRC_W = 256, SRC_H = 224, FB_W = 400, FB_H = 240, DST_W = 274 };
    static uint16_t xmap[DST_W];
    static const uint32_t *rows[FB_H];   // source row for each destination row
    static bool init;
    if (!init) {
        const float scale = (float)FB_H / (float)SRC_H;
        for (int dx = 0; dx < DST_W; dx++) xmap[dx] = (uint16_t)((int)(dx / scale) % SRC_W);
        for (int dy = 0; dy < FB_H; dy++) {
            int sy = (int)(dy / scale);
            rows[dy] = (const uint32_t *)g_pixels + (sy < SRC_H ? sy : SRC_H - 1) * SRC_W;
        }
        init = true;
    }

    uint32_t *fb = (uint32_t *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    const int x_off = (FB_W - DST_W) / 2;
    for (int dx = 0; dx < DST_W; dx++) {
        uint32_t *col = fb + (x_off + dx) * FB_H + (FB_H - 1);   // dy = 0 is the last word
        const int sx = xmap[dx];
        for (int dy = 0; dy < FB_H; dy++)
            col[-dy] = (rows[dy][sx] << 8) | 0xFFu;
    }
}

uint64_t AudioProf_Now(void) {
  return svcGetSystemTick();
}

static float TicksToMs(u64 ticks) {
  return (float)((double)ticks * 1000.0 / SYSCLOCK_ARM11);
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

static void SDLCALL AudioCallback(void *userdata, Uint8 *stream, int len) {
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
  }
}

static void HandleCommand(uint32 j, bool pressed) {
  j = 1 + idx_of_btn(j);
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
  if (!BottomUi_Init(&ui_rom))
    Warning("romfs:/font.bmp missing or invalid: bottom screen text disabled");

  // Setup audio
  g_audio_mutex = SDL_CreateMutex();
  g_apu_queue_mutex = SDL_CreateMutex();
  if (!g_audio_mutex) Die("No mutex");

  g_spc_player = SpcPlayer_Create();
  SpcPlayer_Initialize(g_spc_player);

  SDL_AudioSpec want = { 0 }, have;
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

  mkdir("saves", 0755);
  Debug_Init(APP_VERSION);

  PpuBeginDrawing(snes->snes_ppu, g_pixels, 256 * 4, 0);
  // PpuBeginDrawing(snes->my_ppu, g_my_pixels, 256 * 4, 0);

  RtlReadSram();

  bool running = true;
  uint32 lastTick = SDL_GetTicks();
  uint32 frameCtr = 0;
  bool audio_running = false;

  UiPerf perf = { .is_new3ds = false };
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

    if (g_ui.req_reset) RtlReset(1);
    if (g_ui.req_save_state) RtlSaveLoad(kSaveLoad_Save, g_ui.save_slot);
    if (g_ui.req_load_state) RtlSaveLoad(kSaveLoad_Load, g_ui.save_slot);
    g_ui.req_reset = g_ui.req_save_state = g_ui.req_load_state = false;
    if (g_ui.req_dump) {
      Debug_DumpScreen(g_pixels);
      BottomUi_Toast(Debug_LastMessage());
      g_ui.req_dump = false;
    }

    u64 t_logic = 0, t_draw = 0;
    bool presented = false;
    if (!g_ui.paused) {
      // PPU drawing happens inside RtlRunFrame, so decide before running it.
      bool turbo_skip = (g_turbo ^ (is_replay & g_replay_turbo)) && (frameCtr & 0xf) != 0;
      bool draw = g_ui.render_on && !turbo_skip && !(g_ui.frameskip && skip_render);
      g_snes->disableRender = !draw;

      u64 t0 = svcGetSystemTick();
      int inputs = g_input1_state | g_gamepad_buttons;
      is_replay = RtlRunFrame(inputs);
      t_logic = svcGetSystemTick() - t0;
      frameCtr++;
      logic_window++;

      if (draw) {
        t0 = svcGetSystemTick();
        DrawPpuFrame();
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
    if (!g_ui.paused) {
      // Light smoothing so the numbers are readable.
      perf.logic_ms += (TicksToMs(t_logic) - perf.logic_ms) * 0.1f;
      if (t_draw) perf.draw_ms += (TicksToMs(t_draw) - perf.draw_ms) * 0.1f;
    }

    // A skipped frame must not touch or swap the (double buffered) screens:
    // swapping without drawing would show the frame before last.
    bool swapped = false;
    if (presented) {
      swapped = true;
      BottomUi_DrawTopOverlay(&perf);
      BottomUi_Frame(&perf);
      gfxFlushBuffers();
      gfxSwapBuffers();
      shown_window++;
    } else if (BottomUi_Frame(&perf)) {
      // Nothing new on the top screen (skipped frame, or PPU render off), but
      // the UI changed: swap the bottom screen only.
      gfxFlushBuffers();
      gfxScreenSwapBuffers(GFX_BOTTOM, false);
      swapped = true;
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
    }

    if (g_ui.paused) {
      if (swapped) gspWaitForVBlank();
      else SDL_Delay(16);
      frame_start = svcGetSystemTick();
      continue;
    }

    // With time to spare, let the display pace us. Two swaps inside one vblank
    // leave the next draw in the buffer that is being scanned out, which shows
    // as torn, half-painted screens (seen first on the bottom UI).
    if (swapped && work_ms < 15.0f) {
      gspWaitForVBlank();
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

  // Cleanup
  BottomUi_Exit();
  SDL_PauseAudioDevice(g_audio_device, 1);
  SDL_CloseAudioDevice(g_audio_device);
  SDL_DestroyMutex(g_audio_mutex);
  SDL_DestroyMutex(g_apu_queue_mutex);
  free(g_audiobuffer);
  SDL_DestroyTexture(g_texture);
  SDL_DestroyRenderer(g_renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
