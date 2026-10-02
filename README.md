# sm-3ds

![ceres station on Azahar](screenshots/sm-3ds.gif)

A native Nintendo 3DS port of **Super Metroid**, built on
[snesrev's C reimplementation](https://github.com/snesrev/sm) and
[CharlesAverill's 3DS port](https://github.com/CharlesAverill/sm-3ds).
Stereoscopic 3D is the long-term goal; for now the game runs in 2D.

**The ROM is not included.** You need your own copy of Super Metroid.

## Status

First stable release `v0.1.2`; still early. Tested on a New 3DS and a 2DS:

- Graphics are drawn by the 3DS GPU. 60 fps on New 3DS; on Old 3DS/2DS about 60 fps
  in gameplay and 50-55 on the title, the intro and Ceres.
- Sound is the emulated SNES sound chip, unchanged, and plays cleanly on Old 3DS too.
- Saves at save stations persist across power cycles; save states (10 slots)
  work.
- The bottom screen has tabs for the map, status, save states and options (plus
  debug tools in test builds).

Bugs found while playing go to the
[issue tracker](https://github.com/tinaut1986/sm-3ds/issues). The roadmap is
[`docs/PLAN.md`](docs/PLAN.md).

## Installing

You need a 3DS with custom firmware (e.g. Luma3DS) and FBI or another CIA
installer.

1. Download `sm-3ds-<version>.cia` from the
   [Releases page](https://github.com/tinaut1986/sm-3ds/releases), or scan the
   QR code on that page with FBI ("Remote Install" -> "Scan QR Code").
   Releases marked **Beta** are test builds from a release branch.
2. Install the CIA with FBI.
3. Copy your ROM to the SD card, into this folder (create it if needed; the
   game also creates it on first start):

   ```
   sdmc:/3ds/Super Metroid 3DS/
   ```

   Any file name ending in `.smc` or `.sfc` works. Only the **Japan/USA**
   release is accepted: its headerless SHA-1 is
   `da957f0d63d14cb441d215462904c4fa8519c613`. A 512-byte copier header is
   stripped automatically. PAL and translation-patched ROMs are rejected; the
   game shows an error screen with the expected hash if no valid ROM is found.
4. Start "Super Metroid 3DS Port" from the HOME menu.

Everything else the game writes lives in the same folder:

| Path | What |
|---|---|
| `saves/sm.srm` | in-game save files (the three save slots) |
| `saves/save0.sav` ... `save9.sav` | save states (States tab), each with a `.txt` description |
| `config.ini` | options from the bottom screen |
| `debug/` | logs, perf CSVs and dumps from the Debug tab |

## Controls

The 3DS buttons map to the SNES buttons of the same name (D-pad, A, B, X, Y,
L, R, Start, Select). The game's own controller settings still apply. The
bottom screen is operated by touch: save states on the States tab (tap a button
twice to confirm); pause, turbo, frame skip, audio, FPS overlay, 804 MHz mode
(New 3DS) and reset on the Options tab.

## Building

Needs devkitARM with libctru, citro2d/citro3d (the
[devkitPro](https://devkitpro.org/wiki/Getting_Started) `3ds-dev` group), plus
`cmake`, `git`, `curl` and Python 3. `makerom` and `bannertool` are committed
in `tools/bin/` (Linux x86-64). No ROM is needed to build.

```sh
git clone --recurse-submodules https://github.com/tinaut1986/sm-3ds.git
cd sm-3ds
./build_3ds.sh          # interactive: build, optionally find the 3DS and send it
```

`build_3ds.sh` asks for debug (the default: debug tab, teleport, cheats on the
Status tab, see [`docs/debug-tools.md`](docs/debug-tools.md)) or production,
builds SDL2 the first time, then the CIA
(`output/SuperMetroid3DSPort.cia`). With ftpd or FBI's FTP server running on
the console, it can scan the local network for it and upload the CIA to
`/cias/sm-3ds-<version>.cia`. Non-interactive forms:

```sh
./build_3ds.sh --no-ftp                  # build only (debug)
./build_3ds.sh --mode prod --no-ftp      # production build
./build_3ds.sh --ftp 192.168.1.50        # build and upload (port 5000)
./build_3ds.sh --ftp                     # upload to the last IP used
./build_3ds.sh --help
```

Or with make directly (`DEVKITPRO` defaults to `/opt/devkitpro`):

```sh
make sdl                                               # once
make -j FULL_NATIVE=1 cia                              # -> output/SuperMetroid3DSPort.cia
make -j FULL_NATIVE=1 DEBUG_TOOLS=1 cia                # with the debug tools
make -j FULL_NATIVE=1 ftp FTP_HOST=192.168.1.50        # build and upload
make print-version
```

`FULL_NATIVE=1` runs only the C game code, never the ROM on the bundled SNES
CPU emulator; release builds use it.

### Tests

`make test SM_ROM=/path/to/your/rom.sfc` runs the host regression suite (about 90 s):
the GPU renderer against the CPU one over every room and a new game, the sound chip
against the original, the music queue under repeated teleports. See
[`tools/test/README.md`](tools/test/README.md). Nothing of the ROM is stored.

### Versions

The version comes from git: a tag gives `v0.1.0`; a build from
`release/v0.1.0` (or a branch cut from it) gives
`v0.1.0-dev.<commits since main>[.<commits on the branch>]+<hash>`. It is shown
in the HOME menu description, on the Debug tab and in the debug logs. Pushing a
`v*` tag builds the CIA on GitHub Actions and publishes a release (Beta unless
the tag is on `main`).

## Credits and license

- [snesrev](https://github.com/snesrev/sm): the C reimplementation of the
  game (`sm/`, MIT, see `sm/LICENSE.txt`, which also carries the Opus BSD
  license).
- [Charles Averill](https://github.com/CharlesAverill/sm-3ds): the original
  3DS port this one started from.
- The 5x7 UI font and much of the tooling come from the author's
  [Metroid: Zero Mission 3DS port](https://github.com/tinaut1986/mzm).

This project's code is MIT (see `LICENSE`). Super Metroid is © Nintendo; this
project contains no game data.
