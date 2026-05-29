# Running the mocida-rs examples

This guide walks through every example in `mocida/examples/`, from
zero to a running window. Read it once, then keep it as a cheat sheet.

## 0. Prerequisites

The Rust side does no rendering of its own — it dynamically links
against the C mocida runtime. You need:

| Tool                              | Why                                                     |
| --------------------------------- | ------------------------------------------------------- |
| **A built copy of mocida (C)**    | Provides `mocida.lib` / `mocida.dll`.                   |
| **`clang` on `PATH`**             | `bindgen` (called by `mocida-sys/build.rs`) uses it.    |
| **Rust 1.74+**                    | Workspace `rust-toolchain.toml` pins `stable`.          |
| **Windows 10 / 11 + MSVC toolchain** | Upstream mocida is Windows-first.                    |

If you used the upstream installer, the artifacts land under
`%LOCALAPPDATA%\Programs\Mocida\`:

```text
C:\Users\<you>\AppData\Local\Programs\Mocida\
├── include\uikit\*.h
└── lib\
    ├── mocida.lib
    ├── mocida.dll
    ├── SDL3.dll
    ├── SDL3_image.dll
    ├── SDL3_ttf.dll
    └── WebView2Loader.dll
```

To build from source instead:

```powershell
git clone https://github.com/liy77/mocida.git C:\src\mocida
cd C:\src\mocida\mocida
python setup.py
python build.py
```

## 1. Tell mocida-rs where the C install lives

You almost certainly don't need to do anything here. `mocida-sys/build.rs`
auto-resolves the install in three stages, in order:

1. Honor `MOCIDA_INCLUDE_DIR` / `MOCIDA_LIB_DIR` if they're set in
   the current shell.
2. **On Windows**, read them straight from `HKCU\Environment` —
   covers the case where the var is in the registry but the
   PowerShell session you're in snapshotted its env before the
   value existed.
3. Fall back to the standard installer layout under
   `%LOCALAPPDATA%\Programs\Mocida\{include,lib}`.

If the installer dropped its files under `%LOCALAPPDATA%\Programs\Mocida\`
(the default), `cargo run -p mocida --example demo` works out of the
box — no env-var fiddling required. The build emits a `cargo:warning=`
line telling you which resolution path it picked, so you can see what
happened.

For non-default layouts (you cloned + built from source) or
non-Windows hosts, set the vars explicitly:

```powershell
# Source-build layout:
$env:MOCIDA_INCLUDE_DIR = "C:\src\mocida\src\headers"
$env:MOCIDA_LIB_DIR     = "C:\src\mocida\build"
```

Optional knobs:

| Variable           | Default    | Meaning                                              |
| ------------------ | ---------- | ---------------------------------------------------- |
| `MOCIDA_LIB_NAME`  | `mocida`   | Library base name passed to the linker via `-l`.     |
| `MOCIDA_STATIC`    | unset      | Set to `1` to link statically.                       |
| `SDL3_INCLUDE_DIR` | unset      | Used together with `--features sdl3-headers`.        |

### DLL staging is automatic

`mocida-sys/build.rs` copies every `.dll` from `MOCIDA_LIB_DIR`
into `target/<profile>/`, `target/<profile>/examples/` and
`target/<profile>/deps/` on every build. The copy uses mtime to
skip no-op writes, and each DLL is registered with
`cargo:rerun-if-changed=`, so when you upgrade upstream mocida the
fresh DLL automatically propagates on the next `cargo build` /
`cargo run` — no manual copy, no `PATH` tweak required.

## 2. Smoke-test the build

Type-check the entire workspace without producing binaries:

```powershell
cargo check --workspace --all-targets
```

If that's clean you're ready to run anything.

## 3. The examples

All three live under `mocida/examples/` and are listed under
`[[example]]` in `mocida/Cargo.toml`. They follow the same pattern:

```powershell
cargo run -p mocida --example <name>
```

### 3.1 `hello_world`

```powershell
cargo run -p mocida --example hello_world
```

Minimal window: a white panel, three cards, a title, a live FPS
readout (driven by `App::on_event(Event::FramerateChanged, …)`),
and a button that cycles the FPS target. Good first sanity check —
if this paints, your build is wired correctly.

**What to look for:**

- Three cards (blue, green, purple) under the title.
- The "FPS" label updates roughly once per second.
- Clicking the blue button cycles `30 → 60 → 120 → UNLIMITED` and
  rewrites both its own label and the displayed target.

### 3.2 `signals`

```powershell
cargo run -p mocida --example signals
```

Demonstrates the generic `Signal<T>` machinery + `TextField` +
`Dropdown`.

- A `Signal<String>` holds the live text. The text-field's
  `on_change` mutates it. A `Signal::subscribe` callback retypes the
  title widget.
- A `Signal<i32>` holds the palette index. The dropdown's
  `on_change` mutates it. A second subscriber retints the title.

**What to look for:**

- The title above the text field mirrors what you type, character
  by character.
- Picking a color from the dropdown immediately changes the title's
  color (slate / indigo / emerald / crimson).
- No buttons — close the window to exit.

### 3.3 `demo`

```powershell
cargo run -p mocida --example demo
```

A line-by-line Rust port of upstream's `src/main.c` showcase. The
most visually rich example in the repo.

**What to look for:**

- A white panel inset by 24 px on every side, with a header,
  four cards in a row, three action buttons, and a footer.
- The header labels for **FPS**, **Target FPS** and **AA mode**
  update live (the first via the `FramerateChanged` event, the
  other two when you click the matching button).
- The **orange card** is draggable thanks to a `MouseArea` overlay
  with `drag_target` set to the card and `drag_bounds` clamped to
  the window.
- The first time you drag the orange card, the resize handler
  stops snapping it back into the grid — your drop position
  sticks across window resizes.
- Three buttons:
  - **FPS Target** cycles `30 / 60 / 120 / UNLIMITED`.
  - **AA Mode** cycles `COVERAGE / SSAA 2x / FXAA / TAA`. Watch
    text/edge softness shift between them.
  - **Trim caches** dumps the renderer's cached textures (you'll
    see a one-frame hitch as they rebuild lazily).
- Resize the window: the panel, cards, buttons and footer all
  reflow.

## 4. Common errors

### `LINK : fatal error LNK1181: cannot open input file 'mocida.lib'`

`MOCIDA_LIB_DIR` isn't set, or doesn't actually contain `mocida.lib`.
Re-check the path the upstream `build.py` wrote it to.

### `The code execution cannot proceed because mocida.dll was not found`

The `.dll` is on disk but Windows can't find it at run time. Either
copy it next to the generated `target\debug\examples\*.exe`, or
prepend `MOCIDA_LIB_DIR` to `$env:PATH` before `cargo run`.

### `MOCIDA_INCLUDE_DIR could not be resolved`

You're on a non-Windows host (or a Windows host with no Mocida
installer + no var set). Either install upstream Mocida or point the
env var at your `uikit/` header tree manually.

### DLLs not found at run time

The build script copies DLLs automatically — if you still get
"`mocida.dll` was not found", check that `MOCIDA_LIB_DIR` actually
points at a directory containing the DLL (`Get-Command "$env:MOCIDA_LIB_DIR\mocida.dll"`)
and run `cargo build` once more so the copy fires. As a last
resort:

```powershell
Copy-Item "$env:MOCIDA_LIB_DIR\*.dll" .\target\debug\examples\ -Force
```

### "I upgraded Mocida but the bug fix isn't showing up"

`cargo:rerun-if-changed=` watches the upstream DLL's mtime, so a
fresh install triggers a re-copy on the next `cargo build`. If
your installer copied the file without updating mtime (rare), run

```powershell
(Get-Item "$env:MOCIDA_LIB_DIR\mocida.dll").LastWriteTime = Get-Date
cargo build -p mocida --examples
```

to nudge cargo into refreshing.

### `[mocida.asset] could not find 'assets/logo.svg'`

The window icon is loaded with a path relative to the current
working directory. Run `cargo run` from the repo root and either
let the example skip the icon (it falls back silently) or copy
`assets/logo.svg` next to the generated `.exe`:

```powershell
New-Item -ItemType Directory ".\target\debug\examples\assets" -Force | Out-Null
Copy-Item .\assets\logo.svg .\target\debug\examples\assets\ -Force
```

### Window opens but clicking a button crashes with `ACCESS_VIOLATION`

Fixed in current `main` — the `Drop` impls used to free the
trampoline state while mocida still held its `userdata` pointer.
If you hit this on an old checkout, pull the latest `Button` /
`MouseArea` / etc. drop implementations.

### Window opens but text doesn't render

`UISearchFonts()` couldn't resolve "Arial". On Windows it should
just work; if you're on a stripped-down VM, drop a `.ttf` next to
the binary and pass its path through `Text::font_family(...)`
instead of relying on `get_font`.

### `clang: error: unknown argument: '-include...'`

`bindgen` couldn't find `clang`. Install LLVM (the upstream
`setup.py` does this for you) and re-run.

## 5. Writing your own example

1. Drop a file in `mocida/examples/your_thing.rs`.
2. (Optional) Register it explicitly under `[[example]]` in
   `mocida/Cargo.toml` — Cargo auto-discovers files in the
   `examples/` directory, but pinning it lets you set custom
   metadata.
3. `cargo run -p mocida --example your_thing`.

Cribs from the existing examples:
- `hello_world.rs` — minimum viable window + a button.
- `signals.rs` — reactive state and `TextField` / `Dropdown`.
- `demo.rs` — full layout, `MouseArea` dragging, multi-button
  cycling, `App::on_resize` for fluid layouts.
