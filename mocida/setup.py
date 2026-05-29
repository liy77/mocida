#!/usr/bin/env python3
"""
Mocida - full project bootstrap (cross-platform port of setup.ps1/.bat).

Steps:
  1. Ensure required tools exist (git, cmake, clang, ninja, make). On
     Windows, missing tools are installed via winget; on Linux/macOS they
     must be installed by the user's package manager (we only verify).
  2. Bootstrap a local vcpkg under mocida/ and install libcurl.
  3. Clone SDL / SDL_image / SDL_ttf at the pinned commits.
  4. Init the nested submodules of SDL_image / SDL_ttf.
  5. Clone Microsoft mimalloc (allocator).
  6. Run the first build (build.py) unless --no-build.

Usage:
  python setup.py                 full bootstrap + first build
  python setup.py --no-build      configure deps, skip the build
  python setup.py --force         re-checkout the pinned SDL commits
  python setup.py --skip-install  do not call winget; only verify tools
"""

import argparse
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent  # mocida/
_SYS = platform.system()

# --- pretty -------------------------------------------------------------
_COLOR = (not os.environ.get("NO_COLOR")) and sys.stdout.isatty()
if _COLOR and _SYS == "Windows":
    os.system("")


def _c(t, code):
    return f"\033[{code}m{t}\033[0m" if _COLOR else t


def step(m): print(_c(f"\n== {m} ==", "36;1"))
def ok(m):   print(_c(f"  + {m}", "32"))
def info(m): print(_c(f"  . {m}", "90"))
def warn(m): print(_c(f"  ! {m}", "33"))
def fail(m): print(_c(f"  x {m}", "31"))


class SetupError(Exception):
    pass


# --- pinned SDL commits -------------------------------------------------
SDL_PINS = [
    ("SDL",       "https://github.com/libsdl-org/SDL.git",       "877399b2b2cf21e67554ed9046410f268ce1d1b2"),
    ("SDL_image", "https://github.com/libsdl-org/SDL_image.git", "11154afb7855293159588b245b446a4ef09e574f"),
    ("SDL_ttf",   "https://github.com/libsdl-org/SDL_ttf.git",   "a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b"),
]

# tool -> winget id (Windows only)
WINGET_IDS = {
    "git": "Git.Git",
    "cmake": "Kitware.CMake",
    "clang": "LLVM.LLVM",
    "ninja": "Ninja-build.Ninja",
    "make": "GnuWin32.Make",
}


def have(cmd):
    return shutil.which(cmd) is not None


def run(cmd, cwd=None, check=True):
    r = subprocess.run(cmd, cwd=str(cwd) if cwd else None)
    if check and r.returncode != 0:
        raise SetupError(f"command failed ({r.returncode}): {' '.join(map(str, cmd))}")
    return r.returncode


def vcpkg_triplet():
    if _SYS == "Windows":
        return "x64-windows"
    if _SYS == "Darwin":
        return "arm64-osx" if platform.machine().lower() in ("arm64", "aarch64") else "x64-osx"
    return "x64-linux"


# --- clone helper -------------------------------------------------------
def clone(dirname, url, sha, force):
    target = ROOT / dirname
    pinned = bool(sha)
    if (target / ".git").exists():
        if pinned and force:
            info(f"{dirname}: --force, re-checking out {sha}")
            run(["git", "fetch", "--quiet", "origin"], cwd=target)
            run(["git", "checkout", "--quiet", sha], cwd=target)
        else:
            ok(f"{dirname} already cloned" + (" (use --force to re-checkout)" if pinned else ""))
        return
    if target.exists():
        raise SetupError(f"{dirname} exists but is not a git repo. Remove it and retry.")
    if pinned:
        info(f"{dirname}: cloning {url}")
        run(["git", "clone", "--quiet", url, str(target)])
        run(["git", "checkout", "--quiet", sha], cwd=target)
        ok(f"{dirname} @ {sha}")
    else:
        info(f"{dirname}: shallow clone {url}")
        run(["git", "clone", "--depth", "1", "--quiet", url, str(target)])
        ok(f"{dirname} ready")


# =======================================================================
def cmd_tools(skip_install):
    step("1/5  Checking / installing tools")
    missing = [c for c in WINGET_IDS if not have(c)]
    if not missing:
        ok("all tools present (git, cmake, clang, ninja, make)")
        return
    if _SYS != "Windows":
        fail(f"missing tools: {', '.join(missing)}")
        info("Install them via your package manager, e.g.:")
        info("  Debian/Ubuntu: sudo apt install git cmake clang ninja-build make")
        info("  macOS (brew):  brew install git cmake llvm ninja make")
        raise SetupError("required tools missing")
    if skip_install:
        fail(f"missing tools: {', '.join(missing)} (re-run without --skip-install)")
        raise SetupError("required tools missing")
    winget = shutil.which("winget")
    if not winget:
        raise SetupError("winget not found. Install 'App Installer' from the Microsoft Store.")
    for c in missing:
        info(f"installing {WINGET_IDS[c]} ...")
        code = subprocess.run([winget, "install", "--id", WINGET_IDS[c], "--silent",
                               "--accept-package-agreements", "--accept-source-agreements",
                               "--scope", "user"]).returncode
        if code not in (0, -1978335189, -1978335207):  # 0 / NoUpgrade / no-applicable
            subprocess.run([winget, "install", "--id", WINGET_IDS[c], "--silent",
                            "--accept-package-agreements", "--accept-source-agreements"])
        if have(c):
            ok(f"{c} available")
        else:
            warn(f"{c} installed but not on PATH yet - open a new terminal and re-run.")


def cmd_vcpkg():
    step("2/5  Bootstrap vcpkg + libcurl")
    clone("vcpkg", "https://github.com/microsoft/vcpkg.git", "", force=False)
    vcpkg_dir = ROOT / "vcpkg"
    exe = vcpkg_dir / ("vcpkg.exe" if _SYS == "Windows" else "vcpkg")
    if not exe.exists():
        info("bootstrapping vcpkg ...")
        if _SYS == "Windows":
            run([str(vcpkg_dir / "bootstrap-vcpkg.bat"), "-disableMetrics"])
        else:
            run(["bash", str(vcpkg_dir / "bootstrap-vcpkg.sh"), "-disableMetrics"])
    if not exe.exists():
        raise SetupError("vcpkg bootstrap did not produce the vcpkg executable")
    ok("vcpkg ready")
    triplet = vcpkg_triplet()
    info(f"vcpkg install curl:{triplet}  (first time can take a few minutes)")
    run([str(exe), "install", f"curl:{triplet}"])
    ok(f"libcurl installed ({triplet})")
    os.environ["CMAKE_TOOLCHAIN_FILE"] = str(
        vcpkg_dir / "scripts" / "buildsystems" / "vcpkg.cmake")


def cmd_sdl(force):
    step("3/5  Cloning SDL / SDL_image / SDL_ttf + mimalloc")
    for name, url, sha in SDL_PINS:
        clone(name, url, sha, force)
    clone("mimalloc", "https://github.com/microsoft/mimalloc.git", "", force=False)


def cmd_submodules():
    step("4/5  Nested submodules in SDL_image / SDL_ttf")
    for d in ("SDL_image", "SDL_ttf"):
        target = ROOT / d
        if (target / ".gitmodules").exists():
            info(f"{d}: git submodule update --init --recursive")
            run(["git", "submodule", "update", "--init", "--recursive"], cwd=target)
            ok(f"{d} submodules ready")
        else:
            info(f"{d}: no .gitmodules, skipping")


def cmd_build(no_build):
    step("5/5  Initial build (Debug)")
    if no_build:
        info("--no-build specified, skipping.")
        return
    code = subprocess.run([sys.executable, "build.py", "--clean"], cwd=str(ROOT)).returncode
    if code != 0:
        raise SetupError(f"build.py returned {code}")


def main():
    ap = argparse.ArgumentParser(prog="setup.py", description="Mocida bootstrap.")
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--skip-install", action="store_true")
    args = ap.parse_args()
    os.chdir(ROOT)

    try:
        cmd_tools(args.skip_install)
        cmd_vcpkg()
        cmd_sdl(args.force)
        cmd_submodules()
        cmd_build(args.no_build)
    except SetupError as e:
        fail(str(e))
        return 1

    print(_c("\n== Setup completed ==", "32;1"))
    print("  python build.py            -> Debug rebuild")
    print("  python release.py          -> Release + distribution artefacts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
