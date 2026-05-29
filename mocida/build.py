#!/usr/bin/env python3
"""
Mocida C toolkit build - cross-platform port of build.bat / build.sh.

Build dirs:  build/<platform>/<config>
  build/win32/debug   build/linux/release   build/darwin/relwithdebinfo  ...

Per-config isolation: switching Debug <-> Release does NOT trigger a full
SDL / SDL_image / SDL_ttf rebuild - each config keeps its own dependency
tree and Ninja's incremental engine handles the rest.

Usage:
  python build.py                      static lib + demo, Debug (default)
  python build.py --release            optimised build
  python build.py --relwithdebinfo     optimised + debug symbols
  python build.py --shared             build mocida as a shared lib
  python build.py --tests              also compile tests/test_*.c
  python build.py --no-demo            skip the demo exe
  python build.py --asan               instrument with ASan + LSan + UBSan
  python build.py --force / --clean    wipe build/<plat>/<config> first
  python build.py --reconfigure        wipe only CMakeCache.txt
  python build.py --verbose            stream raw compiler output
"""

import argparse
import os
import platform
import re
import shutil
import subprocess
import sys
import threading
import time
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent  # mocida/

_SYS = platform.system()
if _SYS == "Windows":
    PLATFORM = "win32"
elif _SYS == "Darwin":
    PLATFORM = "darwin"
else:
    PLATFORM = "linux"

WV2_VERSION = "1.0.2792.45"

# =======================================================================
#  Pretty terminal output: colors, spinner, progress bar
# =======================================================================
def _supports_color():
    if os.environ.get("NO_COLOR"):
        return False
    if not sys.stdout.isatty():
        return False
    if _SYS == "Windows":
        os.system("")  # enable ANSI VT processing on Win10+
    return True


_COLOR = _supports_color()


def _c(text, code):
    return f"\033[{code}m{text}\033[0m" if _COLOR else text


# Quiet by default: only warnings, errors, phase headers, the progress bar
# and the completion line are shown. --verbose adds the config block, info
# lines and the full compiler output.
_VERBOSE = False


def info(msg):
    if _VERBOSE:
        print(_c(f"  {msg}", "37"))
def good(msg):  print(_c(f"  + {msg}", "32"))
def warn(msg):  print(_c(f"  ! {msg}", "33"))
def err(msg):   print(_c(f"  x {msg}", "31"))
def step(msg):  print(_c(f"\n== {msg} ==", "36;1"))


class Spinner:
    """Lightweight spinner for indeterminate phases (e.g. CMake configure)."""
    FRAMES = "|/-\\"

    def __init__(self, label):
        self.label = label
        self._stop = threading.Event()
        self._t = None

    def __enter__(self):
        if _COLOR:
            self._t = threading.Thread(target=self._spin, daemon=True)
            self._t.start()
        else:
            print(f"  {self.label} ...")
        return self

    def _spin(self):
        i = 0
        while not self._stop.is_set():
            frame = self.FRAMES[i % len(self.FRAMES)]
            sys.stdout.write(_c(f"\r  {frame} {self.label} ...", "36"))
            sys.stdout.flush()
            i += 1
            time.sleep(0.1)

    def __exit__(self, *exc):
        self._stop.set()
        if self._t:
            self._t.join()
            sys.stdout.write("\r" + " " * (len(self.label) + 12) + "\r")
            sys.stdout.flush()


def _bar(frac, width=28):
    filled = int(round(frac * width))
    return "#" * filled + "-" * (width - filled)


# Ninja:  "[12/345] Building ..."     Make: "[ 42%] Building ..."
_NINJA_RE = re.compile(r"^\[(\d+)/(\d+)\]\s*(.*)")
_MAKE_RE = re.compile(r"^\[\s*(\d+)%\]\s*(.*)")


def run_build_with_progress(cmd, cwd, verbose=False):
    """Run `cmake --build ...`. In verbose mode the raw output is streamed.
    Otherwise only a live progress bar (parsed from the generator's output)
    is shown and the full log is buffered, then dumped *only if the build
    fails* - so a normal build stays quiet. Returns the process exit code."""
    proc = subprocess.Popen(
        cmd, cwd=str(cwd),
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, bufsize=1, encoding="utf-8", errors="replace",
    )
    last_len = 0
    drew_bar = False
    buffered = []
    try:
        for line in proc.stdout:
            line = line.rstrip("\n")
            if verbose:
                print(line)
                continue

            buffered.append(line)  # kept hidden unless the build fails

            frac = None
            tail = ""
            m = _NINJA_RE.match(line)
            if m:
                cur, tot = int(m.group(1)), int(m.group(2))
                frac = cur / tot if tot else 0.0
                tail = f"({cur}/{tot}) {m.group(3)}"
            else:
                m = _MAKE_RE.match(line)
                if m:
                    frac = int(m.group(1)) / 100.0
                    tail = f"({m.group(1)}%) {m.group(2)}"

            if frac is not None and _COLOR:
                pct = int(frac * 100)
                tail = tail[:60]
                txt = f"\r  [{_bar(frac)}] {pct:3d}%  {tail}"
                pad = max(0, last_len - len(txt))
                sys.stdout.write(_c(txt, "36") + " " * pad)
                sys.stdout.flush()
                last_len = len(txt)
                drew_bar = True
    finally:
        proc.wait()
    if drew_bar:
        sys.stdout.write("\r" + " " * last_len + "\r")
        sys.stdout.flush()
    # On failure, surface the captured log so the error is actually visible.
    if not verbose and proc.returncode != 0 and buffered:
        err("build output:")
        print("\n".join(buffered))
    return proc.returncode


# =======================================================================
#  Environment helpers
# =======================================================================
def augment_path_windows():
    extra = [
        os.path.expandvars(r"%LocalAppData%\Microsoft\WinGet\Links"),
        r"C:\Program Files\LLVM\bin",
        os.path.expandvars(r"%LocalAppData%\Programs\LLVM\bin"),
        r"C:\Program Files\CMake\bin",
        r"C:\Program Files (x86)\GnuWin32\bin",
        os.path.expandvars(r"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"),
    ]
    cur = os.environ.get("PATH", "")
    parts = cur.split(os.pathsep)
    for d in extra:
        if d and d not in parts and os.path.isdir(d):
            cur = d + os.pathsep + cur
    os.environ["PATH"] = cur


def ensure_msvc_env():
    """On Windows, import the MSVC environment (INCLUDE/LIB/PATH) that clang
    needs to find the Windows SDK + CRT. No-op if already initialised."""
    if _SYS != "Windows" or os.environ.get("VCINSTALLDIR"):
        return
    vswhere = Path(os.environ.get("ProgramFiles(x86)", "")) \
        / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.exists():
        return
    try:
        vspath = subprocess.run(
            [str(vswhere), "-latest", "-prerelease", "-property", "installationPath"],
            capture_output=True, text=True).stdout.strip()
    except Exception:
        return
    if not vspath:
        return
    vcvars = Path(vspath) / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat"
    if not vcvars.exists():
        return
    info(f"Initialising MSVC environment from {vspath} ...")
    out = subprocess.run(
        ["cmd", "/c", f'call "{vcvars}" x64 >nul && set'],
        capture_output=True, text=True).stdout
    for line in out.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            os.environ[k] = v


def fetch_webview2(mocida_root):
    """Windows-only: download the WebView2 NuGet (static loader) once.
    Returns the NuGet dir if the static lib is present, else None."""
    if _SYS != "Windows":
        return None
    wv2_dir = mocida_root / ".webview2"
    static_lib = wv2_dir / "build" / "native" / "x64" / "WebView2LoaderStatic.lib"
    if static_lib.exists():
        return wv2_dir
    info(f"Fetching Microsoft.Web.WebView2 v{WV2_VERSION} (one-time) ...")
    url = f"https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/{WV2_VERSION}"
    wv2_dir.mkdir(parents=True, exist_ok=True)
    zip_path = wv2_dir / "_webview2.zip"
    try:
        with Spinner("downloading WebView2"):
            urllib.request.urlretrieve(url, zip_path)
        with zipfile.ZipFile(zip_path) as z:
            z.extractall(wv2_dir)
    except Exception as e:
        warn(f"WebView2 fetch failed ({e}) - static builds will ship WebView2Loader.dll.")
        return None
    finally:
        if zip_path.exists():
            zip_path.unlink()
    return wv2_dir if static_lib.exists() else None


# =======================================================================
#  Build
# =======================================================================
def parse_args(argv):
    p = argparse.ArgumentParser(prog="build.py", add_help=True,
                                description="Mocida C toolkit build.")
    cfg = p.add_mutually_exclusive_group()
    cfg.add_argument("--debug", action="store_const", dest="config", const="Debug")
    cfg.add_argument("--release", action="store_const", dest="config", const="Release")
    cfg.add_argument("--relwithdebinfo", action="store_const", dest="config", const="RelWithDebInfo")
    p.set_defaults(config="Debug")
    p.add_argument("--shared", dest="shared", action="store_true")
    p.add_argument("--static", dest="shared", action="store_false")
    p.set_defaults(shared=False)
    p.add_argument("--tests", action="store_true")
    p.add_argument("--no-tests", dest="tests", action="store_false")
    p.add_argument("--no-demo", dest="demo", action="store_false")
    p.set_defaults(demo=True)
    p.add_argument("--asan", action="store_true")
    p.add_argument("--installer", action="store_true",
                   help="build the static GUI installer (mocida_installer) "
                        "instead of lib+demo (Windows only)")
    p.add_argument("--force", "--clean", dest="force", action="store_true")
    p.add_argument("--reconfigure", action="store_true")
    p.add_argument("--verbose", action="store_true")
    return p.parse_args(argv)


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    global _VERBOSE
    _VERBOSE = args.verbose
    os.chdir(ROOT)

    if _SYS == "Windows":
        augment_path_windows()

    # The installer is a static single-exe target; it implies static + no demo.
    if args.installer:
        if _SYS != "Windows":
            err("--installer is Windows-only (WebView2 static loader + MSVC).")
            return 1
        args.shared = False
        args.demo = False

    config = args.config
    config_lc = config.lower()
    build_dir = ROOT / "build" / PLATFORM / config_lc
    sanitize = "address,undefined" if args.asan else ""

    print(_c("Mocida Build" + (" (installer)" if args.installer else ""), "35;1"))

    # --- force / reconfigure -------------------------------------------
    if args.force and build_dir.exists():
        warn(f"Force-rebuild: wiping {build_dir.relative_to(ROOT)} ...")
        shutil.rmtree(build_dir, ignore_errors=True)
    if args.reconfigure:
        cache = build_dir / "CMakeCache.txt"
        if cache.exists():
            warn("Reconfigure: removing CMakeCache.txt + CMakeFiles/ ...")
            cache.unlink()
            shutil.rmtree(build_dir / "CMakeFiles", ignore_errors=True)
    build_dir.mkdir(parents=True, exist_ok=True)

    needs_configure = not (build_dir / "CMakeCache.txt").exists()

    # --- generator ------------------------------------------------------
    if shutil.which("ninja"):
        generator = "Ninja"
    elif shutil.which("make"):
        generator = "Unix Makefiles"
    else:
        err("neither 'ninja' nor 'make' found in PATH. Run setup.py.")
        return 1

    configure_args = []
    if needs_configure:
        if _SYS == "Windows":
            ensure_msvc_env()

        # compiler: prefer clang to match across platforms
        cc = shutil.which("clang") or shutil.which("cc")
        cxx = shutil.which("clang++") or shutil.which("c++")
        if not cc or not cxx:
            err("clang (or cc/c++) not found. Install LLVM or run setup.py.")
            return 1
        os.environ["CC"] = cc
        os.environ["CXX"] = cxx

        # vcpkg toolchain (libcurl) — lives under mocida/
        vcpkg_tc = ROOT / "vcpkg" / "scripts" / "buildsystems" / "vcpkg.cmake"
        if vcpkg_tc.exists():
            configure_args.append(f"-DCMAKE_TOOLCHAIN_FILE={vcpkg_tc}")
        else:
            warn(f"vcpkg toolchain not found at {vcpkg_tc} - find_package(CURL) may fail.")

        # WebView2 static loader (Windows)
        wv2_dir = fetch_webview2(ROOT)
        if wv2_dir:
            configure_args.append(f"-DMOCIDA_WEBVIEW2_NUGET_DIR={wv2_dir}")

        # optional compiler cache
        launcher = shutil.which("sccache") or shutil.which("ccache")
        if launcher:
            configure_args += [f"-DCMAKE_C_COMPILER_LAUNCHER={Path(launcher).stem}",
                               f"-DCMAKE_CXX_COMPILER_LAUNCHER={Path(launcher).stem}"]

    if args.verbose:
        print(f"Platform           : {PLATFORM}")
        print(f"Build dir          : {build_dir.relative_to(ROOT)}")
        print(f"Generator          : {generator}")
        print(f"CMAKE_BUILD_TYPE   : {config}")
        print(f"MOCIDA_BUILD_SHARED: {'ON' if args.shared else 'OFF'}")
        print(f"MOCIDA_BUILD_TESTS : {'ON' if args.tests else 'OFF'}")
        print(f"MOCIDA_BUILD_DEMO  : {'ON' if args.demo else 'OFF'}")
        print(f"MOCIDA_SANITIZE    : {sanitize or '(off)'}")
        print(f"Configure          : {'YES (first build for this config)' if needs_configure else 'SKIPPED (cache exists)'}")
    else:
        print(_c(f"  {PLATFORM}/{config_lc}  ({generator})", "90"))

    # --- configure ------------------------------------------------------
    if needs_configure:
        cmake_cfg = [
            "cmake", "-G", generator,
            "-S", str(ROOT), "-B", str(build_dir),
            *configure_args,
            f"-DCMAKE_BUILD_TYPE={config}",
            f"-DCMAKE_C_COMPILER={os.environ['CC']}",
            f"-DCMAKE_CXX_COMPILER={os.environ['CXX']}",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            f"-DMOCIDA_BUILD_SHARED={'ON' if args.shared else 'OFF'}",
            f"-DMOCIDA_BUILD_TESTS={'ON' if args.tests else 'OFF'}",
            f"-DMOCIDA_BUILD_DEMO={'ON' if args.demo else 'OFF'}",
            f"-DMOCIDA_SANITIZE={sanitize}",
            "-Wno-dev", "--log-level=NOTICE",
        ]
        if args.installer:
            cmake_cfg.insert(-2, "-DMOCIDA_BUILD_INSTALLER=ON")
        step("Configuring CMake")
        with Spinner("running CMake configure"):
            r = subprocess.run(cmake_cfg, cwd=str(ROOT),
                               capture_output=not args.verbose, text=True)
        if r.returncode != 0:
            if not args.verbose and r.stdout:
                print(r.stdout)
            if not args.verbose and r.stderr:
                print(r.stderr)
            err("CMake configuration failed!")
            return r.returncode
        good("Configured.")

    # --- targets --------------------------------------------------------
    if args.installer:
        targets = ["mocida_installer"]
    else:
        targets = ["mocida"]
        if args.demo:
            targets.append("demo")
        if args.tests:
            for t in sorted((ROOT / "tests").glob("test_*.c")):
                targets.append(t.stem)

    jobs = os.environ.get("MOCIDA_BUILD_JOBS")
    if not jobs:
        jobs = max(2, (os.cpu_count() or 4) - 4)

    step(f"Building: {', '.join(targets)}  (jobs: {jobs})")
    build_cmd = ["cmake", "--build", str(build_dir),
                 "--parallel", str(jobs), "--config", config,
                 "--target", *targets]
    t0 = time.perf_counter()
    code = run_build_with_progress(build_cmd, ROOT, verbose=args.verbose)
    dt = time.perf_counter() - t0
    if code != 0:
        err("Build failed!")
        return code

    good(f"Build completed in {dt:.1f}s.")
    print(f"  Artefacts: {build_dir.relative_to(ROOT)}/")
    if args.installer:
        print(f"  Installer: {(build_dir / 'mocida_installer.exe').relative_to(ROOT)}")
        print("  Static single-exe; downloads the SDK zip from the GitHub "
              "'latest' release at install time (build one via release.py).")
    if args.asan:
        print("\n  ASan/LSan/UBSan on. Run a binary directly to see the leak report:")
        print(f"      {build_dir.relative_to(ROOT)}/demo")
    return 0


if __name__ == "__main__":
    sys.exit(main())
