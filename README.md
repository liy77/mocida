<div align="center">

<img src="mocida/assets/banner.svg" alt="Mocida — Modular UI toolkit in C" width="100%"/>

<p>
  <a href="mocida/README.md"><img alt="C11" src="https://img.shields.io/badge/C-11-3949ab?style=flat-square"></a>
  <a href="mocida-rs/README.md"><img alt="Rust" src="https://img.shields.io/badge/Rust-bindings-ce422b?style=flat-square&logo=rust"></a>
  <a href="https://github.com/libsdl-org/SDL"><img alt="SDL3" src="https://img.shields.io/badge/SDL-3-2c5f9e?style=flat-square"></a>
  <img alt="Windows / Linux / macOS" src="https://img.shields.io/badge/build-Windows%20%C2%B7%20Linux%20%C2%B7%20macOS-0078d4?style=flat-square">
  <img alt="License" src="https://img.shields.io/badge/license-MIT-22c55e?style=flat-square">
  <img alt="status" src="https://img.shields.io/badge/status-active-f59e0b?style=flat-square">
</p>

<p>
  <b>A modular UI toolkit in C</b> on
  <a href="https://github.com/libsdl-org/SDL"><img src="mocida/assets/sdl_logo.png" height="14" alt="SDL" style="vertical-align: middle"></a>
  — plus idiomatic <b>Rust bindings</b>, in one monorepo with a single,
  cross-platform <code>python build.py</code>.
</p>

</div>

---

## Monorepo at a glance

```
mocida/                         build.py  ──┐  one command builds
┌───────────────┐   FFI         ┌───────────▼──────────────┐   the whole graph:
│  mocida (C)   │ ───────────►  │  mocida-rs (Rust)        │
│  UIWidget     │  headers +    │  mocida-sys  (raw FFI)   │      c ──► rust
│  SDL3 render  │  mocida.lib   │  mocida      (idiomatic) │   (rust links the
└───────────────┘               └──────────────────────────┘    C lib, so c runs first)
```

| Path         | Contents                                                                                       |
| ------------ | ---------------------------------------------------------------------------------------------- |
| [`mocida/`](mocida/README.md)    | The **C toolkit** — a Windows-first UI library on SDL3 (sources, vendored SDL/SDL_image/SDL_ttf, tests, docs). |
| [`mocida-rs/`](mocida-rs/README.md) | The **Rust bindings** — a Cargo workspace: the `mocida-sys` FFI crate + the safe, idiomatic `mocida` wrapper. |
| `build.py` · `setup.py`          | Cross-platform **build orchestrator** (Turbo-style task graph) and dependency **bootstrap**.   |
| `.github/`                       | CI workflows.                                                                                  |

The Rust side links the C library through environment variables wired
automatically by `build.py`: `MOCIDA_INCLUDE_DIR` (`mocida/src/headers`),
`MOCIDA_LIB_DIR` (the build output), plus optional `MOCIDA_LIB_NAME` /
`MOCIDA_STATIC`.

---

## Quick start

```sh
# 1. First time on a fresh machine — installs tools, vcpkg, SDL, mimalloc,
#    then runs the first build (Windows via winget; Linux/macOS verifies tools).
python mocida/setup.py

# 2. Build the whole monorepo (C toolkit -> Rust bindings) in one shot:
python build.py                 # debug
python build.py --config release
```

> The same command works on **Windows, Linux and macOS**. Thin wrappers
> `build.ps1` / `build.sh` at the repo root forward to `build.py` if you
> prefer `.\build.ps1` / `./build.sh`.

### Build orchestrator (`build.py`)

A small dependency-graph task runner (`setup → c → rust`). Quiet by
default — only progress + errors; pass `--verbose` for the full log.

| Command                         | What it does                                            |
| ------------------------------- | ------------------------------------------------------- |
| `python build.py`               | Build everything (`c` then `rust`), Debug.              |
| `python build.py c`             | Build only the C toolkit.                               |
| `python build.py rust`          | Build only the Rust workspace (against the current C lib). |
| `python build.py --config release` | Optimised build of both.                             |
| `python build.py --force`       | Clean rebuild (C wipe + `cargo clean`).                 |
| `python build.py --verbose`     | Show compiler warnings, cargo log, command echoes.      |
| `python build.py clean`         | Wipe the C build dir + `cargo clean`.                   |
| `python mocida/release.py`      | Package the SDK zip + single-file installer (Windows).  |
| `python mocida/docs.py --serve` | Build the Doxygen docs and serve them locally.          |

---

## What's inside the C toolkit

<img src="mocida/assets/feature-widgets.svg" alt="Mocida widget gallery" width="100%"/>

- **Widgets that look right** — analytic-coverage anti-aliasing, SDF drop shadows, configurable MSAA (1×/4×/16×/64×), optional SSAA / FXAA / TAA.
- **A real component model** — buttons, text, images, text fields/areas, tabs, dialogs, popups, dropdowns, sliders, switches, file-drop zones, scroll views, grids, video, and an embedded WebView2 surface — all on one `UIWidget` envelope.
- **Built-in debug stack** — logger (TCP/file/handler sinks), Chrome-Trace profiler, an opt-in debug overlay (widget bounds + FPS/timing HUD + overdraw heatmap, toggled with F9 / F10 / F8, F12 = all), always-on crash handler, optional ASan/UBSan.
- **Modern allocator** — optional Microsoft [mimalloc](https://github.com/microsoft/mimalloc) wired in transparently.

See **[`mocida/README.md`](mocida/README.md)** for the full feature tour and C API.

---

## Using it from Rust

The `mocida` crate wraps the C `UIWidget` model in safe, chainable
builders (ownership tracked via a `moved` flag; callbacks routed through
`extern "C"` trampolines):

```rust
use mocida::{Button, FontStyle};

let button = Button::new("Click me", 18.0)?
    .font_style(FontStyle::BOLD | FontStyle::ITALIC)
    .on_click(|b| { let _ = b.set_text("Clicked!"); })
    .into_widget()?;
```

Full programs and the FFI wiring are documented in
**[`mocida-rs/README.md`](mocida-rs/README.md)** and
**[`mocida-rs/EXAMPLES.md`](mocida-rs/EXAMPLES.md)**.

---

## Visual quality & debugging

<img src="mocida/assets/feature-quality.svg" alt="Anti-aliasing pipeline" width="100%"/>

Per-pixel analytic coverage for circles and rounded corners — output
matches hardware 16×/64× MSAA, mathematically exact and consistent across
every backend (D3D11/D3D12/OpenGL/Vulkan/Metal).

<img src="mocida/assets/feature-debug.svg" alt="Debug subsystem" width="100%"/>

A logger + Chrome-Trace profiler + live overlays + crash handler ship in
the box. Details in [`mocida/README.md`](mocida/README.md#debug--profiling).

---

## Formatting and pre-commit hooks

Formatting is enforced on **changed/staged files only** via the
[pre-commit](https://pre-commit.com/) framework:

- **C** — `clang-format` (config: `.clang-format`), scoped to `mocida/src` only. Vendored SDL trees are excluded.
- **Rust** — `cargo fmt` (config: `rustfmt.toml`), scoped to the `mocida-rs/` workspace.

One-time setup, from the repository root:

```sh
pipx install pre-commit        # or: pip install --user pre-commit
pre-commit install
```

- **clang-format** — the `mirrors-clang-format` hook vendors its own binary (pinned to v18.1.8); a system `clang-format` on `PATH` also works.
- **rustfmt** — `rust-toolchain.toml` pins `stable` + `rustfmt`, so `cargo fmt` works after `rustup component add rustfmt`.

The hooks reformat only the files you stage; they never mass-reformat the tree.

---

## Working across Windows + WSL

Editing on Windows but building on Linux? [`sync-from-windows.sh`](sync-from-windows.sh)
mirrors the source (only what you edit — vendored deps and build output are
excluded) into the WSL copy and can chain straight into a build:

```sh
./sync-from-windows.sh build            # sync + python build.py
./sync-from-windows.sh build --release  # sync + release build
```

---

<div align="center"><sub>MIT-licensed · C11 + Rust · powered by SDL3</sub></div>
