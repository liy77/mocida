#!/usr/bin/env python3
"""
Mocida monorepo release orchestrator.

Pick a package to release, record a changelog entry, and (optionally)
publish. Works two ways:

  * Interactive — run with no package and answer the prompts:
        python release.py
  * Direct CLI (CI is the default) — cut a cross-platform release:
        python release.py mocida --tag v1.2.0 --notes "Fix X, add Y"
  * Local mode — build only THIS OS's assets on this machine:
        python release.py mocida --tag v1.2.0 --local --upload

Packages:
  * mocida      — the C toolkit. Builds the SDK + single-file installer
                  via mocida/release.py (per-OS). Optional `gh` upload.
  * mocida-rs   — the Rust bindings. Published to crates.io (`cargo publish`)
                  in the future; not wired up yet, so this only records the
                  changelog and prints the intended steps.

DEFAULT (CI) flow for the `mocida` package:
  1. Prepend a CHANGELOG.md entry for the tag.
  2. Commit CHANGELOG.md.
  3. Create the tag on that commit and push branch + tag to origin.
  4. The pushed tag triggers .github/workflows/release.yml, which builds
     windows/linux/macos natively and attaches every platform's assets to
     the one GitHub release. Nothing is built on this machine.

Pass --local to skip CI and build/package this single OS here instead (the
old behaviour); combine with --upload to `gh release` the local assets.
"""

import argparse
import os
import shutil
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
    # Idempotent: if an entry for this tag already exists (e.g. you wrote the
    # changelog by hand before cutting the release), leave it untouched rather
    # than prepending a duplicate stub.
    if CHANGELOG.exists() and f"## [{tag}]" in CHANGELOG.read_text(encoding="utf-8"):
        info(f"changelog already has an entry for {tag} - leaving it as-is.")
        return
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


# --- CI release: commit changelog, tag, push (triggers release.yml) -----
def _git(args, **kw):
    """Run a git command at the repo root, returning the CompletedProcess."""
    return subprocess.run(["git", *args], cwd=str(ROOT), text=True, **kw)


def _git_out(args):
    return _git(args, capture_output=True).stdout.strip()


def release_ci(tag):
    """Commit CHANGELOG.md, create the tag on that commit, and push branch +
    tag to origin. The pushed tag triggers the cross-platform release CI.
    The CHANGELOG.md must already be updated on disk (update_changelog())."""
    if not shutil.which("git"):
        raise RuntimeError("git not found on PATH.")

    branch = _git_out(["rev-parse", "--abbrev-ref", "HEAD"]) or "HEAD"

    # Refuse to clobber an existing tag (local or remote).
    if _git(["rev-parse", "-q", "--verify", f"refs/tags/{tag}"],
            capture_output=True).returncode == 0:
        raise RuntimeError(f"tag {tag} already exists locally. "
                           f"Delete it first: git tag -d {tag}")
    if _git_out(["ls-remote", "--tags", "origin", tag]):
        raise RuntimeError(f"tag {tag} already exists on origin.")

    # 1. Commit the changelog (only if there is something to commit).
    _git(["add", "--", str(CHANGELOG)])
    staged = _git(["diff", "--cached", "--quiet", "--", str(CHANGELOG)]).returncode
    if staged != 0:  # non-zero == there ARE staged changes
        if _git(["commit", "-m", f"release: {tag}"]).returncode != 0:
            raise RuntimeError("git commit of CHANGELOG.md failed.")
        ok(f"committed CHANGELOG.md (release: {tag})")
    else:
        info("CHANGELOG.md unchanged — tagging the current commit.")

    # 2. Create the annotated tag on the (new) HEAD.
    if _git(["tag", "-a", tag, "-m", f"Mocida {tag}"]).returncode != 0:
        raise RuntimeError(f"git tag {tag} failed.")
    ok(f"created tag {tag}")

    # 3. Push the branch (best effort) and the tag (required — it fires CI).
    info(f"pushing {branch} to origin ...")
    if _git(["push", "origin", branch]).returncode != 0:
        warn(f"could not push {branch} (protected?); pushing the tag anyway.")
    info(f"pushing tag {tag} to origin (triggers release.yml) ...")
    if _git(["push", "origin", tag]).returncode != 0:
        raise RuntimeError(f"git push of tag {tag} failed — CI not triggered.")

    slug = _git_out(["config", "--get", "remote.origin.url"])
    ok(f"tag {tag} pushed — cross-platform release CI started.")
    info("Watch it build win/linux/macos and attach assets at:")
    info(f"  {slug}  ->  Actions / Releases (draft {tag})")
    return 0


# --- main ---------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(prog="release.py",
                                 description="Mocida monorepo release orchestrator.")
    ap.add_argument("package", nargs="?", choices=list(PACKAGES),
                    help="package to release (omit for an interactive menu)")
    ap.add_argument("--tag", help="release tag, e.g. v1.2.0")
    ap.add_argument("--notes", help="changelog notes (inline)")
    ap.add_argument("--notes-file", help="file containing changelog notes")
    ap.add_argument("--local", action="store_true",
                    help="build THIS OS's assets locally instead of triggering "
                         "the cross-platform release CI (the default)")
    ap.add_argument("--upload", action="store_true",
                    help="(--local only) publish this OS's assets via gh release")
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

    # CI is the default; --local opts out to a single-OS build on this machine.
    # CI mode only applies to the mocida package (the workflow builds the C
    # toolkit); mocida-rs always takes the recording-only path.
    ci = (not args.local) and pkg == "mocida"

    # Summary + confirmation.
    head("Release plan")
    info(f"package : {pkg} ({PACKAGES[pkg]['title']})")
    info(f"tag     : {tag}")
    if ci:
        info("mode    : CI (commit CHANGELOG, push tag -> build win/linux/macos)")
    else:
        info(f"mode    : local build on this OS"
             f"{' + gh upload' if args.upload else ' (artifacts only)'}")
    info(f"changelog: prepend to {CHANGELOG.name}")
    print(_c("  --- notes ---", "90"))
    print("\n".join(f"  {ln}" for ln in (notes.strip() or "(none)").splitlines()))

    if not args.yes:
        if not is_tty():
            fail("refusing to proceed without --yes in non-interactive mode.")
            return 2
        prompt = ("Commit the changelog and push the tag (this triggers CI)?"
                  if ci else "Proceed with this release?")
        if not confirm(prompt):
            warn("aborted.")
            return 1

    # Always record the changelog first.
    update_changelog(tag, notes, date.today().isoformat())

    # Then either fire CI (default) or run the local per-package build.
    if ci:
        try:
            rc = release_ci(tag)
        except RuntimeError as e:
            fail(str(e))
            return 1
    else:
        rc = RELEASERS[pkg](tag, notes, args.upload)
    if rc != 0:
        fail(f"{pkg} release step failed (exit {rc}).")
        return rc

    ok(f"{pkg} {tag} done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
