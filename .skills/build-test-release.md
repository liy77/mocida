# Build, test, release

The same commands work on Windows, Linux and macOS (Python 3 required).

## Build

```sh
python build.py                  # build everything (c -> rust), Debug
python build.py --config release # optimised
python build.py c                # only the C toolkit
python build.py rust             # only the Rust workspace
python build.py --force          # clean rebuild
python build.py clean            # wipe build dirs + cargo clean
```

- **Quiet by default**: only phase headers, a progress bar, errors and a
  summary are shown. Pass `--verbose` (`-v`) for full compiler/cargo output.
  On failure the captured build log is dumped automatically.
- The task graph is `setup -> c -> rust` (rust links the C lib, so `c` runs
  first). `build.ps1` / `build.sh` at the root just forward to `build.py`.

## First-time bootstrap

```sh
python mocida/setup.py           # tools + vcpkg + SDL + mimalloc, then a build
```

On Windows missing tools are installed via winget; on Linux/macOS the script
only verifies them and tells you what to install.

## Test the Rust bindings

`build.py rust` builds the workspace. For a fuller check, `cargo check`/`build`
need the FFI env vars (`MOCIDA_INCLUDE_DIR`, `MOCIDA_LIB_DIR`,
`MOCIDA_LIB_NAME=mocida`, `MOCIDA_STATIC=1`) — `build.py` sets them for you.
A fully-linked example exe also needs the SDL/SDL_image/SDL_ttf import libs on
the link line, which is a known gap when linking outside the C build.

## Release

```sh
python release.py                # interactive: pick package, tag, notes
python release.py mocida --tag v1.2.0 --notes "..." --upload --yes
python release.py --list
```

- Records an entry in `CHANGELOG.md`, then runs the package's release.
- **`mocida`** (C) → `mocida/release.py` builds the SDK zip + single-file
  installer (Windows) and can `gh release` upload.
- **`mocida-rs`** (Rust) → bindings crate; published to **crates.io**
  (`cargo publish`) in the future. Not wired up yet — `release.py` only
  records the changelog for it today.

## Docs

```sh
python mocida/docs.py            # build Doxygen docs
python mocida/docs.py --serve    # build + serve locally (Python http.server)
```
