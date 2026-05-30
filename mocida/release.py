#!/usr/bin/env python3
"""
Mocida SDK + installer release packager — cross-platform.

Run it on each target OS to produce that platform's release artefacts:

  Windows : mocida-sdk-windows-x64.zip      + mocida-installer-windows-x64.exe
  Linux   : mocida-sdk-linux-<arch>.tar.gz  + mocida-installer-linux-<arch>
  macOS   : mocida-sdk-macos-<arch>.tar.gz  + mocida-installer-macos-<arch>

(`<arch>` is x64 or arm64.) The asset names match MOCIDA_SDK_ASSET_NAME in
installer/installer.c, so the GUI installer downloads the right SDK for the
machine it runs on from the project's GitHub "latest" release.

Two CMake configures are used: the SDK is built with mocida as a SHARED lib
(apps link the shipped runtime); the installer is built fully STATIC so it
ships as a single self-contained binary.

Usage:
  python release.py                  package this OS's assets into release/dist/
  python release.py --upload [tag]   also publish via `gh release`
                                     (tag defaults to v<UTC timestamp>)

`--upload` is host-agnostic: run it on each OS pointing at the SAME tag to
attach all three platforms' assets to one release.
"""

import argparse
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import zipfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent  # mocida/
_SYS = platform.system()                # 'Windows' | 'Linux' | 'Darwin'
_IS_WIN = _SYS == "Windows"
_IS_MAC = _SYS == "Darwin"

_MACH = platform.machine().lower()
ARCH = "arm64" if _MACH in ("arm64", "aarch64") else "x64"

if _IS_WIN:
    OS_TAG = "windows"
    SDK_ASSET = "mocida-sdk-windows-x64.zip"          # Windows SDK stays x64
    INSTALLER_ASSET = "mocida-installer-windows-x64.exe"
    INSTALLER_BIN = "mocida_installer.exe"
    MOCIDA_LIB = "mocida.dll"
else:
    OS_TAG = "macos" if _IS_MAC else "linux"
    SDK_ASSET = f"mocida-sdk-{OS_TAG}-{ARCH}.tar.gz"
    INSTALLER_ASSET = f"mocida-installer-{OS_TAG}-{ARCH}"
    INSTALLER_BIN = "mocida_installer"
    MOCIDA_LIB = "libmocida.dylib" if _IS_MAC else "libmocida.so"

STAGE_DIR = ROOT / "release" / "stage"
DIST_DIR = ROOT / "release" / "dist"
BUILD_SDK = ROOT / "build" / f"{OS_TAG}-release-sdk"
BUILD_INST = ROOT / "build" / f"{OS_TAG}-release-installer"

_COLOR = (not os.environ.get("NO_COLOR")) and sys.stdout.isatty()
if _COLOR and _IS_WIN:
    os.system("")


def _c(t, code):
    return f"\033[{code}m{t}\033[0m" if _COLOR else t


def step(m): print(_c(f"\n== {m} ==", "36;1"))
def info(m): print(_c(f"  {m}", "37"))
def ok(m):   print(_c(f"  + {m}", "32"))
def fail(m): print(_c(f"  x {m}", "31"))


class RelError(Exception):
    pass


# --- MSVC / PATH (Windows only) ----------------------------------------
def augment_path():
    for d in (os.path.expandvars(r"%LocalAppData%\Microsoft\WinGet\Links"),
              r"C:\Program Files\LLVM\bin",
              os.path.expandvars(r"%LocalAppData%\Programs\LLVM\bin"),
              r"C:\Program Files\CMake\bin",
              r"C:\Program Files (x86)\GnuWin32\bin"):
        if d and os.path.isdir(d) and d not in os.environ["PATH"].split(os.pathsep):
            os.environ["PATH"] = d + os.pathsep + os.environ["PATH"]


def ensure_msvc_env():
    if os.environ.get("VCINSTALLDIR"):
        return
    vswhere = Path(os.environ.get("ProgramFiles(x86)", "")) \
        / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.exists():
        return
    vspath = subprocess.run([str(vswhere), "-latest", "-prerelease",
                             "-property", "installationPath"],
                            capture_output=True, text=True).stdout.strip()
    vcvars = Path(vspath) / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat" if vspath else None
    if not vcvars or not vcvars.exists():
        return
    info(f"Initialising MSVC environment from {vspath} ...")
    out = subprocess.run(["cmd", "/c", f'call "{vcvars}" x64 >nul && set'],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            os.environ[k] = v


def configure_and_build(build_dir, extra_defs, target):
    generator = "Ninja" if shutil.which("ninja") else \
                ("Unix Makefiles" if not _IS_WIN else "Ninja")
    toolchain = []
    compilers = []
    if _IS_WIN:
        cc = shutil.which("clang")
        cxx = shutil.which("clang++")
        if not cc:
            raise RelError("clang not found on PATH. Install LLVM or run setup.py.")
        compilers = [f"-DCMAKE_C_COMPILER={cc}", f"-DCMAKE_CXX_COMPILER={cxx}"]
        vcpkg_tc = ROOT / "vcpkg" / "scripts" / "buildsystems" / "vcpkg.cmake"
        if vcpkg_tc.exists():
            toolchain.append(f"-DCMAKE_TOOLCHAIN_FILE={vcpkg_tc}")
        wv2_static = ROOT / ".webview2" / "build" / "native" / "x64" / "WebView2LoaderStatic.lib"
        if wv2_static.exists():
            toolchain.append(f"-DMOCIDA_WEBVIEW2_NUGET_DIR={ROOT / '.webview2'}")
    else:
        # Prefer clang when present; otherwise let CMake pick the default cc.
        cc = shutil.which("clang")
        cxx = shutil.which("clang++")
        if cc:
            compilers = [f"-DCMAKE_C_COMPILER={cc}"]
            if cxx:
                compilers.append(f"-DCMAKE_CXX_COMPILER={cxx}")

    build_dir.mkdir(parents=True, exist_ok=True)
    cfg = ["cmake", "-G", generator, *toolchain,
           "-DCMAKE_BUILD_TYPE=Release", *compilers,
           *extra_defs, "-Wno-dev", "--log-level=NOTICE", str(ROOT)]
    if subprocess.run(cfg, cwd=str(build_dir)).returncode != 0:
        raise RelError(f"configure failed in {build_dir}")
    if subprocess.run(["cmake", "--build", ".", "--parallel", "--config", "Release",
                       "--target", target], cwd=str(build_dir)).returncode != 0:
        raise RelError(f"build of '{target}' failed in {build_dir}")


def _find_first(globs):
    """First existing path matching any of the given (dir, pattern) globs."""
    for d, pat in globs:
        for p in sorted(Path(d).glob(pat)):
            if p.is_file() or p.is_symlink():
                return p
    return None


def stage_sdk():
    step("Staging SDK payload")
    if STAGE_DIR.exists():
        shutil.rmtree(STAGE_DIR, ignore_errors=True)
    (STAGE_DIR / "include").mkdir(parents=True)
    (STAGE_DIR / "lib").mkdir(parents=True)

    headers_src = ROOT / "src" / "headers" / "uikit"
    if not headers_src.is_dir():
        raise RelError(f"headers not found: {headers_src}")
    shutil.copytree(headers_src, STAGE_DIR / "include" / "uikit")

    libdir = STAGE_DIR / "lib"

    if _IS_WIN:
        required = [
            ("mocida.dll", BUILD_SDK),
            ("mocida.lib", BUILD_SDK),
            ("SDL3.dll", BUILD_SDK / "SDL"),
            ("SDL3_image.dll", BUILD_SDK / "SDL_image"),
            ("SDL3_ttf.dll", BUILD_SDK / "SDL_ttf"),
            ("WebView2Loader.dll", ROOT / "vcpkg" / "installed" / "x64-windows" / "bin"),
        ]
        missing = []
        for name, d in required:
            src = d / name
            if src.exists():
                shutil.copy2(src, libdir / name)
            else:
                fail(f"missing build artifact: {src}")
                missing.append(name)
        if missing:
            raise RelError("SDK staging failed (missing artifacts).")
        pdb = BUILD_SDK / "mocida.pdb"
        if pdb.exists():
            shutil.copy2(pdb, libdir / "mocida.pdb")
    else:
        # The mocida shared lib is required. SDL is statically embedded into
        # it (the vendored SDL builds as a static lib), so the .dylib/.so is
        # self-contained — but if any SDL shared libs were produced we ship
        # them too, just in case the build was configured that way.
        pat = "libmocida.*dylib" if _IS_MAC else "libmocida.so*"
        moc = _find_first([(BUILD_SDK, pat)])
        if not moc:
            raise RelError(f"mocida shared lib not found in {BUILD_SDK} ({pat}).")
        shutil.copy2(moc, libdir / MOCIDA_LIB)
        ok(f"staged {MOCIDA_LIB}")

        sdl_glob = "*.dylib" if _IS_MAC else "*.so*"
        extra = 0
        for sub in ("SDL", "SDL_image", "SDL_ttf"):
            for p in (BUILD_SDK / sub).glob(sdl_glob):
                if p.is_file() and "mocida" not in p.name:
                    shutil.copy2(p, libdir / p.name)
                    extra += 1
        if extra:
            info(f"also bundled {extra} SDL shared lib(s)")
        else:
            info("SDL is statically embedded in the mocida lib (self-contained)")

    DIST_DIR.mkdir(parents=True, exist_ok=True)
    sdk_archive = DIST_DIR / SDK_ASSET
    if sdk_archive.exists():
        sdk_archive.unlink()
    info(f"packing {SDK_ASSET} ...")
    if SDK_ASSET.endswith(".zip"):
        with zipfile.ZipFile(sdk_archive, "w", zipfile.ZIP_DEFLATED) as z:
            for sub in ("include", "lib"):
                for p in (STAGE_DIR / sub).rglob("*"):
                    if p.is_file():
                        z.write(p, p.relative_to(STAGE_DIR))
    else:
        with tarfile.open(sdk_archive, "w:gz") as t:
            for sub in ("include", "lib"):
                t.add(STAGE_DIR / sub, arcname=sub)
    ok(f"created {sdk_archive} (~{max(1, sdk_archive.stat().st_size // (1024*1024))} MB)")
    return sdk_archive


def stage_installer():
    src = BUILD_INST / INSTALLER_BIN
    if not src.exists():
        raise RelError(f"installer artefact missing: {src}")
    DIST_DIR.mkdir(parents=True, exist_ok=True)
    dst = DIST_DIR / INSTALLER_ASSET
    shutil.copy2(src, dst)
    if not _IS_WIN:
        dst.chmod(0o755)
    ok(f"copied {dst} (~{max(1, dst.stat().st_size // (1024*1024))} MB)")
    return dst


def upload(tag, sdk, installer, notes=None):
    if not shutil.which("gh"):
        raise RelError("gh CLI not found. Install https://cli.github.com/ or upload manually.")
    if not tag:
        tag = "v" + datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    title = f"Mocida {tag}"
    if not notes:
        notes = (f"Mocida release {tag}. Per-OS GUI installer + SDK payload "
                 f"(headers + the mocida runtime). The installer downloads the "
                 f"matching SDK for the user's platform on first run.")
    exists = subprocess.run(["gh", "release", "view", tag],
                            capture_output=True).returncode == 0
    if exists:
        info(f"release {tag} exists - uploading {OS_TAG} assets (clobber).")
        subprocess.run(["gh", "release", "upload", tag, str(sdk), str(installer),
                        "--clobber"], check=True)
    else:
        info(f"creating GitHub release {tag} ...")
        subprocess.run(["gh", "release", "create", tag, str(sdk), str(installer),
                        "--title", title, "--notes", notes], check=True)
    ok("release assets published.")


def main():
    ap = argparse.ArgumentParser(prog="release.py", description="Mocida release packager.")
    ap.add_argument("--upload", action="store_true", help="also publish via gh release")
    ap.add_argument("--notes-file", default=None,
                    help="file whose contents become the gh release notes")
    ap.add_argument("tag", nargs="?", default=None, help="release tag (default v<timestamp>)")
    args = ap.parse_args()

    os.chdir(ROOT)
    if _IS_WIN:
        augment_path()
        ensure_msvc_env()

    print(_c("=== Mocida release ===", "35;1"))
    print(f"  Host:        {_SYS} {ARCH}")
    print(f"  SDK asset:   {SDK_ASSET}")
    print(f"  Installer:   {INSTALLER_ASSET}")

    try:
        step("Pass 1/2: shared SDK (mocida runtime)")
        configure_and_build(BUILD_SDK, [
            "-DMOCIDA_BUILD_SHARED=ON", "-DMOCIDA_BUILD_INSTALLER=OFF",
            "-DMOCIDA_BUILD_DEMO=OFF", "-DMOCIDA_BUILD_TESTS=OFF",
        ], target="mocida")

        step("Pass 2/2: static single-file installer")
        configure_and_build(BUILD_INST, [
            "-DMOCIDA_BUILD_SHARED=OFF", "-DMOCIDA_BUILD_INSTALLER=ON",
            "-DMOCIDA_BUILD_DEMO=OFF", "-DMOCIDA_BUILD_TESTS=OFF",
        ], target="mocida_installer")

        sdk = stage_sdk()
        installer = stage_installer()

        if args.upload:
            step("Uploading via gh")
            notes = None
            if args.notes_file and Path(args.notes_file).exists():
                notes = Path(args.notes_file).read_text(encoding="utf-8")
            upload(args.tag, sdk, installer, notes=notes)
    except RelError as e:
        fail(str(e))
        return 1

    print(_c("\n=== Release ready ===", "32;1"))
    print(f"  {DIST_DIR / SDK_ASSET}")
    print(f"  {DIST_DIR / INSTALLER_ASSET}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
