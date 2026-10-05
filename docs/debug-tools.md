# On-device debug tools

What the bottom screen offers for debugging, where its files land and how to read
them. Modelled on `../mzm/docs/3ds-debug-tools.md`.

## Builds

Everything below except the save states exists only in a `DEBUG_TOOLS=1` build:

```sh
./build_3ds.sh                          # asks; debug is the default
make -j FULL_NATIVE=1 DEBUG_TOOLS=1 cia
```

`DEBUG_TOOLS` reaches the code through `build/build_config.h`, which the Makefile
rewrites only when the value changes, so switching it rebuilds `bottom_ui.c` and
nothing else; no `make clean` needed. A plain `make` is a production build. The CI
release workflow passes `DEBUG_TOOLS=1` for Beta builds (tag not reachable from `main`)
and 0 for Release ones, so stable CIAs have none of this.

## Bottom screen (debug build)

| Tab | Debug extras |
|---|---|
| MAP | Tap a cell to pick a room, WARP HERE loads it (DOOR n/m picks which door into it). See the decisions log for how the warp works. |
| STATUS | Tap an item or beam to add/remove it; ALL (the free cell after the items) gives every item and beam, and lights up when they are all there. GOD = energy refilled every frame. MAX = capacities to the maximum and ammo kept full; off gives the old capacities back. MAP STATIONS boxes: each tap cycles real -> map station used (all cells show) -> every cell explored -> real (restored exactly). Unlocking a map is what makes its rooms visible for a warp. |
| DEBUG | Timings, game state, raw boss bits, and the DEBUG TOOLS window. |
| STATES | 10 save-state slots (all builds). |

DEBUG TOOLS window: SCREEN DUMP, FRAME DUMP, LOG TO SD (below), LOG MARK, SCENE REC
(below), PERF RECORDER, RENDERER
(GPU by default, CPU for the session; see docs/gpu-ppu-design.md), PLANE TINT (below) and GPU CHECK (draws the next frame with both
renderers: a dump set whose `-top.rgb` is the CPU's and `-gpu.rgb` the GPU's, and a
toast with how many pixels differ). Dumps and frame captures are always drawn by the
CPU renderer, even with RENDERER on GPU.

A loaded state or a reset turns MAX off without restoring (the state brings its own
values) and forgets the forced maps.

The top bar shows `REC` while the scene recorder runs, `PRF` while the perf recorder
does, and `CHT` while GOD or MAX is on.

## REPORT: saying what is wrong

SCREEN DUMP, FRAME DUMP and the stop button of SCENE REC do not write at once: the game
pauses and the bottom screen asks what is wrong. Eight generic reasons (3D DEPTH WRONG, WIDE
VIEW BUG, SPRITE MISSING, SPRITE FLASHES, WRONG COLOURS, GLITCH OR GARBAGE, GAME LOGIC,
PERFORMANCE), WRITE IT... (the 3DS keyboard, any text up to 119 characters) and CANCEL.
A reason writes the capture plus `sm-<kind>-NNNN-note.txt` with it (`Debug_SetNote`, taken by the
capture code through `Debug_WriteNote`); the game goes back to what it was (paused stays
paused after the one frame the dump needs). CANCEL writes nothing, neither file nor note. Stopping a recording
has two exits instead of CANCEL: the play triangle goes back to the game with the recorder still
running, and the cross discards the recording (no file, no note). Nothing else works on the bottom screen while the window is open.
The dump is of the frame after the pause, not of the instant the button was tapped.

## PLANE TINT

GPU renderer only; the cell cycles OFF, PLANES, DRAW ORDER and STEREO DEPTH. PLANES: every quad of the frame is painted in the flat, opaque colour of the
stereo plane it was put on (`StereoDepth_Plane`), keeping its own silhouette (a see-through mix hid the
difference between a dark picture and a far plane); the 3D offsets still apply, so with the slider up each colour also moves by its
own amount. The window shows the legend (nearest first): HUD magenta, FRONT cyan, PLAY
orange, OBJ white, BACK red, MID green, FAR blue (a flat non-gameplay screen is grey).
It is the on-console counterpart of `STEREO_PLANES=a-b` in `tools/gpu-ppu-test`, which
writes one image per plane on the host (`STEREO_QUADS=1` also prints every quad's level
and plane). The setting lasts for the session. The CPU renderer has no planes: the cell
says so.

DRAW ORDER (green) and STEREO DEPTH (blue) each use one colour, dark at the back and bright in front, so
which of the two is on shows at a glance; the window shows the ramp. DRAW ORDER is how the game draws the quad: its compositor level (backdrop 0, BG3 priority 0
1, sprites priority 0 2, sprites priority 1 6, BG2 priority 0 7, BG1 priority 0 8, sprites priority 2 10,
BG2 priority 1 11, BG1 priority 1 12, sprites priority 3 14, BG3 priority 1 and the HUD 15). STEREO DEPTH is
where 3D puts the same quad: the shift of its plane at full slider, far to near, planes set by hand included
(`sm_plane_fixes.inc`). Compare the two: what is brighter in one than in the other is drawn in front but
sits behind in 3D, or the other way round, which is what the depth fixes are about.

## Files

All in `debug/` of the data folder (`sdmc:/3ds/Super Metroid 3DS/debug/`); fetch
them over FTP. Dumps and scene recordings are never overwritten and have no limit: each takes
the number after the last one written (4 digits, `sm-dump-0007-*`, `sm-rec-0003.bin`), which
`sm-<kind>-last.txt` remembers (`dump`, `rec`); the owner clears them out after going through
them. The SD log and the perf recorder still rotate over 10 slots (2 digits). The file times are
not used for any of it: on the console they did not tell slots apart (most likely libctru's
`stat()` leaves `st_mtime` at 0; it has a separate `archive_getmtime`), which is why every dump
once landed in slot 00 (issue #8).

| File | Contents |
|---|---|
| `sm-dump-NNNN-top.rgb` | PPU output, headerless RGB8, 256x240, top to bottom |
| `sm-dump-NNNN-vram.bin`, `-cgram.bin`, `-oam.bin`, `-highoam.bin` | PPU memories |
| `sm-dump-NNNN-wram.bin` | the whole 128 KB of WRAM (`g_ram`) |
| `sm-dump-NNNN-ppu.txt` | PPU register state at the end of the frame |
| `sm-dump-NNNN-game.txt` | game state, room, Samus |
| `sm-dump-NNNN-frame.txt` | FRAME DUMP only: every PPU register write of the frame (below) |
| `sm-dump-NNNN-gpu.rgb` | GPU CHECK only: the GPU renderer's output read back, like `-top.rgb` |
| `sm-log-NN.txt` | the SD log. Debug builds start it at boot (one per session; LOG TO SD toggles it): console model, the core-1 time limit granted, every settings change (CPU clock, renderer, audio, display mode, WIDE, pause), and every 5 s the speed, shown fps, work/logic/draw times, the GPU build stages, submit and GPU wait, bands and quads, tiles and mode 7 cells decoded, and the audio thread's health (block time, callbacks slower than their buffer, late starts); then each exit step |
| `sm-exit.txt` | the steps of the last exit, in every build: if closing hangs, the last line says where |
| `sm-perf-NN.csv` | frame-time recorder |
| `sm-rec-NNNN.bin` | scene recorder (below) |
| `sm-dump-NNNN-note.txt`, `sm-rec-NNNN-note.txt` | what the owner said was wrong when taking it (REPORT, below): one line |
| `sm-crash.txt` | assert / `Unreachable()` notes (all builds) |
| `retroachievements.log` | RetroAchievements, all builds: each server call (URL without its query, which carries the token), login, game load, unlocks, errors |

Save states are `saves/saveN.sav` with `saves/saveN.txt` (where and when, for the
STATES tab).

Cells for something that runs (LOG TO SD, SCENE REC) work as in mzm: the button on
their right starts it (green triangle) or stops it (red square), and the rest of the
cell changes its option.

## LOG TO SD

Side button: log on/off. The rest of the cell: how the log reaches the card. BUFFERED (the default): lines collect in a 16 KB RAM
buffer and are written a block at a time, when it fills, on a LOG MARK, when the log or
buffering is switched off, at every exit step and on an assert. DIRECT: each line is
written and flushed at once, an SD write per line (slower), so a hang or crash cannot
swallow the last lines. The setting lasts for the session.

## SCENE REC

Records the top screen as it was shown, frame by frame, like a video: what the GPU
renderer presented (read back from its 400x240 target) or what the CPU renderer wrote
to the framebuffer, FPS overlay and WIDE margins included. For glitches that last a
few frames, which a single dump never catches.

- Side button to start. It reserves a RAM ring (the biggest of 32, 24, 16, 8 or 4 MB that leaves
  the game 4 MB of heap); the toast says how many seconds it keeps. Nothing is written
  while recording, and it never stops by itself: the ring keeps the **last** seconds.
- Play until the problem shows, then tap the (now red) side button right after. That writes
  `sm-rec-NNNN.bin` (the game freezes a few seconds while it does) and frees the ring.
- The rest of the cell picks the rate (only while stopped; while recording it shows
  frames held / ring size): 60, 30 or 15 Hz, i.e. every shown frame, one in
  2 or one in 4. Frames the frameskip did not draw are not in the file either; the
  `game_frame` column shows the gaps. 32 MB hold ~170 frames: ~2.8 s at 60 Hz, ~11 s
  at 15 Hz.
- Each recorded frame costs a framebuffer copy and, with the GPU renderer, a wait for
  the GPU plus a readback: expect the frame rate to drop while recording.

On the PC: `tools/scene-rec/decode.py sm-rec-NNNN.bin [OUTDIR] [--crop] [--mp4] [--scale N]`
writes `frame-NNNN.png` and `frames.csv` (game frame, game state, room, Samus x/y,
logic and draw ms, renderer, WIDE) and, with `--mp4`, a video via ffmpeg. `--crop`
drops the black bars of 4:3 frames.

Format (little endian): a 64-byte header (`"SMREC1"` padded to 8 bytes, u16 width 400,
u16 height 240, u32 frames, u32 rate divisor, 44 bytes of build version), then per
frame, oldest first: a 20-byte head (u32 game frame, u16 game_state, u16 room_ptr,
u16 Samus x, u16 Samus y, u16 logic ms x100, u16 draw ms x100, u8 flags: bit 0 GPU
renderer, bit 1 WIDE; 3 pad bytes), a u32 count of 16-bit words and the words. They
run-length pack the 400x240 RGB565 pixels in framebuffer order (column x from the
left, each column from the bottom row up): a word with bit 15 set is a run of
`word & 0x7FFF` copies of the next word; otherwise it is the number of literal pixels
that follow.

## FRAME DUMP

A normal dump set of the frame just drawn plus `sm-dump-NNNN-frame.txt`: every write to
`$2100-$213F` during that frame, stamped with the scanline. It is what the GPU
renderer design (PLAN P2.3) needs: which registers a scene changes per line (HDMA,
IRQ splits) and which stay fixed for the frame.

- Line `-1` is the game logic, which runs in vblank before the frame is drawn.
- Lines `0..224` are written while `DrawFrameToPpu` renders: HDMA writes at the end of
  line N take effect from line N+1; the IRQ handler (the HUD split) runs after its
  line and is stamped with the next one.
- Consecutive writes to a data port (`OAMDATA`, `VMDATAL/H`, `CGDATA`) are folded into
  one entry with `xN`.
- `[hdma]` lists the 8 DMA channels as the frame left them: `hdma=1` means HDMA on,
  `dest` is the B-bus register, `table` the A-bus table address (it does not move).
- `[summary]` has one line per register: writes in vblank, writes during the lines,
  on how many lines, and the first and last line.

Example, Crateria (room `93D5`): channel 7 rewrites BG3 scroll on lines 0-31 and the
IRQ at line 32 switches `BG3SC`, `TM` and colour math, which is the HUD/gameplay split.

### On the PC

`tools/frame-capture/run.sh ROM STATE.sav [FRAMES] [OUTDIR]` loads a save state
copied from the console, runs FRAMES frames and writes the same set plus a PNG. It
builds 32-bit with `-malign-double` (`HOST_CFLAGS` overrides) because a state's size
depends on the emulator struct layout, and that is the x86 layout that matches the
3DS (a 64-bit build refuses console states).
