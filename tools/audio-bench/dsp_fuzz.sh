#!/bin/bash
# Checks the optimised S-DSP against the original one (see dsp_fuzz.c). No ROM needed.
# Usage: tools/audio-bench/dsp_fuzz.sh [SEEDS...]
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${WORK:-/tmp/sm-dsp-fuzz}
mkdir -p "$WORK"
R=$ROOT/sm/src
# The reference: dsp.c as vendored from snesrev (commit before the first DSP change).
git -C "$ROOT" show 49c331a:sm/src/snes/dsp.c > "$R/snes/dsp_reference_tmp.c"
trap 'rm -f "$R/snes/dsp_reference_tmp.c"' EXIT
RENAME=$(for f in dsp_init dsp_free dsp_reset dsp_saveload dsp_cycle dsp_read dsp_write dsp_getSamples; do echo -D$f=old_$f; done)
gcc -O2 -w -iquote "$R" -iquote "$R/snes" -c "$R/snes/dsp_reference_tmp.c" -o "$WORK/old.o" $RENAME
gcc -O2 -w -iquote "$R" "$ROOT/tools/audio-bench/dsp_fuzz.c" "$R/snes/dsp.c" "$WORK/old.o" -o "$WORK/dsp_fuzz"
for seed in ${@:-1 2 3}; do
  "$WORK/dsp_fuzz" "$seed"
  "$WORK/dsp_fuzz" "$seed" full
done
