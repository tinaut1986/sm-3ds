# Super Metroid 3DS: action plan

Living document. Read it at the start of every session; update it at the end.

**Active release branch:** `release/v0.1.0` (no tags yet).

- **Goal:** a native 3DS port of Super Metroid that is completable start to
  finish, runs at 60 fps on New 3DS and as close as possible on Old 3DS/2DS,
  and has stereoscopic 3D with per-layer depth, like `../mzm`.
- **Starting point:** `CharlesAverill/sm-3ds` (forked as `tinaut1986/sm-3ds`),
  which wraps `snesrev/sm` in a thin SDL2 frontend. Upstream claims ~50 fps on
  hardware (model not stated) and unreliable saves on hardware.

## How work is tracked

- **This file** is the roadmap: phases, task specs with acceptance criteria,
  decisions. It is the source of truth for "what next" and is what a new
  Claude session reads.
- **GitHub issues** (to be enabled on the fork) are for bugs found by playing:
  things with a repro, screenshots, a console model. Link the issue from the
  task here when a task spawns from one; do not duplicate specs into issues.
- Larger design work gets its own note in `docs/` (as mzm does, e.g.
  `../mzm/docs/3ds-renderer-perf-plan.md`) and is linked from its task.

Task format: `- [ ] **ID** title`, then *Spec* (what) and *Done when*
(verifiable criteria). IDs are stable; never renumber.

---

## Known facts about the upstream code (verified 2026-09-30)

- `source/main.c` (~370 lines) is the whole 3DS frontend. It:
  - uses SDL2 (software renderer) for init, input and audio; video bypasses
    SDL and writes pixels to `gfxGetFramebuffer` by hand, with a **float
    divide per pixel** for 256x224 -> 274x240 scaling, and repeats the same
    copy to the **bottom screen** every frame. Both are obvious CPU sinks.
  - loads the ROM from `romfs:/sm.smc`: the ROM is baked into the app, so
    upstream builds are not distributable.
  - creates saves with `mkdir("saves")`, a relative path: the likely cause of
    "saves work in emulator, not on hardware".
  - has save/load/replay/reset hotkeys commented out.
- `sm/` was a submodule of `CharlesAverill/sm-3ds-lib` (snesrev/sm main + 4
  commits: build options for native/emulated, hard-coded version, no double
  frame check). It is now vendored as a plain directory from its `d4e4f42`.
- License: snesrev/sm is MIT (GitHub shows "Other" only because the Opus BSD
  text is appended). Same terms as sm-3ds; keep `sm/LICENSE.txt`.
- snesrev/sm upstream is dormant since 2023-04 (`main`); branches `devel` and
  `stable` exist and are older. Its README calls it "early version, has bugs".
- The game uses Mode 7 in a few scenes (Ceres, `QueueMode7Transfers` in
  `sm_80.c`), HDMA for per-scanline effects (FX layers, heat, gradients,
  windows) and colour math. These are the hard parts for a GPU renderer.
- Audio is a C port of the SPC700 sound driver plus an emulated S-DSP
  (`spc_player.c`, `snes/dsp.c`), not native like mzm's. It is heavy.

---

## Reuse map from `../mzm`

Paths are relative to `../mzm`. "As is" means game-agnostic; "adapt" means the
structure carries over but GBA-specific parts must be rewritten for SNES.

| Area | mzm source | Reuse |
|---|---|---|
| Build/packaging | `platform/3ds/Makefile`, `tools/build_3ds.py`, `platform/3ds/cia/` | As is (targets `cia`, `ftp`, `print-version`, `test`, portlib check) |
| Versioning from git | `platform/3ds/Makefile` (`print-version`) | As is |
| CI + beta/stable channel | `.github/workflows/build-release.yml`, `../mzm/CLAUDE.md` "Release process" | As is, rename paths |
| ROM from SD + sha1 check | `platform/3ds/source/platform_3ds_minimal.c`, `docs/3ds-port-rom-loading.md` | Adapt (SNES ROM, no header) |
| Self-updater | `port_updater_3ds.c`, `port_updater_parse.c` (+ host test) | As is |
| Emulator test script | `tools/run_azahar_test.sh` | As is |
| Perf profiling | `tools/perf_report.py`, `docs/3ds-renderer-perf-plan.md` | As is / method |
| Debug dumps over FTP | `docs/3ds-debug-tools.md` | Method; dump formats change |
| New3DS clock + L2, frame pacing | `platform_3ds_minimal.c`, `main_3ds.c` | As is |
| Audio output via NDSP | `port_mzm_audio_3ds.c` | Adapt: output path yes, mixer no (SNES DSP instead) |
| GPU renderer (citro3d, tile atlas, CPU fallback) | `port_gpu_renderer.c`, `platform_gpu_3ds.c`, `port_ppu_mzm.c`, `docs/3ds-port-gpu-renderer-status-*.md` | Adapt: architecture and citro3d code yes; register/VRAM decoding must be rewritten for SNES PPU |
| Stereo depth as a pure, host-tested function | `port_stereo_depth.h/.c`, `tests/stereo_depth_test.c` | Adapt: same design, SNES inputs (BG priorities, OBJ priority, mode) |
| Per-sprite depth overrides | `port_sprite_depth_oam.c`, `port_sprite_depth.inc` | Adapt: SNES OAM, SM enemy IDs |
| Layer fixes / workbench | `port_layer_fixes.c`, `tools/layer-workbench/` | Idea only; data is MZM-specific |
| Bottom screen UI | `port_bottom_ui_3ds.c` | Adapt the frame (map view, items, touch); data from SM RAM |
| Bezel | `port_gba_bezel.c` | Adapt (new art, 8:7 area) |
| RetroAchievements | `port_retroachievements_3ds.c`, `tools/gen_ra_iwram_map.py` | Adapt: network/toasts/badges as is; memory map to SNES WRAM. Hardcore stays off (unofficial port) |
| Save states | `port_save_state.c` | Idea only. snesrev already has snapshot code in `sm_cpu_infra.c`/`sm_rtl.c` |
| WIDE view | `port_wide_view.c` | Probably not needed: snesrev has `extended_aspect_ratio` |

Lessons from mzm that apply directly:

- Get 2D correct and fast first, stereo second.
- Keep depth decisions in one pure function with host tests; every stereo bug
  in mzm was either one object split across planes or a depth contradicting
  2D occlusion.
- Keep port code out of the game code where possible; read game state from
  the port side (mzm: `savestate-layout-compat`).
- A plain build compiles the debug menu out; clean-build when toggling debug
  flags.

---

## Phase 0: baseline and ownership

- [x] **P0.1** Own the game code.
  Done 2026-09-30: vendored into `sm/` (see decisions log).
- [ ] **P0.2** Build upstream and run it.
  *Spec:* `make sdl && make -j FULL_NATIVE=1 cia`, with a locally supplied
  ROM in `romfs/` (never committed).
  *Done when:* it boots in Azahar and on hardware.
  Status: builds locally (needed a `<sys/stat.h>` include for current GCC).
  2026-09-30: the CIA with the ROM baked in booted on a New 3DS (no bottom
  screen at all). Emulator and Old 3DS not tried yet.
- [ ] **P0.3** Measure the baseline.
  *Spec:* FPS in fixed spots (Ceres intro, Landing Site, Brinstar, a Norfair
  heat room, Maridia water) on Old 3DS/2DS and New 3DS, with and without audio
  and `FULL_NATIVE`. Record in a table below.
  *Done when:* table filled in.
- [ ] **P0.4** Establish game-logic correctness on PC.
  *Spec:* build the PC version from `sm/` on Linux; play or replay with the
  native-vs-ROM comparison on; note mismatches. Check whether snesrev's
  `devel`/`stable` branches or active forks of snesrev/sm carry fixes that
  `sm/` lacks.
  *Done when:* a short list of known game-logic gaps exists in the decisions
  log (may be empty).

## Phase 1: platform foundations (2D, no stereo)

- [ ] **P1.1** ROM from SD, never bundled. Folder
  `sdmc:/3ds/Super Metroid 3DS/`, any `.smc`/`.sfc`, strip a 512-byte copier
  header if present, verify sha1, clear error screen otherwise. Remove the ROM
  from `romfs/`. *Done when:* CIA built without a ROM boots with the ROM on SD.
  Status 2026-09-30: implemented on `feat/rom-from-sd-bottom-ui`
  (`source/rom_loader.c`, error screen in `main.c`); CIA is 2.2 MB without the
  ROM. Not yet run on hardware. The emulator core already skips a 512-byte
  header; the loader also strips it before hashing.
- [ ] **P1.2** Saves on SD with absolute paths (same folder), SRAM flushed on
  save and on exit/home menu. *Done when:* save at a station, power off, power
  on, continue works on hardware.
- [ ] **P1.3** Replace SDL2 with libctru directly: `hid` input, NDSP audio
  (from mzm), citro3d presentation. Drop the `SDL` submodule.
  *Done when:* same features as upstream, SDL gone, FPS not worse than P0.3.
- [ ] **P1.4** Present the frame on the GPU: upload the 256x224 PPU output as
  a texture, scale with citro3d; stop drawing the game on the bottom screen.
  *Done when:* no per-pixel CPU copy remains in the frontend.
- [ ] **P1.5** New 3DS 804 MHz + L2, frame pacing, FPS/perf overlay (from mzm).
  Status 2026-09-30: 804 MHz is switched on at boot on New 3DS (toggle in the
  Options tab); FPS/timing overlay and bottom-screen status exist
  (`source/bottom_ui.c`). Frame pacing is still upstream's SDL_Delay loop.
- [ ] **P1.6** Build/CI/release: copy mzm's Makefile targets, git-derived
  version, `build-release.yml` with beta/stable channel, CIA-only release, a
  README install section. *Done when:* a tag on a release branch produces a
  "Beta" GitHub release with the CIA.
  Status: workflow, branch model and `tools/bin` done 2026-09-30 (see
  CLAUDE.md); not exercised by a real tag yet. Pending: version from git
  (still static in `resources/AppInfo`), `ftp`/`print-version` targets,
  README rewrite. Tagging before P1.1 would publish a CIA that cannot find
  the ROM.
- [ ] **P1.7** Controls and options: remappable buttons, in-game reset,
  pause/options menu, config file on SD.
  Status 2026-09-30: touch tabs Status/Options/Debug with pause, turbo, audio,
  FPS overlay, save state slots 0-9, reset. No remap, no config file yet.

- [ ] **P1.8** Debug tooling like mzm's (`../mzm/docs/3ds-debug-tools.md`):
  log to SD with marks, screen dumps (top framebuffer, PPU VRAM/CGRAM/OAM/regs,
  WRAM), Samus/room state dump, all into `debug/` in the data folder and
  fetchable over FTP. Needs a real crash handler too: Luma's "generic" dumps
  carry no useful stack, so log to SD before risky calls and hook asserts.
  *Done when:* a crash or a visual bug can be diagnosed from files on the SD.
  Status 2026-09-30: first cut in `source/debug_tools.c`, all under `debug/`
  in the data folder, reached from the Debug tab: log to SD with marks
  (`sm-log-NN.txt`), screen dump set (`sm-dump-NN-{top.rgb,vram,cgram,oam,
  highoam,wram}.bin`, `-ppu.txt`, `-game.txt`; top.rgb is 256x240 RGB8),
  frame-time recorder (`sm-perf-NN.csv`, up to 3600 frames, per-frame logic/
  draw/audio/work ms, game state, area, room) and `__assert_func` replaced so an
  assert leaves `sm-crash.txt` before aborting. Ten rotating slots each; order
  by mtime. Tested on the host only; not yet on hardware. Missing: built-in
  viewer tools on the PC side (`tools/`), HDMA table dump, warp/teleport,
  scene recorder, compile-time gating like mzm's `DEBUG_TOOLS=1`.
- [ ] **P1.9** Bottom UI in the style of mzm's (`port_bottom_ui_3ds.c`, 5.5k
  lines, citro2d), rewritten compactly for SM data, in stages. Current
  `source/bottom_ui.c` is the stopgap. Keep all drawing behind a few
  primitives (rect, text, icon) so it can move to citro2d with P1.3/P1.4.
  - **A. Framework + Status:** DONE 2026-09-30 (`ui_draw.c` primitives, Status tab
    with energy/tanks/reserve, ammo bars, item and beam grids, area/room/time,
    raw boss bits; host preview in `tools/ui-preview/`). Not yet seen on hardware.
    Original text: tab bar with icons, items/equipment, energy,
    ammo, boss and area progress, read from SM RAM (`variables.h`).
  - **B. Cheats (god mode etc.):** DONE 2026-09-30 (`cheats.c`: god mode,
    infinite ammo, all items/beams, max ammo/energy, full heal; refill before and
    after each frame, only inside a room). Not yet seen on hardware. HUD icons
    for newly given items, and the suit palette, may need a refresh call: check.
    Original text: write RAM after each `RtlRunFrame`: refill
    health/reserves and ammo for god mode, give items/beams/suits, set tanks,
    unlock map. Must also be recorded with `StateRecorder_RecordPatchByte` when
    replay/save states are involved (see `RtlCheat` in `sm_rtl.c`).
  - **C. Map tab:** DONE 2026-09-30, pending hardware check (`sm_map.c`, MAP
    tab; see the decisions log for the data layout). Original text: live map from `map_tiles_explored` (RAM `$7F7`) and the room
    headers in ROM (area, map x/y, width, height); Samus marker; area switch;
    zoom/pan.
  - **D. Warp:** DONE 2026-09-30 (`sm_warp.c`, WARP HERE on the MAP tab; host
    regression `tools/warp-test/run.sh`: 255 of 262 rooms work, 6 have no door
    into them, 1 (`D408`) ends up in `D340`). Not yet seen on hardware. Original text: room list by area, and tap-on-map to warp. Like mzm, warp to a
    *door*, not a room: set `door_def_ptr` to a door definition whose
    destination is the target room and let the game run its own transition
    (`door_transition_*`). Needs a door-def scan of bank `$83`. Risk: transition
    state consistency; test on hardware.
  - **E. Debug tab:** what exists now plus HDMA/PPU dumps and the scene tools.
  - Gating: debug tabs stay in every build until the first stable release, then
    go behind `DEBUG_TOOLS=1` like mzm.

## Phase 2: performance (target 60 fps on Old 3DS)

- [ ] **P2.1** Profile. Port mzm's perf instrumentation; split frame time into
  game logic, PPU, audio, present. Write `docs/perf.md` with the numbers.
- [x] **P2.0** Cheap CPU wins found by profiling (2026-09-30).
  `snes_handle_pos_stuff` was 37-52 % of the frame on the host profile: it ran
  154k times per frame, once per 2 master cycles, and only acts at hPos 0, 512
  and 1024. `snes_handle_scanline` visits just those. Verified identical
  (RAM, VRAM, CGRAM, OAM, pixels, audio hash) against the old loop for 4000
  frames on the host; 5.0 s -> 3.4 s there. Also: table-driven top-screen
  copy (5.0 -> 2.8 ms on a New 3DS).
- [ ] **P2.2** Audio off the main thread: run the SPC/DSP on the syscore
  (Old 3DS) or core 2 (New 3DS), fed by a ring buffer. Evaluate cheaper DSP
  paths (interpolation, echo) behind an option if still too slow.
- [ ] **P2.3** GPU PPU renderer, design first (`docs/gpu-ppu-design.md`):
  tiles/palettes to a texture atlas, BG layers and OBJ as quads, priorities as
  draw order/depth, colour math as blending, HDMA as per-scanline register
  tables (split strips or shader lookup), windows via stencil/scissor.
  Hybrid like mzm: fall back to the CPU PPU for frames with unsupported state
  (Mode 7, exotic windows) and report which state caused it.
- [ ] **P2.4** Implement P2.3 incrementally; a frame-diff tool against the
  CPU PPU (like mzm's `tests/rec_render.c`, `tools/compare_render.py`).
  *Done when:* gameplay rooms render on the GPU pixel-identical to the CPU
  path, and Old 3DS reaches the target in the P0.3 spots.

## Phase 3: stereoscopic 3D

- [ ] **P3.1** Depth model as a pure function of SNES PPU state (BG mode,
  per-layer and per-tile priority, OBJ priority, which layer carries HUD/FX),
  with exhaustive host tests like `../mzm/platform/3ds/tests/stereo_depth_test.c`.
- [ ] **P3.2** Wire depth into the GPU renderer: HUD to the front plane,
  Samus/enemies at play plane, BG1 foreground, BG2 mid, BG3 FX/backdrop far.
- [ ] **P3.3** Per-sprite and per-room overrides (enemy IDs, bosses, doors),
  a debug depth tint like mzm's.
- [ ] **P3.4** Non-gameplay screens: title, file select, map/pause, cutscenes
  (flat or with deliberate depth).

## Phase 4: features

- [ ] **P4.1** Bottom screen: live map, items/equipment, touch shortcuts
  (e.g. item select, morph).
- [ ] **P4.2** Bezel/borders for the unused top-screen area.
- [ ] **P4.3** Self-updater.
- [ ] **P4.4** RetroAchievements (softcore only).

## Phase 5: completion

- [ ] **P5.1** Full 100% playthrough on hardware, bugs filed as issues.
- [ ] **P5.2** Any-% and known sequence breaks (wall jumps, shinespark,
  mockball) behave like the original.
- [ ] **P5.3** First stable release (minor bump; ask first).

---

## Baseline measurements (P0.3)

| Spot | Old 3DS | New 3DS | Notes |
|---|---|---|---|
| Ceres intro | | | |
| Landing Site | | | |
| Brinstar | | | |
| Norfair heat room | | | |
| Maridia water | | | |

## Decisions log

- 2026-09-30: Base on `CharlesAverill/sm-3ds` rather than a raw fork of
  `snesrev/sm`, because it already has a working 3DS toolchain, audio and
  input.
- 2026-09-30: Tracking = this file for roadmap/specs, GitHub issues for
  playtest bugs. No heavier spec framework.
- 2026-09-30: Game code vendored into `sm/` instead of a submodule, so the
  whole port lives in one repo (`tinaut1986/sm-3ds`) and game fixes are plain
  commits here. Pulling later snesrev changes, if any ever appear, is a manual
  diff. The temporary `tinaut1986/sm` fork was deleted.
- 2026-09-30: Branch/release model copied from mzm: `main` stable,
  `release/vX.Y.Z` accumulates, topic branches merge back `--no-ff`, tags
  trigger the CIA build (Beta unless reachable from `main`). First line:
  `release/v0.1.0`, matching the version already in `resources/AppInfo`.
- 2026-09-30: Bottom screen is software-drawn straight into the framebuffer
  with `romfs/font.bmp` (8x8 ASCII grid), not citro2d like mzm: the game still
  goes through SDL, and this needs no GPU state. Redraws only on change or
  every 15 frames (the bottom screen is double buffered, so two frames per
  change). Revisit when P1.3/P1.4 move presentation to citro3d. Touch comes
  from SDL finger events, because calling `hidScanInput` ourselves would make
  SDL miss button edges.
- 2026-09-30: ROM lives in `sdmc:/3ds/Super Metroid 3DS/` (any `.smc`/`.sfc`,
  first one whose headerless sha1 matches). Only the JU ROM is accepted; a
  translation-patched ROM is rejected on purpose until we decide how to handle
  those.
- 2026-09-30: Load state crashed on the console: `StateRecorder_Load` asserted a
  hard-coded state size (275493, x86-64). The 3DS writes 275559, so `assert`
  called `abort()`. Now computed at runtime; incompatible states are refused.
  Found by loading the console's `save0.sav` in a host ASAN harness, not from
  the Luma dump (a "generic" dump only has the registers of an unrelated thread).
- 2026-09-30: Everything is still CPU: game logic, the software PPU
  (`sm/src/snes/ppu.c`), the SPC/DSP audio and a per-pixel copy to the top
  framebuffer. Adaptive frameskip keeps game speed but does not make it
  cheaper; the next data point is the timing split shown on the Status tab.

- 2026-09-30: Decided NOT to optimise the software PPU drawing: the GPU
  renderer (P2.3) replaces it, and the CPU PPU only remains as a fallback. The
  work that matters for Old 3DS is what the GPU cannot take: game logic,
  emulator stepping and the DSP audio (~4.9 ms per block on a New 3DS).
  Baseline before P2.0 on a New 3DS, Landing Site, 1655 frames: logic 9.2 ms
  without PPU drawing, 19.0 ms with it, 58 % of frames skipped.
- 2026-09-30: After P2.0, New 3DS (804 MHz), Landing Site, 2025 frames, no
  frame needed skipping: logic+PPU 11.4 ms avg (was 19.0), top copy 2.8 ms,
  work per frame 15.3 ms avg / 23.1 p95 / 28.8 max, 22 % of frames over 16.7 ms
  (absorbed by pacing, so it still shows 60). Headroom ~1.4 ms: nothing to spare
  for Old 3DS. Audio block 4.8 ms on its own thread. Raw CSV not committed.
- 2026-09-30: Audio findings (New 3DS, 1461 frames, all on): of the 4.97 ms audio
  block, DSP cycles are 4.69, SPC driver loop 0.08, resample 0.15, mutex wait
  ~0. SDL already runs the audio thread on the system core (core 1, 30 % CPU
  cap), so the 4.7 ms wall is ~1.5 ms of CPU: the 90x gap to the host (54 us)
  was mostly that cap. The real cost to the game thread was the lock: the
  audio callback holds the APU mutex for a whole block and the game thread
  took it every frame in RtlPushApuState, so it stalled up to a block (logic
  p95 12 ms with audio on vs 1.1 with it off). Fix: separate small lock for the
  port queue (RtlApuQueueLock); audio output verified bit-identical on the host.
  The DSP is still the budget problem for Old 3DS (30 % of core 1 = ~5 ms per
  frame, and the DSP alone is ~1.5 ms of CPU on New 3DS).
- 2026-09-30: Bottom screen tearing ("flashes", half-painted triangles): the loop
  never waited for vblank, so two swaps in one vblank left the next draw in the
  buffer being scanned out. Now `gspWaitForVBlank` after a swap when the frame
  took < 15 ms, and the pacing resyncs to it.
- 2026-09-30: After the APU queue lock + vblank pacing, New 3DS, Landing Site,
  1116 frames, all on: logic p95 9.99 ms (was 19.97), work avg 13.7 / p95 15.4 /
  max 18.3 ms (was 23.7 / 29.6), no frame over 20 ms, no frame skipped. The
  average did not move, only the stalls went away. Remaining per frame: logic
  ~1 ms + PPU ~8.4 ms + top copy 2.8 ms on the game thread; DSP ~1.5 ms CPU on
  the system core.
- 2026-09-30: The UI font `romfs/font.bmp` had a broken lowercase `u` (it looked
  like a `v`); redrawn in place. `tools/ui-preview/build.sh` renders the bottom
  tabs to PNG on the host with a fake `<3ds.h>`, so layout can be checked
  without the console.
- 2026-09-30: Correction to the decision above: mzm does not use citro2d for text
  either, it draws its own 5x7 bitmap font (6 px advance, upper case) as
  rectangles. The 8x8 `font.bmp` inherited from upstream was the reason the first
  UI looked bigger than mzm's. Now `source/ui_font.c` is that 5x7 table, copied
  from mzm (plus , _ '), and `romfs/font.bmp` is gone (the romfs only keeps its
  `blank` placeholder). Remaining differences from mzm: tabs are text, not 30 px
  icons, and there are no modals yet.
- 2026-09-30: Map data layout, verified against the ROM on the host. The pause
  map tilemap per area is 64x32 words stored as two 32-column screens (index =
  (col>=32 ? 1024 : 0) + row*32 + col%32), pointer table at `$82:964A` (3-byte
  entries, areas 0-5 + Ceres = 6). Row 0 is an empty margin: a room header's map y
  is the tilemap row minus 1 (82 % of header rectangles are filled at +1, 61 % at
  0). Tile `0x1F` = blank. `map_tiles_explored` (RAM `$7F7`, 256 bytes, same cell
  order, MSB first) is live for the current area; other areas are in
  `explored_map_tiles_saved` (RAM `$CD52`, 256 bytes each, areas 0-5); Ceres has no
  saved bits. `map_station_byte_array[area]` != 0 shows every cell. Room headers:
  scanning bank `$8F` for the 11-byte `RoomDefHeader` followed by a known room-state
  condition routine (`$E5E6/E5EB/E5FF/E612/E629/E640/E652/E669/E676`) finds 262
  rooms; 250 are reachable from Landing Site by following door lists (u16 pointers
  to door definitions in bank `$83`, the list itself in bank `$8F`). The 12 others
  are Ceres and a few scripted rooms. Gotcha: `RomFixedPtr(addr)` does not
  parenthesise `addr`; never pass it an expression.
- 2026-09-30: Teleport design, verified on the host. A warp injects what
  `BlockColl_Horiz_Door` does when Samus hits a door: `door_def_ptr = <door def
  whose destination is the target room>`, `door_transition_function =
  FUNC16(DoorTransitionFunction_HandleElevator)`, `elevator_flags = 0`,
  `game_state = 9`. It is only safe between frames of normal gameplay
  (`game_state == 8` and `coroutine_state_0 == 0`): the game dispatcher resumes the
  state function recorded in `coroutine_state_0`, so switching state while an async
  one (door transition, pause) is running would resume the wrong code. Doors are
  indexed by walking each room's door list in bank `$8F` (u16 pointers to door defs
  in `$83`, list ends at the first entry that is not a door into a known room).
  Host test: boot headless with scripted inputs (the frontend's bit layout, Start =
  0x08, A = 0x100, not the SNES one) to reach gameplay (Ceres), save a state, and
  warp into each room from it. No door leads into rooms `A201 A734 B0B4 B1BB B3E1
  DF1B` (scripted/boss rooms); `D408` (Maridia) lands in `D340`, not investigated.
- 2026-10-01: Teleport crash on the console (Luma dump 60): data abort in
  `BlockInsideDetection` from `Samus_FrameHandlerAlfa_Func11`, `r3 = 0xFFFF`.
  Root cause: after a warp Samus can arrive outside the room. The game places her
  at `layer1 + (uint8)old_position` (`DoorTransitionFunction_PlaceSamusLoadTiles`),
  keeping the low byte of where she stood, which is consistent only through a real
  door. `CalculateBlockAt` returns the sentinel `cur_block_index = 0xFFFF` for a
  negative or >= 4096 coordinate, and `BlockInsideDetection` then reads
  `level_data[0xFFFF]`, 128 KB past the end of `g_ram` (`$7F:0002` + 2*0xFFFF). On a
  real SNES that is harmless mirrored memory, on the host it lands in other globals
  (so ASAN never saw it and the PC tests passed), on the 3DS it is unmapped. Two
  fixes: (1) `g_ram` now has 128 KB of zero padding after the WRAM, guarded by a
  `_Static_assert`, so the sentinel read is an air block; (2) `SmWarp_AfterFrame`
  checks Samus once the transition ends (in room, not in a solid block) and moves
  her to the nearest standing spot (air/special air/shootable air over solid), and
  the warp itself starts from a position derived from the source door's cap.
  Host results with the start position forced to (0,0): 572 of 586 room/door pairs
  fail the position check without (2), 583 pass with it. Remaining: `af3f` door 2
  (automatic second transition), and Samus dying on arrival at `d461` door 2,
  `d4c2` door 1 and `dc19` door 0 (idle Samus; cause not investigated).
  Water rooms use block type 3 for air: anything that assumes type 0 is air is
  wrong there.
- 2026-10-01: Teleport placement, second round (user report: an extra door appeared
  next to the real one and Samus arrived inside it, closed in). `DoorDef`'s
  `x_pos_plm/y_pos_plm` are in the DESTINATION room: `SpawnDoorClosingPLM` spawns the
  door that closes behind Samus there (or re-arms the existing cap,
  `CheckIfColoredDoorCapSpawned`). The earlier code treated them as source-room
  coordinates and derived the start position from them, so she ended up at the door.
  Now, after arrival, Samus is put two blocks in front of that door: horizontal doors
  snap to the nearest standing spot (three passable blocks over a floor block;
  passable = types 0, 3, 4; floor = 8, 1 slope, B, C, E, F, because door tunnels use
  slopes as flat floors), vertical doors (ceiling/floor) drop her in the air if it
  fits. Door defs with cap (0,0) are scripted transitions: only the generic validity
  check applies. Host test with a distance check (<= 64 px from the aimed spot, not in
  a door or solid block): 581 of 586 pass. The others: Kraid's room (`a59f`, the boss AI
  pins Samus to the first screen, found with a gdb hardware watchpoint on her x
  position), `a641`, two automatic second transitions, one death on arrival.
- 2026-10-01: Teleport, third round: the door transition itself was the wrong tool.
  Hardware dumps (user) showed a second, stale copy of the door cap in BG1 (same
  columns, two blocks lower; level data and PLM list had only one), and the warp
  was still a door transition started from a room that is not the source room: the
  game keeps the old room's scroll registers and the low byte of Samus's old
  position across a door. Could not reproduce the drawing on the host (its VRAM does
  not match the console's in the test harness), so instead of chasing the exact
  stale write, the warp now loads the room like "Continue": `game_state = 6`
  (`InitAndLoadGameData_Async`) with a hook in `LoadFromLoadStation`
  (`g_rtl_warp_load`, sm_80.c) that substitutes room, door definition, camera and Samus
  position once; it also saves the old area's explored-map bits and mirrors the new
  area's. Builds the room from scratch (BG, PLMs, enemies, music), keeps all progress
  (items, bosses, doors). No door closes behind Samus any more. Host: 584 of 586.
  The earlier failures that were second automatic transitions (`af3f`, `d408`) and the
  death at `d461`/`d4c2` are gone with the door transition.
- Debug technique that worked: a gdb hardware watchpoint on a RAM variable
  (`watch *(unsigned short*)&g_ram[0xAF6]`) finds the writer of a game-state value in
  seconds; a `SM_WARP_DEBUG` no-op function gives a breakpoint at the right moment.
- 2026-10-01: The warp-by-load leaves Samus in the "just loaded a game" state:
  `Samus_Initialize` sets pose 0 and `frame_handler_beta = Samus_Func16`, which
  plays the load fanfare (`PlaySamusFanfare`, ~360 frames, substate counter,
  controls locked, room music queued after 0x168 frames). `SmWarp_AfterFrame` now
  ends that state on arrival the way the fanfare does when it finishes: alfa =
  `Samus_FrameHandlerAlfa_Func11`, beta = `Samus_FrameHandlerBeta_Func17`, `substate`
  = 0, standing pose facing into the room (pose 1/2, x_dir 8/4) and
  `PlayRoomMusicTrackAfterAFrames(16)`. `Samus_Initialize` also clears RAM
  `$0A02-$0E0B`, so `hud_item_index` (selected weapon) is reset on every warp.
  Host: 583 of 586 (`d408` door 1 makes an automatic second transition again, `b482`
  and `dc19` die on arrival).
