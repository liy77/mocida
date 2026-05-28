#!/usr/bin/env bash
# ---------------------------------------------------------------------
# Mocida build script — Linux / macOS counterpart of build.bat.
#
# Build dirs: build/<platform>/<config>
#   build/linux/debug          | build/linux/release          | build/linux/relwithdebinfo
#   build/darwin/debug         | build/darwin/release         | build/darwin/relwithdebinfo
#
# Per-config isolation means switching Debug ⇄ Release does NOT trigger
# a full SDL / SDL_image / SDL_ttf rebuild — each config keeps its own
# fully-built dependency tree and Ninja's incremental engine handles
# everything from there.
#
# Flags (order-agnostic):
#   --release         CMAKE_BUILD_TYPE=Release
#   --debug           CMAKE_BUILD_TYPE=Debug (default)
#   --relwithdebinfo  optimised + debug symbols
#   --shared          mocida as a shared library (.so / .dylib)
#   --static          force the static-archive flavour (default)
#   --tests           also compile tests/test_*.c
#   --no-tests        skip tests (default)
#   --no-demo         skip the demo executable
#   --asan            instrument with AddressSanitizer + LeakSanitizer
#   --force           wipe the build/<platform>/<config> dir before
#                     configuring — forces a full rebuild including SDL.
#   --clean           alias for --force (kept for backwards compat)
#   --reconfigure     wipe just CMakeCache.txt, keep compiled objects.
#                     Use after editing CMakeLists.txt or toggling a
#                     CMake option that needs a fresh configure pass.
#   --verbose         pass -v to cmake --build (per-command output)
# ---------------------------------------------------------------------
set -euo pipefail

MOCIDA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$MOCIDA_ROOT"

case "$(uname -s)" in
    Linux*)   PLATFORM=linux  ;;
    Darwin*)  PLATFORM=darwin ;;
    *)        echo "Unsupported platform: $(uname -s) (use build.bat on Windows)"; exit 1 ;;
esac

DO_FORCE=0
DO_RECONFIGURE=0
DO_VERBOSE=0
MOCIDA_SHARED=OFF
MOCIDA_TESTS=OFF
MOCIDA_DEMO=ON
MOCIDA_ASAN=""
BUILD_TYPE=Debug

for arg in "$@"; do
    case "$arg" in
        --clean|--force)  DO_FORCE=1                        ;;
        --reconfigure)    DO_RECONFIGURE=1                  ;;
        --verbose)        DO_VERBOSE=1                      ;;
        --shared)         MOCIDA_SHARED=ON                  ;;
        --static)         MOCIDA_SHARED=OFF                 ;;
        --tests)          MOCIDA_TESTS=ON                   ;;
        --no-tests)       MOCIDA_TESTS=OFF                  ;;
        --no-demo)        MOCIDA_DEMO=OFF                   ;;
        --asan)           MOCIDA_ASAN="address,undefined"   ;;
        --release)        BUILD_TYPE=Release                ;;
        --debug)          BUILD_TYPE=Debug                  ;;
        --relwithdebinfo) BUILD_TYPE=RelWithDebInfo         ;;
        -h|--help)
            sed -n '2,32p' "$0"
            exit 0
            ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

# Per-config dir. Lowercased so `build/linux/release` matches the user's
# spec; the BUILD_TYPE token sent to CMake stays canonical.
BUILD_TYPE_LC="$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"
BUILD_DIR="build/$PLATFORM/$BUILD_TYPE_LC"

if [[ "$DO_FORCE" == "1" && -d "$BUILD_DIR" ]]; then
    echo "Force-rebuild: wiping $BUILD_DIR ..."
    rm -rf "$BUILD_DIR"
fi
if [[ "$DO_RECONFIGURE" == "1" && -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    echo "Reconfigure: removing $BUILD_DIR/CMakeCache.txt + CMakeFiles/ ..."
    rm -f "$BUILD_DIR/CMakeCache.txt"
    rm -rf "$BUILD_DIR/CMakeFiles"
fi
mkdir -p "$BUILD_DIR"

# Generator: prefer Ninja when present, fall back to Unix Makefiles.
if command -v ninja >/dev/null 2>&1; then
    GENERATOR=Ninja
else
    GENERATOR="Unix Makefiles"
fi

# Compiler: prefer clang to match the Windows flavour, fall back to cc/c++.
if command -v clang >/dev/null 2>&1; then
    CC_BIN=$(command -v clang)
    CXX_BIN=$(command -v clang++)
else
    CC_BIN=$(command -v cc)
    CXX_BIN=$(command -v c++)
fi
export CC="$CC_BIN"
export CXX="$CXX_BIN"

# Optional ccache as a compiler launcher — same idea as sccache on the
# Windows side. Hits make incremental rebuilds across `--force` near-
# instant if you didn't actually touch the source.
LAUNCHER_ARGS=()
if command -v ccache >/dev/null 2>&1; then
    LAUNCHER_ARGS+=(
        -DCMAKE_C_COMPILER_LAUNCHER=ccache
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
    )
fi

# Optional vcpkg toolchain — only used if the user bootstrapped vcpkg here.
TOOLCHAIN_ARG=()
if [[ -f "$MOCIDA_ROOT/vcpkg/scripts/buildsystems/vcpkg.cmake" ]]; then
    TOOLCHAIN_ARG=(-DCMAKE_TOOLCHAIN_FILE="$MOCIDA_ROOT/vcpkg/scripts/buildsystems/vcpkg.cmake")
fi

# Skip the full configure pass when the CMake cache already exists and
# nothing structural changed. CMake's `cmake --build` will reconfigure
# on its own if CMakeLists.txt / dep CMakeLists.txt are newer than the
# cache — so this is purely about avoiding the ~10-30 s SDL / SDL_image
# / SDL_ttf re-detection cost on every re-run.
NEEDS_CONFIGURE=1
if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    NEEDS_CONFIGURE=0
fi

echo "Platform           : $PLATFORM"
echo "Build dir          : $BUILD_DIR"
echo "Generator          : $GENERATOR"
echo "CMAKE_BUILD_TYPE   : $BUILD_TYPE"
echo "MOCIDA_BUILD_SHARED: $MOCIDA_SHARED"
echo "MOCIDA_BUILD_TESTS : $MOCIDA_TESTS"
echo "MOCIDA_BUILD_DEMO  : $MOCIDA_DEMO"
echo "MOCIDA_SANITIZE    : ${MOCIDA_ASAN:-(off)}"
echo "CC / CXX           : $CC / $CXX"
if [[ "$NEEDS_CONFIGURE" == "0" ]]; then
    echo "Configure          : SKIPPED (CMakeCache.txt exists — pass --reconfigure to force)"
else
    echo "Configure          : YES (first build for this config)"
fi

if [[ "$NEEDS_CONFIGURE" == "1" ]]; then
    cmake -G "$GENERATOR" -S "$MOCIDA_ROOT" -B "$BUILD_DIR" \
        "${TOOLCHAIN_ARG[@]}" \
        "${LAUNCHER_ARGS[@]}" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_C_COMPILER="$CC_BIN" \
        -DCMAKE_CXX_COMPILER="$CXX_BIN" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DMOCIDA_BUILD_SHARED="$MOCIDA_SHARED" \
        -DMOCIDA_BUILD_TESTS="$MOCIDA_TESTS" \
        -DMOCIDA_BUILD_DEMO="$MOCIDA_DEMO" \
        -DMOCIDA_SANITIZE="$MOCIDA_ASAN" \
        -Wno-dev --log-level=NOTICE
fi

# Pick which targets to build. Default is mocida + demo (+ tests when
# enabled) — explicit list avoids building any stray SDL utility target
# that crept into the dependency graph. When SDL was built once for
# this config, none of these targets force a rebuild of it on a
# no-source-change run because Ninja stats first and rebuilds only
# what is stale.
TARGETS=(mocida)
if [[ "$MOCIDA_DEMO" == "ON" ]]; then
    TARGETS+=(demo)
fi
if [[ "$MOCIDA_TESTS" == "ON" ]]; then
    # Build every test_* target. Resolve the list at build time so
    # newly-added tests don't need a script change.
    while IFS= read -r t; do
        TARGETS+=("$t")
    done < <(find tests -maxdepth 1 -name 'test_*.c' -printf '%f\n' 2>/dev/null \
             | sed 's/\.c$//')
fi

BUILD_ARGS=(--build "$BUILD_DIR" --parallel --config "$BUILD_TYPE" --target "${TARGETS[@]}")
if [[ "$DO_VERBOSE" == "1" ]]; then
    BUILD_ARGS+=(--verbose)
fi

echo
echo "Building targets: ${TARGETS[*]}"
cmake "${BUILD_ARGS[@]}"

echo
echo "Build completed."
echo "Artefacts: $BUILD_DIR/"
if [[ -n "$MOCIDA_ASAN" ]]; then
    echo
    echo "ASan/LSan instrumentation is on. Run a binary directly to see"
    echo "the leak report at exit:"
    echo "    $BUILD_DIR/demo"
    echo "Optional env knobs:"
    echo "    LSAN_OPTIONS=verbosity=1:log_threads=1"
    echo "    ASAN_OPTIONS=detect_leaks=1:print_stats=1:halt_on_error=0"
fi
