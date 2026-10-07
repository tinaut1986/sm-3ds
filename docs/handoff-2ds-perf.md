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
(v0.3.3-dev.2, several rooms). The time column in the log looks like frames at 60/s (300
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

### 4.2 Where to cut, in the order I proposed (the owner has not chosen yet)

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
- I was waiting for the owner to pick: start with **1 (tile decode)**, or first send the PERF.
  My recommendation: start with 1, since the logs already make it clear, and get the PERF in
  parallel.

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
