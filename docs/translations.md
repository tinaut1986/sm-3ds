# Translations

Every text the port translates, the bottom screen's and the game's own (message boxes, menus,
intro, pause screen, game over, credits, the HUD's ENERGY), comes from a **language file**: one
UTF-8 text file per language. A new language, or a fix to one, needs no rebuild: put the file on
the SD card and pick it in OPTIONS -> LANGUAGE or in the game's OPTION MODE -> LANGUAGE.

## Where the files are

| Folder | What |
|---|---|
| `romfs/lang/` in this repository (inside the CIA) | the port's own: `es.txt`, `ca.txt`, `fr.txt`, `pt.txt` |
| `sdmc:/3ds/Super Metroid 3DS/lang/` on the SD card | added by the player: a new code adds a language, the code of one of the port's replaces it |

The file's name is the language's code (`de.txt`, `it.txt`); `en` and `ja` are built in and cannot
be files. The languages offered are English, then the port's own files (Spanish, Catalan, French,
Portuguese), then any other by code, then Japanese (the game's own Japanese text, with English
everywhere else). `config.ini` keeps the chosen language's code. With no saved choice the
console's language is used when there is a file for it.

## The format

```
# A comment: a line starting with #.
[language]
name = DEUTSCH

[ui]
ON = AN
Saved to slot %d = In Platz %d gespeichert

[message boxes]
MAP DATA ACCESS COMPLETED. = KARTENDATEN | | GELADEN.
MISSILE (line) = wähle {} und drücke {}.
```

- `[section]` lines, then `ENGLISH = translation` lines. The key is the English exactly as the
  port has it: never change it. Spaces around the `=` and at the ends are ignored.
- A key that is missing or left empty keeps the English. A file may translate only part.
- One line per value, however long (the intro pages are wrapped by the port).
- `[language] name` is the language in itself, as the menus list it.

What each section is (the notes in [`lang-template.txt`](lang-template.txt) give each key's limits):

| Section | Where | Notes |
|---|---|---|
| `ui` | the bottom screen | no wrapping: a label must fit where the English one is. Keep each `%d` and `%s` of the English in the same order: a translation that does not is ignored (English is shown) |
| `items`, `beams`, `ammo`, `areas`, `areas short` | the Status tab, the map, the save states | short forms with a length limit each |
| `message boxes` | item pickups, stations, the save prompt | a box's rows split by `\|` (an empty row keeps the English one); `(line)`: the lowercase line under an item's name, with `{}` where each picture of the English line goes; `(yes)` / `(no)`: 3 letters at most |
| `options`, `file select`, `pause`, `game over`, `ending` | the game's screens | the English is found on the screen and written over in the game's letters, in about the English one's room |
| `intro` | the typed story pages | the whole page |
| `hud` | ENERGY over the energy count | about 6 letters, a small font |

## What the fonts can draw

- **The game's screens and boxes:** capitals A-Z (lowercase is drawn as capitals), digits, common
  punctuation, `¿` and `¡`, and the accented letters `ÁÀÂÃÄ ÉÈÊË ÍÌÎÏ ÓÒÔÕÖ ÚÙÛÜ Ñ Ç` (the accent is
  drawn as a mark on the plain letter). The `(line)` rows use a small lowercase font with the same
  accents.
- **The bottom screen:** a 5x7 font with the same letters plus `ß` and `·`.
- Any other letter (`Œ`, Cyrillic, Greek, kana...) is drawn as a blank. Another script needs its
  glyphs drawn into the code first.

## Starting a language

Copy [`docs/lang-template.txt`](lang-template.txt) (every key, empty, with its limits) or one of
the port's files (`romfs/lang/es.txt`, already filled) to `<code>.txt` and translate the values.

## Checking a file

On the PC (gcc; no ROM needed):

```sh
tools/lang-check/run.sh path/to/de.txt
```

It reports errors (a `%d` or `%s` unlike the English, a value over its limit, more than three rows
in a box, no `[language] name`) and warnings (a key the port does not look up, most likely a
typo; a letter the fonts may not draw), then how many keys are translated.

What it cannot see is the room on screen. To look at it, put the file in a folder with the port's
own (`cp romfs/lang/*.txt yourfolder/`) and:

- the bottom screen: `SM_LANG_DIR=yourfolder SM_ROM=rom.sfc tools/ui-preview/build.sh` renders
  every tab in every language;
- the game's screens: `SM_LANG_DIR=yourfolder GAME_LANG=n tools/gpu-ppu-test/run.sh ...` with the
  captures of [`tools/game-text/README.md`](../tools/game-text/README.md) (`n`: the language's
  place in the list, English being 0: the port's four, then the others by code).

Or on the console: copy it to `/3ds/Super Metroid 3DS/lang/` and pick it.

## For the code

A translatable string is its English in the code: `kText` (`source/ui_lang.c`) for the bottom
screen, `kMsgKeys` (`source/game_text.c`) for the message boxes, the phrase tables of
`source/game_text_screens.c` for the screens. `UiLang_ForEachKey`, `GameText_ForEachKey` and
`GameTextScreens_ForEachKey` list every key: `tools/lang-check` checks the files against them and
writes the template. After adding or changing an English string:

1. add its translation to each `romfs/lang/*.txt` (under the English key);
2. `tools/lang-check/run.sh template docs/lang-template.txt`;
3. `tools/test/run.sh` (its `lang-files` check fails while a file or the template is behind).

Changing an English string changes its key: the files' old line stops being used (lang-check warns
about it) until it is renamed there too.
