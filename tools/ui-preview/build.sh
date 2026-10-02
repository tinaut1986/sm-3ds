#!/bin/bash
# Renders the bottom-screen UI to PNGs on the host, with sample game data.
# Usage: SM_ROM=/path/to/rom.sfc tools/ui-preview/build.sh [outdir]
#   The ROM is needed: the map tab and the room list read it.
#   DEBUG_TOOLS=0 renders what a plain build shows (default 1, like build_3ds.sh).
# Needs gcc and python3 + Pillow.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-/tmp/sm-ui-preview}
[ -n "$SM_ROM" ] || { echo "set SM_ROM to the Super Metroid ROM" >&2; exit 1; }
mkdir -p "$OUT"
printf '#pragma once\n#define DEBUG_TOOLS %s\n' "${DEBUG_TOOLS:-1}" > "$OUT/build_config.h"
S=$ROOT/sm
SRCS=$(ls $S/src/*.c $S/src/snes/*.c | grep -v "/main.c\|opengl.c\|glsl_shader.c")
gcc -O1 -fno-strict-aliasing -I"$OUT" -I"$ROOT/tools/ui-preview/fake" -I"$ROOT/source" -I"$S" \
    -I"$ROOT/SDL/include" -I"$ROOT/SDL/build/include" -DSYSTEM_VOLUME_MIXER_AVAILABLE=0 -DFULL_NATIVE \
    -w $SRCS \
    "$ROOT/source/bottom_ui.c" "$ROOT/source/ui_draw.c" "$ROOT/source/ui_font.c" "$ROOT/source/cheats.c" "$ROOT/source/sm_map.c" "$ROOT/source/sm_warp.c" "$ROOT/source/debug_tools.c" "$ROOT/source/scene_rec.c" \
    "$ROOT/tools/ui-preview/preview.c" -o "$OUT/preview" -lm
(cd "$OUT" && rm -f shot*.ppm shot*.png && ./preview "$SM_ROM" && python3 - <<'PY'
from PIL import Image
import glob
for f in sorted(glob.glob('shot*.ppm')):
    Image.open(f).resize((640, 480), Image.NEAREST).save(f.replace('.ppm', '.png'))
    print(f.replace('.ppm', '.png'))
PY
)
