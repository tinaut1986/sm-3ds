# Project notes for Claude

Native Nintendo 3DS port of **Super Metroid**, with stereoscopic 3D as the end
goal. The owner already shipped the same kind of port for Metroid: Zero Mission
(`../mzm`), and that port is the reference for everything here.

**Start every session by reading [`docs/PLAN.md`](docs/PLAN.md).** It holds the
phase roadmap, the task specs with acceptance criteria, and the decisions log.
When a task is finished, tick it there and add anything non-obvious to the
decisions log. The next session starts from that file, not from chat history.

## Language

- Chat with the owner in **Spanish (castellano)**.
- All code, comments, identifiers, commit messages and docs: **English**.
- Exception: the game code inherited from snesrev (`sm/src/`) keeps its own style.
  Do not churn it.

## Where things come from

| Path | Origin | Notes |
|---|---|---|
| `source/` | CharlesAverill/sm-3ds, now mostly ours | 3DS frontend: `main.c` (still SDL2 for input/audio, see PLAN P1.3), ROM loader, bottom UI (`bottom_ui.c`, `ui_draw.c`, `ui_font.c`), cheats, map and teleport (`sm_map.c`, `sm_warp.c`), debug tools. |
| `sm/` (vendored, plain directory) | CharlesAverill/sm-3ds-lib @ `d4e4f42` = snesrev/sm `main` + 4 commits | The game: C reimplementation of the whole ROM plus an SNES emulator (`sm/src/snes/`) used as reference/fallback. Edited in place, committed in this repo. MIT (snesrev, elzo_d) + Opus BSD: keep `sm/LICENSE.txt`. It still builds as the original PC version on Linux (`make -C sm`, needs `libsdl2-dev`), including the native-vs-ROM frame comparison. |
| `SDL/` (submodule) | libsdl-org/SDL, SDL2 branch | Planned to be dropped in favour of libctru + citro3d directly (see PLAN). |
| `romfs/` | | Only a `blank` placeholder. Never put a ROM here: the ROM is read from `sdmc:/3ds/Super Metroid 3DS/` at runtime (PLAN P1.1). |

Other local checkouts:

- `../mzm`: the Zero Mission 3DS port (`tinaut1986/mzm`). Reference code, see
  the reuse map in PLAN.

## What `snesrev/sm` is (and is not)

It is **not** a matching decompilation like mzm. It is a hand-written C
reimplementation, one `sm_XX.c` file per ROM bank (~85k lines), validated by
running the real ROM on the bundled CPU emulator in lockstep and comparing RAM
frame by frame. Consequences:

- The ROM is still required at runtime (graphics, levels, music data).
- Gaps or bugs in the C can be found by running both side by side and comparing
  snapshots (`sm/src/sm_cpu_infra.c`). This is the main debugging tool for game
  logic problems.
- Video (`sm/src/snes/ppu.c`) and audio (`sm/src/spc_player.c`, `dsp.c`) are
  emulated hardware, rendered on the CPU. That, not the game logic, is the
  performance problem on 3DS.

## Git and GitHub

- **Every push, PR, issue and release goes to `tinaut1986/sm-3ds`.** Never to
  CharlesAverill or snesrev.
  - `origin` = `tinaut1986/sm-3ds`, `upstream` = `CharlesAverill/sm-3ds`.
  - The push URL of `upstream` is set to `DISABLED` as a safety net (local
    config; redo it in a fresh clone with
    `git remote set-url --push upstream DISABLED`).
- `gh repo set-default tinaut1986/sm-3ds` has been run (`remote.origin.gh-resolved=base`).
  Re-run it in a fresh clone, otherwise `gh` targets the fork parent.
- Issues are enabled on `tinaut1986/sm-3ds` and used for playtest bugs.

### Branches

- `main` = stable. Only receives `--no-ff` merges of a finished release branch.
- `release/vX.Y.Z` = the current release line; work accumulates here, and being
  on it means "not stable yet". Active line: see PLAN.md header.
- Topic branches (`feat/...`, `fix/...`, `perf/...`, `chore/...`) are cut from
  the active release branch and merged back into it with `--no-ff`.
- Never commit directly on `main` or `release/*`. Commit only when the owner asks.
- Version numbers: minor = milestone, patch = fix round. Ask before a minor
  bump; never infer one. Also bump `resources/AppInfo` (APP_VER_*) until the
  version is derived from git (PLAN P1.6).

### Releases (installer on GitHub)

`.github/workflows/build-release.yml` builds the CIA on any `v*` tag push (and
on manual dispatch) and publishes a GitHub Release with the CIA and a QR code.
The channel depends on whether `main` can reach the tagged commit:

| Built from | Channel |
|---|---|
| Tag on an unmerged `release/*` branch | `Beta` (pre-release) |
| Tag on `main`, or on a commit merged into `main` | `Release` |
| Manual dispatch on any branch | `Beta` |

**Beta**: tag the release branch, push branch then tag.

```sh
git tag -a v0.1.0 -m "v0.1.0"
git push origin release/v0.1.0
git push origin v0.1.0
```

**Stable**: merge into `main`, tag the merge, push **main before the tag**
(the tag build checks reachability from `origin/main`).

```sh
git checkout main
git merge --no-ff release/v0.1.0
git tag -a v0.1.0 -m "v0.1.0"
git push origin main
git push origin v0.1.0
```

A tag already shipped as a beta is promoted by re-running the workflow from the
Actions tab **on the tag itself** (not on `main`, which would describe as
`vX.Y.Z-1-g<hash>` and create a second page).

After tagging, rename the release branch to the next patch and delete the old
remote branch:

```sh
git branch -m release/v0.1.0 release/v0.1.1
git branch --unset-upstream
git push -u origin release/v0.1.1
git push origin --delete release/v0.1.0
```

The workflow uses the workflow file **at the tagged commit**, and it refuses to
build if a `.smc`/`.sfc` is present in `romfs/`.

## ROM and legal

- Never commit, package or upload a ROM, nor any build that contains one.
- Supported ROM: Super Metroid (Japan, USA), headerless, sha1
  `da957f0d63d14cb441d215462904c4fa8519c613`.
- Code license: MIT (sm-3ds), plus snesrev's own license for `sm/`. Keep both.

## Build

Toolchain lives in `/opt/devkitpro` (devkitARM, libctru, citro2d/3d; CI uses
the `devkitpro/devkitarm:20260610` image, same as mzm). `bannertool` and
`makerom` are committed in `tools/bin/` (copied from mzm).

```sh
git submodule update --init --recursive
export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
export PATH=$PWD/tools/bin:/opt/devkitpro/tools/bin:$PATH
make sdl                        # once; builds SDL/build/libSDL2.a
make -j FULL_NATIVE=1 cia       # -> output/SuperMetroid3DSPort.cia
```

`FULL_NATIVE`: run only the C game code, never the ROM on the emulated CPU.
The CIA never contains the ROM. At runtime it reads any `.smc`/`.sfc` in
`sdmc:/3ds/Super Metroid 3DS/` whose headerless sha1 matches the JU ROM, and shows
an error screen otherwise. Saves, `config.ini` and `debug/` live in that folder too.

Host-side tools (no console needed; the ones that run the game need a local ROM,
never committed): `tools/ui-preview/build.sh` renders the bottom-screen tabs to PNG,
`tools/warp-test/run.sh` boots the game headless and checks the teleport into every
room. Installing on the owner's console: FBI's FTP server, `curl -T
output/SuperMetroid3DSPort.cia ftp://<3ds-ip>:5000/cias/sm-3ds-dev.cia`; files from the
console come back the same way (`/3ds/Super Metroid 3DS/debug/`, Luma dumps in
`/luma/dumps/arm11/`).

Test on hardware whenever performance is involved; Azahar is fine for logic
but its timings say nothing about Old 3DS.
