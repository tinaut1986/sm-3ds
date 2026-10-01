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

if [ -z "$ROM" ] || [ ! -f "$ROM" ]; then
  echo "No ROM (pass it or set SM_ROM): the other tests need it."
  exit $FAILED
fi
ROM=$(realpath "$ROM")

# ---- Builds (once) ------------------------------------------------------------------------
echo "building the host test programs..."
head -c 8192 /dev/zero > "$OUT/empty.srm"
mkdir -p "$OUT/gpu-build" "$OUT/audio-build"
# Built once, then copied to each test's folder.
WORK=$OUT/gpu-build "$ROOT/tools/gpu-ppu-test/run.sh" "$ROM" build > "$OUT/gpu-build.log" 2>&1
WORK=$OUT/audio-build "$ROOT/tools/audio-bench/run.sh" "$ROM" build > "$OUT/audio-build.log" 2>&1
if [ ! -x "$OUT/gpu-build/gpu_ppu_test" ] || [ ! -x "$OUT/audio-build/audio_bench" ]; then
  echo "  FAIL  build (see $OUT/gpu-build.log, $OUT/audio-build.log)"
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
MASH_B=400 MASH_B_STATE=4 run_gpu soft-reset boot "$OUT/empty.srm" 1200 &
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
