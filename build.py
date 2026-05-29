#!/usr/bin/env python3
"""
Mocida monorepo build orchestrator (Turbo-style task runner).

One command to build the whole monorepo. Models the two packages as a small
task graph with an explicit dependency edge, like a decent JS monorepo runner
(turbo / nx):

        setup ---.
                 v
                 c  -->  rust        (rust links the C lib, so c runs first)

Tasks:
  setup   Bootstrap deps into mocida/ (tools, vcpkg+curl, SDL*, mimalloc).
          Delegates to mocida/setup.py (cross-platform).
  c       Build the C toolkit (mocida/) via build.bat (Windows) or build.sh.
  rust    Build the Rust workspace (mocida-rs/) via cargo, wiring the
          MOCIDA_* FFI env vars to the C build outputs.
  all     c -> rust   (default)
  clean   Wipe the C build dir for this config + cargo clean.

Incremental caching is delegated to the real engines (Ninja for C, cargo for
Rust) - same model Turbo uses: the orchestrator owns the graph, the tools own
the per-file cache. A C task whose lib timestamp did not move is CACHED.

Usage:
  python build.py                       # debug build of everything
  python build.py all --config release  # optimised build of C + Rust
  python build.py c --force             # clean rebuild of just the C lib
  python build.py rust                  # rebuild Rust against the current C lib
  python build.py setup                 # first-time machine bootstrap (Windows)
  python build.py clean
"""

import argparse
import os
import platform
import shutil
import subprocess
import sys
import time
from pathlib import Path

# --- locations ----------------------------------------------------------
ROOT = Path(__file__).resolve().parent
CDIR = ROOT / "mocida"        # C toolkit package
RSDIR = ROOT / "mocida-rs"    # Rust workspace package

# --- platform detection -------------------------------------------------
_SYS = platform.system()
if _SYS == "Windows":
    PLATFORM, LIBFILE = "win32", "mocida.lib"
elif _SYS == "Darwin":
    PLATFORM, LIBFILE = "darwin", "libmocida.a"
else:
    PLATFORM, LIBFILE = "linux", "libmocida.a"

# --- task-graph dependency edges (who must run before whom) -------------
DEPENDS = {
    "setup": [],
    "c": [],
    "rust": ["c"],
    "clean": [],
    "all": ["c", "rust"],
}

# --- colored output -----------------------------------------------------
def _supports_color():
    if os.environ.get("NO_COLOR"):
        return False
    if not sys.stdout.isatty():
        return False
    if _SYS == "Windows":
        os.system("")  # enable ANSI VT processing on Windows 10+ consoles
    return True

_COLOR = _supports_color()


def _c(text, code):
    return f"\033[{code}m{text}\033[0m" if _COLOR else text


# Verbosity: by default the orchestrator only shows phase headers, the C
# progress bar, errors and the summary. --verbose adds command echoes, the
# FFI wiring notes, full compiler warnings and cargo's per-crate log.
VERBOSE = False


def head(msg):
    print(_c(f"\n| {msg}", "36"))            # cyan


def run_line(msg):
    if VERBOSE:
        print(_c(f"  > {msg}", "37"))        # white


def note(msg):
    if VERBOSE:
        print(_c(f"    {msg}", "90"))        # bright black


# --- results bookkeeping ------------------------------------------------
RESULTS = []  # list of dicts: task, status, ms, detail


def add_result(task, status, ms, detail=""):
    RESULTS.append({"task": task, "status": status, "ms": int(ms), "detail": detail})


class TaskError(Exception):
    pass


# --- helpers ------------------------------------------------------------
def c_build_dir(cfg):
    return CDIR / "build" / PLATFORM / cfg


def cflag(cfg):
    return {"debug": "--debug",
            "release": "--release",
            "relwithdebinfo": "--relwithdebinfo"}[cfg]


def run_c_script(script_args):
    """Run mocida/build.py (the C package build) from inside mocida/.
    It creates build/<platform>/<config> relative to its own CWD, so the
    cwd MUST be mocida/."""
    cmd = [sys.executable, "build.py"] + script_args
    return subprocess.run(cmd, cwd=str(CDIR)).returncode


def mtime(path):
    return path.stat().st_mtime if path.exists() else None


# ========================================================================
#  TASK: setup
# ========================================================================
def task_setup(opts):
    head("setup - bootstrap dependencies into mocida/")
    t0 = time.perf_counter()
    script = CDIR / "setup.py"
    if not script.exists():
        raise TaskError(f"setup.py not found at {script}")
    run_line("python mocida/setup.py")
    code = subprocess.run([sys.executable, "setup.py"], cwd=str(CDIR)).returncode
    ms = (time.perf_counter() - t0) * 1000
    if code != 0:
        add_result("setup", "FAIL", ms, f"exit {code}")
        raise TaskError(f"setup failed (exit {code})")
    add_result("setup", "OK", ms)


# ========================================================================
#  TASK: c  (build the C toolkit via build.bat / build.sh)
# ========================================================================
def task_c(opts):
    head(f"c - build C toolkit (mocida/) [{PLATFORM}/{opts.config}]")
    t0 = time.perf_counter()
    script = CDIR / "build.py"
    if not script.exists():
        raise TaskError(f"build.py not found at {script}")

    lib = c_build_dir(opts.config) / LIBFILE
    prev = mtime(lib)

    flags = [cflag(opts.config)]
    if opts.force:
        flags.append("--force")
    if opts.tests:
        flags.append("--tests")
    if opts.no_demo:
        flags.append("--no-demo")
    if opts.verbose:
        flags.append("--verbose")

    run_line(f"python mocida/build.py {' '.join(flags)}")
    code = run_c_script(flags)
    ms = (time.perf_counter() - t0) * 1000

    if code != 0:
        add_result("c", "FAIL", ms, f"exit {code}")
        raise TaskError(f"C build failed (exit {code})")
    if not lib.exists():
        add_result("c", "FAIL", ms, f"{LIBFILE} missing")
        raise TaskError(f"C build produced no {LIBFILE} at {lib}")

    cached = prev is not None and not opts.force and mtime(lib) == prev
    add_result("c", "CACHED" if cached else "OK", ms, str(lib))


# ========================================================================
#  TASK: rust  (cargo build, wired to the C outputs via MOCIDA_* env vars)
# ========================================================================
def task_rust(opts):
    head(f"rust - build Rust workspace (mocida-rs/) [{PLATFORM}/{opts.config}]")
    t0 = time.perf_counter()

    libdir = c_build_dir(opts.config)
    lib = libdir / LIBFILE
    incdir = CDIR / "src" / "headers"
    sdlinc = CDIR / "SDL" / "include"

    if not lib.exists():
        raise TaskError(f"C lib not found at {lib} - run "
                        f"'python build.py c --config {opts.config}' first.")
    if not incdir.exists():
        raise TaskError(f"C headers not found at {incdir}")

    # FFI wiring consumed by mocida-rs/mocida-sys/build.rs. MOCIDA_LIB_NAME
    # stays the base name 'mocida' on every platform - build.rs adds the
    # lib prefix / .lib|.a suffix per linker convention.
    env = os.environ.copy()
    env["MOCIDA_INCLUDE_DIR"] = str(incdir.resolve())
    env["MOCIDA_LIB_DIR"] = str(libdir.resolve())
    env["MOCIDA_LIB_NAME"] = "mocida"
    env["MOCIDA_STATIC"] = "1"
    if sdlinc.exists():
        env["SDL3_INCLUDE_DIR"] = str(sdlinc.resolve())

    note(f"MOCIDA_INCLUDE_DIR = {env['MOCIDA_INCLUDE_DIR']}")
    note(f"MOCIDA_LIB_DIR     = {env['MOCIDA_LIB_DIR']}")
    note(f"MOCIDA_STATIC      = 1  (lib: {LIBFILE})")

    if opts.force:
        run_line("cargo clean")
        subprocess.run(["cargo", "clean"], cwd=str(RSDIR), env=env)

    cargo = ["cargo", "build"]
    if opts.config != "debug":
        cargo.append("--release")
    if not opts.verbose:
        cargo.append("--quiet")   # only show errors; --verbose restores the log
    run_line(" ".join(cargo))
    code = subprocess.run(cargo, cwd=str(RSDIR), env=env).returncode
    ms = (time.perf_counter() - t0) * 1000

    if code != 0:
        add_result("rust", "FAIL", ms, f"exit {code}")
        raise TaskError(f"Rust build failed (exit {code})")
    add_result("rust", "OK", ms)


# ========================================================================
#  TASK: clean
# ========================================================================
def task_clean(opts):
    head(f"clean - wipe C build [{PLATFORM}/{opts.config}] + cargo clean")
    t0 = time.perf_counter()
    cbuild = c_build_dir(opts.config)
    if cbuild.exists():
        run_line(f"rm -rf {cbuild}")
        shutil.rmtree(cbuild, ignore_errors=True)
    if (RSDIR / "Cargo.toml").exists():
        run_line("cargo clean")
        subprocess.run(["cargo", "clean"], cwd=str(RSDIR))
    add_result("clean", "OK", (time.perf_counter() - t0) * 1000)


RUNNERS = {
    "setup": task_setup,
    "c": task_c,
    "rust": task_rust,
    "clean": task_clean,
}


# --- dependency-graph resolution (topological order) --------------------
def resolve_plan(task):
    plan = []

    def add(name):
        for dep in DEPENDS[name]:
            add(dep)
        if name != "all" and name not in plan:
            plan.append(name)

    add(task)
    return plan


# --- summary ------------------------------------------------------------
def print_summary(total_ms):
    print(_c("\n-- Summary --------------", "36"))
    color_for = {"OK": "32", "CACHED": "90", "SKIP": "33", "FAIL": "31"}
    for r in RESULTS:
        line = f"  {r['task']:<6} {r['status']:<7} {r['ms']:>7} ms"
        if r["detail"]:
            line += f"   {r['detail']}"
        print(_c(line, color_for.get(r["status"], "37")))
    print(f"  {'TOTAL':<6} {'':<7} {int(total_ms):>7} ms")


def main():
    ap = argparse.ArgumentParser(
        prog="build.py",
        description="Mocida monorepo build orchestrator (Turbo-style).")
    ap.add_argument("task", nargs="?", default="all",
                    choices=["all", "c", "rust", "setup", "clean"],
                    help="task to run (default: all)")
    ap.add_argument("--config", default="debug",
                    choices=["debug", "release", "relwithdebinfo"],
                    help="build configuration (default: debug)")
    ap.add_argument("--force", action="store_true",
                    help="force a full rebuild (C: --force; Rust: cargo clean)")
    ap.add_argument("--tests", action="store_true",
                    help="C build also compiles tests/test_*.c")
    ap.add_argument("--no-demo", dest="no_demo", action="store_true",
                    help="C build skips the demo executable")
    ap.add_argument("--verbose", "-v", action="store_true",
                    help="show full build output (compiler warnings, cargo log, "
                         "command echoes); default shows only progress + errors")
    opts = ap.parse_args()

    global VERBOSE
    VERBOSE = opts.verbose

    plan = resolve_plan(opts.task)
    print(_c(f"Mocida monorepo  *  task={opts.task}  config={opts.config}  "
             f"platform={PLATFORM}  plan=[{' -> '.join(plan)}]", "35"))

    total0 = time.perf_counter()
    failed = False
    try:
        for step in plan:
            RUNNERS[step](opts)
    except TaskError as e:
        print(_c(f"\nX {e}", "31"))
        failed = True
    total_ms = (time.perf_counter() - total0) * 1000

    print_summary(total_ms)
    sys.exit(1 if (failed or any(r["status"] == "FAIL" for r in RESULTS)) else 0)


if __name__ == "__main__":
    main()
