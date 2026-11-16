#!/bin/bash
# Host check of REDUX (P4.26), tools/redux-test/redux_test.c: its ROM data goes in while the option is on and
# comes out when it goes off, and a door transition takes at most 3/4 of the original's frames.
# Usage: tools/redux-test/run.sh /path/to/rom.sfc
set -e
ROM=$(realpath "$1")
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${WORK:-/tmp/sm-redux-test}
mkdir -p "$WORK/saves"
S=$ROOT/sm
SRCS=$(ls $S/src/*.c $S/src/snes/*.c | grep -v "/main.c\|opengl.c\|glsl_shader.c")
gcc -O2 -fno-strict-aliasing -I"$S" -I"$ROOT/source" -I"$ROOT/third_party/sdl_keys" \
    -DSYSTEM_VOLUME_MIXER_AVAILABLE=0 -DFULL_NATIVE -DSM_WARP_DEBUG -w $SRCS "$ROOT/source/sm_map.c" "$ROOT/source/sm_warp.c" \
    "$ROOT/tools/redux-test/redux_test.c" -o "$WORK/redux_test" -lm
cd "$WORK" && ./redux_test "$ROM" 2>&1 | grep -v "^Warning\|Unable\|v1=\|\*\*\* \|arrival:"
