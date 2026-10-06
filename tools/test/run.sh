#!/bin/bash
# Host regression tests: everything that can be checked on the PC, in one go, with a
# PASS/FAIL per test and a non-zero exit if anything failed. See tools/test/README.md.
#
# Usage: tools/test/run.sh [--full] [--update] [ROM]
#   ROM       the Super Metroid (Japan, USA) ROM; or set SM_ROM. Only the DSP fuzz runs
#             without it. Nothing of the ROM is stored: every state is generated from it.
#   --full    also the teleport test (ASAN build, several minutes more)
#   --update  write the values found to tools/test/expected.txt instead of checking them
#             (after a change that is meant to alter the game, e.g. a game-logic fix)
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
EXPECTED=$ROOT/tools/test/expected.txt
FULL=0
UPDATE=0
ROM=${SM_ROM:-}
for a in "$@"; do
  case $a in
    --full) FULL=1 ;;
    --update) UPDATE=1 ;;
    *) ROM=$a ;;
  esac
done
OUT=${TEST_WORK:-/tmp/sm-tests}
mkdir -p "$OUT"
START=$(date +%s)

declare -A FOUND
FAILED=0

expect() {   # expect NAME VALUE: compare with (or, --update, record) expected.txt
  FOUND[$1]=$2
  [ $UPDATE = 1 ] && return 0
  local want
  want=$(grep "^$1=" "$EXPECTED" 2>/dev/null | cut -d= -f2-)
  if [ "$want" = "$2" ]; then
    echo "  ok    $1 = $2"
  else
    echo "  FAIL  $1 = $2 (expected ${want:-nothing})"
    FAILED=1
  fi
}

check() {   # check NAME CONDITION-DESCRIPTION RESULT(0 = ok)
  if [ "$3" = 0 ]; then echo "  ok    $1: $2"; else echo "  FAIL  $1: $2"; FAILED=1; fi
}

field() { grep -o "$2" "$1" | head -1; }

# ---- No ROM needed ------------------------------------------------------------------------
echo "dsp-fuzz: optimised S-DSP against the vendored one, random register writes"
WORK=$OUT/dsp-fuzz "$ROOT/tools/audio-bench/dsp_fuzz.sh" 1 2 > "$OUT/dsp-fuzz.log" 2>&1
check dsp-fuzz "4 runs identical" "$( [ "$(grep -c '^OK:' "$OUT/dsp-fuzz.log")" = 4 ]; echo $?)"
echo "stereo-depth: the stereo depth mapping against the SNES compositor's order"
WORK=$OUT/stereo-test "$ROOT/tools/stereo-test/run.sh" > "$OUT/stereo-test.log" 2>&1
check stereo-depth "0 failed" "$(grep -q ', 0 failed$' "$OUT/stereo-test.log"; echo $?)"
echo "states: the save-state store (list, ids, info, screenshot, removal)"
WORK=$OUT/states-test "$ROOT/tools/states-test/run.sh" > "$OUT/states-test.log" 2>&1
check states "states_test: OK" "$(grep -q '^states_test: OK$' "$OUT/states-test.log"; echo $?)"
echo "updater: version comparison and release-list parsing of the self-updater"
WORK=$OUT/updater-test "$ROOT/tools/updater-test/run.sh" > "$OUT/updater-test.log" 2>&1
check updater "updater_parse_test: OK" "$(grep -q '^updater_parse_test: OK$' "$OUT/updater-test.log"; echo $?)"

if [ -z "$ROM" ] || [ ! -f "$ROM" ]; then
  echo "No ROM (pass it or set SM_ROM): the other tests need it."
  exit $FAILED
fi
ROM=$(realpath "$ROM")

# ---- Builds (once) ------------------------------------------------------------------------
echo "building the host test programs..."
head -c 8192 /dev/zero > "$OUT/empty.srm"
mkdir -p "$OUT/gpu-build" "$OUT/audio-build"
# Built once, then copied to each test's folder. A binary left by an earlier run must never
# stand in for a failed build (it once ran every test on code days old: no 32-bit libc
# headers, so gcc -m32 failed and the old program went on to "fail" checks it predates).
rm -f "$OUT/gpu-build/gpu_ppu_test" "$OUT/audio-build/audio_bench"
build_ok=1
WORK=$OUT/gpu-build "$ROOT/tools/gpu-ppu-test/run.sh" "$ROM" build > "$OUT/gpu-build.log" 2>&1 || build_ok=0
WORK=$OUT/audio-build "$ROOT/tools/audio-bench/run.sh" "$ROM" build > "$OUT/audio-build.log" 2>&1 || build_ok=0
if [ $build_ok = 0 ] || [ ! -x "$OUT/gpu-build/gpu_ppu_test" ] || [ ! -x "$OUT/audio-build/audio_bench" ]; then
  echo "  FAIL  build (see $OUT/gpu-build.log, $OUT/audio-build.log)"
  echo "        32-bit host builds need the 32-bit libc headers: sudo apt install gcc-multilib"
  exit 1
fi

run_gpu() {   # run_gpu NAME ARGS... (env passed through)
  local name=$1
  shift
  # A fresh folder: a run leaves sm.srm behind, and the next boot would differ.
  rm -rf "${OUT:?}/$name"
  mkdir -p "$OUT/$name/saves"
  cp "$OUT/gpu-build/gpu_ppu_test" "$OUT/$name/"
  NO_BUILD=1 WORK=$OUT/$name "$ROOT/tools/gpu-ppu-test/run.sh" "$ROM" "$@" > "$OUT/$name.log" 2>&1
  echo $? > "$OUT/$name.exit"
}

run_audio() {
  local name=$1
  shift
  # A fresh folder: a run leaves sm.srm behind, and the next boot would differ.
  rm -rf "${OUT:?}/$name"
  mkdir -p "$OUT/$name/saves"
  cp "$OUT/audio-build/audio_bench" "$OUT/$name/"
  NO_BUILD=1 WORK=$OUT/$name "$ROOT/tools/audio-bench/run.sh" "$ROM" "$@" > "$OUT/$name.log" 2>&1
  echo $? > "$OUT/$name.exit"
}

# ---- The tests, in parallel ------------------------------------------------------------------
echo "running (in parallel)..."
run_gpu gpu-rooms rooms 10 &
CERES_BOOM=1 run_gpu gpu-newgame boot "$OUT/empty.srm" 10500 &
PBOMB=1 run_gpu gpu-pbomb rooms 200 91F8 &
WIDE=60 run_gpu wide-rooms rooms 10 &
# The 3D's edge columns without WIDE (4 px margins, the HUD in its band): the 256 px view must come out the same.
WIDE=4 WIDE_EDGE=1 run_gpu edge-rooms rooms 10 &
WIDE=60 PBOMB=1 run_gpu wide-pbomb rooms 200 91F8 &
# OPTIONS -> HUD: the status half or the minimap of the HUD not drawn (SmWide_HideHud), Landing Site.
HIDE_HUD=1 run_gpu hud-hide-status rooms 30 91F8 &
HIDE_HUD=2 run_gpu hud-hide-map rooms 30 91F8 &
# The X-ray scope in Landing Site with WIDE, aimed right, up and down into the margin.
WIDE=60 XRAY=1 ROOM_SEQ=0@0,80@5,0@8,1@20,11@60,21@140 run_gpu wide-xray rooms 220 91F8 &
# The spike-shooting plant of Brinstar A408 sits just outside the normal view: its spikes (DAFE) must go out into the
# margin (the game deleted them outside its own 256 px window, #22).
WIDE=60 EPROJ_MARGIN=1 EPROJ_ID=DAFE run_gpu wide-spikes rooms 90 A408 &
# Brinstar 9E52's yellow pipe bug flies left along the platform: it must go on into the left margin, not reset at the
# normal view's edge (#39). WARP_AT puts the camera and Samus where the bug starts its flight.
WARP_AT=192,256,330,315 WIDE=72 WIDE_Y=8 ENEMY_MARGIN=F253 run_gpu wide-pipebug rooms 150 9E52 &
# Norfair A56B's pipe bug (F193) resets through IsEnemyLeavingScreen, not CheckIfEnemyIsOnScreen: it must go on into the left margin
# too, not vanish at the normal view's edge (a scene recording from the console, 2026-10-07).
WARP_AT=105,256,110,411 WIDE=72 WIDE_Y=8 ENEMY_MARGIN=F193 run_gpu wide-pipebug-leaving rooms 200 A56B &
# PIXEL PERFECT's extra rows in every room: they lean off a room's top or bottom (#7).
WIDE=72 WIDE_Y=8 run_gpu wide-rows rooms 10 &
# The X-ray scope on and off in a Brinstar room (9FBA) with WIDE: the frames in which it goes off
# showed the BG2 pages' garbage in the margin.
WIDE=72 WIDE_Y=8 XRAY=1 SAMUS_AT=170,171 ROOM_SEQ=0@0,1@40,0@100 run_gpu wide-xray-off rooms 160 9FBA &
# The scope aimed right and left at the edge of the view in Brinstar 96BA: it goes on in the margins
# (the cone, the dimming, the blocks it reveals there; #38).
WIDE=72 WIDE_Y=0 XRAY=1 SAMUS_AT=444,139 SCROLLS_OPEN=1 ROOM_SEQ=0@0,1@40,0@100 run_gpu wide-xray-margin rooms 130 96BA &
# And aimed left, in the middle of Landing Site: the cone goes through the left margin (the other end of the map's pages).
WIDE=72 WIDE_Y=0 XRAY=1 ROOM_SEQ=80@5,0@150,40@152,0@154,1@170,0@230 run_gpu wide-xray-left rooms 240 91F8 &
# The Ceres elevator shaft (DF45, mode 7) tilting in the escape, PIXEL PERFECT margins,
# Samus shooting left then right (the beams' OAM is in the WRAM hash).
WIDE=72 WIDE_Y=8 CERES_ESCAPE=1 AUTOFIRE=1 ROOM_SEQ=0@0,240@40,280@120 run_gpu wide-ceres rooms 200 DF45 &
# Room 93D5 (Crateria) out through its right door into 92FD and back. and back.
WIDE=60 ROOM_SEQ=0@0,80@10,280@20,240@200 AUTOFIRE=1 WARP_AT=0,0,150,139 run_gpu wide-door rooms 400 93D5 &
MASH_B=400 MASH_B_STATE=4 run_gpu soft-reset boot "$OUT/empty.srm" 1200 &
# The save prompt in Landing Site, YES/NO toggled, in English and in Spanish (game_text.c).
MSGBOX=23 ROOM_SEQ=0@0,100@60,80@70 run_gpu msgbox-en rooms 200 91F8 &
GAME_LANG=1 MSGBOX=23 ROOM_SEQ=0@0,100@60,80@70 run_gpu msgbox-es rooms 200 91F8 &
# The other screens in Spanish (game_text_screens.c): the new game of gpu-newgame (title, file
# select, options, intro, Ceres), the pause screen's map and equipment, the game over menu.
GAME_LANG=1 CERES_BOOM=1 run_gpu newgame-es boot "$OUT/empty.srm" 10500 &
ITEMS=ffff ROOM_SEQ=8@20,0@26,800@150,0@156 run_gpu pause-en rooms 300 91F8 &
GAME_LANG=1 ITEMS=ffff ROOM_SEQ=8@20,0@26,800@150,0@156 run_gpu pause-es rooms 300 91F8 &
SAMUS_HEALTH=0 run_gpu gameover-en rooms 500 91F8 &
GAME_LANG=1 SAMUS_HEALTH=0 run_gpu gameover-es rooms 500 91F8 &
# The intro's typing cursor follows the translated letters, in every language (#36), and Samus
# pinned above or below the view (an elevator shaft) shows nothing at the opposite edge of the
# PIXEL PERFECT view (#26).
for lang in 1 2 3 4; do
  GAME_LANG=$lang INTRO_CURSOR_CHECK=1 run_gpu intro-cursor-$lang boot "$OUT/empty.srm" 3800 &
done
WIDE=72 WIDE_Y=8 EDGE_CHECK=1 SAMUS_PIN=-48,0 run_gpu samus-edge-above rooms 60 91F8 &
WIDE=72 WIDE_Y=8 EDGE_CHECK=1 SAMUS_PIN=270,0 run_gpu samus-edge-below rooms 60 91F8 &
MUSIC_CHECK=1 MUSIC_CHAIN=1 MUSIC_SETTLE=30 run_gpu warp-music rooms 1 &
run_audio audio-rooms rooms 120 &
if [ $FULL = 1 ]; then
  ( WORK=$OUT/warp "$ROOT/tools/warp-test/run.sh" "$ROM" > "$OUT/warp.log" 2>&1; echo $? > "$OUT/warp.exit" ) &
fi
wait

gpu_checks() {   # gpu_checks NAME: GPU list == CPU renderer, nothing refused, game state
  local log=$OUT/$1.log
  local r
  r=$(field "$log" "RESULT.*")
  check "$1" "ran to the end (no crash)" "$( [ -n "$r" ]; echo $?)"
  check "$1" "capture replay and GPU list identical to the CPU renderer" \
    "$(echo "$r" | grep -q "capture mismatches 0, GPU mismatches 0"; echo $?)"
  expect "$1.refused" "$(echo "$r" | grep -o 'refused [0-9]*' | cut -d' ' -f2)"
  expect "$1.wram" "$(field "$log" 'WRAM hash [0-9a-f]*' | cut -d' ' -f3)"
}

echo "gpu-rooms: every room reachable by a door, 10 frames each"
gpu_checks gpu-rooms
echo "gpu-newgame: power-on, title, intro, new game, Ceres (mode 7), Ceres exploding"
gpu_checks gpu-newgame
echo "gpu-pbomb: a power bomb in Landing Site (layer and colour windows)"
gpu_checks gpu-pbomb
wide_checks() {   # wide_checks NAME: the WIDE build of every frame
  local r
  r=$(field "$OUT/$1.log" "WIDE frames.*")
  check "$1" "WIDE middle equals the normal frame, margins black while the room is not filled in" \
    "$(echo "$r" | grep -q "bad 0,"; echo $?)"
}
echo "wide-rooms: every room with WIDE on (60 px margins), 10 frames each"
gpu_checks wide-rooms
wide_checks wide-rooms
echo "edge-rooms: every room with the 3D's 4 px edge columns (no WIDE), 10 frames each"
gpu_checks edge-rooms
wide_checks edge-rooms
echo "hud-hide-status, hud-hide-map: one half of the HUD not drawn (OPTIONS -> HUD), Landing Site"
for t in hud-hide-status hud-hide-map; do
  gpu_checks $t
  check $t "the hidden half is black (lit pixels 0) and the other one is drawn" "$(grep -q 'HIDE_HUD frames 30, lit pixels in the hidden halves 0, in the shown ones [1-9]' "$OUT/$t.log"; echo $?)"
done
echo "wide-spikes: the spike-shooting plant's spikes outside the normal view (A408) with WIDE"
gpu_checks wide-spikes
expect wide-spikes.outside "$(field "$OUT/wide-spikes.log" 'EPROJ_MARGIN frames with a projectile outside the 256 px window: [0-9]*' | awk '{print $NF}')"
check wide-spikes "spikes live outside the normal view (frames > 0)" "$([ "$(field "$OUT/wide-spikes.log" 'EPROJ_MARGIN frames with a projectile outside the 256 px window: [0-9]*' | awk '{print $NF}')" -gt 0 ]; echo $?)"
echo "wide-pipebug: the yellow pipe bug of 9E52 keeps flying into the left margin (#39)"
gpu_checks wide-pipebug
check wide-pipebug "the bug goes beyond 40 px left of the normal view (frames > 0)" "$([ "$(field "$OUT/wide-pipebug.log" 'ENEMY_MARGIN frames with the enemy beyond 40 px left of the view: [0-9]*' | awk '{print $NF}')" -gt 0 ]; echo $?)"
echo "wide-pipebug-leaving: the pipe bug of A56B keeps flying into the left margin (IsEnemyLeavingScreen)"
gpu_checks wide-pipebug-leaving
check wide-pipebug-leaving "the bug goes beyond 40 px left of the normal view (frames > 0)" "$([ "$(field "$OUT/wide-pipebug-leaving.log" 'ENEMY_MARGIN frames with the enemy beyond 40 px left of the view: [0-9]*' | awk '{print $NF}')" -gt 0 ]; echo $?)"
echo "wide-rows: every room with WIDE PIXEL PERFECT (72 px, 8 extra rows), 10 frames each"
gpu_checks wide-rows
wide_checks wide-rows
expect wide-rows.image "$(field "$OUT/wide-rows.log" 'WIDE image hash [0-9a-f]*' | cut -d' ' -f4)"
echo "wide-xray: the X-ray scope's cone with WIDE (it stays in the view, no BG2 garbage in the margins)"
gpu_checks wide-xray
wide_checks wide-xray
expect wide-xray.image "$(field "$OUT/wide-xray.log" 'WIDE image hash [0-9a-f]*' | cut -d' ' -f4)"
echo "wide-xray-margin: the X-ray scope reaches the margins (96BA, aimed right at the view's edge)"
gpu_checks wide-xray-margin
wide_checks wide-xray-margin
expect wide-xray-margin.image "$(field "$OUT/wide-xray-margin.log" 'WIDE image hash [0-9a-f]*' | cut -d' ' -f4)"
echo "wide-xray-left: the X-ray scope aimed left through the left margin (Landing Site)"
gpu_checks wide-xray-left
wide_checks wide-xray-left
expect wide-xray-left.image "$(field "$OUT/wide-xray-left.log" 'WIDE image hash [0-9a-f]*' | cut -d' ' -f4)"
echo "wide-xray-off: the X-ray scope switched off in 9FBA with WIDE PIXEL PERFECT (no garbage in the margin)"
gpu_checks wide-xray-off
wide_checks wide-xray-off
expect wide-xray-off.image "$(field "$OUT/wide-xray-off.log" 'WIDE image hash [0-9a-f]*' | cut -d' ' -f4)"
echo "wide-pbomb: the power bomb in Landing Site with WIDE on"
gpu_checks wide-pbomb
wide_checks wide-pbomb
expect wide-pbomb.image "$(field "$OUT/wide-pbomb.log" 'WIDE image hash [0-9a-f]*' | cut -d' ' -f4)"
echo "wide-ceres: the Ceres elevator shaft (mode 7) tilting, WIDE PIXEL PERFECT, shooting"
gpu_checks wide-ceres
wide_checks wide-ceres
expect wide-ceres.image "$(field "$OUT/wide-ceres.log" 'WIDE image hash [0-9a-f]*' | cut -d' ' -f4)"
echo "wide-door: WIDE through a door and back (93D5 -> 92FD -> 93D5)"
gpu_checks wide-door
wide_checks wide-door
check wide-door "back in room 93D5" "$(grep -q 'game_state 08 room 93d5' "$OUT/wide-door.log"; echo $?)"
# Frames whose margins show the room: drops if the door fades go back to black margins.
expect wide-door.filled "$(field "$OUT/wide-door.log" 'room filled in [0-9]*' | cut -d' ' -f4)"
echo "msgbox-es: a message box in Spanish changes only VRAM (same game state as in English)"
gpu_checks msgbox-es
check msgbox-es "WRAM identical to the English run" \
  "$( [ "$(field "$OUT/msgbox-es.log" 'WRAM hash [0-9a-f]*')" = "$(field "$OUT/msgbox-en.log" 'WRAM hash [0-9a-f]*')" ]; echo $?)"
same_wram() {   # same_wram NAME OTHER: the WRAM hash of NAME's run equals OTHER's
  check "$1" "WRAM identical to $2" \
    "$( [ "$(field "$OUT/$1.log" 'WRAM hash [0-9a-f]*')" = "$(field "$OUT/$2.log" 'WRAM hash [0-9a-f]*')" ]; echo $?)"
}
echo "newgame-es: gpu-newgame in Spanish, menus and intro translated in VRAM only"
gpu_checks newgame-es
same_wram newgame-es gpu-newgame
echo "pause-es: the pause screen (map, equipment) in Spanish"
gpu_checks pause-es
same_wram pause-es pause-en
echo "gameover-es: the game over menu in Spanish"
gpu_checks gameover-es
same_wram gameover-es gameover-en
for lang in 1 2 3 4; do
  echo "intro-cursor-$lang: the intro's typing cursor next to the last translated letter (UI language $lang)"
  r=$(field "$OUT/intro-cursor-$lang.log" "INTRO CURSOR frames.*")
  check "intro-cursor-$lang" "ran to the end (no crash)" "$( [ -n "$r" ]; echo $?)"
  check "intro-cursor-$lang" "no frame with the cursor away from the last letter or the next line's start" \
    "$(echo "$r" | grep -q ", bad 0,"; echo $?)"
  check "intro-cursor-$lang" "the cursor was looked at (over 1000 frames)" \
    "$( [ "$(echo "$r" | sed 's/INTRO CURSOR frames \([0-9]*\).*/\1/')" -gt 1000 ] 2>/dev/null; echo $?)"
done
for side in above below; do
  echo "samus-edge-$side: Samus pinned $side the view, PIXEL PERFECT's extra rows"
  r=$(field "$OUT/samus-edge-$side.log" "EDGE frames.*")
  check "samus-edge-$side" "ran to the end (no crash)" "$( [ -n "$r" ]; echo $?)"
  check "samus-edge-$side" "every OAM entry in the bottom band is tagged with its full position (or parked)" \
    "$(echo "$r" | grep -q "untagged 0,"; echo $?)"
  check "samus-edge-$side" "entries tagged outside the view were seen (Samus was off it)" \
    "$( [ "$(echo "$r" | sed 's/.*outside the view \([0-9]*\)/\1/')" -gt 0 ] 2>/dev/null; echo $?)"
done
echo "soft-reset: B on the file-select screens, soft resets back to the title"
gpu_checks soft-reset
check soft-reset "soft resets happened" \
  "$( [ "$(field "$OUT/soft-reset.log" 'soft resets seen: [0-9]*' | cut -d' ' -f4)" -gt 0 ] 2>/dev/null; echo $?)"
echo "warp-music: teleport room after room every 30 frames, music queue stays sane"
r=$(field "$OUT/warp-music.log" 'MUSIC.*')
check warp-music "ran to the end" "$( [ -n "$r" ]; echo $?)"
check warp-music "no music queue left stuck (it froze the next door)" "$(echo "$r" | grep -q 'stuck in 0,'; echo $?)"
expect warp-music.result "$(echo "$r" | sed 's/^MUSIC //')"
echo "audio-rooms: audio output over every room, 120 frames each"
check audio-rooms "ran to the end" "$(grep -q '^frames' "$OUT/audio-rooms.log"; echo $?)"
expect audio-rooms.hash "$(field "$OUT/audio-rooms.log" 'hash [0-9a-f]*' | cut -d' ' -f2)"
if [ $FULL = 1 ]; then
  echo "warp: teleport into every room (ASAN)"
  expect warp.result "$(field "$OUT/warp.log" 'RESULT.*' | sed 's/^RESULT //')"
fi

if [ $UPDATE = 1 ]; then
  {
    echo "# Expected values for tools/test/run.sh. Regenerate with --update after a change"
    echo "# that is meant to alter them, and say why in the commit."
    for k in $(printf '%s\n' "${!FOUND[@]}" | sort); do
      # keep values of tests not run this time (e.g. warp without --full)
      echo "$k=${FOUND[$k]}"
    done
    [ -f "$EXPECTED" ] && grep -v '^#' "$EXPECTED" | while IFS= read -r line; do
      k=${line%%=*}
      [ -z "${FOUND[$k]+x}" ] && echo "$line"
    done
  } > "$EXPECTED.new"
  mv "$EXPECTED.new" "$EXPECTED"
  echo "updated $EXPECTED"
fi
echo "done in $(( $(date +%s) - START )) s; logs in $OUT"
if [ $FAILED = 0 ]; then echo "ALL PASSED"; else echo "SOME TESTS FAILED"; fi
exit $FAILED
