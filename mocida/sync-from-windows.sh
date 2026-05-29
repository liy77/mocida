#!/usr/bin/env bash
# ---------------------------------------------------------------------
# Pull the Windows-side checkout into the WSL home copy. Run from WSL.
#
# Why two checkouts?
#   - /mnt/c/... is the Windows NTFS mount. CMake / Ninja choke on POSIX
#     metadata semantics there ("Operation not permitted" inside
#     configure_file), and I/O is 5-10x slower than ext4. So Linux
#     builds live under ~/mocida on the WSL ext4 root.
#   - We edit on Windows (VSCode etc.); this script mirrors the edits
#     into ~/mocida so ./build.sh has fresh source.
#
# Excludes:
#   - /build     : per-platform output dirs (build/linux gets rebuilt here;
#                  build/win32 is from Windows, useless on Linux)
#   - /vcpkg     : Windows-only package manager (~2 GB)
#   - /.webview2 : Windows-only WebView2 NuGet cache
#   - /.git      : we don't run git from here
#
# Usage:
#   ./sync-from-windows.sh                  # just sync
#   ./sync-from-windows.sh build            # sync + ./build.sh
#   ./sync-from-windows.sh build --tests    # sync + ./build.sh --tests
#   WIN_SRC=/mnt/d/foo ./sync-from-windows.sh   # override source path
# ---------------------------------------------------------------------
set -euo pipefail

# Source: the Windows-side project. Default works for hcsbr; override
# via WIN_SRC env if your checkout lives elsewhere.
WIN_SRC="${WIN_SRC:-/mnt/c/Users/hcsbr/Documents/mocida/mocida}"

# Destination: this script's directory (the WSL-side ~/mocida copy).
DST="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ ! -d "$WIN_SRC" ]]; then
    echo "ERROR: WIN_SRC not found: $WIN_SRC"
    echo "Set WIN_SRC=/mnt/c/path/to/mocida and rerun."
    exit 1
fi

if [[ "$WIN_SRC" -ef "$DST" ]]; then
    echo "ERROR: source and destination point to the same dir ($DST)."
    echo "This script is meant to copy FROM Windows TO ~/mocida."
    exit 1
fi

echo "Syncing: $WIN_SRC/  ->  $DST/"
rsync -a --delete \
    --exclude=/build \
    --exclude=/vcpkg \
    --exclude=/.webview2 \
    --exclude=/.git \
    "$WIN_SRC/" "$DST/"
echo "Sync done."

# Optional: chain into the build with any extra args forwarded.
if [[ "${1:-}" == "build" ]]; then
    shift
    echo
    echo "Running ./build.sh $*"
    exec "$DST/build.sh" "$@"
fi
