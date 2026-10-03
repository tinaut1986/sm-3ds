#!/bin/bash
# Host test of the stereo depth mapping (source/stereo_depth.c, docs/stereo-design.md).
# No ROM needed. Usage: tools/stereo-test/run.sh
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${WORK:-/tmp/sm-stereo-test}
mkdir -p "$WORK"
gcc -O1 -g -Wall -Wextra -I"$ROOT/source" "$ROOT/source/stereo_depth.c" "$ROOT/tools/stereo-test/stereo_test.c" \
    -o "$WORK/stereo_test" -lm
"$WORK/stereo_test"
