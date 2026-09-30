#!/bin/bash
# Renders the bottom-screen UI tabs to PNGs on the host, with sample game data.
# Usage: tools/ui-preview/build.sh [outdir]      (needs gcc and python3 + Pillow)
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-/tmp/sm-ui-preview}
mkdir -p "$OUT"
S=$ROOT/sm
SRCS=$(ls $S/src/*.c $S/src/snes/*.c | grep -v "/main.c\|opengl.c\|glsl_shader.c")
gcc -O1 -fno-strict-aliasing -I"$ROOT/tools/ui-preview/fake" -I"$ROOT/source" -I"$S" \
    -I"$ROOT/SDL/include" -I"$ROOT/SDL/build/include" -DSYSTEM_VOLUME_MIXER_AVAILABLE=0 -DFULL_NATIVE \
    -w $SRCS \
    "$ROOT/source/bottom_ui.c" "$ROOT/source/ui_draw.c" "$ROOT/source/ui_font.c" "$ROOT/source/cheats.c" "$ROOT/source/sm_map.c" "$ROOT/source/sm_warp.c" "$ROOT/source/debug_tools.c" \
    "$ROOT/tools/ui-preview/preview.c" -o "$OUT/preview" -lm
(cd "$OUT" && ./preview && python3 - <<'PY'
from PIL import Image
import glob
for f in sorted(glob.glob('tab*.ppm')):
    Image.open(f).resize((640, 480), Image.NEAREST).save(f.replace('.ppm', '.png'))
    print(f.replace('.ppm', '.png'))
PY
)
