#!/bin/bash
# Host check of AIR SPIN (#68) and the HEAVY PHYSICS wall jump (#65), tools/air-spin-test/air_spin_test.c.
# Usage: tools/air-spin-test/run.sh /path/to/rom.sfc
# Boots the game headless and puts Samus on Landing Site's floor. With the option off a second jump press
# in a plain jump changes nothing; with it on, the press turns a rising jump and a fall into a spin jump
# without a new jump's speed. With HEAVY PHYSICS the wall jump starts as fast as a jump.
set -e
ROM=$(realpath "$1")
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${WORK:-/tmp/sm-air-spin-test}
mkdir -p "$WORK/saves"
S=$ROOT/sm
SRCS=$(ls $S/src/*.c $S/src/snes/*.c | grep -v "/main.c\|opengl.c\|glsl_shader.c")
gcc -O2 -fno-strict-aliasing -I"$S" -I"$ROOT/source" -I"$ROOT/third_party/sdl_keys" \
    -DSYSTEM_VOLUME_MIXER_AVAILABLE=0 -DFULL_NATIVE -DSM_WARP_DEBUG -w $SRCS "$ROOT/source/sm_map.c" "$ROOT/source/sm_warp.c" \
    "$ROOT/tools/air-spin-test/air_spin_test.c" -o "$WORK/air_spin_test" -lm
cd "$WORK" && ./air_spin_test "$ROM" 2>&1 | grep -v "^Warning\|Unable\|v1=\|\*\*\* \|arrival:"
