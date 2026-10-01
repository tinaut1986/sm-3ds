#!/bin/bash
# Audio benchmark / bit-exactness check (see audio_bench.c).
# Usage: tools/audio-bench/run.sh ROM STATE.sav [FRAMES]
#        tools/audio-bench/run.sh ROM rooms [FRAMES]   (every room, FRAMES each)
# Same 32-bit -malign-double build as the other host tools, so console states load.
set -e
ROM=$(realpath "$1")
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${WORK:-/tmp/sm-audio-bench}
mkdir -p "$WORK/saves"
S=$ROOT/sm
SRCS=$(ls $S/src/*.c $S/src/snes/*.c | grep -v "/main.c\|opengl.c\|glsl_shader.c")
gcc ${HOST_CFLAGS:--m32 -malign-double} -O2 -g -fno-strict-aliasing -I"$S" -I"$ROOT/source" -I"$ROOT/SDL/include" \
    -I"$ROOT/SDL/build/include" -DSYSTEM_VOLUME_MIXER_AVAILABLE=0 -DFULL_NATIVE -w $SRCS \
    "$ROOT/source/sm_map.c" "$ROOT/source/sm_warp.c" "$ROOT/tools/audio-bench/audio_bench.c" -o "$WORK/audio_bench" -lm
if [ "$2" = rooms ]; then
  # Boot from an empty SRAM every time: a run leaves sm.srm behind, and the next boot
  # would start a different game.
  rm -f "$WORK"/saves/*
  cd "$WORK" && ./audio_bench "$ROM" rooms ${3:-120} 2>&1 | grep -v "^Warning\|Unable\|v1=\|\*\*\* \|RomPtr - Invalid"
  exit
fi
cp "$(realpath "$2")" "$WORK/saves/save9.sav"
cd "$WORK" && ./audio_bench "$ROM" ${3:-3600} 2>&1 | grep -v "^Warning\|Unable\|v1=\|\*\*\* \|RomPtr - Invalid"
