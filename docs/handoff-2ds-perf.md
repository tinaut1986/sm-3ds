# Handoff: where the session of 2026-10-07 left off (2DS / Old 3DS performance)

Written so a new Claude session on another machine can continue without the chat. Read
`CLAUDE.md` and `docs/PLAN.md` first (they stay the source of truth); this file only adds what
is not written there yet. **Temporary:** when the work starts, fold what is still true into
PLAN's P2.5 and the decisions log, then delete this file and `docs/handoff/`.

The owner chats in Spanish (castellano); code, comments, docs and commits are English.

## 1. State of the repo (2026-10-07, evening)

| Thing | State |
|---|---|
| `main` | `v0.3.2` (stable, published). |
| `release/v0.3.3` | Current release line, pushed. Holds, after `v0.3.2`: P4.3 ticked in PLAN, and the **notes cache** (`update/notes.txt`, WHAT'S NEW works with no network; checked by the owner on the console). Nothing else yet. |
| `chore/author-name` | Pushed, **not merged**: the CIA's publisher field reads `snesrev, C. Averill, tinaut1986` (the SMDH publisher holds 32 characters). The owner has not seen it in HOME yet (a CIA with it was uploaded to the console). **Ask before merging.** |
| `chore/handoff-2ds-perf` | This file and the logs, to be deleted once read. |
| Releases | `v0.3.1` (first a beta, then promoted in place to stable), `v0.3.2` (stable, "Latest"). Issue #42 (self-updater) closed. |

This session shipped P4.13 (release notes in the updater, BETA marker baked into the build,
OPTIONS regrouped, notes cache); PLAN and CLAUDE.md already describe it, so it is not repeated.

## 2. How the owner works (from the previous sessions' memory notes)

- They test every change on their consoles and report in rounds. After a fix: build the debug
  CIA and **upload it over FTP without asking** (`/cias/sm-3ds-dev.cia`); verify by
  downloading it back and comparing md5 (an empty file is `d41d8cd9...`).
- Commit each finished step. **Merge into `release/*` only when the owner confirms it works
  on the console.** Before merging, squash the topic branch to one commit with only the useful
  change, merge `--no-ff`, push, delete the topic branch.
- Never claim a fix works on the console before they confirm: say "checked on the host".
- Bugs found while playing go to GitHub issues (`tinaut1986/sm-3ds`).
- Never commit on `main` or `release/*` directly; topic branches (`feat/`, `fix/`, `perf/`,
  `chore/`) are cut from the active release branch.
- Releases: follow CLAUDE.md "Releases" (write `docs/release-notes/<tag>.md` first, push `main`
  before the tag, rename the release branch to the next patch right after tagging).

## 3. Test setup

- Consoles: a **2DS** (the one in the logs; the Old 3DS CPU, 268 MHz, the system core gives the
  app 30 %) and a New 3DS. The FTP server is FBI's (port 5000). The IP moves with DHCP: this
  session it was `192.168.1.141` (earlier `.142`, and `.139` was a New 3DS in an older note);
  use the one the owner gives. `.3ds_ftp_ip` in the repo root keeps the last one used by
  `build_3ds.sh`.
- Data folder on the SD: `/3ds/Super Metroid 3DS/` (logs in `debug/`, `update/`, saves, ini).
- ROM for host tools (`SM_ROM=...`): on the old PC it was
  `/media/WD/Consolas y juegos/Juegos/SNES/Super Metrod/Super Metroid (Japan, USA).sfc`; on the
  new one ask the owner. Never commit it.
- Build and upload (see CLAUDE.md "Build"):
  ```sh
  export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
  export PATH=$PWD/tools/bin:/opt/devkitpro/tools/bin:$PATH
  make -j8 FULL_NATIVE=1 DEBUG_TOOLS=1 cia
  curl -T output/SuperMetroid3DSPort.cia ftp://<ip>:5000/cias/sm-3ds-dev.cia
  curl ftp://<ip>:5000/cias/sm-3ds-dev.cia | md5sum      # compare with the local file
  ```
  Careful: a build made right on a tagged commit calls itself the plain tag (`v0.3.1`); commit
  something on the topic branch first so it reads `v0.3.x-dev.N+hash`.
- Test the updater without publishing: `tools/update-mock-server.py` plus a one-line
  `update_url.txt` in the data folder (do not accept the install while pointed at it).

## 4. The performance question

The owner wants to start on smoothness on the 2DS / Old 3DS. Their words: they are **not**
interested in bisecting which version lost frames (many WIDE-related tweaks went in since
`v0.2.x`); the question is *where can it be cut from what exists now*. They saw ~45 fps on
average even in the Landing Site.

### 4.1 What the logs say (2DS, 268 MHz, audio on, WIDE on, PIXEL PERFECT, FRAMES auto)

Raw logs, from the debug build, are in `docs/handoff/logs/`: `sm-log-03.txt`
(v0.3.1-dev.2, 44 Landing Site samples), `sm-log-07.txt` and `sm-log-08.txt`
(v0.3.3-dev.2, several rooms), `sm-perf-00.csv` and `sm-log-09.txt` (the PERF RECORDER run, 4.5). The time column in the log looks like frames at 60/s (300
between samples = the 5 s period). The `stats:` line is the 5 s average; the `gpu:` and
`gpu build` lines are the **last frame** of the period (noisy, one frame).

Landing Site `91F8`, frames with **no tile-decode event** (0-1 tiles decoded):

| Stage | ms |
|---|---|
| game logic | 4.6-5.0 |
| `lines+bands` (analysis of the captured scanlines) | ~3.1 |
| BG quads (`bg`) | ~1.1 |
| submit (CPU pushing quads) | 2.4-3.5 |
| rest of draw (sprites, present, bottom UI...) | ~1 |
| **work** | **~13.5 of 16.7** |

Shown fps is 40-46 while the game speed stays 57-59: logic keeps up, the **drawing does not**.
What pushes frames over the budget:
- rain: 60-67 sprites (`sprites 0.2-0.6 ms` but also more quads and submit);
- tile-decode events: 600-2165 tiles in one frame cost 3-10 ms;
- room entry: **6138 tiles decoded in one frame = 26-36 ms** (`bg 26-37 ms`, seen at 9AD9 and 9969);
  a palette change in 9AD9 re-decodes 1280-1360 tiles (`bg 8.6-11.5 ms`).

Other spots (`work` ms, shown fps): Norfair `AF14`/`AFFB` 14-18 ms, 33-34 fps (250 quads, build
8-9, submit ~5); title/intro (room `0000`) 15 ms, 40-43 fps (182 quads, 32 bands, 49 sprites,
sprites 4.2 ms); Brinstar `9AD9`/`9969` 13-24 ms, 40-46 fps. Audio is clean: no underruns, 0-4 slow
callbacks per sample. The 2DS baseline of 2026-10-03 for the Landing Site was 52-55 fps shown,
work ~11 ms (draw ~5: build 3.0, submit 1.0), so there is a loss since then, but see the owner's
wish above.

One 4 s stall at the end of `sm-log-08` (work 255 ms, speed 11) was the owner **saving a state**
(known: `BottomUi_Busy` / the SD write blocks the game; PLAN P4.11 lists saving in the
background as left for later). Not a bug in the numbers.

### 4.2 Where to cut, as first proposed (see 4.5: the order changed once the PERF came in)

Code pointers are in `source/gpu_ppu.c` unless noted.

1. **Tile decode** (largest gain, medium risk). `SyncSurface` (~l.560) re-decodes the whole
   tilemap of a layer: 64x32 = 2048 entries per layer, three layers. `DecodeBgTile` (~l.535)
   and `DecodeTile` (~l.401) cost ~6 us per tile. Ideas:
   - decode only the tiles the visible window reaches (about 20-25 % of the tilemap, WIDE
     margins and the tilemap's wrap-around included) and only those that use the **palette row
     that changed** (`g_pal4_dirty` / `g_pal2_dirty` already know which); decode the columns
     that scroll into view on the frame they appear. This is PLAN P2.5's "after a palette
     change decode only the visible tiles", still open;
   - make `DecodeTile` write 32 bits at a time: in Morton order texel pairs are adjacent, so
     half the stores (the loop does `dst[m[x]] = lut[pix & 15]`, 64 16-bit stores per tile).
     Maybe 1.5-2x, low risk.
2. **`lines+bands` ~3 ms** in the Landing Site against 1.2 ms in the intro. Find out what makes
   that room expensive, and reuse the previous frame's band list when the captured per-line
   registers did not change (`EmitScreen` ~l.1231, `g_stats.t_lines` computed ~l.1507, printed
   from `source/main.c` ~l.643). Probable gain 1.5-2.5 ms on **every** frame.
3. **Submit 2.4-3.5 ms** (5 ms with 250 quads): read `PushQuad` / `DrawEye` in
   `source/gpu_ppu_3ds.c` for per-quad cost. No stereo on the 2DS, so one eye only. ~1 ms, unsure.
4. **Logic ~4.8 ms:** profile the game code on the PC in the same room (gprof / perf, no console
   needed) to find hot functions; the New 3DS shows ~1.9 ms for the same room, so it is the
   clock ratio, not a bug.
5. **~1 ms of `draw` not explained** by build + submit: suspected to be the conversion of the
   bottom screen (`UiDraw_Present`, ~77k pixels, estimated 1-2 ms in PLAN P2.5, never
   measured).

### 4.3 What I asked the owner for, and what I was about to do

(The PERF RECORDER run was made later the same evening: its result is section 4.5, and it
changes the order of 4.2.)

- A **PERF RECORDER** run (Debug tab -> DEBUG TOOLS -> PERF RECORDER, writes
  `debug/sm-perf-NN.csv`, `PRF` shows in the top bar) of ~30 s walking the Landing Site, with no
  new build. It gives the per-frame distribution: how many frames pass 16.7 ms and whether they
  are the tile-event frames or the rain ones. The 5 s log cannot say.
- Optionally, a second round with a build that adds counters: the split of `lines+bands`, the
  cost and count of `UiDraw_Present`, the per-quad submit cost. **Do not time inside
  `dsp_cycleBlock`** (PLAN P2.6: clock reads there made the intro pop on the 2DS).
- Tools already there: the SD log (starts at boot in debug builds, LOG MARK adds a mark), the
  PERF recorder, `docs/debug-tools.md`, the host harness `tools/gpu-ppu-test` (every room, GPU vs
  CPU renderer identical; `make test SM_ROM=...` ~90 s before merging anything that touches the
  renderer).
- ~~I was waiting for the owner to pick: start with 1 (tile decode), or first send the PERF.~~
  Superseded by 4.5: start with the two periodic events found there.

### 4.5 PERF RECORDER result (2026-10-07 16:31, 2DS, `v0.3.3-dev.2.1+cf0cc31`)

Files: `docs/handoff/logs/sm-perf-00.csv` (2587 frames = 43 s, gameplay `game_state 08`, Landing
Site as far as the owner said) and `sm-log-09.txt` (the same session's SD log). Columns:
`frame,logic_ms,draw_ms,audio_ms,work_ms,shown,game_state,area,room,audio_*`. `draw_ms` is 0 on
a skipped frame; **`work_ms` is the whole loop iteration**, so `work - logic - draw` is what
happens outside both: `BottomUi_Frame`, `UiDraw_Present(GFX_BOTTOM)` and the swap
(`source/main.c` ~l.960-1020). `area` and `room` are always 0 in this file (not filled by
`Debug_PerfFrame`? check, small). **Owner's answer:** it is not a recorder bug: the recording
started right after restoring a save state, and the game's area/room variables are 0 until it
runs on; the room was the Landing Site.

Result: 1987 of 2587 frames shown = **46.1 fps**, 600 skipped (runs of 1 or 2 frames), logic 5.1 ms
steady (p99 5.9), audio 10.6 ms (separate thread, not in `work`).

**The shortfall is entirely two periodic events.** The 1594 shown frames that have neither event
take **13.4 ms on average, p95 14.2, max 15.0**: all under the 16.7 ms budget. 598 of the 600
skipped frames follow one of these:

| Event | Frames | Period | Cost | Skipped frames it caused |
|---|---|---|---|---|
| **A. draw spike** | 303 | every **10** frames (gap 10 x231) | draw 18.7 ms against 8.1 normal (+10) | 419 |
| **B. outside logic+draw** | 368 (spans 2 frames: gap 1 x189, gap 14 x166) | every **15** frames, = `REFRESH_FRAMES` (`bottom_ui.c` l.28, l.2322) | +17.5 ms on average (the frames reach 19-37 ms) | 179 |

88 frames have both (the two periods meet every 30 frames: work 35-37 ms there).

- **B is the bottom screen's periodic full redraw.** `BottomUi_Frame` redraws the whole tab every
  15 frames "so live numbers and the clock keep moving", then `UiDraw_Present` converts ~77k pixels
  to the 24-bit framebuffer. That is the cost PLAN P2.5 estimated at 1-2 ms and never measured:
  it is ~13-17 ms **every 15 frames**. The tab was **OPTIONS**: the recorder
  is started on the DEBUG tab, but the owner switched to OPTIONS right after, a fairly static
  tab, so a ~17 ms redraw is what even a quiet tab costs, not a Debug-tab artefact.
  Still worth one recording on STATUS or MAP, which draw more. Fix ideas: redraw only the dirty
  region (the clock, the live numbers) or convert only the changed rows; do it far less often
  (the clock moves once a minute; only the Debug tab has numbers that move); never on the same
  frame as event A.
- **A is a Landing Site thing that happens every 10 frames.** The logs of the same room show
  frames with ~630-680 tiles decoded (`bg` 2-4 ms, `lines+bands` 4.4 against 3.1), so most likely
  an animated tile set or a palette cycle (a changed char or palette row makes `SyncSurface`
  re-decode every tilemap entry that uses it, then `GpuBackend_TexWritten` flushes the rows).
  630 tiles x 6 us = 4 ms does not explain +10 ms: the rest is not yet attributed (texture
  upload/flush? vram diff? sprites?). Needed: stage timings **per frame** in the CSV.
  Also check what in the game changes every 10 frames there (VRAM writes of the room's tile
  animation).
- Estimate (not measured): fixing only B gives ~51 fps, only A ~54 fps, both ~60 fps, since
  everything else fits in 13.4 ms.
- The 264 ms frame at 2393 (and 67 ms at 2398) is the end of the recording being written to the SD.

Next steps, in this order:
1. Add columns to the PERF CSV (and the log): `t_lines, t_diff, t_sprites, t_bg, tiles_decoded,
   quads` from `g_stats` (`gpu_ppu.c`, printed from `main.c` ~l.643), submit ms, and the time of
   `BottomUi_Frame + UiDraw_Present + swap` as its own column. One more recording then explains A.
2. Cut B first (clear, mechanical, no renderer risk): it also helps every other room and the New
   3DS. Check the bottom UI on the host with `tools/ui-preview` (it renders the tabs to PNG).
3. Cut A with what the extra columns show: the tile decode work of 4.2 point 1 is probably it
   (decode only the visible tiles of the changed palette/char), plus the cheaper `DecodeTile`.
4. Only then look at 4.2 points 2-5 (`lines+bands`, submit, logic): the steady frame already fits.

### 4.6 Second PERF run, after the bottom-screen change (2026-10-07 21:31, `v0.3.3-dev.10.1+afe829a`)

File: `docs/handoff/logs/sm-perf-01.csv` (2417 frames = 40 s, OPTIONS tab; the owner walked through
rooms 9C5E, A322 and A59F, not only the Landing Site, so logic is 6.0 ms here). The CSV now has the
per-stage columns (see `docs/debug-tools.md`).

**46.1 -> 50.1 fps shown** (398 frames skipped against 600). Event B, the bottom screen, is mostly
gone: `BottomUi_Frame` drew on 76 frames instead of ~170; when it does it costs ui 6.3 ms +
present 4.9 ms, and `present_wait_ms` (blocked in `gspWaitForVBlank` inside `UiDraw_Present`) reaches
**8.9 ms on average, up to 16 ms**, in the 57 presents that waited. Two things left there:
- 13 redraws come 120 frames apart, i.e. right after the battery poll in `BottomUi_Frame`: something
  in the clock/Wi-Fi/battery chrome changes at every poll. `ChromeChanged` now logs which field
  in the debug log (`bottom UI: redraw, wifi .. battery .. charging ..`), to see it in the next run.
- 31 redraws 15 frames apart: the tab was live part of the time (STATUS/MAP/DEBUG, e.g. when stopping
  the recorder on DEBUG).

**Event A is now the whole problem** (314 of the 398 skipped frames follow a draw spike). The new
columns, mean of the 287 spike frames against 1732 normal ones:

| | normal | spike |
|---|---|---|
| draw | 8.1 | 18.3 |
| gpu build | 5.3 | 8.9 |
| gpu **submit** | 1.8 | **8.5** (+6.7) |
| lines | 3.1 | 4.1 |
| bg | 1.1 | 3.6 |
| tiles decoded | 0.2 | **832** (628 in 223 of them; 1536 and 2164 on room entries) |
| quads / bands | 79 / 1 | 79 / 1 (the same) |

So a spike is a frame that **re-decodes ~628 tilemap entries** (every 10 frames in these rooms:
the tile animation of the room, or a palette cycle; `diff_ms` is 0.06 so the VRAM diff is cheap).
The decode itself is +3.5 ms of build (bg +2.5, lines +1). The bigger part is **+6.7 ms in submit**,
and the cause is in `source/gpu_ppu_3ds.c`: `FlushTextures` -> `CopyDirty` copies to the GPU's
texture the whole **span of 8-row blocks between the first and the last dirty row**
(`GpuBackend_TexWritten(tex, y0, y1)`, called from `SyncSurface` with the first and last tile row
touched), then flushes the data cache for it. 628 scattered tiles touch every tile row, so each
event copies and flushes both priority textures entirely (512x256x2 bytes x 2 = 512 KB) when the
tiles that changed are ~628 x 128 bytes x 2 = 160 KB, and `DecodeBgTile` also clears the same
block in the other texture even when it was already empty.

Cut A like this (**done in the next commit, 2026-10-07 evening**: 1 and 2 below plus a 32-bit `DecodeTile`; `make test` passes; to check on the console; 3 stays open, it would save under 1 ms with WIDE + PIXEL PERFECT because ~73 % of the tilemap is visible):
1. **Mark dirty per tile block, not per row span**: a bitmap per texture of its 64-texel blocks
   (128 bytes each, tiles are contiguous in the Morton layout: `block = (ty * (w >> 3) + tx) << 6`),
   and `CopyDirty` copies and flushes runs of dirty blocks (merging runs a few blocks apart to save
   cache-flush calls). The range version stays for the mode 7 plane and the rest.
2. **Do not clear the other priority's block when it is already empty**: `s->map[]` keeps the entry
   each tile was decoded with, so the previous priority bit says which texture held it; clear the
   other only when the tile moved (or the surface is fresh, or extra planes are involved).
3. Then decode **only the visible tiles** (4.2 point 1) if the build part (+3.5 ms) still matters.
Expected: a spike frame back to ~10-11 ms, under the budget with logic.
The host harness cannot check this path (the copy is 3DS-backend only): the check is on the console,
by the picture (missing or stale tiles in rooms with animated tiles, room entries, fades) and by a
PERF run. Also run `make test` since `SyncSurface` changes.

The 1298 ms frame at frame 72 is not a draw cost (the recording was started right after a load).

### 4.7 Third and fourth PERF runs (2026-10-07 22:00, `v0.3.3-dev.10.4+4ba9bc8`: dirty tile blocks, FORCE 3D)

Files: `docs/handoff/logs/sm-perf-02.csv` (2328 frames, FORCE 3D off) and `sm-perf-03.csv` (1692 frames,
FORCE 3D on; **the same room and spot** (the owner switched the option and recorded again), though the quad count differs: 52 mostly against 79, unexplained, the rain varies maybe). Scripts like the ones in this section are easy
to rewrite: group the shown frames by `tiles` and compare the columns.

**The per-tile copy did not help the spike: it made it a little worse.** 02: 50.3 fps (no change from
50.1), spike frames still every 10 frames, `gpu_submit_ms` 10.6 against 8.5 before, draw 19.6.
Only the build got cheaper (8.0 against 8.9: the 32-bit `DecodeTile`). The shape of the cost says why:

| tiles in the frame | frames | submit ms |
|---|---|---|
| 0 | 1409 | 1.75 |
| 1-50 | 267 | 1.9 |
| **300-700** (the every-10-frames event: 628) | 223 | **11.6** |
| 700-1700 (the 1536-tile ones) | 41 | **4.3** |
| 1700+ (room entry) | 10 | 14.3 |

Submit does not follow the number of tiles or bytes: 1536 tiles (**contiguous**: they fill whole
rows) are cheap, 628 **scattered** tiles are the worst. So the cost is per **flush call**:
`GSPGPU_FlushDataCache` is an IPC to the GSP, and `CopyBlocks` makes one per run of dirty blocks
(runs closer than `kRunGap` = 8 blocks merge), maybe 40+ runs a texture for 628 scattered tiles. The old
code made two calls and copied 512 KB (8.5 ms); this one copies ~160 KB with many calls (11.6 ms).
The per-call cost is estimated at ~0.1 ms, not measured: the build `v0.3.3-dev.10.5+82c63ed` adds
`tex_copy_ms, tex_flush_ms, tex_runs, tex_kb` to the CSV (copy and flush timed apart, calls and KB), and
the next recording fits both costs (copy ms per KB, flush ms per call). Then choose the run size
(merge runs when the clean blocks between them cost less to copy than a call: if a call is ~0.1 ms and
copying ~13 us/KB, runs up to ~60 blocks apart should merge, which at 31 % density is one run per
texture again, i.e. no gain over the old code; then the way forward is fewer dirty tiles or
a cheaper upload, below).

Ideas if the fit says copying dominates (the flush calls do not): the animation changes few chars but
628 tilemap entries use them; **decode and upload per distinct char instead of per entry** is out of
reach in this design, but **uploading as 8x8 blocks with the GPU's own copy** (`GX_RequestDma`,
the source flushed once) would take the memcpy off the CPU. Another is a different texture
layout for the BG: the tile chars in an atlas (once per char) and the tilemap as quads or a lookup
(a bigger change; the renderer draws the tilemap as a texture today).

**FORCE 3D (03): 45.9 fps against 50.3**, same room and spot. A normal frame (no tile event) costs:

| | FORCE 3D off (79 quads) | on (52 quads) |
|---|---|---|
| draw | 8.0 | **10.7** (+2.7) |
| gpu build | 5.3 | 5.9 (+0.5) |
| gpu submit | 1.8 | 2.2 (+0.5) |
| draw outside build + submit | 0.9 | **2.7** (+1.7, not explained) |
| work | 13.5 | **16.2** (p95 18.2) |

The second eye costs ~2.7 ms per frame with fewer quads, so it is not the amount drawn; two thirds of
it falls outside the build and the submit as measured (look in `DrawAndPresent` before `t1`, in
`main.c` around the build call, and the per-eye setup). With FORCE 3D the steady frame is on the
budget by itself (127 skipped frames with no event, "other"): even with the spike fixed, an Old 3DS
with the slider up would hover at 55-60 fps with frequent skips, and then the fixed costs matter
(`lines+bands` 3.4, submit 2.2, logic 5.2, and that 1.7 ms).

Also: the bottom screen's redraws are down to ~72 in 39 s (gaps 15 and 120: the log shows no
"bottom UI: redraw" line with a non-clock field, so the 120-frame ones are the minute or ... check
once the log has run longer).

### 4.8 Fifth and sixth PERF runs (2026-10-07 22:06, `v0.3.3-dev.10.5+82c63ed`): the texture upload, measured

Files: `docs/handoff/logs/sm-perf-04.csv` (26 s, FORCE 3D off, 51.7 fps) and `sm-perf-05.csv` (19 s, FORCE 3D on,
45.6 fps), same room and spot, quads 79 in both this time (the 52 of the earlier run is unexplained, and not a
difference of the mode).

**The upload has a clean cost model** (fit on the 1358 frames of 04 that uploaded something):
- one `GSPGPU_FlushDataCache` call costs **0.108 ms whatever its size**, plus 0.0016 ms/KB (the size hardly matters);
- the `memcpy` into the texture costs **0.0109 ms/KB** (~91 MB/s), ~1.4 us per 128-byte block;
- a normal frame uploads 32 KB in 1 run (the sprite atlas): 0.34 + 0.24 = **0.58 ms every frame**;
- the **628-tile event: 67 runs, 262 KB: copy 2.9 + flush 7.7 = 10.6 ms** of its 11.9 ms submit. The 1536-tile
  frames: 3.3 runs, 224 KB: 0.8 + 2.4 = 3.2 ms. So the cost is the **number of flush calls**, as suspected; the
  per-tile marking made one call per scattered run (67), the old code two calls but 512 KB of copy.

So the right run size follows the model: a clean gap of g blocks costs 1.4 us per block to copy through and a
call costs 108 us, so runs closer than ~80 blocks should merge. `kRunGap` is now 80 (was 8); the prediction is
~4 runs and ~480 KB (copy ~5.2 + flush ~1.1 = ~6.3 ms) for the event instead of 10.6: about -4.5 ms on the
spike frame, which does not by itself bring it under the budget (logic 5.3 + draw ~15.6). The build
`v0.3.3-dev.10.8+37dbc26` records it (`tex_*` columns) and the whole `DrawAndPresent` call as `dp_ms`.

What would take the copy itself (91 MB/s on the CPU) off the frame, the next lever:
- `GX_RequestDma(src, dst, length)` (libctru `gx.h`): the GSP's DMA copies the runs, the CPU only queues them; the
  shadow's dirty range is flushed with one call per texture (0.0016 ms/KB: ~0.8 ms for 512 KB), the DMA waited
  with `gspWaitForDMA` or ordered before the render command list. Untested: ordering against the GPU's own
  commands, and the destination cache. Estimated submit for the event ~3-4 ms instead of 11.9.
- Or fewer re-decoded tiles: 628 entries re-decode every 10 frames, probably because a **palette row** is animated
  (`g_pal4_dirty`): the decoded texels bake the palette in, so every tile using that row is rewritten. The visible
  share (~73 % with WIDE) is the only cut available short of a GPU-side palette, which the PICA200 cannot do.

**FORCE 3D (05), same room**: a normal frame is draw 10.9 against 8.1 (build +0.6, submit +0.5, **rest +1.8**:
0.96 -> 2.75 ms outside the build and the submit), work **16.4 ms mean** (p95 18.3): over the budget by itself (104
frames skipped with no event). The new `dp_ms` column splits the rest: how much is inside `DrawAndPresent` and how
much before it (overlay, toast, setup).

### 4.9 Seventh and eighth PERF runs (2026-10-07 22:18, `v0.3.3-dev.10.8+37dbc26`: runs merged at 80 blocks)

Files: `docs/handoff/logs/sm-perf-06.csv` (24 s, FORCE 3D off) and `sm-perf-07.csv` (25 s, on), same room and spot.

**FORCE 3D off: 55.7 fps** (51.7 before; 103 frames skipped of 1453). The upload change did what the model said:
the 628-tile event now has 5 runs and 389 KB (copy 3.85 + flush 1.08 ms, against 67 runs and 10.6 ms), submit
**6.0 ms against 11.9**, draw 14.0 against ~20. What is left, in frames drawn over 16.7 ms of work:

| cause | frames | note |
|---|---|---|
| the 628-tile event (every 10 frames) | 138 | draw 14.0 + logic 5.3 = 19.3: one skipped frame each |
| the 1536/2164-tile events (every ~4 s) | 41 | draw 17-24; `bg_ms` 7.9, `tiles` 1643 mean: a bigger re-decode |
| the bottom screen (redraw) | 10 | the 120-frame ones |
| steady frames | 2 | the steady frame never overruns |

Next cuts for the 628 event, in order: (1) the copy is still 3.85 of its ~5 ms of upload and the CPU does it at
91 MB/s: `GX_RequestDma` or `GX_TextureCopy` (one call per texture, synced with `C3D_SyncTextureCopy`'s
PPF event; **not** several DMA requests waited with `gspWaitForEvent`: the DMA event is one `LightEvent` that
coalesces, N waits can hang) would save ~2 ms; (2) its decode, +2 ms of `bg_ms` (visible tiles only would give
~25 % of it).

**FORCE 3D on: 46.4 fps, and the steady frame is the problem, not the events.** The steady frames overrun on their
own (485 of the 978 steady frames, mean 18.0 ms of work): `gpu_wait_ms` (the CPU waiting in `C3D_FrameBegin` for
the GPU to finish the previous frame) is **1.47 ms** per frame against 0.01 without the second eye, and
`DrawAndPresent` costs 3.73 ms against 1.83: the **GPU is the bottleneck with two eyes**, ~16 ms a frame for the
two renders, so the CPU waits. Fixing the CPU spikes will not give 60 there. The `gpu_draw_ms`, `gpu_proc_ms`
and `cmdbuf` columns (build `v0.3.3-dev.10.10+d6757f1`) give the GPU's own time. What the GPU does per eye: the BG bands
onto the 512x256 main target (and the sub target when colour math uses the subscreen), then the main target
onto the 400x240 top target with the stereo plane offsets; ideas to test once the numbers say where its time
goes: fewer passes (compose once when no plane is shifted), a smaller main target, or skipping the sub target.

### 4.4 Constraints to respect when changing the renderer

- The GPU output must stay pixel-identical to the CPU renderer (host tests; max error 8 on the
  console is the known 8-bit maths).
- WIDE margins, the extra rows above and below the picture (PIXEL PERFECT), stereo planes
  (`SetSlotPlanes`, per-room plane fixes) all hook into the same surfaces: a "visible only"
  decode must cover them, and the tilemap wraps at 512 px.
- Measure on the 2DS, not Azahar, and keep `make test` green. Tick PLAN tasks only after the
  owner confirms on the console.

## 5. Loose ends

- `chore/author-name` waits for the owner (see section 1).
- PLAN's Status says `release/v0.3.3 (nothing yet)`: update the branch table when a perf branch
  opens (`perf/...`, cut from `release/v0.3.3`).
- The owner may say "la 2DS" for the Old 3DS family; the New 3DS is the one that already holds
  60 fps.
