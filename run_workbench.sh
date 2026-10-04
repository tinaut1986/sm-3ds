#!/usr/bin/env bash
# Starts the layer workbench (tools/layer-workbench/README.md).
# Usage: ./run_workbench.sh [ROM.sfc] [serve.py options]
# The rooms are exported on first use (needs the ROM: first argument, SM_ROM or a .sfc/.smc in the repo root) into
# ROOMS_DIR (default ~/sm-3ds-rooms); EXPORT=1 forces a new export. Other options go to serve.py.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOMS_DIR="${ROOMS_DIR:-$HOME/sm-3ds-rooms}"

if command -v python3 >/dev/null 2>&1; then
    PYTHON_BIN="python3"
elif command -v python >/dev/null 2>&1; then
    PYTHON_BIN="python"
else
    echo "Error: no Python 3 interpreter found on this system." >&2
    exit 1
fi

ROM="${SM_ROM:-}"
if [ $# -gt 0 ] && [[ "$1" != -* ]] && [ -f "$1" ]; then
    ROM="$1"
    shift
fi

if [ -z "$ROM" ]; then   # a ROM left in the repo root (git ignores *.sfc/*.smc)
    for f in "$ROOT_DIR"/*.sfc "$ROOT_DIR"/*.smc; do [ -f "$f" ] && ROM="$f" && break; done
fi

ROM_ARG=()
[ -n "$ROM" ] && ROM_ARG=(--rom "$ROM")   # only for the area map's room positions

if [ "${EXPORT:-0}" = 1 ] || ! ls "$ROOMS_DIR"/*.room >/dev/null 2>&1; then
    if [ -z "$ROM" ]; then
        echo "No rooms in $ROOMS_DIR yet: give the ROM as the first argument or in SM_ROM." >&2
        exit 1
    fi
    echo "Exporting the rooms (once)..."
    "$ROOT_DIR/tools/layer-workbench/export.sh" "$ROM" "$ROOMS_DIR"
fi

echo "Starting Layer Workbench..."
exec "$PYTHON_BIN" "$ROOT_DIR/tools/layer-workbench/serve.py" "$ROOMS_DIR" ${ROM_ARG[@]+"${ROM_ARG[@]}"} "$@"
