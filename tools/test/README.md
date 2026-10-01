# Host regression tests

`make test SM_ROM=/path/to/rom.sfc` (or `tools/test/run.sh [--full] [--update] ROM`)
runs everything that can be checked on the PC and prints PASS/FAIL per check; it
exits non-zero if anything failed. About 90 s; `--full` adds the teleport test (ASAN,
~2 min more). The ROM is never stored: every state is generated from it (an empty
SRAM, scripted inputs, warps).

| Test | What | Checks |
|---|---|---|
| dsp-fuzz | random S-DSP register writes, optimised `dsp.c` against the vendored one | samples, registers and APU RAM identical (no ROM needed) |
| gpu-rooms | every room reachable by a door, 10 frames each | GPU list == CPU renderer, nothing refused, WRAM hash |
| gpu-newgame | power-on, title, intro, new game, Ceres (mode 7), Ceres exploding (`CERES_BOOM`) | same |
| gpu-pbomb | a power bomb in Landing Site (layer and colour windows) | same |
| soft-reset | B on the file-select screens (`MASH_B`) | no crash, soft resets happen, same |
| warp-music | a teleport every 30 frames, room after room (`MUSIC_CHECK`, `MUSIC_CHAIN`) | the music queue never ends up stuck (it froze the next door) |
| audio-rooms | audio over every room, 120 frames each | output hash |
| warp (`--full`) | `tools/warp-test`: teleport into every room and door | ok/fail counts |

Expected values live in `expected.txt`. The WRAM and audio hashes change whenever the
game's behaviour changes, on purpose or not: after an intended change (a game-logic fix,
a change to the teleport), rerun with `--update` and say why in the commit. Logs and
mismatching frames (`diff-NN.ppm`) end up in `/tmp/sm-tests` (`TEST_WORK` to move it).

Not covered: anything only the console shows (citro3d output, timings, audio thread);
use GPU CHECK and the debug log there.
