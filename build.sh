#!/usr/bin/env bash
# Thin wrapper: all build logic lives in build.py. This just locates a
# Python 3 interpreter and forwards every argument to it.
#   ./build.sh [all|c|rust|setup|clean] [--config release] [--force] ...
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if command -v python3 >/dev/null 2>&1; then
    PY=python3
elif command -v python >/dev/null 2>&1; then
    PY=python
else
    echo "Python 3 not found on PATH (tried python3, python). Install it and retry." >&2
    exit 1
fi

exec "$PY" "$DIR/build.py" "$@"
