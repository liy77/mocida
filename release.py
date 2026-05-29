#!/usr/bin/env python3
"""
Mocida monorepo release orchestrator.

Pick a package to release, record a changelog entry, and (optionally)
publish. Works two ways:

  * Interactive — run with no package and answer the prompts:
        python release.py
  * Direct CLI — script everything:
        python release.py mocida --tag v1.2.0 --notes "Fix X, add Y" --upload

Packages:
  * mocida      — the C toolkit. Builds the SDK zip + single-file installer
                  via mocida/release.py (Windows). Optional `gh` upload.
  * mocida-rs   — the Rust bindings. Published to crates.io (`cargo publish`)
                  in the future; not wired up yet, so this only records the
                  changelog and prints the intended steps.

Every release prepends an entry to CHANGELOG.md at the repo root.
"""

import argparse
import os
import subprocess
import sys
import tempfile
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parent
CHANGELOG = ROOT / "CHANGELOG.md"

PACKAGES = {
    "mocida": {
        "title": "mocida (C toolkit)",
        "blurb": "SDK zip + single-file installer (Windows)",
        "available": True,
    },
    "mocida-rs": {
        "title": "mocida-rs (Rust bindings)",
        "blurb": "crates.io publish - coming soon, not wired up yet",
        "available": False,
    },
}

# --- pretty -------------------------------------------------------------
_COLOR = (not os.environ.get("NO_COLOR")) and sys.stdout.isatty()
if _COLOR and os.name == "nt":
    os.system("")


def _c(t, code):
    return f"\033[{code}m{t}\033[0m" if _COLOR else t


def head(m): print(_c(f"\n== {m} ==", "35;1"))
def info(m): print(_c(f"  {m}", "37"))
def ok(m):   print(_c(f"  + {m}", "32"))
def warn(m): print(_c(f"  ! {m}", "33"))
def fail(m): print(_c(f"  x {m}", "31"))


def is_tty():
    return sys.stdin.isatty() and sys.stdout.isatty()


# --- interactive helpers ------------------------------------------------
def choose_package_interactive():
    head("Which package to release?")
    keys = list(PACKAGES)
    for i, k in enumerate(keys, 1):
        p = PACKAGES[k]
        tag = "" if p["available"] else _c("  [coming soon]", "33")
        print(f"  {i}) {p['title']} - {p['blurb']}{tag}")
    while True:
        sel = input(_c("  > choose [1-%d]: " % len(keys), "36")).strip()
        if sel.isdigit() and 1 <= int(sel) <= len(keys):
            return keys[int(sel) - 1]
        print("    invalid choice.")


def ask(prompt, default=None):
    suffix = f" [{default}]" if default else ""
    val = input(_c(f"  {prompt}{suffix}: ", "36")).strip()
    return val or (default or "")


def collect_notes_interactive():
    info("Changelog notes (one bullet per line; blank line to finish):")
    lines = []
    while True:
        try:
            line = input("    - ")
        except EOFError:
            break
        if not line.strip():
            break
        lines.append(f"- {line.strip()}")
    return "\n".join(lines)


def confirm(prompt):
    return input(_c(f"  {prompt} [y/N]: ", "33")).strip().lower() in ("y", "yes")


# --- changelog ----------------------------------------------------------
def update_changelog(tag, notes, today):
    entry = f"## [{tag}] - {today}\n\n{notes.strip() or '- (no notes)'}\n"
    if CHANGELOG.exists():
        existing = CHANGELOG.read_text(encoding="utf-8")
        if existing.lstrip().startswith("# "):
            first_nl = existing.index("\n")
            header, body = existing[: first_nl + 1], existing[first_nl + 1:]
        else:
            header, body = "# Changelog\n", existing
    else:
        header, body = "# Changelog\n\nAll notable changes to this monorepo.\n", ""
    CHANGELOG.write_text(f"{header}\n{entry}\n{body.lstrip()}", encoding="utf-8")
    ok(f"changelog updated: {CHANGELOG.name}")


# --- per-package release ------------------------------------------------
def release_mocida(tag, notes, upload):
    script = ROOT / "mocida" / "release.py"
    if not script.exists():
        raise RuntimeError(f"mocida/release.py not found at {script}")
    cmd = [sys.executable, str(script)]
    notes_file = None
    if upload:
        cmd.append("--upload")
        if notes.strip():
            fd, notes_file = tempfile.mkstemp(prefix="mocida-notes-", suffix=".md", text=True)
            with os.fdopen(fd, "w", encoding="utf-8") as f:
                f.write(f"Mocida {tag}\n\n{notes.strip()}\n")
            cmd += ["--notes-file", notes_file]
        cmd.append(tag)
    try:
        return subprocess.run(cmd).returncode
    finally:
        if notes_file and os.path.exists(notes_file):
            os.unlink(notes_file)


def release_mocida_rs(tag, notes, upload):
    warn("mocida-rs is a bindings crate; crates.io publishing is not wired up yet.")
    info("When it is, the flow will be roughly:")
    info("  cd mocida-rs && cargo publish -p mocida-sys && cargo publish -p mocida")
    info("For now, the changelog entry above records this release intent.")
    return 0


RELEASERS = {"mocida": release_mocida, "mocida-rs": release_mocida_rs}


# --- main ---------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(prog="release.py",
                                 description="Mocida monorepo release orchestrator.")
    ap.add_argument("package", nargs="?", choices=list(PACKAGES),
                    help="package to release (omit for an interactive menu)")
    ap.add_argument("--tag", help="release tag, e.g. v1.2.0")
    ap.add_argument("--notes", help="changelog notes (inline)")
    ap.add_argument("--notes-file", help="file containing changelog notes")
    ap.add_argument("--upload", action="store_true",
                    help="actually publish (gh release for mocida)")
    ap.add_argument("--yes", "-y", action="store_true", help="skip the confirmation prompt")
    ap.add_argument("--list", action="store_true", help="list packages and exit")
    args = ap.parse_args()

    if args.list:
        head("Releasable packages")
        for k, p in PACKAGES.items():
            state = _c("ready", "32") if p["available"] else _c("coming soon", "33")
            print(f"  {k:<10} {state}  - {p['blurb']}")
        return 0

    # Resolve the package.
    pkg = args.package
    if not pkg:
        if not is_tty():
            fail("no package given and not a TTY. Pass one: python release.py mocida")
            return 2
        pkg = choose_package_interactive()

    if not PACKAGES[pkg]["available"]:
        warn(f"{pkg}: {PACKAGES[pkg]['blurb']}")

    # Resolve tag + notes (prompt only when interactive).
    tag = args.tag
    if not tag:
        tag = ask("Release tag", default="v0.0.0") if is_tty() else None
    if not tag:
        fail("a --tag is required (e.g. --tag v1.2.0).")
        return 2

    notes = args.notes or ""
    if args.notes_file:
        notes = Path(args.notes_file).read_text(encoding="utf-8")
    if not notes and is_tty():
        notes = collect_notes_interactive()

    # Summary + confirmation.
    head("Release plan")
    info(f"package : {pkg} ({PACKAGES[pkg]['title']})")
    info(f"tag     : {tag}")
    info(f"publish : {'YES (gh upload)' if args.upload else 'no (local artifacts only)'}")
    info(f"changelog: prepend to {CHANGELOG.name}")
    print(_c("  --- notes ---", "90"))
    print("\n".join(f"  {ln}" for ln in (notes.strip() or "(none)").splitlines()))

    if not args.yes:
        if not is_tty():
            fail("refusing to proceed without --yes in non-interactive mode.")
            return 2
        if not confirm("Proceed with this release?"):
            warn("aborted.")
            return 1

    # Record the changelog, then run the package release.
    update_changelog(tag, notes, date.today().isoformat())
    rc = RELEASERS[pkg](tag, notes, args.upload)
    if rc != 0:
        fail(f"{pkg} release step failed (exit {rc}).")
        return rc

    ok(f"{pkg} {tag} done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
