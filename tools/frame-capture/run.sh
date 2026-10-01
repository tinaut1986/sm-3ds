#!/bin/bash
# Frame capture on the PC from a console save state (what Debug tab -> FRAME DUMP
# writes on the console), plus a PNG of the frame.
#
# Usage: tools/frame-capture/run.sh ROM STATE.sav [FRAMES] [OUTDIR]
#   STATE.sav  a save state from the console (saves/saveN.sav in the data folder)
#   FRAMES     frames to run after loading, before the captured one (default 10)
#   OUTDIR     default /tmp/sm-frame-capture; files land in OUTDIR/debug/
set -e
ROM=$(realpath "$1")
STATE=$(realpath "$2")
FRAMES=${3:-10}
OUT=${4:-/tmp/sm-frame-capture}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$OUT/saves" "$OUT/debug"
S=$ROOT/sm
SRCS=$(ls $S/src/*.c $S/src/snes/*.c | grep -v "/main.c\|opengl.c\|glsl_shader.c")
gcc ${HOST_CFLAGS:--m32 -malign-double} -O2 -fno-strict-aliasing -I"$S" -I"$ROOT/source" -I"$ROOT/SDL/include" -I"$ROOT/SDL/build/include" \
    -DSYSTEM_VOLUME_MIXER_AVAILABLE=0 -DFULL_NATIVE -w $SRCS \
    "$ROOT/source/debug_tools.c" "$ROOT/tools/frame-capture/frame_capture.c" -o "$OUT/frame_capture" -lm
cp "$STATE" "$OUT/saves/save9.sav"
cd "$OUT"
./frame_capture "$ROM" 9 "$FRAMES" 2>&1 | grep -v "^Warning\|Unable\|v1=\|\*\*\* "
# The newest set is the most recently written top.rgb.
TOP=$(ls -t debug/sm-dump-*-top.rgb | head -1)
python3 - "$TOP" <<'PY'
import sys
from PIL import Image
top = sys.argv[1]
img = Image.frombytes('RGB', (256, 240), open(top, 'rb').read())
png = top.replace('-top.rgb', '-top.png')
img.resize((512, 480), Image.NEAREST).save(png)
print(png)
PY
