# Super Metroid 3DS: action plan

Living document. Read it at the start of every session; keep it current as things
change (what goes where: the table in CLAUDE.md).

- **Goal:** a native 3DS port of Super Metroid that is completable start to
  finish, runs at 60 fps on New 3DS and as close as possible on Old 3DS/2DS,
  and has stereoscopic 3D with per-layer depth, like `../mzm`.
- **Starting point:** `CharlesAverill/sm-3ds` (forked as `tinaut1986/sm-3ds`),
  which wraps `snesrev/sm` in a thin SDL2 frontend. Upstream claims ~50 fps on
  hardware (model not stated) and unreliable saves on hardware.

## Status (2026-10-02)

Only what no other place records. Bugs: the open GitHub issues. Tasks: the unticked
boxes below. History: `git log` and the decisions log.

**Release line:** `release/v0.1.4`. Last stable: `v0.1.3` (2026-10-02: WIDE fixes, debug
tools pass, Ceres escape fixes); before it `v0.1.2` (2026-10-01) and betas `v0.1.0`, `v0.1.1`.

**Branches waiting for the owner's check on the console** (merge into the release line
with `--no-ff` only after they confirm, closing the issues that turned out fine): none.

**Priority** (owner's order; the reasons are in the decisions log):
P0.3 → P4.5 open issues (before Phase 3: stereo is designed with the margins already
there) → Phase 3 (P3.1 first) → P1.3 → P2.5 → P1.9 E, P1.7, P0.4 when useful.

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
- [ ] **P0.3** Measure the baseline.
  *Spec:* FPS in fixed spots (Ceres intro, Landing Site, Brinstar, a Norfair
  heat room, Maridia water) on Old 3DS/2DS and New 3DS, with and without audio
  and `FULL_NATIVE`. Record in a table below.
  *Done when:* table filled in.
  Status 2026-10-01: Landing Site on both, title/intro/Ceres on the 2DS (see the
  table). Tools: the debug log (every 5 s: speed, work, logic, GPU build/submit, audio)
  and Debug tab -> PERF (`debug/sm-perf-NN.csv`).
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
- [ ] **P1.3** Replace SDL2 with libctru directly: `hid` input, NDSP audio
  (from mzm), citro3d presentation. Drop the `SDL` submodule.
  *Done when:* same features as upstream, SDL gone, FPS not worse than P0.3.
  Note: SDL currently puts the audio thread on the system core (30 % cap) and
  delivers touch as finger events; keep both behaviours.
- [ ] **P1.4** Present the frame on the GPU: upload the 256x224 PPU output as
  a texture, scale with citro3d.
  *Done when:* no per-pixel CPU copy remains in the frontend.
  Status 2026-10-01: with the GPU renderer (on by default) citro3d draws and presents
  the top screen; the table-driven CPU copy (`DrawPpuFrame`) is only used for frames
  drawn by the CPU renderer (refused by the GPU path, or the renderer switched off).
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
  Status 2026-10-01: done except remapping. Options tab: pause, turbo,
  frameskip, audio, FPS overlay, 804 MHz, save state slots, reset;
  `config.ini` in the data folder keeps tab, frameskip, audio, overlay, 804 MHz
  and slot (not pause/turbo/cheats on purpose).
- [x] **P1.8** Debug tooling like mzm's.
  Done 2026-09-30, used on hardware to diagnose real bugs (load-state assert,
  teleport crash, stale door drawing, audio lock stall): log to SD with marks,
  screen dump sets (top RGB, VRAM, CGRAM, OAM, WRAM, PPU regs, game state),
  frame-time recorder CSV with the audio split, `__assert_func`/`Unreachable()`
  crash note with file:line. All in `debug/` of the data folder, fetch over FTP.
  Follow-ups go to P1.9 E.
- [ ] **P1.9** Bottom UI in the style of mzm's, rewritten for SM data.
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
  - **E. Debug tab, still open:** a scene recorder (many frames), PC-side viewers
    for the dump files, mzm's low-energy tab tint. The PPU/HDMA dump is done
    (FRAME DUMP, F).

## Phase 2: performance (target 60 fps on Old 3DS)

- [x] **P2.0** Cheap CPU wins found by profiling (2026-09-30), checked on hardware
  (New 3DS holds 60 fps in Landing Site with no frame skipped).
  `snes_handle_pos_stuff` ran 154k times per frame and only acts at hPos 0, 512
  and 1024; `snes_handle_scanline` visits just those (bit-identical on the host,
  4000 frames). Table-driven top-screen copy (5.0 -> 2.8 ms).
- [ ] **P2.1** Profile. Split frame time into game logic, PPU, audio, present;
  write `docs/perf.md` with the numbers.
  Status: the split exists (perf CSV, the debug log, decisions log) with Old 3DS
  numbers; `docs/perf.md` not written (the decisions log has the numbers).
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
- [ ] **P2.4** Implement P2.3 incrementally; a frame-diff tool against the
  CPU PPU (like mzm's `tests/rec_render.c`, `tools/compare_render.py`).
  Status 2026-10-01: frame-diff tool is `tools/gpu-ppu-test` (host, in `make test`) and
  GPU CHECK (console). Host: every room, a new game through Ceres exploding, power bombs,
  file select identical to the CPU renderer, nothing refused; New 3DS: 6 sets, max
  error 8. Open for "done": the remaining P0.3 spots on Old 3DS.
  *Done when:* gameplay rooms render on the GPU pixel-identical to the CPU
  path, and Old 3DS reaches the target in the P0.3 spots.
  Note: the host harness's PPU/VRAM does not match the console's yet (seen while
  debugging the teleport); fix that before relying on host frame diffs.

- [ ] **P2.5** GPU renderer leftovers (moved from the old "Next up", 2026-10-02).
  *Spec:* sprites are decoded every frame (~0.5 ms on the 2DS) and the line analysis
  costs ~1.1 ms; after a palette change decode only the visible tiles; check the X-ray
  scope on hardware (never seen there). *Done when:* each is either measured and cut on
  the 2DS or noted here as not worth it, and the X-ray scope has been seen on the console.

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
  Status: the live map and items/equipment exist (P1.9 A and C, debug-flavoured).
  Open: player-facing polish, touch shortcuts.
- [ ] **P4.2** Bezel/borders for the unused top-screen area.
- [ ] **P4.3** Self-updater.
- [ ] **P4.4** RetroAchievements (softcore only).
- [ ] **P4.5** Display options: PIXEL PERFECT / SCALED, and WIDE (more of the room on
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

- [ ] **P4.6** FRAME SKIP off warns. It is on by default (`frameskip` in `config.ini`) and
  keeps heavy scenes playable (the Ceres escape shaft ran at 20-30 fps without it); players
  who do not know what it does may switch it off and blame the port. Turning it off in
  OPTIONS shows a short toast ("FRAME SKIP OFF: heavy rooms may slow down"); turning it on
  says nothing. The option stays (useful for debugging and for those who prefer it).
  *Done when:* the toast shows on every switch to off, not at boot with it saved off.

## Phase 5: completion

- [ ] **P5.1** Full 100% playthrough on hardware, bugs filed as issues.
- [ ] **P5.2** Any-% and known sequence breaks (wall jumps, shinespark,
  mockball) behave like the original.
- [x] **P5.3** First stable release. Done 2026-10-01 as `v0.1.2` (the owner chose a patch
  number, not a minor bump): Release builds come without DEBUG_TOOLS, betas keep them.

---

## Baseline measurements (P0.3)

| Spot | Old 3DS | New 3DS | Notes |
|---|---|---|---|
| Title / intro / Ceres | 2DS, GPU renderer (mode 7): 50-55 fps (owner, overlay). Was 22-30 with the CPU renderer | | 2026-10-01 |
| Landing Site | 2DS, GPU renderer, no frameskip: 59.8 fps, work ~11 ms (logic 5.3, draw 5.0 = build 3.0 + submit 1.0), audio clean. CPU renderer: speed ~49, shown ~16 | 60 fps, work 13.7 ms avg / 15.4 p95 (CPU renderer) | N3DS 2026-09-30, all on, 804 MHz; logic ~1 ms, PPU ~8.4, top copy 2.8. 2DS 2026-10-01 |
| Brinstar | | | |
| Norfair heat room | 2DS, demo (AFFB): ~36 fps before per-line quads (build 15-17 ms composing rows); owner reports 50-55 after | | numbers pending from the log |
| Maridia water | 2DS (D340): 40-50 fps with submit ~7 ms before the priority ordering; owner reports fine after | | numbers pending from the log |

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
