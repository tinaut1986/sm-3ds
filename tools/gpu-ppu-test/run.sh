#!/bin/bash
# Host check of the GPU renderer's frame building against the CPU renderer
# (see gpu_ppu_test.c for what is compared).
#
# Usage: tools/gpu-ppu-test/run.sh ROM state STATE.sav [FRAMES]
#        tools/gpu-ppu-test/run.sh ROM rooms [FRAMES] [ROOM_HEX]
#        tools/gpu-ppu-test/run.sh ROM pbomb STATE.sav [FRAMES]   (power bomb, every frame to pb-NNN.ppm)
#        tools/gpu-ppu-test/run.sh ROM boot SRAM.srm [FRAMES]     (continue the first file, test from the load)
# Mismatching frames are written to $WORK/diff-NN.ppm (CPU | GPU | difference).
# Built 32-bit with -malign-double so console save states load (docs/debug-tools.md).
set -e
ROM=$(realpath "$1")
MODE=$2
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${WORK:-/tmp/sm-gpu-ppu-test}
mkdir -p "$WORK/saves"
S=$ROOT/sm
SRCS=$(ls $S/src/*.c $S/src/snes/*.c | grep -v "/main.c\|opengl.c\|glsl_shader.c")
# NO_BUILD=1: reuse $WORK/gpu_ppu_test (tools/test copies one build to every test)
if [ -z "$NO_BUILD" ]; then
  gcc ${HOST_CFLAGS:--m32 -malign-double} -O2 -g -fno-strict-aliasing -I"$S" -I"$ROOT/source" -I"$ROOT/SDL/include" \
      -I"$ROOT/SDL/build/include" -DSYSTEM_VOLUME_MIXER_AVAILABLE=0 -DFULL_NATIVE -w $SRCS \
      "$ROOT/source/gpu_ppu.c" "$ROOT/source/gpu_ppu_ref.c" "$ROOT/source/sm_map.c" "$ROOT/source/sm_warp.c" \
      "$ROOT/tools/gpu-ppu-test/gpu_ppu_test.c" -o "$WORK/gpu_ppu_test" -lm
fi
[ "$2" = build ] && exit 0   # tools/test: compile only
rm -f "$WORK"/diff-*.ppm
if [ "$MODE" = state ] || [ "$MODE" = pbomb ]; then
  cp "$(realpath "$3")" "$WORK/saves/save9.sav"
  rm -f "$WORK"/pb-*.ppm
  cd "$WORK" && ./gpu_ppu_test "$ROM" $MODE "$3" ${4:-60} 2>&1 | grep -v "^Warning\|Unable\|v1=\|\*\*\* "
elif [ "$MODE" = boot ]; then
  # boot SRAM [FRAMES]: continue the first file of that SRAM and test from the load on
  rm -f "$WORK"/saves/*
  cp "$(realpath "$3")" "$WORK/saves/sm.srm"
  cd "$WORK" && ./gpu_ppu_test "$ROM" boot ${4:-1500} 2>&1 | grep -v "^Warning\|Unable\|v1=\|\*\*\* \|RomPtr - Invalid"
else
  cd "$WORK" && ./gpu_ppu_test "$ROM" rooms ${3:-20} $4 2>&1 | grep -v "^Warning\|Unable\|v1=\|\*\*\* "
fi
