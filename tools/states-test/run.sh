#!/bin/bash
# Host test of the save-state store (source/states_store.c). No ROM needed.
# Usage: tools/states-test/run.sh
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${WORK:-/tmp/sm-states-test}
rm -rf "$WORK"; mkdir -p "$WORK/run"
gcc -O1 -g -Wall -Wextra -I"$ROOT/source" "$ROOT/source/states_store.c" "$ROOT/tools/states-test/states_test.c" -o "$WORK/states_test"
"$WORK/states_test" "$WORK/run"
