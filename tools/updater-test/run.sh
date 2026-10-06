#!/bin/bash
# Host test of the self-updater's pure part (source/updater_parse.c: version comparison and
# GitHub release-list parsing). No ROM needed. Usage: tools/updater-test/run.sh
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${WORK:-/tmp/sm-updater-test}
mkdir -p "$WORK"
gcc -O1 -g -Wall -Wextra -I"$ROOT/source" "$ROOT/source/updater_parse.c" "$ROOT/tools/updater-test/updater_test.c" \
    -o "$WORK/updater_test"
"$WORK/updater_test"
