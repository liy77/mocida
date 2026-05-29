#!/usr/bin/env bash
# ---------------------------------------------------------------------
# Pull the Windows-side MONOREPO checkout into the WSL home copy.
# Lives at the monorepo root and syncs BOTH packages (mocida/ + mocida-rs/).
# Run from WSL.
#
# Why two checkouts?
#   - /mnt/c/... is the Windows NTFS mount. CMake / Ninja / cargo choke on
#     POSIX metadata there and I/O is 5-10x slower than ext4. So Linux
#     builds live under ~/mocida on the WSL ext4 root.
#   - We edit on Windows (VSCode etc.); this script mirrors the edits into
#     ~/mocida so `python build.py` has fresh source.
#
# Fast by design: only the source you actually edit is synced. The heavy
# vendored trees (SDL*, vcpkg, mimalloc, .webview2) and all build output
# (build/, target/) are EXCLUDED - bootstrap those once on the Linux side
# with `python mocida/setup.py`. Excluded dirs are also protected from
# --delete, so your local clones/builds survive every sync.
#
# Usage:
#   ./sync-from-windows.sh                  # just sync
#   ./sync-from-windows.sh build            # sync + python build.py (c + rust)
#   ./sync-from-windows.sh build --release  # sync + python build.py --release
#   ./sync-from-windows.sh build c          # sync + build only the C package
#   WIN_SRC=/mnt/d/foo ./sync-from-windows.sh   # override source path
# ---------------------------------------------------------------------
set -euo pipefail

# Source: the Windows-side monorepo root. Override via WIN_SRC if needed.
WIN_SRC="${WIN_SRC:-/mnt/c/Users/hcsbr/Documents/mocida}"

# Destination: this script's directory (the WSL-side ~/mocida monorepo).
DST="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ ! -d "$WIN_SRC" ]]; then
    echo "ERROR: WIN_SRC not found: $WIN_SRC"
    echo "Set WIN_SRC=/mnt/c/path/to/mocida and rerun."
    exit 1
fi
if [[ "$WIN_SRC" -ef "$DST" ]]; then
    echo "ERROR: source and destination are the same dir ($DST)."
    exit 1
fi

# Heavy / generated / Windows-only trees we never want to copy. Listed as
# anchored paths so rsync both skips copying AND skips deleting them on the
# destination (vendored deps cloned by setup.py on the Linux side survive).
EXCLUDES=(
    --exclude=/.git/
    --exclude=__pycache__/
    --exclude='*.py[cod]'
    # C package: build output + vendored deps (re-created by setup.py here)
    --exclude=/mocida/build/
    --exclude=/mocida/vcpkg/
    --exclude=/mocida/mimalloc/
    --exclude=/mocida/.webview2/
    --exclude=/mocida/SDL/
    --exclude=/mocida/SDL_image/
    --exclude=/mocida/SDL_ttf/
    --exclude=/mocida/docs/generated/
    --exclude=/mocida/release/dist/
    --exclude=/mocida/release/stage/
    # Rust package: build output
    --exclude=/mocida-rs/target/
    # logs
    --exclude='*.log'
)

echo "Syncing (source only): $WIN_SRC/  ->  $DST/"
rsync -a --delete --info=stats1 "${EXCLUDES[@]}" "$WIN_SRC/" "$DST/"
echo "Sync done."

# Optional: chain into the monorepo build, forwarding any extra args.
if [[ "${1:-}" == "build" ]]; then
    shift
    PY=python3; command -v python3 >/dev/null 2>&1 || PY=python
    echo
    echo "Running $PY build.py $*"
    cd "$DST"
    exec "$PY" build.py "$@"
fi
