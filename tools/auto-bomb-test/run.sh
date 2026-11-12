#!/bin/bash
# Host check of the automatic bomb jump (source/auto_bomb.c, tools/auto-bomb-test/auto_bomb_test.c).
# Usage: tools/auto-bomb-test/run.sh /path/to/rom.sfc
# Boots the game headless, gives Samus the morph ball and bombs, puts her on the floor under the
# tallest open column of three rooms and holds the bomb button in the ball for 3000 frames. Each
# room prints the height climbed; the test passes when every one climbs most of its column, with the
# original physics and with HEAVY PHYSICS (#65). Also
# checks that with the option off the held button lays one bomb only, and that a charged beam
# carried into the ball with the button held still becomes the bomb spread.
set -e
ROM=$(realpath "$1")
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${WORK:-/tmp/sm-auto-bomb-test}
mkdir -p "$WORK/saves"
S=$ROOT/sm
SRCS=$(ls $S/src/*.c $S/src/snes/*.c | grep -v "/main.c\|opengl.c\|glsl_shader.c")
gcc -O2 -fno-strict-aliasing -I"$S" -I"$ROOT/source" -I"$ROOT/third_party/sdl_keys" \
    -DSYSTEM_VOLUME_MIXER_AVAILABLE=0 -DFULL_NATIVE -DSM_WARP_DEBUG -w $SRCS "$ROOT/source/sm_map.c" "$ROOT/source/sm_warp.c" \
    "$ROOT/source/auto_bomb.c" "$ROOT/tools/auto-bomb-test/auto_bomb_test.c" -o "$WORK/auto_bomb_test" -lm
cd "$WORK" && ./auto_bomb_test "$ROM" 2>&1 | grep -v "^Warning\|Unable\|v1=\|\*\*\* \|arrival:"
