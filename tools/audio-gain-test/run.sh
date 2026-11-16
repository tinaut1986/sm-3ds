#!/bin/bash
# Host test of the master volume's gain and soft limit (source/audio_gain.c, #67).
# No ROM needed. Usage: tools/audio-gain-test/run.sh
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${WORK:-/tmp/sm-audio-gain-test}
mkdir -p "$WORK"
gcc -O1 -g -Wall -Wextra -I"$ROOT/source" "$ROOT/source/audio_gain.c" "$ROOT/tools/audio-gain-test/audio_gain_test.c" \
    -o "$WORK/audio_gain_test"
"$WORK/audio_gain_test"
