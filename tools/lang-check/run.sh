#!/bin/bash
# Builds tools/lang-check and runs it (see lang_check.c). No ROM needed.
# Usage: tools/lang-check/run.sh template OUT.txt     the template of a new language
#        tools/lang-check/run.sh FILE.txt...          checks language files
#        tools/lang-check/run.sh                      checks romfs/lang/*.txt and that
#                                                     docs/lang-template.txt is up to date
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${WORK:-/tmp/sm-lang-check}
mkdir -p "$WORK"
S=$ROOT/sm
# Only the key lists of game_text*.c run: the game they draw over is left out, and the link
# ignores the symbols it would have given (never reached).
gcc -O1 -fno-strict-aliasing -w -I"$S" -I"$ROOT/source" -I"$ROOT/third_party/sdl_keys" \
    -DSYSTEM_VOLUME_MIXER_AVAILABLE=0 -DFULL_NATIVE -no-pie -Wl,--unresolved-symbols=ignore-all \
    "$ROOT/source/game_text.c" "$ROOT/source/game_text_screens.c" "$ROOT/source/ui_lang.c" "$ROOT/source/lang_file.c" \
    "$ROOT/source/ui_font.c" "$ROOT/tools/lang-check/lang_check.c" -o "$WORK/lang_check" -lm
if [ $# -gt 0 ]; then
  exec "$WORK/lang_check" "$@"
fi
status=0
"$WORK/lang_check" "$ROOT"/romfs/lang/*.txt || status=1
"$WORK/lang_check" template "$WORK/lang-template.txt"
if ! cmp -s "$WORK/lang-template.txt" "$ROOT/docs/lang-template.txt"; then
  echo "docs/lang-template.txt is out of date: tools/lang-check/run.sh template docs/lang-template.txt"
  status=1
fi
[ $status = 0 ] && echo "lang_check: OK"
exit $status
