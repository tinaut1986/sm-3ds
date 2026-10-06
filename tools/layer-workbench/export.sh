#!/bin/bash
# Dumps every room's data for the layer workbench (needs the ROM, writes outside the repo by default).
# Usage: tools/layer-workbench/export.sh /path/to/Super\ Metroid\ \(Japan,\ USA\).sfc [outdir] [room_hex]
set -e
ROM=$(realpath "$1")
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${2:-$HOME/sm-3ds-rooms}
WORK=${WORK:-/tmp/sm-layer-workbench}
mkdir -p "$WORK"
S=$ROOT/sm
SRCS=$(ls $S/src/*.c $S/src/snes/*.c | grep -v "/main.c\|opengl.c\|glsl_shader.c")
gcc -O2 -g -DSM_WARP_DEBUG -fno-strict-aliasing -I"$S" -I"$ROOT/source" -I"$ROOT/third_party/sdl_keys" \
    -DSYSTEM_VOLUME_MIXER_AVAILABLE=0 -DFULL_NATIVE -w $SRCS \
    "$ROOT/source/sm_map.c" "$ROOT/source/sm_warp.c" "$ROOT/tools/layer-workbench/export_rooms.c" -o "$WORK/export_rooms" -lm
mkdir -p "$WORK/saves" "$OUT"
cd "$WORK"
./export_rooms "$ROM" "$OUT" $3 2>&1 | grep -v "^Warning\|Unable\|v1=\|\*\*\* "
