#!/bin/bash
# Host regression test for the debug teleport (source/sm_warp.c).
# Boots the game headless with scripted inputs to reach real gameplay, saves a state,
# then for every room: loads it, warps, and checks the game settles in normal
# gameplay inside that room (each in its own process, so a crash is reported and
# does not stop the run).
#
# Usage: tools/warp-test/run.sh /path/to/Super\ Metroid\ \(Japan,\ USA\).sfc [room_hex]
# Expected today (2026-10-01, with ASAN): ok 584, fail 2, nodoor 6 (of 586 room/door pairs).
#   b482 door 0, dc19 door 0 -> Samus dies on arrival (a hazard at the landing spot).
# "note:" lines are informational: no floor near the door (a shaft) or a boss room that
# holds Samus in place (Kraid, a59f), so she ends up elsewhere.
#
# Extra knobs, all host-only (they need SM_WARP_DEBUG, which this script sets):
#   WARP_FORCE_X / WARP_FORCE_Y  start the warp from that Samus position instead of
#                                the one derived from the door (try 0 and 0)
#   WARP_NOFIX=1                 skip the arrival check, to see what it prevents
#   WARP_STRESS=1                force low bytes 0xF0 before the warp (overridden by
#                                the derived position today)
#   WARP_TRACE=1 / WARP_DUMP=1   per-30-frame trace / dump the level block types
#   WARP_AROUND=1                print the block types around the target
#   WARP_FOLLOW=1                print Samus and the camera for 40 frames after arrival
#   WARP_ONLY_DOOR=n             test only door n of the selected room (handy under gdb)
#   ASAN=1                       build with AddressSanitizer (slower; note the
#                                out-of-range level_data read is not caught on the host,
#                                see decisions log: it lands in other globals)
set -e
ROM=$(realpath "$1")
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${WORK:-/tmp/sm-warp-test}
mkdir -p "$WORK/saves"
S=$ROOT/sm
SRCS=$(ls $S/src/*.c $S/src/snes/*.c | grep -v "/main.c\|opengl.c\|glsl_shader.c")
EXTRA=""
if [ -n "$ASAN" ]; then EXTRA="-fsanitize=address -fno-omit-frame-pointer"; export ASAN_OPTIONS=detect_leaks=0:replace_intrin=0:replace_str=0; fi
gcc -O2 -g -DSM_WARP_DEBUG $EXTRA -fno-strict-aliasing -I"$S" -I"$ROOT/source" -I"$ROOT/third_party/sdl_keys" \
    -DSYSTEM_VOLUME_MIXER_AVAILABLE=0 -DFULL_NATIVE -w $SRCS \
    "$ROOT/source/sm_map.c" "$ROOT/source/sm_warp.c" "$ROOT/tools/warp-test/warp_test.c" -o "$WORK/warp_test" -lm
cd "$WORK"
./warp_test "$ROM" $2 2>&1 | grep -v "^Warning\|Unable\|v1=\|\*\*\* "
