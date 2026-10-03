# Finding a screen's text (game_text_screens.c)

How the screens in `source/game_text_screens.c` were worked out, to add the ones still open
(docs/PLAN.md P4.8). Everything runs on the PC from `tools/gpu-ppu-test` captures; the ROM
stays outside the repo.

## 1. Capture the screen

Build once (`WORK` is any folder; `HOST_CFLAGS=-malign-double` if the 32-bit build fails for
lack of `gcc-multilib`):

```sh
WORK=/tmp/gt tools/gpu-ppu-test/run.sh ROM build
```

Then get to the screen and dump frames with `SHOTS=a-b` (`SHOTS_STEP=n` for every n-th): each
dumped frame writes `shot-NNNN.ppm`, `vram-NNNN.bin`, `cgram-NNNN.bin`, `wram-shot-NNNN.bin`,
`oam-NNNN.bin` and `regs-NNNN.txt` (mode, each BG's tilemap, chars and scroll). Ways in:

| Screen | How |
|---|---|
| title, file select, options, intro, Ceres | `boot` with an empty SRAM; `BOOT_SEQ=hex@frame,...` for buttons, `BOOT_SEQ_STATE=4` to count frames from the file-select menus |
| file select with a saved file | `SRAM_SAVE=0` in the `rooms` mode writes one to `saves/sm.srm`, then `boot` with it |
| pause screen | `rooms ... 91F8` with `ROOM_SEQ=8@20,0@26` (START), `800@150,0@156` (R: equipment); `ITEMS=ffff` |
| game over | `rooms` with `SAMUS_HEALTH=0` |
| ending and credits | `rooms` with `FORCE_STATE=38` (about 24000 frames to the end) |

Buttons: B 0x01, Y 0x02, SELECT 0x04, START 0x08, UP 0x10, DOWN 0x20, LEFT 0x40, RIGHT 0x80,
A 0x100, X 0x200, L 0x400, R 0x800. `GAME_LANG=n` shows the translation (ui_lang.h order).

## 2. Find the layer, the font and the codes

Run these where the dumps are:

- `bg.py FRAME LAYER [OUT.ppm] [-v]`: one BG layer alone as a picture; `-v` prints its
  tilemap (cell words in hex), so a text's chars and palette can be read off.
- `chars.py FRAME CHARBASE BPP PAL N OUT.ppm`: the char sheet at CHARBASE, to see a font.
- `maps.py FRAME CHARBASE BPP OUT.ppm`: every VRAM page drawn as a tilemap, to find where a
  screen keeps the tilemaps it is not showing.

A screen's letters are usually a char per letter (A at some base) or, for 16 px fonts, a top
and a bottom char, halves shared between letters. Match the English on screen to its codes,
then check the font's other letters are really there (a screen may reuse the chars of the
letters it does not need: the credits' last screen does; then embed the glyphs, as
`kCreditsSmallGlyphs`). Menus' compressed tilemaps can be read decompressed from WRAM
(`wram-shot-*.bin`); small-font strings can be found in the ROM by scanning for runs of the
font's codes.

## 3. Add it

A `case` for the game state in `Hook()`, a `Font` and `Phrase`s (`TranslateLayer`), `Page`s
for typed text (`TranslatePages`), `WordRun`s or `BoxWord`s for words drawn as pictures. Check
with `GAME_LANG` in every language, and that the WRAM hash equals English (`tools/test` does
for the new game, the pause screen and game over).
