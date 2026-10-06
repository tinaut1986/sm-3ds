# Host regression tests

Needs `gcc` with 32-bit support (`sudo apt install gcc-multilib` on Debian/Ubuntu): the test programs are built `-m32`, like the console. If that build fails the run stops with `FAIL  build`; it never falls back to a program left by an earlier run.

`make test SM_ROM=/path/to/rom.sfc` (or `tools/test/run.sh [--full] [--update] ROM`)
runs everything that can be checked on the PC and prints PASS/FAIL per check; it
exits non-zero if anything failed. About 90 s; `--full` adds the teleport test (ASAN,
~2 min more). The ROM is never stored: every state is generated from it (an empty
SRAM, scripted inputs, warps).

| Test | What | Checks |
|---|---|---|
| dsp-fuzz | random S-DSP register writes, optimised `dsp.c` against the vendored one | samples, registers and APU RAM identical (no ROM needed) |
| stereo-depth | `tools/stereo-test`: every input of the stereo depth mapping (`source/stereo_depth.c`) | no layer nearer than what draws over it beyond the listed exceptions (and the list exact), one sprite plane, HUD nearest, whole-pixel symmetric eye offsets at every slider step (no ROM needed) |
| gpu-rooms | every room reachable by a door, 10 frames each | GPU list == CPU renderer, nothing refused, WRAM hash |
| gpu-newgame | power-on, title, intro, new game, Ceres (mode 7), Ceres exploding (`CERES_BOOM`) | same |
| gpu-pbomb | a power bomb in Landing Site (layer and colour windows) | same |
| wide-rooms | every room with WIDE on (60 px margins), 10 frames each | GPU list == CPU renderer; the WIDE middle equals the normal frame; margins black while the room is not filled in (doors, fades), except the HUD's columns |
| edge-rooms | every room with the 3D's edge columns without WIDE (4 px margins, `WIDE_EDGE`, the HUD in its band), 10 frames each | GPU list == CPU renderer; the 256 px view equals the normal frame; the same WRAM hash as wide-rooms (the margin does not change the game) |
| wide-spikes | Brinstar `A408`, 90 frames with WIDE (60 px): the spike-shooting plant sits just outside the normal view (`EPROJ_MARGIN`, `EPROJ_ID=DAFE`) | same, and its spikes live outside the game's 256 px window (frames counted, > 0; the projectile checks of `sm_86.c` ignored the margins, #22) |
| wide-rows | every room with WIDE PIXEL PERFECT (72 px margins, 8 extra rows above and below), 10 frames each | same, and a hash of the whole WIDE images: the extra rows lean off a room's top or bottom with the HUD kept in place (issue #7) |
| msgbox-es | the save prompt (message box 23) in Landing Site with YES/NO toggled, in Spanish (`GAME_LANG=1`), against the same in English | GPU list == CPU renderer, and the WRAM hash equal to the English run's: the translation only touches VRAM |
| newgame-es | gpu-newgame in Spanish: title, file select, options, intro pages, Ceres | GPU list == CPU renderer, and the WRAM hash equal to gpu-newgame's (`game_text_screens.c` only touches VRAM) |
| pause-es, gameover-es | the pause screen (map, then equipment with every item) and the game over menu, in Spanish against English | same |
| wide-xray | the X-ray scope in Landing Site with WIDE, aimed right, up and down towards the margin | same, and a hash of the WIDE images: the cone stays within the view (the margins once showed BG2 garbage) |
| wide-xray-margin, wide-xray-left | the scope aimed right at the view's edge in Brinstar `96BA` (a block it reveals there), and left through the left margin in Landing Site | same, and a hash of the WIDE images: the cone, its dimming and the blocks it reveals go on in the margins (#38; the margins' tilemap is built in `sm_wide.c`, `XrayMargins`) |
| wide-xray-off | the X-ray scope switched on and off in Brinstar `9FBA` with WIDE PIXEL PERFECT (`XRAY`, `SAMUS_AT`) | same, and a hash of the WIDE images: the frames in which the scope goes off showed the BG2 pages' garbage in the margin |
| wide-pbomb | the power bomb in Landing Site with WIDE on | same, and a hash of the whole WIDE images (margins and HUD rows, where the explosion was cut off once): when it changes, look at the frames (`WIDE_DUMP`) before `--update` |
| wide-ceres | the Ceres elevator shaft (`DF45`, mode 7) tilting in the escape (`CERES_ESCAPE`), WIDE with PIXEL PERFECT's extra rows, Samus shooting left then right | same; a hash of the whole WIDE images (the plane in the margins, the extra rows and under the HUD); the WRAM hash covers where the beams and their explosions are drawn (once at a garbage Y / the previous shot's place, sm_93.c) |
| wide-door | WIDE through Crateria `93D5`'s right door into `92FD` and back (garbage beside the HUD once; margins that skipped the fades) | same, it ends back in `93D5`, and the count of frames with the margins filled in (fades included) |
| intro-cursor-1..4 | the intro's story text from a new game, in each UI language (`GAME_LANG`, `INTRO_CURSOR_CHECK`) | the typing cursor (a sprite, tile `0xFC`) is right after the last letter on screen or at the start of the next line, on every frame it shows (issue #36); over 1000 frames looked at |
| samus-edge-above, -below | Samus pinned 48 px above / 270 px below the camera in Landing Site (`SAMUS_PIN`), WIDE PIXEL PERFECT (`EDGE_CHECK`) | no OAM entry in the bottom band (raw Y 224-255, where the extra rows show a sprite wrapped to the opposite edge) is untagged, and some tagged entries lie outside the view (issue #26) |
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
