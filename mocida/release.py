#!/usr/bin/env python3
"""
Mocida SDK + installer release packager (port of release/release.bat).

Builds two Release artefacts:
  - mocida-sdk-windows-x64.zip        headers + mocida.dll/.lib + SDL DLLs
  - mocida-installer-windows-x64.exe  single-file static GUI installer

The installer downloads the SDK zip from the project's GitHub "latest"
release at install time, so the SDK asset name must stay in sync with the
MOCIDA_SDK_ASSET_NAME macro in installer/installer.c.

Two CMake configures are needed: the SDK wants SDL as shared libs (apps
link the shipped DLLs); the installer must be fully static (single .exe).

Usage:
  python release.py                  package both assets into release/dist/
  python release.py --upload [tag]   also publish via `gh release`
                                     (tag defaults to v<UTC timestamp>)

Windows-only (installer + WebView2 static loader + MSVC).
"""

import argparse
import os
import platform
import shutil
import subprocess
import sys
import zipfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent  # mocida/
_SYS = platform.system()

SDK_ASSET = "mocida-sdk-windows-x64.zip"
INSTALLER_ASSET = "mocida-installer-windows-x64.exe"

STAGE_DIR = ROOT / "release" / "stage"
DIST_DIR = ROOT / "release" / "dist"
BUILD_SDK = ROOT / "build" / "win32-release-sdk"
BUILD_INST = ROOT / "build" / "win32-release-installer"

_COLOR = (not os.environ.get("NO_COLOR")) and sys.stdout.isatty()
if _COLOR and _SYS == "Windows":
    os.system("")


def _c(t, code):
    return f"\033[{code}m{t}\033[0m" if _COLOR else t


def step(m): print(_c(f"\n== {m} ==", "36;1"))
def info(m): print(_c(f"  {m}", "37"))
def ok(m):   print(_c(f"  + {m}", "32"))
def fail(m): print(_c(f"  x {m}", "31"))


class RelError(Exception):
    pass


# --- MSVC / PATH (Windows) ---------------------------------------------
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
    generator = "Ninja" if shutil.which("ninja") else "Unix Makefiles"
    cc = shutil.which("clang")
    cxx = shutil.which("clang++")
    if not cc:
        raise RelError("clang.exe not found on PATH. Install LLVM or run setup.py.")
    toolchain = []
    vcpkg_tc = ROOT / "vcpkg" / "scripts" / "buildsystems" / "vcpkg.cmake"
    if vcpkg_tc.exists():
        toolchain.append(f"-DCMAKE_TOOLCHAIN_FILE={vcpkg_tc}")
    wv2_static = ROOT / ".webview2" / "build" / "native" / "x64" / "WebView2LoaderStatic.lib"
    if wv2_static.exists():
        toolchain.append(f"-DMOCIDA_WEBVIEW2_NUGET_DIR={ROOT / '.webview2'}")
    build_dir.mkdir(parents=True, exist_ok=True)
    cfg = ["cmake", "-G", generator, *toolchain,
           "-DCMAKE_BUILD_TYPE=Release",
           f"-DCMAKE_C_COMPILER={cc}", f"-DCMAKE_CXX_COMPILER={cxx}",
           *extra_defs, "-Wno-dev", "--log-level=NOTICE", str(ROOT)]
    if subprocess.run(cfg, cwd=str(build_dir)).returncode != 0:
        raise RelError(f"configure failed in {build_dir}")
    if subprocess.run(["cmake", "--build", ".", "--parallel", "--config", "Release",
                       "--target", target], cwd=str(build_dir)).returncode != 0:
        raise RelError(f"build of '{target}' failed in {build_dir}")


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

    artefacts = [
        ("mocida.dll", BUILD_SDK),
        ("mocida.lib", BUILD_SDK),
        ("SDL3.dll", BUILD_SDK / "SDL"),
        ("SDL3_image.dll", BUILD_SDK / "SDL_image"),
        ("SDL3_ttf.dll", BUILD_SDK / "SDL_ttf"),
        ("WebView2Loader.dll", ROOT / "vcpkg" / "installed" / "x64-windows" / "bin"),
    ]
    missing = []
    for name, d in artefacts:
        src = d / name
        if src.exists():
            shutil.copy2(src, STAGE_DIR / "lib" / name)
        else:
            fail(f"missing build artifact: {src}")
            missing.append(name)
    if missing:
        raise RelError("SDK staging failed (missing artifacts).")
    pdb = BUILD_SDK / "mocida.pdb"
    if pdb.exists():
        shutil.copy2(pdb, STAGE_DIR / "lib" / "mocida.pdb")

    DIST_DIR.mkdir(parents=True, exist_ok=True)
    sdk_zip = DIST_DIR / SDK_ASSET
    if sdk_zip.exists():
        sdk_zip.unlink()
    info(f"zipping {SDK_ASSET} ...")
    with zipfile.ZipFile(sdk_zip, "w", zipfile.ZIP_DEFLATED) as z:
        for sub in ("include", "lib"):
            for p in (STAGE_DIR / sub).rglob("*"):
                if p.is_file():
                    z.write(p, p.relative_to(STAGE_DIR))
    ok(f"created {sdk_zip} (~{sdk_zip.stat().st_size // (1024*1024)} MB)")
    return sdk_zip


def stage_installer():
    src = BUILD_INST / "mocida_installer.exe"
    if not src.exists():
        raise RelError(f"installer artefact missing: {src}")
    dst = DIST_DIR / INSTALLER_ASSET
    shutil.copy2(src, dst)
    ok(f"copied {dst} (~{dst.stat().st_size // (1024*1024)} MB)")
    return dst


def upload(tag, sdk_zip, installer, notes=None):
    if not shutil.which("gh"):
        raise RelError("gh CLI not found. Install https://cli.github.com/ or upload manually.")
    if not tag:
        tag = "v" + datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    title = f"Mocida {tag}"
    if not notes:
        notes = (f"Mocida release {tag}. {INSTALLER_ASSET} is the single-file GUI "
                 f"installer (no DLLs); {SDK_ASSET} is the SDK payload (headers + "
                 f"mocida.dll + mocida.lib + SDL DLLs) the installer downloads on first run.")
    exists = subprocess.run(["gh", "release", "view", tag],
                            capture_output=True).returncode == 0
    if exists:
        info(f"release {tag} exists - uploading assets only.")
        subprocess.run(["gh", "release", "upload", tag, str(sdk_zip), str(installer),
                        "--clobber"], check=True)
    else:
        info(f"creating GitHub release {tag} ...")
        subprocess.run(["gh", "release", "create", tag, str(sdk_zip), str(installer),
                        "--title", title, "--notes", notes], check=True)
    ok("release published.")


def main():
    ap = argparse.ArgumentParser(prog="release.py", description="Mocida release packager.")
    ap.add_argument("--upload", action="store_true", help="also publish via gh release")
    ap.add_argument("--notes-file", default=None,
                    help="file whose contents become the gh release notes")
    ap.add_argument("tag", nargs="?", default=None, help="release tag (default v<timestamp>)")
    args = ap.parse_args()

    if _SYS != "Windows":
        fail("release.py is Windows-only (installer + WebView2 static + MSVC).")
        return 1

    os.chdir(ROOT)
    augment_path()
    ensure_msvc_env()

    print(_c("=== Mocida release ===", "35;1"))
    print(f"  SDK asset:   {SDK_ASSET}")
    print(f"  Installer:   {INSTALLER_ASSET}")

    try:
        step("Pass 1/2: shared SDK (mocida.dll + SDL DLLs)")
        configure_and_build(BUILD_SDK, [
            "-DMOCIDA_BUILD_SHARED=ON", "-DMOCIDA_BUILD_INSTALLER=OFF",
            "-DMOCIDA_BUILD_DEMO=OFF", "-DMOCIDA_BUILD_TESTS=OFF",
        ], target="mocida")

        step("Pass 2/2: static installer (single .exe)")
        configure_and_build(BUILD_INST, [
            "-DMOCIDA_BUILD_SHARED=OFF", "-DMOCIDA_BUILD_INSTALLER=ON",
            "-DMOCIDA_BUILD_DEMO=OFF", "-DMOCIDA_BUILD_TESTS=OFF",
        ], target="mocida_installer")

        sdk_zip = stage_sdk()
        installer = stage_installer()

        if args.upload:
            step("Uploading via gh")
            notes = None
            if args.notes_file and Path(args.notes_file).exists():
                notes = Path(args.notes_file).read_text(encoding="utf-8")
            upload(args.tag, sdk_zip, installer, notes=notes)
    except RelError as e:
        fail(str(e))
        return 1

    print(_c("\n=== Release ready ===", "32;1"))
    print(f"  {DIST_DIR / SDK_ASSET}")
    print(f"  {DIST_DIR / INSTALLER_ASSET}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
