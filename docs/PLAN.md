# Super Metroid 3DS: action plan

Living document. Read it at the start of every session; keep it current as things
change (what goes where: the table in CLAUDE.md).

- **Goal:** a native 3DS port of Super Metroid that is completable start to
  finish, runs at 60 fps on New 3DS and as close as possible on Old 3DS/2DS,
  and has stereoscopic 3D with per-layer depth, like `../mzm`.
- **Starting point:** `CharlesAverill/sm-3ds` (forked as `tinaut1986/sm-3ds`),
  which wraps `snesrev/sm` in a thin SDL2 frontend. Upstream claims ~50 fps on
  hardware (model not stated) and unreliable saves on hardware.

## Status (2026-10-08)

Only what no other place records. Bugs: the open GitHub issues. Tasks: the unticked
boxes below. History: `git log` and the decisions log.

**Release line:** `release/v0.5.1` (nothing yet). Last stable: **`v0.5.0`** (2026-10-08, on `main`; the owner chose the number, a minor bump: ~60 fps on Old 3DS / 2DS also with 3D in the rooms measured, the live tabs, the low-energy blink; first tagged as a beta, then promoted in place after the owner's check; the Ceres escape and the Ridley fight with 3D are still at 39-53 fps, next); before it `v0.4.0` (2026-10-08, on `main`; the owner chose the number: a minor bump for the 2DS / Old 3DS smoothness work, P2.5; first tagged as a beta, then promoted in place to stable after the owner's check; the map crash of #49 confirmed fixed on the console); before it `v0.3.2` (2026-10-07, on `main`: OPTIONS regrouped, the updater's settings, UPDATES and WHAT'S NEW in one row, RESET GAME alone at the bottom); before it `v0.3.1` (2026-10-07, first a beta and then promoted in place: the updater's release notes and the BETA marker (P4.13), OPTIONS -> HUD, half buttons, WIDE and Spore Spawn fixes (#46, #25, #23)); before it `v0.3.0` (2026-10-06, on `main`: a charging bolt on the bottom screen's battery, WIDE showing projectile and enemy pieces in the rows above the picture (#41), HOME showing the game and not freezing the console after closing it (#20), PAUSE and TURBO out of OPTIONS (#43), the self-updater (#42, its install still to be checked against a newer release), and any number of save states with a detail window (#44, #45); before it `v0.2.2` (2026-10-06: the map tab drawn like the game's with zoom, sprites and room outlines (#31), the lava under the HUD and enemies in the WIDE margins (#40, #39), libctru input and audio); before it betas `v0.2.1` (2026-10-05: block fixes and render priorities from the layer workbench, BG3 effects following what they cover, Kraid and the translated HUD, the 40 fps fix, the 19 achievements that never unlocked) and `v0.2.0` (2026-10-03: stereo 3D, circle pad as D-pad, translated screens and item names, achievements tab as cards); stable before: `v0.1.3` (2026-10-02: WIDE fixes, debug tools pass, Ceres escape fixes), `v0.1.2` (2026-10-01), betas `v0.1.0`, `v0.1.1`.

**Branches:** `perf/2ds-periodic-spikes` is **kept on origin as an archive, never to be merged**: it holds the analysis, the owner's recordings (`docs/handoff/logs/`) and the day-by-day notes behind `v0.4.0` (`docs/handoff-2ds-perf.md`, sections 4.x); `v0.4.0` has its work squashed into three commits. `chore/crocomire-garbage-evidence` (#47, closed; kept because the issue's images link to its commit). What waits for the owner: nothing. (2026-10-08: `feat/low-energy-blink` (P1.10) and `perf/ceres` (the mode 7 upload) checked by the owner on the 2DS and merged into `release/v0.4.1`; `perf/live-tabs` (P2.7) checked by the owner on the 2DS and merged into `release/v0.4.1`; `perf/second-eye-reuse` (fewer clears, the submit split) checked by the owner on the 2DS and the New 3DS and merged into `release/v0.4.1`; `perf/ab64-scene-on-main` (per-line strips drawn once a frame with two eyes) checked by the owner and merged into `release/v0.4.1`; `perf/fast-dma` (P2.5: fast DMA, palette cycles spread, cheaper colour math on the GPU, GPU TEST) was checked by the owner on the 2DS and merged into `release/v0.4.1`, squashed by content; `fix/map-level-buffer` and the perf work went into `release/v0.4.0`, published as `v0.4.0`; #47 (the teleport leaves stale VRAM, a debug-tool artefact) and #49 were closed. 2026-10-07: `feat/updater-notes` and `feat/options-layout` were checked by the owner on the console and merged; `v0.3.1` was published as a beta and promoted in place, `v0.3.2` followed. 2026-10-06: `chore/remove-pause-turbo`, `feat/self-updater` and `feat/save-states-list` were checked by the owner on the 2DS and merged into `release/v0.2.3`, squashed.) `feat/map-like-ingame` (2026-10-06, #31: map tab from the game's tiles, zoom, sprites, exact room outlines)
was checked by the owner on the console and merged into `release/v0.2.2`, which was then merged into `main` as `v0.2.2`.
`feat/libctru-input-audio` (2026-10-06, P1.3) was merged into `release/v0.2.2` after the owner ran it on a
New 3DS ("as before") and later on the 2DS (sound works; see P2.6). `feat/plane-fixes` (2026-10-04), `fix/stereo-fx-follows-owner` and `fix/wide-window-kraid-tint` (2026-10-05)
were merged into `release/v0.2.1` (now `v0.2.2`) at the owner's request. Checked on the console by the owner: block fixes and render
priorities in 9AD9, A6A1, A011 and the spikes' row at the WIDE edge (#33, #24, #28), the ash following what it covers (9CB3, #35),
no flashing of the A66A statues with WIDE (#19), the menu and RetroAchievements after cheats (#27, #29), the PLANE TINT views
(#30), Ridley's room E0B5, and Kraid (arms in WIDE, garbage strip). The fog (BG3 on the main screen with the scene on the
subscreen) was seen working by the owner. Still unchecked there: the fps after decoding into a shadow (P2.5), the Ceres
elevator shaft (#34, left pending: a Mode 7 room is one layer, see the issue), Ridley's depth ramp. The
achievements: 19 of the set's now unlock (#37, checked by the owner), through a table that belongs to the set as it is (P4.4). The rest of what the earlier merges need
from the console is in its task (P4.4, P4.8, P3.2).

**Priority** (owner's order; the reasons are in the decisions log):
**P2.5 and P2.7 done for the rooms measured** (2DS ~59-60 fps mono and with FORCE 3D in the Landing Site, `AB64`, `AF14`; P2.5 stays open for other rooms) → P3.3 only as depth bugs turn up (#23, #34) → P4.9 → P1.10, P1.7, P0.4 when useful.

Tasks ticked [x] have been checked on a New 3DS by the owner; do not re-propose them.

## How work is tracked

- **This file** is the roadmap: phases, task specs with acceptance criteria,
  decisions, and a short Status (open branches, priority). It is the source of
  truth for "what next" and is what a new Claude session reads.
- **GitHub issues** (enabled on `tinaut1986/sm-3ds`) are for bugs found by playing:
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
| WIDE view | `port_wide_view.c`, `platform_gpu_3ds.c` (display style / aspect) | Adapt: margins and option names. snesrev's `extended_aspect_ratio` does not help: nothing in the game reads it and `kPpuExtraLeftRight` is 0 (P4.5) |

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
- [x] **P0.2** Build upstream and run it.
  Done 2026-09-30: the upstream build (ROM baked into romfs) booted on a New 3DS.
  Superseded by P1.1; that build no longer exists. Azahar never tried.
- [x] **P0.3** Measure the baseline.
  *Spec:* FPS in fixed spots (Ceres intro, Landing Site, Brinstar, a Norfair
  heat room, Maridia water) on Old 3DS/2DS and New 3DS, with and without audio
  and `FULL_NATIVE`. Record in a table below.
  *Done when:* table filled in.
  Status 2026-10-01: Landing Site on both, title/intro/Ceres on the 2DS (see the
  table). Tools: the debug log (every 5 s: speed, work, logic, GPU build/submit, audio)
  and Debug tab -> PERF (`debug/sm-perf-NN.csv`).
  Done 2026-10-03: every spot on both consoles, audio off on the 2DS; `FULL_NATIVE=0`
  not measured (decisions log).
- [ ] **P0.4** Establish game-logic correctness on PC.
  *Spec:* build the PC version from `sm/` on Linux; play or replay with the
  native-vs-ROM comparison on; note mismatches. Check whether snesrev's
  `devel`/`stable` branches or active forks of snesrev/sm carry fixes that
  `sm/` lacks.
  *Done when:* a short list of known game-logic gaps exists in the decisions
  log (may be empty).
  Note: `make -C sm` needs `libsdl2-dev`, not installed on the dev machine; the
  host harnesses in `tools/` build the game code without it.

## Phase 1: platform foundations (2D, no stereo)

- [x] **P1.1** ROM from SD, never bundled.
  Done 2026-09-30, checked on hardware: `source/rom_loader.c` looks in
  `sdmc:/3ds/Super Metroid 3DS/` for a `.smc`/`.sfc` whose headerless sha1 is the
  JU ROM, strips a copier header, shows an error screen otherwise (seen working on
  the console: folder created, expected hash shown). CIA is ~2.2 MB, no ROM inside.
- [x] **P1.2** Saves on SD.
  Done 2026-09-30, checked on hardware (save at a station, power cycle, continue).
  The game `chdir`s to the data folder, so `saves/sm.srm` lands there; the game
  writes SRAM when saving at a station, so no extra flush on exit was needed.
  Save states (Options tab, slots 0-9) also work after the state-size fix.
- [x] **P1.3** Replace SDL2 with libctru directly: `hid` input, NDSP audio
  (from mzm), citro3d presentation. Drop the `SDL` submodule.
  *Done when:* same features as upstream, SDL gone, FPS not worse than P0.3.
  Note: SDL currently puts the audio thread on the system core (30 % cap) and
  delivers touch as finger events; keep both behaviours.
  Status 2026-10-06 (`feat/libctru-input-audio`, merged; checked by the owner on a New 3DS and on the 2DS): input
  (`hidScanInput`, key edges, `hidTouchRead`), time (system tick) and audio (NDSP, one
  channel, 3 x 2048-frame buffers, thread on core 1 one priority above the main one) no
  longer go through SDL; `libSDL2` is not linked, `make sdl` is gone, the CIA is 0.5 MB
  smaller; the `SDL` submodule is gone (the key codes `sm/src/config.c` reads are vendored in
  `third_party/sdl_keys/`). To check on the console: buttons
  and circle pad, touch on every bottom tab, sound (music, effects, the achievement
  chime, pause and resume, no crackle at 268 MHz on an Old 3DS), fps as before, HOME and
  closing the software, sleep mode and wake-up (sound back).
- [x] **P1.4** Present the frame on the GPU: upload the 256x224 PPU output as
  a texture, scale with citro3d.
  *Done when:* no per-pixel CPU copy remains in the frontend.
  Status 2026-10-01: with the GPU renderer (on by default) citro3d draws and presents
  the top screen; the table-driven CPU copy (`DrawPpuFrame`) is only used for frames
  drawn by the CPU renderer (refused by the GPU path, or the renderer switched off).
  Closed 2026-10-06 (owner): the aim is that the GPU refuses no frame, so the CPU copy is not worth
  working on; if a refused frame shows up it is a renderer bug (the Debug tab says which state).
- [x] **P1.5** New 3DS 804 MHz + L2, frame pacing, FPS/perf overlay.
  Done 2026-09-30, checked on hardware: 804 MHz at boot (Options toggle, saved),
  vblank-locked pacing with adaptive frameskip, FPS/timing overlay on the top
  screen, timing split on the Debug tab.
- [x] **P1.6** Build/CI/release: copy mzm's Makefile targets, git-derived
  version, `build-release.yml` with beta/stable channel, CIA-only release, a
  README install section. *Done when:* a tag on a release branch produces a
  "Beta" GitHub release with the CIA.
  Done 2026-10-01: tag `v0.1.0` on `release/v0.1.0` produced "Beta v0.1.0" with
  `sm-3ds-v0.1.0.cia` and the QR (Actions run 36790298623). Version from
  git (mzm's scheme, `make print-version`; `build/version.h` is generated,
  `resources/AppInfo` has no version any more), `ftp` target, `build_3ds.sh`
  assistant (LAN scan + FTP upload, tested against the console), README rewritten
  for players (install, ROM, data folder) and builders.
- [ ] **P1.7** Controls and options: remappable buttons, in-game reset,
  pause/options menu, config file on SD.
  Status 2026-10-01: done except remapping. Options tab: frameskip, audio, FPS overlay, 804 MHz,
  display, WIDE, language, reset (pause and turbo cells removed 2026-10-06, #43);
  `config.ini` in the data folder keeps tab, frameskip, audio, overlay, 804 MHz
  and slot (not pause/cheats on purpose).
- [x] **P1.8** Debug tooling like mzm's.
  Done 2026-09-30, used on hardware to diagnose real bugs (load-state assert,
  teleport crash, stale door drawing, audio lock stall): log to SD with marks,
  screen dump sets (top RGB, VRAM, CGRAM, OAM, WRAM, PPU regs, game state),
  frame-time recorder CSV with the audio split, `__assert_func`/`Unreachable()`
  crash note with file:line. All in `debug/` of the data folder, fetch over FTP.
  Follow-ups go to P1.9 E.
- [x] **P1.9** Bottom UI in the style of mzm's, rewritten for SM data.
  Stages A-D done and checked on hardware (2026-10-01):
  - **A. Status:** energy/tanks/reserve, ammo bars, item and beam grids,
    area/room/time, raw boss bits. 5x7 font from mzm.
  - **B. Cheats:** god mode, infinite ammo, all items/beams, max ammo/energy,
    full heal (inside a room only).
  - **C. Map:** whole area at 5 px per cell, explored bits, map station, Samus
    marker, area buttons, follow.
  - **D. Teleport:** WARP HERE / DOOR n/m on the map. Loads the room like
    "Continue" (see decisions log). Owner accepted it as a debug tool: the
    selected HUD weapon resets on each warp; `b482`, `dc19` kill an idle Samus;
    `d408` door 1 bounces to a neighbour.
  - **F. mzm layout (2026-10-01, checked on hardware by the owner):** icon
    tabs MAP / STATUS / DEBUG / STATES / OPTIONS with clock, wifi and battery;
    the CHEATS tab is gone, folded into STATUS as in mzm (tap items/beams, GOD and
    MAX buttons, MAP STATIONS boxes that unlock an area's map for warping);
    Ceres has no box: the game has no map station and no saved explored bits for it.
  STATES tab with 10 slots, two-tap confirm and a `saveN.txt` description;
    OPTIONS as a cell grid with a RESET confirm window; DEBUG with a tools window
    (SCREEN DUMP, FRAME DUMP, log, mark, perf, PPU render, give all, heal).
    `DEBUG_TOOLS=1` gates the debug parts (`build/build_config.h`). See
    `docs/debug-tools.md`.
  - **E. Debug tab:** done. The PPU/HDMA dump (FRAME DUMP, F), the scene recorder
    (`tools/scene-rec/decode.py` on the PC) and the PC-side tools (`tools/layer-workbench`,
    `tools/frame-capture`, `tools/ui-preview`) exist. mzm's low-energy tint is its own task, P1.10.
- [x] **P1.10** Low-energy tint (as mzm's, which tints the screen by how low Samus's energy is).
  *Spec:* look at how `../mzm` does it (colour, thresholds, which screen; the bottom screen's tab in
  mzm's case, owner wants it as mzm) and port it with SM's energy and tanks, with an Options switch if mzm has one.
  Read 2026-10-08: mzm (`port_bottom_ui_3ds.c`, RenderTabBar and Port_BottomUI_Render) blinks the tab bar and the whole bottom
  background every 8 frames below 60 energy (yellow) and below 30 (red), in gameplay only. Here a full bottom redraw costs ~15 ms
  on an Old 3DS, so blink the tab bar through the partial updates of P2.7 (`PartAdd`, `UiDraw_PresentRect`: rows 0-23 only);
  the whole background would need a full redraw every 8 frames, so only if the owner wants it after seeing the bar.
  2026-10-08: the tab buttons blink (mzm's colours and thresholds, gameplay only), as a partial update of the buttons; the owner
  chose the buttons only (in mzm the background's two colours are near black, so only the tabs show). No Options switch (mzm has none).
  Checked by the owner on the 2DS.
  *Done when:* the tint appears and fades with the energy on the console and is off at full energy.

## Phase 2: performance (target 60 fps on Old 3DS)

- [x] **P2.0** Cheap CPU wins found by profiling (2026-09-30), checked on hardware
  (New 3DS holds 60 fps in Landing Site with no frame skipped).
  `snes_handle_pos_stuff` ran 154k times per frame and only acts at hPos 0, 512
  and 1024; `snes_handle_scanline` visits just those (bit-identical on the host,
  4000 frames). Table-driven top-screen copy (5.0 -> 2.8 ms).
- [x] **P2.1** Profile. Split frame time into game logic, PPU, audio, present;
  write `docs/perf.md` with the numbers.
  Closed 2026-10-06: the split exists (perf CSV, the debug log) and its numbers live in the P0.3
  table ("Baseline measurements") and the decisions log; `docs/perf.md` is not written, a second
  copy would only go stale.
- [x] **P2.2** Audio cost. Done 2026-10-01: the S-DSP is ~40 % cheaper with the output
  bit-identical (decisions log); 2DS audio blocks take 10-14 ms of the 16.7 ms budget,
  no late callbacks in play, no crackling. A quality setting was not needed.
  History, 2026-10-01 (2DS): the system core gives the app 30 % and no more
  (`APT_SetAppCpuTimeLimit` 80/70/50 -> 0xD8E05BF4, PM "not implemented"); the audio
  block takes ~16 ms of wall time for 16.7 ms of sound (DSP ~14), most callbacks miss
  their buffer: sound breaks up on Old 3DS. Needs a cheaper DSP or core 0.
  Earlier: SDL already runs audio on the system core; the stall on the
  game thread (APU mutex held for a whole block) is fixed with a separate queue
  lock. Open: the DSP itself (~1.5 ms CPU per frame on New 3DS, likely over the
  30 % core-1 budget on Old 3DS). Options: cheaper interpolation/echo behind a
  quality setting, or core 2 on New 3DS.
- [x] **P2.3** GPU PPU renderer, design first (`docs/gpu-ppu-design.md`).
  Done 2026-10-01: implemented including mode 7 and windows, on by default since
  v0.1.1; checked on a New 3DS with GPU CHECK (identical within the 8-bit maths) and
  played on a 2DS.
  Original spec:
  tiles/palettes to a texture atlas, BG layers and OBJ as quads, priorities as
  draw order/depth, colour math as blending, HDMA as per-scanline register
  tables (split strips or shader lookup), windows via stencil/scissor.
  Hybrid like mzm: fall back to the CPU PPU for frames with unsupported state
  (Mode 7, exotic windows) and report which state caused it.
  Start by dumping PPU/HDMA state of a few representative rooms with the Debug
  tab to see what SM actually uses in gameplay: Debug tab -> DEBUG TOOLS -> FRAME
  DUMP on the console, or `tools/frame-capture/run.sh` on the PC from a console
  save state (see `docs/debug-tools.md`). Rooms to capture: Landing Site (rain,
  scroll), a Brinstar room with BG2 parallax, a Norfair heat room, Maridia water
  (FX layer), a dark room, a boss (Kraid, Ridley), Ceres (Mode 7) and the pause
  map.
- [x] **P2.4** Implement P2.3 incrementally; a frame-diff tool against the
  CPU PPU (like mzm's `tests/rec_render.c`, `tools/compare_render.py`).
  Status 2026-10-01: frame-diff tool is `tools/gpu-ppu-test` (host, in `make test`) and
  GPU CHECK (console). Host: every room, a new game through Ceres exploding, power bombs,
  file select identical to the CPU renderer, nothing refused; New 3DS: 6 sets, max
  error 8. The P0.3 spots were measured on the 2DS on 2026-10-03 (shown 44-58 fps with frameskip, speed 56-60);
  what is left of "Old 3DS at the target" is the per-frame cost in P2.5. Closed 2026-10-06.
  *Done when:* gameplay rooms render on the GPU pixel-identical to the CPU
  path, and Old 3DS reaches the target in the P0.3 spots.
  Note: the host harness's PPU/VRAM does not match the console's yet (seen while
  debugging the teleport); fix that before relying on host frame diffs.

- [ ] **P2.5** GPU renderer leftovers (moved from the old "Next up", 2026-10-02).
  *Spec:* sprites are decoded every frame (~0.5 ms on the 2DS) and the line analysis
  costs ~1.1 ms; after a palette change decode only the visible tiles. The X-ray scope
  was seen on the console (#38, closed). Since 2026-10-05 the frame decodes into a shadow and copies
  the changed rows after the GPU is done (`370f21a`): its cost on the 2DS has not been measured.
  Measured 2026-10-06 (2DS, logs of `v0.2.2-dev.111`, 509 samples, Landing Site, Brinstar, Norfair, Maridia): shadow copy 0.02 ms
  (max 0.11; the old full copy was 0.75), VRAM diff 0.43 (p95 1.5), sprites 0.82 (p95 4.2, the spikes are palette changes), lines+bands
  2.3 (p95 3.2), BG 1.6, fallback 0 frames. Shown fps: 52-58 in most rooms, 44-49 in the Norfair rooms AF14, AFFB, B1E5 (draw 9-12 ms).
  So the shadow costs nothing; what is left are the Norfair heat rooms and the sprite decode spikes.
  Added 2026-10-06: since the framebuffers became 24-bit (#20) the bottom UI is drawn in 32 bits into a buffer per screen and
  converted by `UiDraw_Present` each time it changes (~77k pixels; estimated 1-2 ms on the 2DS, not measured). Measure it
  (fps in the Norfair rooms against `v0.2.2`, or a counter around `UiDraw_Present`). Drawing everything natively in 24 bits was
  weighed and not done: 3-byte writes are slower by CPU, and RGB8 render targets lose the alpha the renderer may use; revisit
  only if the conversion shows up in the measurement.
  2026-10-08, shipped in `v0.4.0` (beta): the Landing Site went from 46 to ~57 shown fps on the 2DS (mono), and rooms of Brinstar and Norfair
  to 57-59 (`A0A4`, `AA82`, `AB64`, `9AD9`); with the 3D slider up (the debug FORCE 3D draws the second eye on a 2DS) 56-57 in most, `AB64` 45.6;
  `AF14` (a heat room) 49. What the recordings said, frame by frame: every frame over budget was one of two periodic events (the bottom
  screen's redraw every 15 frames, ~15 ms; a 628-tile animated set every 10 frames) plus palette cycles, never the quiet frame (13.4 ms).
  Done: the bottom screen redraws only on change (battery every 20 s); no quad for a BG texture with nothing visible (GPU 7.6 -> 6.7 ms an
  eye; with two eyes the GPU was the limit, 15.6 ms); one decode per distinct tile and frame; a palette change decodes only the tiles that
  draw a changed colour (-80-93 %); the texture upload per dirty tile block with runs merged at 80 blocks (cost model: a flush call 0.108 ms,
  a memcpy 0.011 ms/KB); SPREAD TILES (animated tiles decoded <= 224 a frame, on by default; a debug state also spreads palette changes);
  the perf CSV with per-stage columns (`tools/perf-csv/analyze.py`, `docs/debug-tools.md`) and FORCE 3D. Left: `AF14`'s quiet frame is
  15.2 ms (logic 6.9, against 4.6-5.2 elsewhere) so any event overruns it; `AB64` with the slider up (263 per-line scroll quads, GPU 8.3 ms an
  eye); the biggest events' upload by hardware copy (needs linearAlloc'ed shadows, `GX_RequestDma`'s event can hang: the analysis is in
  the archive branch). Issues from this stretch: #47 (garbage in Crocomire's room after the debug teleport: stale VRAM, the warp does not
  clear it), #48 (WIDE: Crocomire's parked body shows in the right margin), #49 (the map's buffer, fixed).
  2026-10-08, second round (`release/v0.4.1`): a DMA ran byte by byte through a cycle counter and was ~75 % of the "logic" time;
  now each transfer goes in one go (logic on the 2DS: Landing Site 5.3 -> 1.6 ms, `AF14` 6.4 -> 3.0, same state bit for bit).
  Palette cycles (at most 6 colours of a row) are spread like animated tiles: `AF14` mono 49.6 -> 57.9 fps. GPU TEST (debug)
  measured the GPU with two eyes: texture reads cost nothing (so no VRAM textures), colour math was half the time, the clears
  ~1 ms an eye. Now: one pass for the subscreen add, a flat effect subscreen (rain, fog) composed once a frame, no clear of the
  main target. With FORCE 3D: Landing Site GPU 12.8 -> 8.9 ms, 59.8 fps; `AB64` 17.2 -> 15.8 ms, 46.5 -> 50.6 fps. Left: rooms
  whose whole scene is on the subscreen under a main-screen effect (`AB64`, `AF14`: BG3 heat glow on main, everything else on
  sub) compose two full pictures an eye; next is to compose the scene straight into the main target and add the effect on top.
  Then (same day): the per-line strips (`AB64`'s water and `AF14`'s lava surface, quads 1-2 rows high, ~9 us each on the GPU
  whatever their size) are drawn once a frame into a target per run and shown as one quad an eye: FORCE 3D `AB64` 48.9 -> 56.4
  fps (GPU 15.8 -> 14.4 ms), `AF14` 53.6 -> 55.2. The GPU is no longer the limit with 3D; the CPU is (`AB64`: 15.1 ms a steady
  frame, 2.7 of it building both eyes' quads). Next: the second eye reuses the first eye's vertices with a per-plane offset in the
  shader (mzm `ead13da1`); `AF14` with 3D overruns on its tile animation frames.
  Measured before doing it: the second eye costs only ~0.4 ms of CPU, so it was not done. What held `AB64` was that the GPU sits
  idle during the submit (a frame = GPU + submit, 14.4 + 2.5 ms). Two more clears went (a scene subscreen, a top screen the
  picture fills): `AB64` FORCE 3D GPU 14.4 -> 13.0 ms, no wait, 59.8 fps (mono 59.6). Depth checked on the New 3DS. The same
  build took `AF14` to 59.4 mono / 59.8 with FORCE 3D (9 of 1407 frames skipped), so its tile frames need nothing more. Left: P2.7.
  Ceres, 2DS with FORCE 3D (owner's PERF, 2026-10-08): the elevator (`DF45`, rotating mode 7) fell to ~30 fps because M7Sync
  marked every row between the cells it decoded (1.4 MB uploaded a frame for ~4 KB); now block by block. The escape rooms run
  at 40-45: their frame has 17 bands that differ only in the fixed colour (a blue gradient added every 8 rows), and each band
  costs its passes again in each eye (5 ms of CPU for both). Next: merge bands that differ only in `fixed` into one, with the
  gradient as a 1x224 texture for the math pass (touches the band building and `gpu_ppu_ref.c`). The owner saw the elevator at
  57-60 after the fix; the escape's first room (falling debris) at 53 with FORCE 3D (eyes 5.5 ms of CPU, 17 bands). The Ridley
  fight (`v0.5.0`, FORCE 3D): 38.7 fps, 18 bands of the same gradient and 182 quads (eyes 6.7 ms, work 18.2): the same fix.
  *Done when:* each is either measured and cut on the 2DS or noted here as not worth it.
- [x] **P2.6** Audio cost, second pass. Closed 2026-10-06 without more changes: on the 2DS a
  16.7 ms block of sound costs ~14 ms of wall time (New 3DS: 4 ms) on a core that grants
  30 %, but nothing is heard as a gap and the underrun counter (no wave buffer queued when
  one is refilled, `underruns` in the log's `audio:` line) reads 0, so the margin is
  thin, not failing. "slower than their buffer" (1-30 per 5 s) overstates it: three
  buffers are queued, a 54 ms callback does not empty them. Done:
  1. Output at the S-DSP's own 32 kHz, NDSP resamples (checked on both consoles: same
     sound, same cost: the resampling loop was never the cost).
  2. Measured where the time goes (2DS, per block): voices 7-13 ms (BRR decoding 0.2-0.4,
     up to 5 when notes start), SPC 0.2; the mix and echo figures were inflated by the
     clock reads. On the host, over every room: echo on 100 % of the frames, 1.25 voices
     sounding on average, so skipping silent voices or the echo saves almost nothing
     (`dsp_cycleBlock` already batches, has a one-tap FIR and skips silent voices).
     **Do not time inside `dsp_cycleBlock`**: it runs one sample at a time when the echo
     may feed voices, and the clock reads (system calls) made the intro pop on the 2DS.
  Ideas left, only if a real gap shows up (`underruns` > 0 or heard), cheapest first:
  - A fourth wave buffer (`kAudioBufs`): absorbs spikes up to ~150 ms, costs ~50 ms of
    latency. Preferred over touching priorities.
  - A higher priority for the audio thread (now one above the main thread, 0x2F; the
    highest an application may use is 0x19, 0x18 is video). Risk: on the system core it
    would take turns from services (HOME, sleep, Wi-Fi) and the app has hung on exit
    before; and it does not raise the 30 % cap, so it may do nothing. If tried, a middle
    value (~0x28) and check closing with HOME.
  - A cheaper DSP: `dsp_getSample` (Gaussian) and `dsp_decodeBrr`, bit-exact under
    `dsp-fuzz` and `audio-rooms`; ARM11 assembly if it pays. Profile on the host or with
    a counter per block, never per sample.
  - A quality setting for Old 3DS/2DS (linear interpolation, simpler echo): changes the
    sound, so an Options toggle, off on New 3DS.
  - New 3DS only: the audio thread on core 2 (no 30 % cap); does not help the 2DS.
  - Volume controls (Options): master 0-200 % through `ndspChnSetMix`, free of CPU (above
    100 % it clips); the unlock chime on its own is trivial. Music and effects separately
    only if the driver keeps them on fixed voices (not checked): a per-voice-group
    multiplier in `dsp_cycleBlock` is a few tenths of a ms; rendering them apart would
    double the cost, so no.

- [x] **P2.7** The STATUS and MAP tabs cost fps on Old 3DS / 2DS (asked by the owner, 2026-10-08).
  *Spec:* those two tabs are "live" (`UiIsLive` in `source/bottom_ui.c`): the whole bottom screen is redrawn
  and presented every 15 frames whether or not anything changed, and on an Old 3DS a redraw costs ~15 ms
  (P2.5's recordings), so every 15th frame overruns and the frame skip drops one. Redraw only when what the
  tab shows changes (a cheap key of the game values each one draws: energy, ammo, items, Samus's map cell,
  the markers), and make a redraw that does happen cheaper (only the changed part, or the 32-to-24-bit
  conversion of `UiDraw_Present` limited to the dirty rows). Measure with the PERF RECORDER's `ui_ms` and
  `present_ms` on the 2DS with each tab open.
  *Done when:* on the 2DS the shown fps with STATUS or MAP open is within ~1 fps of the same room with a
  static tab (OPTIONS), or what is left is noted here as not worth it.
  Done 2026-10-08: with nothing over them the two tabs redraw whole only when a value they show changes (`LiveKey`,
  `SmMap_AreaKey`); the STATUS clock and the map's blinking mark rewrite and present only their rectangle (`PartAdd`,
  `UiDraw_PresentRect`). 2DS, 35 s on both tabs: 27 full redraws (room or tab changes) and 54 partial ones at ~1 ms of present,
  against ~140 full ones before; 59.0 fps shown.

## Phase 3: stereoscopic 3D

- [x] **P3.1** Depth model as a pure function of SNES PPU state (BG mode,
  per-layer and per-tile priority, OBJ priority, which layer carries HUD/FX),
  with exhaustive host tests like `../mzm/platform/3ds/tests/stereo_depth_test.c`.
  Design: `docs/stereo-design.md` (planes, whole-pixel offsets, the platform-thickness
  plane, drawing twice; agreed with the owner 2026-10-03).
  Done on the host 2026-10-03: `source/stereo_depth.{h,c}`, `tools/stereo-test` (in
  `make test`). Nothing to see on the console until P3.2.
- [x] **P3.2** Wire depth into the GPU renderer: HUD to the front plane,
  Samus/enemies at play plane, BG1 foreground, BG2 mid, BG3 FX/backdrop far.
  First cut 2026-10-03 (branch `feat/stereo`, to check on the New 3DS): each eye drawn
  from the one frame build with every quad moved by its plane's whole-pixel offset
  (gpu_ppu_3ds.c `QuadDx`), right-eye top target, `gfxSet3D` while the slider is up, the
  CPU path flat in both eyes. Edge columns without WIDE (2026-10-05, branch
  `feat/stereo-edge-columns`, checked by the owner): with the slider up in gameplay the frame
  gets `kStereoMaxPx` (4) margin columns a side, built like WIDE's but without its HUD over the
  room, and the present step crops them (`GpuPpu_SetCropToView`): a shifted layer no longer
  leaves backdrop at the view's edge. Colour windows stay unshifted: the power bomb looks right
  (owner, 2026-10-06), not worth doing. Left open on purpose, for whoever has an Old 3DS (an
  Old 3DS or XL has the 3D screen; the 2DS does not): the second eye pushes every quad's vertices
  again, which is CPU work on the 268 MHz core. Measure first: fps in Landing Site with the slider
  up against down; if it drops a lot, share the vertices between the eyes (`DrawEye`, `PushQuad`
  in `gpu_ppu_3ds.c`), otherwise note it as not worth it. Not needed on the New 3DS (`wait for GPU 0.0`). No option to turn it off: the slider is the switch
  (owner, 2026-10-05).
- [ ] **P3.3** Per-sprite and per-room overrides (enemy IDs, bosses, doors). The workbench
  (`tools/layer-workbench`, issue #33) and its `.inc` are the way: exporter, viewer and the layer rules
  (`SM_LAYER_PLANE`, read by the renderer through `sm_planes.c`) exist; block fixes (`SM_PLANE_FIX`) are read too
  (`GpuPpu_SetSlotPlanes`: the fixed tiles go to a texture per plane, host-checked, not yet on the console). First rules: `E0B5` BG2 and `DF45` sprites (#34), `9D19` BG3 (#35), unchecked on the console.
  The debug depth tint (Debug tools -> PLANE TINT, `docs/debug-tools.md`) is in the release line (the owner has not looked at it closely yet).
- [x] **P3.4** Non-gameplay screens: title, file select, map/pause, cutscenes
  (flat or with deliberate depth). Owner's rule (2026-10-06): text in front of everything.
  Done 2026-10-06 (checked by the owner): the title's sprites, file
  select, the intro's text, the pause screens and game over have their text and interface on the `HUD`
  plane and the art behind flat (`docs/stereo-design.md`, "Screens outside gameplay"; `SmPlanes_Screen`,
  host test `stereo-depth`). The message boxes inside gameplay (save prompt, item texts: BG3 tilemap
  `0x5800`) go on `HUD` too (`GpuPpu_SetMessageBoxMap`, tags their quads; a room's hand rules do not move
  them). Not yet: the options menu, file-select map, ending and credits (flat).

## Phase 4: features

- [ ] **P4.1** Bottom screen: live map, items/equipment, touch shortcuts
  (e.g. item select, morph).
  Status: the live map and items/equipment exist (P1.9 A and C, debug-flavoured).
  Open: player-facing polish, touch shortcuts.
- [ ] **P4.2** Bezel/borders for the unused top-screen area.
- [x] **P4.3** Self-updater (issue #42; after mzm's `port_updater_3ds.c`, owner's request 2026-10-06).
  Done 2026-10-06 (merged into `release/v0.2.3`); the owner ran it on the 2DS and the check works, but no newer release has existed to install yet: `source/updater.c` (libcurl + mbedtls, the console's own
  TLS cannot talk to GitHub; worker thread; downloads the release's `.cia` to `update/sm-update.cia` in the data folder and installs
  it with `am:net`, over the running title or after deleting it, keeping the file for FBI if both fail), `updater_parse.c` (version
  comparison and release-list parsing, host-tested by `tools/updater-test`, in `make test`). OPTIONS gains AUTO UPDATE (`auto_update`
  in `config.ini`, on by default: a check at boot, silent when it fails) and UPDATES (tap = check now); a newer build raises a prompt
  over any tab (install? / installing bar / restart? / failed). Builds with the debug tools follow the pre-releases (Beta), the
  others the releases. The CI image needs `dkp-pacman -S 3ds-curl 3ds-mbedtls 3ds-zlib` (added to the workflow).
  *Known limit:* the TLS certificate is not verified (no CA bundle, as in mzm): someone on the same network could serve another CIA.
  *Done when:* on the console, UPDATES finds a newer release and installs it, the game restarts into it, and the boot check stays quiet
  without Wi-Fi.
  Checked by the owner 2026-10-07 on the console: a dev build was offered `v0.3.1` and `v0.3.2`, installed the update, restarted into the new
  version; without Wi-Fi the UPDATES cell reads ERROR and nothing pops up. (The notes are kept in memory only: with no Wi-Fi WHAT'S NEW says
  there is no data yet. A copy on the SD card is possible if it is ever wanted, P4.13.) (The old text of this task: "Self-updater.")
- [x] **P4.4** RetroAchievements (softcore only).
  Implemented 2026-10-02 (owner's request) after mzm's `port_retroachievements_3ds.c`:
  rcheevos vendored (`third_party/rcheevos`, mzm's copy), `source/retro_ach.c`, trophy tab.
  The existing SNES set runs with one adaptation (below): rcheevos' "System RAM" is `g_ram` (same layout as the
  SNES WRAM), "Cartridge RAM" `g_sram`; the game hash is the JU ROM's MD5, a constant.
  **The port is a reimplementation, not the original code** (`sm/` is hand-written C checked against the ROM, see CLAUDE.md), so
  the sets are adapted to the one that exists now (2026-10-05, 134 achievements) and **must be adapted again if it changes**:
  19 of its achievements (every item pick-up, four bosses, two map downloads) also ask for two 16-bit words of the original's
  direct-page scratch ($0032, $0034) to hold a pair of numbers when the event happens, which the C code keeps in locals;
  `retro_ach.c` (`kDpTags`) writes the pair in the frame of the event. The port saves the set it downloads as
  `debug/ra-set.json`; `tools/ra-tags/dp_tags.py` compares it with the table (issue #37). Anything else a new set reads from
  outside the game's own variables (the stack, other scratch) would need the same.
  Progress goes with save states (`saves/saveN.rap`). Cheats and the teleport do not pause
  it (see the decisions log, 2026-10-04). Settings as mzm's (SETTINGS window, kept in `retroachievements.ini`): the
  notice on the bottom or top screen with a sample, the unlock sound (mzm's, mixed into the
  game's audio), the list's order and direction. A response may grow to 4 MB (the set is
  over 64 KB: the first build cut it there, "Invalid JSON" on the console).
  2026-10-03 (owner's request): the tab looks like mzm's windows: the settings on top, then
  the set as cards with badges, type and lock glyphs, dragged with the stylus or by its bar;
  a tap opens the achievement (badge at 64 px, state and date, type, description). Badges
  are downloaded on the worker thread and kept in `badges/` in the data folder.
  *Done when:* logged in on the console, the list loads with its badges, dragging scrolls
  without opening cards and a tap opens one, an unlock in play shows the notice (with its
  badge) and appears on the RA site.
- [x] **P4.5** Display options: PIXEL PERFECT / SCALED, and WIDE (more of the room on
  the sides), both in the OPTIONS tab and saved in `config.ini`, like mzm's display
  style and aspect settings (`../mzm/platform/3ds/source/platform_gpu_3ds.c`,
  `port_wide_view.c`).
  *Today:* there is only one mode. Both paths scale 256x224 to 274x240 with nearest
  sampling (x1.071, uneven rows and columns), centred, black sides
  (`GpuPpu3ds_DrawAndPresent`, `DrawPpuFrame` in `main.c`).
  Status 2026-10-02: parts A and B merged into `release/v0.1.3` and played on hardware;
  follow-up fixes and open bugs: see "Where we are" at the top and issues #1-#8. The
  history below is kept for the reasoning.
  Status 2026-10-01: part A implemented on `feat/display-options` (OPTIONS -> DISPLAY,
  `pixel_perfect` in `config.ini`, both renderers).
  Part B, renderer side (horizontal): `GpuPpu_SetMargin` widens the frame build (margins
  outside both windows), 512-wide citro3d targets, black mask rectangles (HUD rows),
  OPTIONS -> WIDE VIEW (`wide` in `config.ini`), margins only in gameplay states.
  Host: `WIDE=M` in tools/gpu-ppu-test rebuilds every frame with margins and checks the
  middle 256 columns equal the normal frame (every room, and power-on through Ceres
  exploding with M = 72: all equal, nothing refused); `WIDE_DUMP=N` writes images. The
  margins show stale BG1 columns until the game streams them (next step).
  Game side done the same day: `source/sm_wide.c` fills the margins' tilemap areas from the
  level data before the PPU draws (hook `g_rtl_before_ppu_draw`), masks what is outside the
  room or in red scroll screens, and `g_rtl_wide_margin_x` widens the on-screen checks for
  enemies, projectiles and sprite objects. HUD over the room: the HUD IRQ keeps BG1/BG2/OBJ
  (from TM or TS) on in lines 0-31, the HUD's opaque blank cells (entry 0x2C0F) become a
  transparent BG3 char while WIDE shows the room, and BG3 stays out of the margins on those
  lines (`GpuPpu_SetNarrowBg3Rows`). Door transitions: margins masked whole. Not on hardware
  yet. PIXEL PERFECT extra rows (8 above, 8 below): `GpuPpu_SetExtraRows` grows the first
  and last bands with the first/last line's registers (not BG3 in the HUD band, not mode 7
  or composed layers), the citro3d targets have 16 spare rows, the fill writes rows -1/0/15
  for the game's columns, `DrawSpritemap`'s bottom cut and the enemies' top check move by
  `g_rtl_wide_extra_*`, and the masks cover the extra rows too. Host: every room (72 px +
  8 rows) and power-on through Ceres, middle identical, nothing refused.
  *Spec, part A (display style, renderer only):*
  - SCALED: what exists now.
  - PIXEL PERFECT: 256x224 at 1:1, centred (72 px sides, 8 px top and bottom).
  - Both renderers (GPU, and the CPU path for refused frames) honour it; the FPS overlay
    and the bottom-screen tricks that use the left margin (`bottom_ui.c:970`) still fit.
  *Spec, part B (WIDE, renderer + game):* fill the sides with the room instead of black,
  in gameplay only (`game_state` 8, and door transitions); title, file select, pause map,
  cutscenes and Ceres mode 7 stay 4:3. Margin per side M = 59 SNES px when SCALED (400 /
  1.071 = 373 px; 60 used, 376 px fill the screen), 72 when PIXEL PERFECT.
  Height (owner wants it too, 2026-10-01): SCALED already fills the 240 rows (224 x 1.071),
  so it gains height only through the HUD over the room (32 rows). PIXEL PERFECT adds 8
  rows above and 8 below (240 = 224 + 16), as mzm's PIXEL PERFECT has a Y margin and its
  SCALED none. Cost: lines -8..231 span 240 px, and with the fine scroll they touch 16
  block rows, the tilemap's full height. Over one `layer1_y_block` they need rows -1..15
  (17), and rows -1 and 15 share a tilemap row, so the game's row upload (on block changes
  only, rows `+1` / `+15`) is not enough: refresh row -1 or 15 when the fine scroll
  crosses 8. The renderer extends the first and last captured lines' registers over the
  extra rows (HDMA effects there are approximate). Enemy activation already reaches
  `layer1_y_pos + 248`.
  Findings (2026-10-01) that make it feasible:
  - BG1 and BG2 use 64x32 tilemaps (`BG1SC = 0x51`, `BG2SC = 0x49`): 32 blocks of
    16 px across, of which the game keeps only 17 current. `UpdateBgGraphicsWhenScrolling`
    (sm_80.c) uploads the column at `layer1_x_block + 16` (scrolling right) or `+ 0`
    (left), and `DisplayViewablePartOfRoom` loads columns 0..16 when a room is entered.
    With M = 59 or 72, 4 or 5 more columns per side give 25 or 27 columns, which fit in
    32 without wrapping onto themselves. Change: upload `x - m` / `x + 16 + m` there, and
    the wider range on room load (same for BG2 when it scrolls with level data,
    `layer2_scroll_x & 1 == 0`; a library BG2 is static and already 512 px).
  - Outside the room (camera at a room edge, `layer1_x_pos` clamped to
    `[0, room width - 256]`) the level-data read would wrap into the previous row: never
    upload those columns, and draw the margin there as black.
  - Hidden areas: the camera never enters a red scroll screen (`scrolls[]` at
    `$7E:CD20`, 0 = red, `room_width_in_scrolls` per row), so the original never shows
    them; WIDE can show part of them. First decided to leave them visible like mzm, but
    on the host they are often unfinished filler (e.g. `CF80`: a screen of "X" blocks
    behind the door), so the margins' parts in red screens are masked black, like those
    outside the room (2026-10-01, decided while the owner was away; easy to drop in
    `SmWide_AddMasks`). The 256 px view itself is never masked.
  - Enemies are only processed and drawn inside the 256 px view (`EnemyMain` and the
    active list in sm_a0.c: `x_pos + x_width` against `layer1_x_pos .. +256`, and
    `EnemyWithNormalSpritesIsOffScreen`), so without changes they would freeze and pop
    in at the old screen edge. Widen those checks by M while WIDE is on. This changes
    game logic (enemies wake a few blocks earlier), as mzm's
    `Port_WideMarginSubPixelX` does; with WIDE off it must stay bit-identical
    (`make test` hashes unchanged). Enemy projectiles (`CheckIfEprojIsOffScreen`,
    sm_86.c) need the same; Samus's projectiles already live to -64 / +320.
  - Sprites: `DrawSpritemap` (sm_81.c) culls only in Y and stores the 9-bit X, so a
    piece in the right margin (x 256..256+M) arrives in OAM as -256..-256+M, which the
    SNES treats as off-screen left. A piece visible in the left margin starts at
    -M-64 or later (sprites are at most 64 px wide), so the ranges never overlap while
    M <= 96: decode x < -128 as x + 512 in the renderer, only in WIDE gameplay. Watch the 128-sprite OAM limit (more on screen at once).
  - HUD over the room (owner's request): in the original, lines 0-31 show only the HUD
    on black. `IrqHandler_4_Main_BeginHudDraw` (sm_80.c) sets `TM = 4` (BG3 only) and
    colour math off for them, and `IrqHandler_6_Main_EndHudDraw` restores
    `gameplay_TM` at line 31. Those 32 lines are inside the camera (`layer1_y_pos` is
    screen line 0), so the room is there, just switched off. With WIDE on, the HUD lines
    show the room under the HUD: BG3 (HUD) plus `gameplay_TM`'s BG1/BG2/OBJ, across the
    whole width including the margins. The HUD's own 32-tile BG3 tilemap stays centred
    (no repeat into the margins).
    To check first, with a FRAME DUMP or the host harness: (1) the HUD's empty
    pixels are transparent (colour 0) rather than opaque black tiles, and its tiles have
    the BG3 priority bit so they stay above BG1 in mode 1; (2) the top tilemap row is up to date:
    scrolling up, `UpdateBgGraphicsWhenScrolling` refreshes row `layer1_y_block + 1`,
    not `+ 0`, so the row under the HUD may be stale. If so, refresh `+ 0` while WIDE is on
    (rows y..y+15 are the 16 rows of the tilemap, no overlap); (3) the door-transition
    and Draygon IRQ variants (handlers 8-26) do the same HUD split: keep their band
    black or handle them alike; (4) readability over bright rooms.
  - BG3 FX layers below the HUD (water, lava, fog) wrap at 256 px; that repetition in
    the margins is expected to look right, check it.
  - Renderer: the GPU main/sub targets are 256x256 (`gpu_ppu_3ds.c`), the frame build
    clips at 256, and quads, backdrop, windows and colour math rectangles span 0..256.
    WIDE needs 512x256 targets and those spans widened. The CPU renderer has
    zelda3-style side space half wired (`extraLeftCur/extraRightCur` in ppu.c, never
    set, `kPpuExtraLeftRight = 0`): finish it or show black margins on refused frames
    (they should be rare in gameplay).
  - Stereo (Phase 3): per-eye offsets need a few more pixels at the edges; size the
    margins once for both.
  *Done when:* the three modes (SCALED, PIXEL PERFECT, each with WIDE on or off) work in
  both renderers; with WIDE on, a walk through Landing Site, Brinstar, a Norfair heat
  room and Maridia shows no garbage columns or rows, the HUD over the room and not
  repeated, no frozen or popping enemies, no sprite on the wrong side; WIDE off
  keeps `make test` hashes; 2DS still ~60 fps in Landing Site with WIDE on.

- [x] **P4.6** FRAME SKIP off warns. It is on by default (`frameskip` in `config.ini`) and
  keeps heavy scenes playable (the Ceres escape shaft ran at 20-30 fps without it); players
  who do not know what it does may switch it off and blame the port. Turning it off in
  OPTIONS shows a short toast ("FRAME SKIP OFF: heavy rooms may slow down"); turning it on
  says nothing. The option stays (useful for debugging and for those who prefer it).
  *Done when:* the toast shows on every switch to off, not at boot with it saved off.

- [x] **P4.7** Bottom-screen languages (owner's request, 2026-10-02): English, Spanish,
  Catalan, French, Portuguese. OPTIONS -> LANGUAGE cycles them, `language` in `config.ini`
  (index, append-only order); with no saved choice the console's language is used when the UI
  has it (Catalan never comes from there: the 3DS has none). Only player-facing text is
  translated (`source/ui_lang.c`): the DEBUG tab, the debug tools and the DEBUG_TOOLS-only
  parts stay English. The game's names (items, beams, ammo, areas: Status tab, map and
  states) are translated too since 2026-10-03 (owner's request), short forms of the same
  names as the message boxes (P4.8; `TrItem` and the others in `ui_lang.c`). Strings are UTF-8;
  the font draws accents as marks above the plain capital. `tools/ui-preview` renders the
  tabs in every language and fails if a translation changes a format string's conversions.
  *Done when:* checked on the console in each language, nothing cut or overlapping.

- [x] **P4.8** The game's own texts in the UI language (owner's request, 2026-10-02), with the
  ROM file untouched and RetroAchievements valid: only VRAM changes, never the game's RAM
  (`source/game_text.c`, hook `g_rtl_message_box_hook` in `sm_85.c`; `source/game_text_screens.c`
  for the rest; host tests `msgbox-es`, `newgame-es`, `pause-es`, `gameover-es` check the WRAM
  hash equals English).
  Done: the message boxes (item names and their instruction lines, map/energy/missile
  stations, the save prompt and its YES/NO). Titles use the box's capitals, accents are marks
  in the blank cell above (cedilla below), the box widens for a longer text; the instruction
  line is drawn with a lowercase font of ours into the characters of the English words, put
  back when the box closes.
  2026-10-03, the other screens (`game_text_screens.c`): option menus (OPTION MODE, controller
  and special settings), file select (data copy/clear and their prompts, the ENERGY/TIME
  pictures), the intro (THE LAST METROID page and the six typed story pages, the typed share of
  the translation following the English), the HUD's ENERGY, the pause screen (equipment names,
  headers, MAP/EXIT buttons, the Wrecked Ship title), game over, the credits' headings with
  SEE YOU NEXT MISSION and the item rate (names stay). Checked on the PC in all languages.
  How to add a screen: `tools/game-text/README.md`.
  Ticked 2026-10-06 (owner: the screens and boxes are translated and checked); what was left
  open moved to P4.10.

- [ ] **P4.9** One language setting for the port and the game (owner's request, 2026-10-03; after
  P4.8). Today the game's own OPTION MODE choice (ENGLISH TEXT / JAPANESE TEXT, the Japanese
  subtitles of the intro) and the bottom screen's LANGUAGE are separate. Two ways, to pick:
  (a) keep both in step: LANGUAGE on the bottom screen gains JAPANESE, which sets the game's
  Japanese text flag, and choosing JAPANESE TEXT in the game's menu sets LANGUAGE (and English
  text puts back the last non-Japanese language); (b) the game's menu line becomes one
  language entry that opens a list of the port's languages, Japanese among them. Either way
  the bottom screen needs a Japanese UI translation (and a font with kana for it).
  *Done when:* the language is chosen in one place and the other follows, checked on the console.

- [x] **P4.11** Save states without a fixed number (issue #44, owner's request 2026-10-06). Done 2026-10-06, checked by the owner on the 2DS (merged): `source/states_store.c` (the list is `saves/save<id>.sav` found by scanning, newest first; ids 0-9 are the old
  slots, a new state takes one past the largest; the game side is `RtlSaveLoadFile` in `sm_rtl.c`, which takes the file), the STATES tab as
  a scrollable list of cards like the achievements, `+ NEW` to save, and a colour mark per state (8). Host test `tools/states-test`.
  *Done when:* on the console, many states are saved and listed, the list scrolls, a mark shows on its card, and the old ten still load.
  Left for later: a name typed with the keyboard (the mark was what was asked); saving in the background (serialise the state into memory, write the
  file on a thread) so the game does not stop while the SD card is written: today `BottomUi_Busy` draws PLEASE WAIT first, since the main loop
  is also what draws and nothing animates while it is blocked (the same for loading, deleting and the debug dumps).
- [x] **P4.12** A state's detail window (issue #45, owner's request 2026-10-06). Done with P4.11, checked by the owner on the 2DS: tapping a card opens a window with the
  screenshot of the top screen when it was saved (`saveN.img`, 200x120 RGB565, taken from the GPU's top target or the CPU's shadow buffer),
  the date, area and room, energy, reserve, missiles, supers, power bombs, play time and the build that saved it, the marks, and LOAD /
  SAVE OVER / DELETE with a second tap each (delete removes `.sav`, `.txt`, `.rap` and `.img`).
  *Done when:* on the console the screenshot matches what was on screen, and delete leaves no files behind.
- [ ] **P4.10** Texts of the game still in English (left over from P4.8): the ones drawn as sprites
  (PLANET ZEBES on the file-select map, the ending's mode 7 THE OPERATION WAS COMPLETED SUCCESSFULLY
  and CLEAR TIME), the pause screen's RESERVE TANK and MODE AUTO/MANUAL (not seen yet), the beams'
  names (drawn, not seen with beams collected).
  *Done when:* each is translated or noted as not worth it, checked on the console.

- [x] **P4.13** Release notes in the updater and the BETA marker (hand-off from `../mzm/docs/port-updater-notes-to-sm-3ds.md`,
  owner's request 2026-10-07; mzm's commits `e16d2305`, `79037321`, `e18bbd62`, `20ef0ec2`). Done 2026-10-07, checked by the owner on
  the console with the published `v0.3.1` beta and its promotion to stable (merged from `feat/updater-notes`):
  - `docs/release-notes/vX.Y.Z.md` per tag (format in its README); `build-release.yml` puts it in the release body between
    `<!-- sm-notes -->` markers and only warns when it is missing.
  - `updater_parse.c`: `Updater_CollectNotes` and `Updater_IsNewerBuild` (host tests in `tools/updater-test`); the worker keeps the
    text (`Updater_CopyNotes`, 6 KB under the text lock), and when the build is up to date it lists the latest published releases instead.
  - OPTIONS gains WHAT'S NEW (slot under RESET GAME): a window over any tab with the text wrapped to 44 columns, drag or bar scrolling.
    The "new version" prompt gets the same button under YES / NO when notes exist (the prompt steps aside while the window is up) and a
    "current > new" line.
  - The channel is baked into `build/version.h` (`APP_IS_BETA`, `APP_VERSION_LABEL` = "vX.Y.Z BETA"), written like `build_config.h`, so
    switching `CHANNEL` needs no `make clean`. The label is shown in OPTIONS' footer, STATES, the prompt and the logs.
  - `update_url.txt` in the data folder (one line) points the check at `tools/update-mock-server.py`.
  OPTIONS layout since `v0.3.2`: UPDATE | CHANNEL and UPDATES | WHAT'S NEW share a row (the UPDATES half button uses short forms of its
  states, about 11 characters), HUD sits beside DISPLAY | VIEW, RESET GAME is alone at the bottom.
  *Done when:* on the console, a published beta shows its notes in the prompt and WHAT'S NEW, a beta build says BETA next to the version
  and is offered the stable build of its own version once it exists, and a stable build shows no BETA. (The offer of a stable to a beta
  build and the notes before installing were seen with `v0.3.1`.)

## Phase 5: completion

- [ ] **P5.1** Full 100% playthrough on hardware, bugs filed as issues.
- [ ] **P5.2** Any-% and known sequence breaks (wall jumps, shinespark,
  mockball) behave like the original.
- [x] **P5.3** First stable release. Done 2026-10-01 as `v0.1.2` (the owner chose a patch
  number, not a minor bump): Release builds come without DEBUG_TOOLS, betas keep them.

---

## Baseline measurements (P0.3)

Audio off on the 2DS (2026-10-03, WIDE on) changes little: A923 shown 44.3 (46.5 with audio), D055 54.9 (52.7), Landing Site 54.9 (52.3-54.0), work within ±1.5 ms. The audio thread is not what makes the frameskip drop frames there.

| Spot | Old 3DS | New 3DS | Notes |
|---|---|---|---|
| Title / intro / Ceres | 2DS, GPU renderer (mode 7): 50-55 fps (owner, overlay). Was 22-30 with the CPU renderer. Log 2026-10-03 (268 MHz, audio on, WIDE on): speed 56.6-59.6, shown 44.4 (E0B5) to 55.2 (DF45), work 11-15 ms (draw 7-12); audio callbacks slower than their buffer 46-303 per room (crackles likely), against 0-40 elsewhere | GPU renderer: speed 59.6-59.7 in DF45-E021, 58.4 E06B, 58.6 E0B5 (Ridley, with the escape); work 5-8 ms (logic ~2, draw 3-5 = build 2-4 + submit ~1) | 2DS 2026-10-01. N3DS 2026-10-03 from the debug logs (5 s windows; the last window before a quit, 300-800 ms, left out), 804 MHz, audio on, frameskip on |
| Landing Site | 2DS, GPU renderer, no frameskip: 59.8 fps, work ~11 ms (logic 5.3, draw 5.0 = build 3.0 + submit 1.0), audio clean. CPU renderer: speed ~49, shown ~16 | 60 fps, work 13.7 ms avg / 15.4 p95 (CPU renderer). GPU renderer (91F8, 2026-10-03): speed 59.8, work 4.9 ms (logic 1.9, draw 2.4 = build 1.5 + submit 0.4); other Crateria rooms 59.0-60.0, work 3-5 ms | N3DS 2026-09-30, all on, 804 MHz; logic ~1 ms, PPU ~8.4, top copy 2.8. 2DS 2026-10-01 |
| Brinstar | 2DS 2026-10-03, 268 MHz, audio on (9E9F, 9F11, 9F64, A107): speed 59.1-59.7, shown 53.8-58.1, work 9-11 ms (logic 1.8-4.1, draw 5.7-7.8) | GPU renderer: speed 58.9-59.9 over 9AD9-A107 (9E9F morph ball room 58.9), work 4-5 ms (logic 1.2-2.0, draw 2.1-3.1); audio callbacks slower than their buffer: 0-1 per room | N3DS 2026-10-03, same logs |
| Norfair heat room | 2DS, demo (AFFB): ~36 fps before per-line quads (build 15-17 ms composing rows); owner reports 50-55 after | B1E5, GPU renderer: speed 59.8-60.0 over 50 s, work 5.0-5.4 ms (logic 1.9, draw 2.8-3.2; build 1.4-2.0, submit 0.6-0.8), audio clean. A heat room (owner: it took health, lava, Samus jumped in); 238-260 quads: a layer drawn one quad per line (per-line scroll), as AFFB and A923 on the host (~209 quads each, GPU equal to the CPU renderer) | 2DS: A923 2026-10-03, 268 MHz, audio on: speed 58.4, shown 46.5 (frameskip), work 14.6 ms (logic 6.3, draw 8.6 = build 6.2 + submit 1.5), 284 quads |
| Maridia water | 2DS (D340): 40-50 fps with submit ~7 ms before the priority ordering; owner reports fine after. D055 2026-10-03: speed 58.5, shown 52.7, work 12.7 ms avg / 16.5 max (logic 5.7, draw 6.1), 223 quads | D017, GPU renderer: speed 59.9 over 25 s, work 4.8-5.2 ms (logic 1.9, draw 2.6-2.9), 414-451 quads in one band (per-line scroll), audio clean | numbers pending from the log |

## Decisions log

- 2026-10-08: **`v0.5.0`** (the owner's choice, a minor bump), beta first and promoted in place. The first tag build failed before
  compiling: `dkp-pacman -S` reinstalled the portlibs the image already has and pkg.devkitpro.org answered 403; the workflow now
  passes `--needed`, and the tag (no release had been made from it) was moved to that commit. mzm's workflow has the same line.
  After the promotion GitHub still showed `v0.4.0` as latest (a promoted pre-release is not made latest): set by hand, and the
  workflow now passes `make_latest` (true for Release, false for Beta).
- 2026-10-08 (`perf/fast-dma`): SPREAD TILES is gone as a choice: the renderer always spreads both kinds of tile decodes (char data and
  palette, 224 a frame), the state the owner judged no different by eye and that took `AF14` mono from 51 to 58 fps; the owner asked to
  drop settled debug switches so they are not toggled by mistake later (the host test keeps `DEFER`/`DEFER_PAL`). The debug tools
  window is no longer redrawn every 15 frames (only on a tap, the scene recorder's count, or the perf recorder stopping by itself):
  the owner keeps it open while stepping GPU TEST, and its periodic redraw cost frames under a window that hides the tab anyway.
  The perf CSV records `eyes` and `slider`, so one recording can switch the 3D on and off.
  Same day: spreading every palette change made the Landing Site's lightning (8 colours of one row, ~1940 tiles) reach the screen a
  band of rows at a time over ~13 frames (owner's scene recording); only rows that change at most 6 colours (a cycle: the lava's 5)
  are spread now, a bigger change lands in one frame.
- 2026-10-08: **`v0.4.0`** for the smoothness work (the owner's choice, a minor bump), shipped beta first and promoted in place to stable after
  their check, like `v0.3.1`: the stable is then the same commit rebuilt without the debug tools. SPREAD TILES is on by default (animated tiles
  show up to ~3 frames late; the owner saw no difference in `AF14`'s lava between off, CHARS and CHARS + COLOURS); the stable has no cell to turn
  it off, so it is a candidate for an OPTIONS entry if it is ever noticed. The state of the 2DS work was recorded in
  `docs/handoff-2ds-perf.md` on the archive branch `perf/2ds-periodic-spikes`.
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
- 2026-10-01: Version from git, copied from mzm: exact tag, else
  `vX.Y.Z-dev.<main..release count>[.<release..HEAD count>]+<hash>` from the
  release branch HEAD is on or was cut from. The CIA header's major/minor/micro
  come from the leading `X.Y.Z`; the SMDH long description carries the full
  string. `build/version.h` is written at parse time only when it changes, so a
  new commit recompiles `main.c` alone. `tools/build_3ds.py` is mzm's assistant
  minus the debug/production mode (no `DEBUG_TOOLS` switch here yet, see P1.9 E)
  and minus the portlib check (no self-updater yet), in English per the language
  rule. `tools/ui-preview` crashes in `SmMap_Init` (the map tab reads the ROM,
  which the preview does not load); not fixed yet.
- 2026-10-01: Bottom UI reorganised like mzm's (P1.9 F). Cheats moved into the
  Status tab; the per-item toggle is missing <-> collected+equipped, and turning
  Spazer or Plasma on unequips the other (the beam tables have no Spazer+Plasma
  entry and end in `Unreachable()`). MAX remembers the capacities and gives them
  back when switched off; a loaded state or reset drops that memory instead of
  restoring it. Map unlock uses the game's own RAM (`map_station_byte_array`,
  explored bits live for the current area and in `explored_map_tiles_saved`
  otherwise), so the pause map and a station save see it; returning to "real"
  restores both byte for byte (host check on a console state, all 6 areas).
  `RtlSaveLoad`/`RtlSaveSnapshot` now return whether they worked (the save path
  also no longer crashes when `fopen` fails).
- 2026-10-01: FRAME DUMP: `g_ppu_write_hook` in `ppu_write` (null unless a capture
  runs, one predictable branch per write) logs every `$21xx` write with
  `inVblank ? -1 : vPos`. In `FULL_NATIVE` the whole frame is RunOneFrameOfGame
  (vblank) then DrawFrameToPpu (lines, HDMA at hPos 1024, `Vector_IRQ` after a
  line), so the stamp says which part wrote. Console save states load on the PC
  only in a 32-bit `-malign-double` build: that layout matches the 3DS's
  (275559 bytes); x86-64 is 275555 now.
- 2026-10-01: GPU renderer architecture (docs/gpu-ppu-design.md): capture the
  registers per line instead of drawing, build a draw list from the capture,
  and fall back to the CPU renderer *from the same capture* (ppu_replayLines),
  so a refused frame costs nothing extra and looks exactly like today. Verified
  on the host by comparing normal render == capture replay == draw list run
  in software (gpu_ppu_ref.c), on every room. The citro3d backend calibrates
  what cannot be checked off the console (readback byte order, depth test
  direction, render-target and transfer orientation) at start-up. Lazily
  initialised and off by default, so the CPU path is untouched until the
  RENDERER tool is used.
- 2026-10-01: GPU renderer checked on a New 3DS: six GPU CHECK sets, four identical, two
  off by at most 8 on a few hundred pixels (8-bit colour math). FPS overlay added to the
  GPU path as a texture. Power bomb: the owner saw "no inward wave"; the C code and the
  ROM's shape tables only have outward stages (pre-explosion, explosion, afterglow), and
  the owner confirmed the original only pulses outwards. Not a bug.
- 2026-10-01: Bottom-screen flashes, worst at 268 MHz: a swap only takes effect at the
  next vblank, and the loop waits for one only when the frame took < 15 ms. A slow frame
  followed by a quick one (a skipped frame redrawing the UI) drew into the buffer still
  on screen. `UiDraw_Screen` and `DrawPpuFrame` now wait for that vblank when a swap is
  less than one refresh old (`UiDraw_Swapped`/`UiDraw_VBlankSeen` in the main loop).
- 2026-10-01: Choppy audio at 268 MHz: SDL starts the audio thread on core 1 with
  `APT_SetAppCpuTimeLimit(30)`; the DSP needs ~1.5 ms CPU per block at 804 MHz, ~4.5 at
  268, i.e. ~27 % of core 1, right at the cap. main.c now asks for 80/70/50 % after
  opening the device (mzm does the same); the granted value goes to the debug log.
- 2026-10-01: First Old 3DS data (2DS, Landing Site 91F8, debug log): CPU renderer
  speed ~49 / shown ~16 fps, GPU renderer speed ~58 / shown ~48 fps: the GPU renderer
  is what makes Old 3DS playable. Audio is broken there (see P2.2: 30 % of core 1 only).
  The GPU "draw" is still ~11 ms of CPU at 268 MHz: worth profiling.
- 2026-10-01: Closing from HOME hung ("Closing software") with the GPU renderer on: the
  exit trace stopped inside `GpuPpu3ds_Exit`, which submitted an empty citro3d frame to
  wait for the GPU; after HOME, citro3d's suspend hook stops handling vblanks, so a
  frame linked to the top screen never finishes. Exit no longer submits a frame.
  DEBUG_TOOLS builds now start the log at boot, with a line per second on settings
  changes, timings and audio-callback health every 5 s, and every exit step (also in
  `debug/sm-exit.txt`, in every build).
- 2026-10-01: S-DSP made ~40 % cheaper on the host with output bit-identical
  (`sm/src/snes/dsp.c`, marked "3DS port"): voices run voice by voice over each block
  between SPC driver ticks (`dsp_cycleBlock`, state in a local copy so ARM11 keeps it in
  registers), no interpolation for voices at gain 0, an idle voice skips the envelope,
  echo input gathered in the mix pass, one-tap FIR path (SM always uses 127,0,...,0).
  Exactness: `tools/audio-bench/run.sh ROM rooms 120` (every room, music hash) and
  `tools/audio-bench/dsp_fuzz.sh` (random register writes against the vendored dsp.c:
  samples, registers and APU RAM identical). The fuzz found that voice-major order
  differs from the original when a voice reads sample data the echo is writing; such
  blocks fall back to one sample at a time. Host: 75 -> 44 us per frame of audio.
  Hardware numbers pending. Why mzm never had this problem: GBA sound is a few PCM
  channels mixed natively at ~13 kHz; here an 8-voice sampler with Gaussian
  interpolation and echo is emulated at 32 kHz.
- 2026-10-01: Closing still hung once: after the empty frame was removed it stopped in
  `C3D_RenderTargetDelete`, which also waits for the GPU queue. Exit now leaves citro3d
  alone; the process teardown frees it. Checked on the 2DS: closes.
- 2026-10-01: GPU renderer cost on a 2DS (268 MHz, Landing Site), per frame: wait for the
  GPU 0 ms (the GPU is never the bottleneck), submit 3 ms -> 1 ms once the vertex cache
  flush (a syscall) moved from every batch to once per frame, build ~5.5 ms steady:
  lines+bands 1.1, VRAM diff 2.1, sprites 1.0, BG 0.55, shadow copy 0.75; spikes of
  7-15 ms when a palette change re-decodes a whole 32x32 surface (Landing Site does this
  often). Changes: the PPU marks changed 8-word VRAM groups as the data port writes them
  (`g_ppu_vram_dirty`; loads and resets call `GpuPpu_Invalidate`), replacing the 64 KB
  diff and copy; tile decoding is table driven with palettes converted once per frame.
  Host: identical over every room, build 46 -> 25 us. Next candidates: lines+bands,
  sprite cache across frames, decoding only visible tiles after a palette change.


- 2026-10-01: Result on the 2DS (Landing Site, GPU renderer, frameskip off): 59.8 fps,
  work ~11 of 16.7 ms per frame: logic 5.3, build 3.0 (lines+bands 1.1, VRAM tracking
  0.04, sprites 0.5, BG 0.5, cgram copy 0.02), submit 1.0. A palette change re-decoding
  629 tiles now costs 4.3 ms (was up to 15). Audio: 0 late callbacks in steady play.
  Exit clean. Remaining headroom ~5.5 ms per frame for WIDE and stereo.
- 2026-10-01: GPU renderer on by default in every build (production CIAs have no Debug
  tab to switch it): 2DS ~60 fps against ~25 with the CPU renderer, and refused frames
  go to the CPU renderer anyway. Superseded: "off by default" in the GPU entry above.
- 2026-10-01: Crash on the 2DS (Luma dump, pc = 0, lr in RunOneFrameOfGameInner) while
  pressing B repeatedly on the file-select screens: `SoftReset` (sm_81.c) only sets
  `game_state = 0xffff`, which `RunOneFrameOfGame_Both` turns into a reset; the
  FULL_NATIVE path (`RtlRunFrameCompare`, RM_MINE) did not, so the next frame indexed
  `kGameStateFuncs` with 0xffff. Now it sets `coroutine_state_0 = 3` (Vector_RESET_Async)
  there too. Reproduced and checked on the host: `MASH_B=400 tools/gpu-ppu-test/run.sh
  ROM boot SRAM` (B every other frame from the file-select map on). Not GPU related.
- 2026-10-01: Mode 7 on the GPU (title, intro, Ceres; the 2DS showed ~22-30 fps there on
  the CPU renderer). SM only uses one matrix per frame, but rows are emitted per line,
  so perspective would work too. Host: new game from power-on through Ceres (9000
  frames) and every room identical to the CPU renderer, nothing refused any more.
  Bug found on the way: VRAM change marks are cleared after every frame built, so the
  mode 7 plane must take note of them on every frame, not only on mode 7 frames.
- 2026-10-01: Norfair heat (demo in the intro, 2DS ~36 fps): BG2/BG3 vertical scroll on
  every line made the frame build compose 193 rows per layer on the CPU (~15 ms). Now one
  quad per scroll run however many (the GPU never waits on the 2DS); composing is only
  the fallback when quads would not fit. Mode 7 rows with linear starts merge into one
  quad (title: 224 -> 1).
- 2026-10-01: Ceres exploding (escape cutscene, DF45, 269 frames at ~23 fps on the 2DS)
  used a colour window that splits lines with prevent-math "inside". Now drawn on the
  GPU as per-band clip / no-math rectangles. Reproduced on the host with
  `CERES_BOOM=1 tools/gpu-ppu-test/run.sh ROM boot EMPTY.srm 10500` (new game, then
  game_state 0x20 in DF45). With that, no frame of a new game through Ceres, the demo,
  every room, a power bomb or the file-select screens is refused any more.
- 2026-10-01: Maridia water (per-line horizontal scroll): submit ~7 ms on the 2DS because
  per-line quads alternated the two priority textures (a texture switch and a new draw
  batch per quad). Runs are now emitted priority 0 first, then 1.
- 2026-10-01: Freeze at a door in Maridia (2DS; game logic stuck, frontend fine): the
  teleport stacked music queue entries (load fanfare: a stop held 360 frames, plus the
  room music) and a second warp within 6 s overflowed the 8-entry queue, which has no
  full check; HasQueuedMusic then stayed true and DoorTransition_WaitForMusicToClear never
  finished. Diagnosed from the WRAM in a screen dump. The warp now empties the queue on
  arrival and queues what a door would. Only the teleport (a debug tool) could do this.
- 2026-10-01: Host regression suite, `make test SM_ROM=...` (tools/test/README.md):
  DSP fuzz, GPU list vs CPU renderer (every room, new game through Ceres exploding, power
  bomb, soft resets), music queue under repeated warps, audio hash, optional warp test.
  ~90 s. Expected hashes in tools/test/expected.txt (`--update` after an intended change).
  Run it before merging anything that touches the game, the renderer or the audio.
- 2026-10-01: WIDE view (P4.5 B) built while the owner was away; host-checked only. How it
  works: the renderer builds frames over [-M, 256+M) x [-top, 224+bottom); margins are
  outside both windows. The game is not taught to stream more: a hook between the game
  logic (and its NMI, which runs last in RunOneFrameOfGame) and the PPU drawing writes the
  margin tilemap columns, and the rows the game leaves stale, from the level data each
  frame, compare-before-write, marking VRAM groups dirty for the GPU renderer. Only in
  states where the level data is the room on screen (not door transitions: margins masked
  whole there). Game logic changes only through g_rtl_wide_* (enemy activation/drawing,
  projectile and sprite object culling, the sprite bottom cut, the HUD IRQ's layers); all
  0 with WIDE off, and `make test` checks that. Decisions taken alone: red-scroll screens
  masked in the margins (they show filler), the HUD over the room whenever WIDE is on (its
  opaque blank cells swapped for a transparent char), heat rooms' sub-screen layers shown
  under the HUD without colour math. Margins are drawn by the GPU renderer only; a frame
  refused to the CPU renderer shows black sides.
- 2026-10-01: Found while testing WIDE: LoadLevelDataAndOtherThings used memcpy on
  overlapping ranges (room background data, rooms with > 0x3C00 bytes of level data), so
  the result depended on the build; the ROM does a descending copy, i.e. memmove. Fixed;
  two expected hashes changed. Technique: dump WRAM per frame from two builds
  (`WRAM_TRACE=1`), diff, then a gdb watchpoint on the first differing address.
- 2026-10-01: WIDE bug from hardware (Landing Site, owner's screen dumps 03/04): the gunship
  showed in both margins. OAM X has 9 bits, so a piece really at x 433 read as -79 (left
  margin) and one at -210 as 302 (right). The spritemap drawers (sm_81.c) now record each
  entry's full X (`g_rtl_oam_x`), and the GPU builder uses it while margins are on. Enemies
  pass an X whose high bits can be meaningless (gunship: -15487 for 385, from its spawn
  offset), so WriteEnemyOams sets an anchor, the enemy's own screen X, and a piece is the
  X mod 512 nearest to it. Reproduced on the host with `WARP_AT=` at the dumps' camera and
  `WIDE_NO_FULLX=1` for the old decode; `WIDE_OAM=1` prints both X.
- 2026-10-01: Second hardware round of WIDE bugs. (1) Samus invisible: the drawers' X has
  meaningless high bits for Samus too (-15153 for 207), so "full X" from the caller was
  wrong; now only objects that set an anchor (enemies in WriteEnemyOams, enemy projectiles,
  sprite objects) get recorded positions, everything else keeps the 9-bit decode. (2) Ship
  tiles flashing at the top while it left the bottom: the HUD lines now show sprites, and
  the SNES wraps OAM Y at 256, so pieces below the screen (and pieces SM parks at y 0xF0)
  appeared at the top; anchored entries now record their full Y (no wrap, parked ones
  dropped). `WIDE_TOPCHECK=1` compares the top rows with/without and found such pieces in
  Ceres (DBCD, DE7A). (3) Shaft in 92FD: masking every red screen hid real room until the
  game turned it blue (pop-in); now only red screens made of a single repeated block
  (filler, as in CF80) are masked. Superseded: "red screens masked" above.
  Test gap that let (1) through: the middle check used the recorded positions on both
  sides; `WIDE` now also counts frames where they change the view below the HUD.
- 2026-10-01: Room edges in WIDE: the margins lean away from a room edge (all of the margin
  goes to the other side), so the edge sits on the screen border as if the camera stopped
  there; narrower rooms are centred. The game's camera is untouched (door transitions
  scroll from a screen-aligned camera, DoorTransitionFunction_ScrollScreenToAlignment); the
  lean is worked out in the WIDE hook from the room's screen rectangle, kept during door
  transitions, and fed to the next frame's on-screen checks (left/right separately). The
  HUD is drawn moved so it keeps its place on the screen; the citro3d targets take the
  left margin as a per-frame offset (up to 256 px of margins in all). Vertical (PIXEL
  PERFECT's 8 rows) not done.
- 2026-10-01: Door at Landing Site's bottom left looked closed but let Samus through, and
  only its top block took shots (owner, dump 09). Its cap's lower blocks are type D
  (extension) and need BTS ff/fe/fd ("1/2/3 blocks up"); the console's RAM had 00, and
  8948 of 8960 BTS bytes repeated the byte 0xA00 back: the forward memcpy of the
  overlapping BTS copy. First blamed on old RAM, wrongly: the morning's memmove fix covered
  LoadLevelDataAndOtherThings (game load, the teleport, which the host tests use) but not
  its twin LoadLevelScrollAndCre, the door-transition path (same ROM loops at $82:EA73),
  still memcpy; glibc happened to copy it right, newlib forwards. Fixed there too; checked
  on the host by leaving Landing Site through that door and coming back (ROOM_INPUT2,
  AUTOFIRE), under ASAN: no overlap reported, cap BTS ff/fe/fd. (Correction, 2026-10-02:
  that run never left the room; the check was redone with ROOM_SEQ, a scripted route out
  to 92B3 and back through the door, TRACE_SAMUS showing both transitions.) It affected every room
  with more than 0x3C00 bytes of level data entered through a door (doors, slopes, special
  blocks).
- 2026-10-01: WIDE lean, second round (owner): (1) the parallax kept moving while the leaned
  view stood still: the view stands for a camera the game does not have (layer 1 + margin
  - left); BG2 is now moved to where CalculateLayer2Xpos would put it for that camera
  (GpuPpu_SetLayerShiftX, and the BG2 fill follows it, writing every column then). (2) A
  door at a leaned edge travelled 256 px and then jumped to the new room's lean: during a
  transition the lean now goes from the old room's to the new room's (taken at
  door_destination_x_pos) along door_transition_frame_counter (64 frames across, 57 up or
  down), so the door crosses the screen once, smoothly. The HUD rows are left unmasked in
  transitions (the moved HUD was cut).
- 2026-10-02: WIDE lean, third round (owner): (1) Landing Site's sky still moved with Samus:
  scrolling-sky rooms (room code ScrollingSkyLand/Ocean/Shakes) scroll BG2 by HDMA bands
  that drift with time and ignore the camera's X, so BG2 is fixed to the screen there, not
  the layer 2 formula. (2) From the second door on, the door jumped mid-transition: the
  counter keeps the previous transition's 64 until it is reset, and the reset and the
  first scrolling step happen in the same frame, so the hook never saw 0 and stayed on
  the old lean (or used the old end). The reset is now detected as the counter going down.
  Host check: ROOM_SEQ route out of Landing Site and back, both doors cross the screen
  edge to edge with no jump.
- 2026-10-02: Fireflea rooms (fx type 0x24) darken by subtracting COLDATA with colour math;
  the HUD lines had math off, so with the room shown under the HUD that strip stayed lit.
  With WIDE the HUD IRQ now keeps the room's math on its layers (not BG3) when it is
  fixed-colour math (sub-screen math would add the room to itself there). Host:
  FIREFLEA_DARK=n in tools/gpu-ppu-test. FPS overlay: box fitted to the text, translucent
  black on the GPU path (the 64x64 overlay texture was drawn without blending, a black
  square over the picture in WIDE).
- Open (small): vertical lean. In PIXEL PERFECT the 8 extra rows show black at a room's top
  or bottom edge instead of the edge sitting on the screen border, as the sides do.
- 2026-10-02: Fourth WIDE round (owner; the screenshots did not reach the SD card, so worked
  from the description). (1) Item message box repeated into a margin: message boxes are
  BG3 with their own 32x32 tilemap (BG3SC 0x58); bands using it keep BG3 narrow, at the
  HUD's place (GpuPpu_SetNarrowBg3Map). Gating on gameplay_BG3SC instead narrowed the lava
  layer for the 3 frames before BG3 switched. Host: MSGBOX=n. (2) Ceres escape: the screen
  shake (HandleRoomShaking, room shakes) adds to the BG scroll registers after the game
  placed its columns; the fill mapped level blocks by the shaken scroll, one column off
  whenever the shake crossed a block edge, and those columns showed once the camera came.
  Now mapped by the unshaken scroll (bgN offset + layer position). Host: EARTHQUAKE=type
  and FRAME_HASH (play area with WIDE on vs off): 300/300 frames differed before, 0 after.
  (3) A boss losing tiles when moving up: not reproduced. Two likely causes fixed: extended
  spritemap parts are anchored at their own position (a part far from the enemy's centre
  put pieces 256 px off), and the BG2 fill stays off in rooms where an enemy writes BG2
  (QueueEnemyBG2TilemapTransfers: Spore Spawn, Kraid, Mother Brain...). Waiting for a
  FRAME DUMP if it persists.
- 2026-10-02: Fifth WIDE round (owner, dump 00 + description). (1) Spore Spawn cut at the
  HUD rows: the HUD lines put the room's sub-screen layers on the main screen with math
  off, so a high-priority BG2 covered BG1. Now the HUD lines keep the room's own TM, TS
  (minus BG3) and colour math, BG3 added on top outside the math; math is kept off only
  when the colour window shapes it (power bomb: its window is not set up on those lines).
  HUD rows are 0-30 (31 rows; 32 narrowed the first gameplay row's BG3). (2) Power bomb not
  in the margins: a window touching the view's edge (left 0 / right 255) now continues to
  the frame's edge (WinCalc). (3) Stray sprites on top in Ceres: with WIDE, sprites no
  longer wrap from the bottom to the top (GpuPpu_SetNoSpriteWrap): the SNES hid those rows'
  sprites under the HUD. (4) The margins jittered while the room shook: the lean followed
  the shaken scroll; it now uses the unshaken one (masks still the shaken). Dumps always
  overwrite slot 00 (found when the owner's captures were lost): open.
- 2026-10-02: Debug tools pass (branch `feat/debug-tools`, cut from
  `fix/firefly-and-fps-overlay` because both touch bottom_ui.c/main.c; merge that one
  first). Removed PPU RENDER (no use since the GPU renderer), GIVE ALL and FULL HEAL;
  STATUS got an ALL button (every item and beam) in the free item cell, capacities stay
  MAX's job. Added from mzm: buffered log writes (16 KB blocks by default, or direct per
  line) and a SCENE RECORDER; both cells have mzm's side start/stop button, the rest of
  the cell picks the option (owner's request). The recorder stores what was shown (RGB565 framebuffer, GPU
  target read back) in a RAM ring and writes on stop, like mzm's RAM presets: mzm records
  GBA state per sample instead, but SNES state alone cannot be redrawn offline (per-line
  HDMA/IRQ writes), and the pixels are what a renderer bug needs. Not done from mzm: log
  stream filters (ALL/GPU/AUDIO/PERF; our log is a few lines every 5 s), depth tint (for
  Phase 3), kill Samus. Issue #8 (dumps always slot 00): most likely libctru's stat() leaves
  st_mtime at 0 (not checked on the console), so "least recently written" was always
  slot 0; slots now follow a `debug/sm-<kind>-last.txt` counter, which works either way.
- 2026-10-02: PLAN's "Where we are" and "Next up" removed (owner agreed): they retold the
  issues and the specs and went stale. Replaced by a short Status (branches waiting for
  the owner, priority line). What goes where is a table in CLAUDE.md. The small GPU
  leftovers that lived only in "Next up" became P2.5.
- 2026-10-02: `v0.1.3` released as stable (owner: "todo funcionando"). The three stacked
  branches (`fix/firefly-and-fps-overlay`, `feat/debug-tools`, `fix/wide-door-hud-margins`)
  were squashed by content before merging (WIDE round 1, debug tools, docs, scene recorder,
  the snesrev escape-beam fix, WIDE round 2) and merged in that order. Issues #1-#6 and
  #8-#14 closed with it; #7 (vertical lean) stays open as an enhancement, and P4.6
  (FRAME SKIP off warning) was added at the owner's request.
- 2026-10-03: The owner asked to merge the whole stack of topic branches (one CIA carried
  them all for testing) into `release/v0.1.4` once #16 (FX over the HUD's rows) was seen
  fixed. Merged branch by branch with `--no-ff`, oldest first. The tasks stay unticked until
  each is checked on its own (Done when), as do #7 and #18.
- 2026-10-03: P0.3 closed from the debug logs of both consoles. `FULL_NATIVE=0` (the ROM on
  the emulated CPU) was not measured: every build is FULL_NATIVE and that path is only a
  host debugging aid. On the 2DS, switching audio off barely moves the shown fps (A923
  46.5 -> 44.3, D055 52.7 -> 54.9): the game runs at ~58-59 while frames shown drop to
  44-55 because work is 12-16 ms, close to the 16.7 ms budget, and draw is the larger part.
  Ceres on the 2DS has 46-303 audio callbacks slower than their buffer per room (0-40
  elsewhere). Both are P2 material, after Phase 3 in the owner's order. #7 confirmed by the
  owner (Ridley's room: the walls go on up to the top edge, no black band) and closed;
  #18 stays open for the X-ray scope (the security eyes were confirmed). The debug log's
  settings line now carries the display mode and WIDE (it did not, so these runs' WIDE
  state came from the owner: on).
- 2026-10-03: Item names (message boxes and the Status tab) follow Nintendo's own Spanish
  and French where it has them, at the owner's request ("BOLA SALTARINA" read wrong): Zero
  Mission's (Morfosfera, Supersalto, Salto en barrena, Aceleración, Rayo recarga, Rayo de
  ondas, Traje climático, Bomba de energía; Costume Varia, Méga Saut, Attaque en Vrille,
  Rayon à Vague, Bombe de Puissance), then the later games' (Rotosalto for the Spring Ball;
  the Spanish wiki's "Saltosfera" has no official source we could find; Rayo enganche, Visor
  de rayos X, Salto espacial, Traje gravitatorio, Tanque de reserva). The 2012 fan
  translation ("Super Metroid [Esp]") uses the same set except Ataque en barrena and Rayo
  ancho. Spazer has no official Spanish name: Rayo múltiple, as the Spanish Metroid wiki.
  Catalan follows the Spanish choices. Portuguese: the only official Metroid in it is Prime 4
  (PT-BR, 2025, hardly any of these items); Morfosfera as its Brazilian guides, Salto Esfera
  as the 2013 PT-BR fan translation (denim, spyblack), the rest as before. Super Metroid itself never had an official Spanish or French text.
- 2026-10-03: The game's screens other than the message boxes are translated by reading the
  English back from VRAM, not by hooking each screen's code: the PPU keeps a copy of VRAM as the
  game wrote it (`g_ppu_vram_shadow`, set in `ppu.c`'s data port) and `g_rtl_game_text_hook`
  (before the PPU draws) finds the current screen's English phrases there by their letters and
  writes over them; what was changed and is no longer wanted is put back from the copy. One
  engine covers all the fonts (a letter is a char, or a top and a bottom char, often sharing
  halves: menu B, V, Y, Z exist that way; Q is O's top over a drawn bottom). Letters a screen lacks are
  drawn into chars no tilemap on screen uses, never a tilemap's or the sprites' bytes nor the
  font's own letters (the credits' last screen reuses B and D: those letters are a copy).
  Pictured words (pause screen names, ENERGY) are redrawn with a 3 px font of ours.
  The game's RAM is never touched (RetroAchievements), and save states are written with the
  game's own VRAM (`GameTextScreens_PutBack`).
- 2026-10-03: The owner made stereo 3D the next minor: `release/v0.1.4` (never tagged)
  renamed to `release/v0.2.0`. For the design they asked for (1) text and HUD with depth
  in whole pixels, so they never ghost as in mzm before its rounding, and (2) mzm's
  platform thickness kept (scenery slightly nearer than Samus and enemies); mzm's
  menu/map/cutscene override lists are not to be copied, SM's screens are looked at on
  their own.
- 2026-10-03: Achievement badges are downloaded at runtime (rcheevos' `badge_url`, cached as
  PNG in `badges/`, decoded with the stb_image already built for `sm/src/glsl_shader.c`)
  rather than baked into the source as mzm's 55 are: SM's set is larger, a set update needs
  no new build, and the repo carries no RA artwork. Locked badges are the same image drawn
  grey (no `_lock` download). Loads wait behind rcheevos' calls on the one worker thread;
  the badge the UI is drawing goes first.
- 2026-10-03: `v0.2.0` tagged as a beta on `release/v0.2.0` (stereo, circle pad as D-pad,
  `feat/item-names`, `feat/ra-cards` merged at the owner's request after they tried the CIA
  on both consoles); the line is now `release/v0.2.1`. The tasks stay unticked until the
  owner checks each one in detail.
- 2026-10-04: Debug captures made for playtesting (owner's request). (1) PLANE TINT, as mzm's
  depth tint: the quad's own texel mixed 78% towards its plane's flat colour in a second TEV
  stage, so the scene stays readable and the offsets still move each colour; names and
  colours live in `stereo_depth.c` (pure, host-tested). (2) REPORT window before SCREEN DUMP,
  FRAME DUMP and stopping SCENE REC: the game pauses, a reason or typed text becomes
  `sm-<kind>-NNNN-note.txt`, CANCEL writes nothing (a recording being stopped has a play button that keeps it running and a cross that discards it). (3) Dumps and
  recordings no longer rotate: each takes the next number (4 digits), the owner deletes what
  has been handled (Claude asks after each one, then removes it over FTP). Rotating hid
  captures: ten slots were gone after a day of notes. The dump is of the frame after the pause.
- 2026-10-04: Cheats, the teleport and save states no longer pause RetroAchievements: the port is softcore
  only and RA's softcore mode allows all three (the first cut stopped evaluating after any of them until
  restart; the owner noticed when an item picked up after a teleport did not unlock, `sm-dump-0002`, #29).
- 2026-10-04: BG3 effects follow what they cover (`fix/stereo-fx-follows-owner`): they reach the screen by colour math, either
  as the subscreen added over the scene (ash: the effect takes the depth of the pixels it is added to) or as the main BG3
  with the scene on the subscreen (fog, water: BG3 goes on `FRONT`), so a plane fixed by layer put them in front of
  what they were drawn on in some rooms and behind it in others (the owner's report). Host-side nothing can check the
  GPU passes (the reference renderer has no stereo): to look at on the console. The workbench shows BG3 (needs the
  rooms exported again).
- 2026-10-05: 19 achievements of the RA set never unlocked (#37: Morph Ball, Varia, every beam and suit, four bosses, two maps): next to
  the item bit they ask that the 16-bit words at $0032 and $0034 hold a pair of numbers per event (`0x 000032=42`, `0x 000034=14`
  for the Morph Ball): direct-page scratch the original asm leaves there in that frame, kept in locals by the C code. Read off the
  downloaded set (`debug/ra-set.json`, saved by the port); `retro_ach.c` writes the pair in the frame the bit, boss bit or map byte
  changes and puts the old bytes back (`kDpTags`). The table is for the set as it is (2026-10-05): if the set changes the table must be
  adapted again, `tools/ra-tags/dp_tags.py ra-set.json source/retro_ach.c` says what differs. Checked on the console by the owner: they unlock.
- 2026-10-05: The garbage strip in Kraid's body (A59F, GLITCH OR GARBAGE dumps 0005, 0009, 0010) was the translated HUD: `HudScreen` wrote
  the letters of "ENERGY" into BG3 chars 0x0b-0x0d at a fixed VRAM word 0x4000, but in Kraid's room BG3's chars are at 0x2000 and
  0x4000 is BG2's tilemap (VRAM 0x4058-0x406F, 24 words, with any UI language but English). Both text hooks now take the address
  from the room's BG3 (`bgLayer[2].tileAdr`). Reproduced on the host from the console's save state with GAME_LANG=1.
- 2026-10-04: `SM_TILE_PRIO` (workbench: "Draw the selection with priority"): a tile fix only moved a tile's depth, and a wall the game
  draws at priority 0 was still crossed by Samus's weapon (A6A1: 3D in front, drawn behind the sprite). It sets the priority a tile is
  drawn with (the texture, and compositor level, it goes to); GPU renderer only, and the host test compares without it
  (`PRIO_FIXES=1` keeps it). Ash on the subscreen now takes one plane in front of the sprites too, not each pixel's owner's:
  it jumped in front of Samus only while she crossed the layer it was on.
- 2026-10-04: Block fixes were placed by `2*bx` in the tilemap, which only holds when the room's scroll offset
  (`bg1_x_offset`, `bg2_x_scroll`) is a multiple of the tilemap's size: after a door it is the screen the room was
  entered at (256 in B1E5), and the fixes landed on other tiles (A6A1 on the console). The game, and SmWide's `FillLayer`,
  put block bx at tilemap block `vx0 + bx - lx0` (vx0 from position + offset): checked against WRAM and VRAM (B1E5 from 0 to
  896 of 896 tiles). The rule `SM_LAYER_PLANE(0x9D19, 3, 0, FRONT)` (#35) was removed: it set the plane of an effect
  that now follows what it covers.
- 2026-10-04: The wait before texel writes (#19) cost a whole GPU frame on every frame that decoded anything (sprites' atlas,
  animated tiles: "build 18.5, wait for GPU 16.4 ms", the New 3DS at 40 fps in every room, from the owner's perf). Texels
  are now decoded into a copy in ordinary memory and the changed rows are copied to the GPU's texture after
  `C3D_FrameBegin` has waited, so the CPU builds the frame while the GPU draws the last one and the GPU never samples a
  texture being written. Costs the textures' size again in the heap (~4 MB, more with mode 7).
- 2026-10-04: `fix/gpu-texture-race` (#19) was thought lost; it was on its own branch. Its commit `d88a5a7` is in
  `feat/plane-fixes` now (the owner checked it on the console: no flashing in A66A with WIDE), plus a wait before the
  block fixes' texture clears, which write texels earlier than `DecodeTile`.
- 2026-10-04: Block fixes (`SM_PLANE_FIX`) reach the renderer (#33, `feat/plane-fixes`). The dropped attempt (#28) was
  not a failure of the mechanism: its rule was wrong (the plane by the block type under each tile). A fix names a block
  of the level by hand, and a block is found in the tilemap at `(2*bx + i, 2*by + j)` modulo the tilemap's size, the
  one nearest the camera when several share a slot: checked against WRAM and VRAM of rooms with BG1 and BG2 as level
  data, every visible tile matching. The workbench had stale defaults (BG2 MID, BG3 FAR), from before the BG2
  priority 1 and BG3 changes of v0.2.1: fixed to `StereoDepth_Plane`'s, so fixes saved before may be redundant or
  point at the wrong plane.
- 2026-10-04: Tried and dropped, never merged: a rule sending BG1 priority 0 to a plane by the type of the
  block under each tile (#28, #24). On the console `sm-dump-0004` (`9C5E`) showed all the scenery behind
  Samus, although the block lookup was right on that dump's WRAM and VRAM (checked offline) and GPU and
  CPU frames were identical on the host in every room: the cause was never found. Per-room and per-tile
  depth is chosen by hand instead, with a layer workbench like mzm's and a `.inc` (#33); PLANE TINT is the
  way to see the result on the console.
- 2026-10-03: Fireflea room (9C5E) looked flat in 3D. A scene recording and `STEREO_PLANES`
  with `ONLY_LEVEL` showed its whole level is BG2 priority 1 (BG1 holds only the door cap),
  and BG2 was all on `MID` (-3 px): floors behind Samus and level with the background.
  BG2 priority 1 draws over Samus, so it joins BG1 priority 1 on `PLAY`; `MID` is now BG2
  priority 0 only. The accepted "BG2 always mid" exceptions in stereo-test are gone.
- 2026-10-03: Falling ash (room 9CB3) read as behind the background: it is BG3 priority 0, which
  sat on `FAR` (-4) under BG2's `MID` (-3). The two swapped: BG2 priority 0 (background) is
  `FAR`, BG3 priority 0 FX is `MID`. stereo-test pins "BG2 prio 0 over BG3 prio 0, farther"
  as accepted. The Fireflea room's thorns (reported behind, should be at Samus's depth) are
  not located yet: not in BG1/BG2 at the room's left end in the host runs.
- 2026-10-05: P4.5 and P3.1 ticked at the owner's word that both work on the console. #18 (the X-ray
  scope in WIDE) is closed, which was the last thing P4.5 waited on, and it also meets P2.5's
  "X-ray scope seen on the console"; P2.5 keeps only the 2DS measurements. P3.1's host model has
  been in use on the console since v0.2.0. Priority is now Phase 3 (P3.2 onward) → P1.3 → P2.5.
- 2026-10-05: no OPTIONS entry to turn the 3D off (P3.2): the slider is the switch.
- 2026-10-06: P3.2 ticked. Colour windows are left unshifted (the power bomb looks right) and the
  second eye's vertex re-push is kept in the task as a to-measure item for an Old 3DS: the owner has
  none, so it waits for someone's feedback or hardware.
- 2026-10-06: P3.4 ticked with the options menu, the file-select map, the ending and the credits left flat:
  nobody has asked for depth there. Each is one case in `ScreenPlane` plus a line in `stereo-test` when wanted.
- 2026-10-06: P1.3 first half: SDL is out of the runtime (input, touch, ticks, mutexes, audio), `main.c` talks to libctru. Kept the SDL
  driver's behaviour on purpose so nothing shifts: audio thread on core 1 (asked at 80/70/50 %, 30 % as last resort), priority one
  above main, 3 buffers of 2048 frames, silence while paused, `osSetSpeedupEnable(true)` at start (SDL's `main` did it). The pause
  keeps feeding silent buffers, as SDL did. `sm/src/opengl.c` is no longer built (it only called SDL's GL). `sm/src/config.c` still
  needs SDL's key codes (vendored in `third_party/sdl_keys/`, zlib) and `SDL_GetKeyFromName` (stubbed in `main.c`); with that the
  `SDL` submodule is removed and `make sdl` / `git clone --recurse-submodules` are no longer needed. The
  host tests give the same 26 FAILs with and without the change (wide-rows, wide-xray*, intro-cursor-*; checked against
  `release/v0.2.2`), so they say nothing about this branch.
- 2026-10-06: Audio goes out at 32 kHz (the S-DSP's rate), 3 buffers of 3 DSP blocks (1602 frames, ~50 ms, as the 2048 at 44.1 kHz
  were), NDSP resamples. `RetroAch_MixAudio` takes the rate; the unlock chime (32 kHz) now plays 1:1. `dsp_getSamples` returns a
  straight copy for 534 frames, bit-identical to what the nearest-sample loop gave at step 1.0. The host audio tests render 736
  frames, so they do not cover the 534 path: the check for it is the console (P2.6 step 1).
- 2026-10-06: P2.6 closed without further audio work: no gaps heard, `underruns 0` on the 2DS, so the thin margin is left as it is (the owner's
  call). Raising the audio thread's priority was weighed and not done (it may starve system services on the system core and the 30 % cap
  stays); a fourth buffer is the first thing to try if a gap ever appears. Ideas are listed in the task.
- 2026-10-06: PLAN tidy-up with the owner. Ticked: P1.4 (the aim is a GPU that refuses no frame, so the CPU copy is not worked on), P1.9 (its
  stage E is done; the low-energy tint is now P1.10), P2.1 (numbers stay in the P0.3 table, no `docs/perf.md`), P2.4 (P0.3 spots measured
  on 2026-10-03), P4.6, P4.7, P4.8 (what was open in it is P4.10). The fog was seen working (not recorded before). Priority line: P1.3 is
  done, so P2.5's measurements come next, then P4.9. P0.4, P1.7, P4.9 and the rest stay open.
- 2026-10-06: Map tab (#31, part of P4.1) is drawn from the game's own pause-map tilemap and tile graphics
  (`$B68000`, palettes `$B6F000`), not from the room list: walls, doors and the save/item marks are already tiles, so only the
  sprites the game adds on top (refills, map station, bosses; `$82:C7CB` tables) are ours, as plain letter blocks. Zoom 1X/2X/3X
  = 5/8/12 px a cell (`map_zoom` in `config.ini`); 1X shows the 64 columns of an area in 320 px, the 8x8 tile averaged down.
  Picking a room (debug) resolves on release so a drag can scroll.
- 2026-10-06: Closing the game from HOME froze the console for the next application (FBI, ftpd; #20), and HOME showed the
  game's screens black. Two causes, both found by elimination on the 2DS (a `bisect.txt` that stopped the game after each
  subsystem's set-up, since removed; every stage was clean until citro3d, and the CPU renderer never froze it):
  (1) citro3d ends a frame asynchronously (the swap is a callback when its GPU queue finishes) and HOME takes the GPU away,
  so a frame in flight is never acknowledged, and every wait on the queue (`C3D_RenderTargetDelete`, `C3D_Fini`) never
  returns; skipping the teardown, as the code did since 2026-10-01, left the GPU half done for the next application.
  `AptHook` (`gpu_ppu_3ds.c`) drains the queue with an empty frame while the app still has the GPU, and the real close tears
  citro3d down on a thread with a timeout. Not one more frame runs after the quit event. Tried and not the cause: the 3D
  mode left on, `AffinityMask`, tearing citro3d down in the suspend hook (it then restarted wrongly on return: black).
  (2) The framebuffers were 32-bit (RGBA8, inherited from the upstream frontend): HOME cannot capture those. Both screens are
  now 24-bit like mzm's; the CPU drawing keeps its 32-bit pixels in a buffer per screen (`UiDraw_Screen`), converted by
  `UiDraw_Present` right before the swap. Why mzm never had either problem was not established (it already used 24 bits and its
  GPU queue is probably empty when HOME is pressed). `debug/sm-exit.txt` now also records the GPU steps of the exit.
- 2026-10-06: `v0.3.0` tagged on `main` as the stable release (the owner asked for the next version up; a minor bump, so it was confirmed, and
  "3.0.0" was read as v0.3.0). The release branch was renamed `release/v0.3.1`. The self-updater's first real test is the next release after
  this one (#42 stays open until a build installs a newer one).
- 2026-10-06: OPTIONS regrouped (owner's request). A slot of the grid is one button or two half buttons (IMAGE | VIEW, UPDATE | CHANNEL, FPS | CPU);
  AUDIO keeps a full button with a loudspeaker shortcut at its side (the button itself is meant to open a window with a volume for each kind of
  sound one day; for now a tap on either switches the sound). Frame skip became FRAMES with three values: AUTO (drop the drawing of a late frame,
  the old "frame skip on"), LOCK 30 (one frame in two is drawn, the logic stays at 60 Hz: a steady 30 on a console that cannot hold 60; after mzm's
  "lock 30") and NO SKIP (the old "off"). `config.ini` keeps `pacing` (the old `frameskip` is still read), `fps_overlay` is 0-4 (off and the four
  corners; the old 1 is the top-left corner) and `update_beta` (default on in pre-release builds). Not checked on the console yet.
- 2026-10-06: OPTIONS -> HUD (owner's request, as mzm's AUTO-HIDE HUD): with it on, the STATUS tab hides the HUD's status half on the top screen
  (energy, reserve, ammunition and the selected weapon: tile columns 0-25 of its four rows) and the MAP tab its minimap (columns 26-31);
  the cells are blanked in VRAM just before the PPU draws (`SmWide_HideHud`, `sm_wide.c`; the game rewrites rows 1-3 every frame, row 0
  holds the minimap's top border and is put back). The game's RAM is untouched (the `hud-hide-*` tests keep the WRAM hash). Since the weapon
  chosen with SELECT is then not on the top screen, the STATUS tab marks it (`hud_item_index`: missiles, super missiles, power bombs on their
  panels, grapple and X-ray on their cells). Off by default. Not checked on the console yet.
- 2026-10-07: release notes in the updater (P4.13), ported from mzm with these differences. The marker is `sm-notes`. The channel goes
  through `build/version.h` (generated, rewritten only when its text changes) and not through a `-D` flag in CFLAGS as in mzm: objects that
  include it rebuild by themselves, so no `make clean` when `CHANNEL` changes. A `-dev` build is never a beta (`CHANNEL` is empty
  outside CI), so `Updater_IsNewerBuild` leaves it to the plain comparison. The mock server is pointed at with `update_url.txt`, a file of
  its own, because `config.ini` is rewritten whole by the UI and would drop an extra line. The viewer is a window (`MODAL_NOTES`) over any
  tab instead of living on one tab, so the prompt's button needs no tab switch; while it is up the prompt hides and its touches go to the
  viewer. The notes of a tag are written when the tag is made (one summary of the whole range, not one per commit), see CLAUDE.md.
