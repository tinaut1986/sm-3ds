#!/bin/bash
# Host check of the map's room scan and its level-data buffer (tools/map-test/map_test.c).
# Usage: tools/map-test/run.sh /path/to/rom.sfc
set -e
ROM=$(realpath "$1")
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${WORK:-/tmp/sm-map-test}
mkdir -p "$WORK"
S=$ROOT/sm
SRCS=$(ls $S/src/*.c $S/src/snes/*.c | grep -v "/main.c\|opengl.c\|glsl_shader.c")
EXTRA=""
[ -n "$ASAN" ] && EXTRA="-fsanitize=address -fno-omit-frame-pointer"
gcc -O1 $EXTRA -fno-strict-aliasing -I"$S" -I"$ROOT/source" -I"$ROOT/third_party/sdl_keys" \
    -DSYSTEM_VOLUME_MIXER_AVAILABLE=0 -DFULL_NATIVE -w $SRCS "$ROOT/source/sm_map.c" "$ROOT/tools/map-test/map_test.c" \
    -o "$WORK/map_test" -lm
cd "$WORK" && ./map_test "$ROM" 2>&1 | grep -v "^Warning\|Unable\|v1=\|\*\*\* "
