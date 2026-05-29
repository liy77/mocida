#!/usr/bin/env python3
"""
Mocida - documentation toolchain (cross-platform port of docs.bat).

Builds the Doxygen site, mirrors assets/ into the generated HTML, and can
serve it locally with Python's built-in http.server (no PowerShell / Node).

Usage:
  python docs.py                       build docs only
  python docs.py --open                build + open index.html
  python docs.py --serve [--port N]    build + start local HTTP server
  python docs.py --serve --no-build    just serve what is already built
  python docs.py --serve --no-open     start server but don't open browser

Defaults: port 8080, rebuilds, opens the browser on --open/--serve.
"""

import argparse
import functools
import http.server
import os
import platform
import shutil
import subprocess
import sys
import webbrowser
from pathlib import Path

ROOT = Path(__file__).resolve().parent  # mocida/
_SYS = platform.system()
_COLOR = (not os.environ.get("NO_COLOR")) and sys.stdout.isatty()
if _COLOR and _SYS == "Windows":
    os.system("")


def _c(t, code):
    return f"\033[{code}m{t}\033[0m" if _COLOR else t


def info(m): print(_c(f"[docs] {m}", "36"))
def fail(m): print(_c(f"[docs] {m}", "31"))


HTML_ROOT = ROOT / "docs" / "generated" / "html"
INDEX = HTML_ROOT / "index.html"


def build():
    if not shutil.which("doxygen"):
        fail("doxygen not found in PATH.")
        info("  install: winget install --id DimitriVanHeesch.Doxygen")
        info("  or: https://www.doxygen.nl/download.html")
        return 1
    if not (ROOT / "Doxyfile").exists():
        fail(f"Doxyfile not found at {ROOT / 'Doxyfile'}")
        return 1
    info("running doxygen ...")
    if subprocess.run(["doxygen", "Doxyfile"], cwd=str(ROOT)).returncode != 0:
        fail("doxygen reported errors.")
        return 1
    # Doxygen does not auto-copy the project's assets/ that README/mainpage
    # reference via <img src="assets/...">. Mirror them in.
    assets = ROOT / "assets"
    if assets.is_dir():
        info("copying assets/ into the generated site ...")
        dst = HTML_ROOT / "assets"
        if dst.exists():
            shutil.rmtree(dst, ignore_errors=True)
        shutil.copytree(assets, dst)
    info(f"docs ready: {INDEX}")
    return 0


def serve(port, do_open):
    if not INDEX.exists():
        fail(f"{INDEX} not found - run without --no-build first.")
        return 1
    handler = functools.partial(http.server.SimpleHTTPRequestHandler,
                                directory=str(HTML_ROOT))
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", port), handler)
    url = f"http://localhost:{port}/"
    info(f"serving {HTML_ROOT}")
    info(f"open {url}  (Ctrl+C to stop)")
    if do_open:
        webbrowser.open(url)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        info("stopped.")
    finally:
        httpd.server_close()
    return 0


def main():
    ap = argparse.ArgumentParser(prog="docs.py", description="Mocida docs.")
    ap.add_argument("--open", dest="open", action="store_true")
    ap.add_argument("--serve", action="store_true")
    ap.add_argument("--no-build", dest="build", action="store_false")
    ap.add_argument("--no-open", dest="open", action="store_false")
    ap.add_argument("--port", type=int, default=8080)
    ap.set_defaults(build=True, open=None)
    args = ap.parse_args()

    # --serve implies opening the browser unless --no-open was given.
    do_open = args.open if args.open is not None else args.serve

    if args.build:
        rc = build()
        if rc != 0:
            return rc

    if args.serve:
        return serve(args.port, do_open)

    if do_open:
        if not INDEX.exists():
            fail(f"{INDEX} not found.")
            return 1
        webbrowser.open(INDEX.as_uri())
    return 0


if __name__ == "__main__":
    sys.exit(main())
