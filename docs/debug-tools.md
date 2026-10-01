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
release workflow passes `DEBUG_TOOLS=1` while every release is a beta (PLAN P5.3).

## Bottom screen (debug build)

| Tab | Debug extras |
|---|---|
| MAP | Tap a cell to pick a room, WARP HERE loads it (DOOR n/m picks which door into it). See the decisions log for how the warp works. |
| STATUS | Tap an item or beam to add/remove it. GOD = energy refilled every frame. MAX = capacities to the maximum and ammo kept full; off gives the old capacities back. MAP STATIONS boxes: each tap cycles real -> map station used (all cells show) -> every cell explored -> real (restored exactly). Unlocking a map is what makes its rooms visible for a warp. |
| DEBUG | Timings, game state, raw boss bits, and the DEBUG TOOLS window. |
| STATES | 10 save-state slots (all builds). |

DEBUG TOOLS window: SCREEN DUMP, FRAME DUMP, LOG TO SD, LOG MARK, PERF RECORDER,
PPU RENDER (off = measure the game without the PPU), GIVE ALL, FULL HEAL.

A loaded state or a reset turns MAX off without restoring (the state brings its own
values) and forgets the forced maps.

## Files

All in `debug/` of the data folder (`sdmc:/3ds/Super Metroid 3DS/debug/`); fetch
them over FTP. Captures rotate over 10 slots: the newest is whichever slot was free
or least recently written, so sort a listing by time, not by name.

| File | Contents |
|---|---|
| `sm-dump-NN-top.rgb` | PPU output, headerless RGB8, 256x240, top to bottom |
| `sm-dump-NN-vram.bin`, `-cgram.bin`, `-oam.bin`, `-highoam.bin` | PPU memories |
| `sm-dump-NN-wram.bin` | the whole 128 KB of WRAM (`g_ram`) |
| `sm-dump-NN-ppu.txt` | PPU register state at the end of the frame |
| `sm-dump-NN-game.txt` | game state, room, Samus |
| `sm-dump-NN-frame.txt` | FRAME DUMP only: every PPU register write of the frame (below) |
| `sm-log-NN.txt` | the SD log, one per LOG TO SD session |
| `sm-perf-NN.csv` | frame-time recorder |
| `sm-crash.txt` | assert / `Unreachable()` notes (all builds) |

Save states are `saves/saveN.sav` with `saves/saveN.txt` (where and when, for the
STATES tab).

## FRAME DUMP

A normal dump set of the frame just drawn plus `sm-dump-NN-frame.txt`: every write to
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
