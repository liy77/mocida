# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

Rust bindings for [Mocida](https://github.com/liy77/mocida), a Windows-first C UI toolkit on SDL3. **Not a Rust reimplementation** — the C library does all the rendering and event handling; this workspace is an FFI layer (`mocida-sys`) plus an idiomatic wrapper (`mocida`).

Every public header in upstream `mocida/src/headers/uikit/*.h` has a corresponding Rust module under `mocida/src/`. The README's "API coverage" table is the source of truth for what's wrapped.

## Build commands

The build requires two env vars pointing at a local mocida C install:

```powershell
$env:MOCIDA_INCLUDE_DIR = "C:\path\to\mocida\src\headers"
$env:MOCIDA_LIB_DIR     = "C:\path\to\mocida\build"   # contains mocida.lib / mocida.dll
cargo check --workspace --all-targets
```

For a docs-only build (skips bindgen + linking — useful when you only want to typecheck `mocida-sys`):
```powershell
cargo check -p mocida-sys --features docs-only
```

Other useful commands:
- `cargo clippy --workspace --all-targets -- -A clippy::all -W clippy::correctness` — correctness sweep (current baseline: zero warnings).
- `cargo check -p mocida --example hello_world` — typecheck the basic example.
- `cargo check -p mocida --example signals` — typecheck the reactive demo.
- `cargo build -p mocida --example hello_world` — full build (will fail at link without a real `mocida.lib`).

`bindgen` needs `clang` on `PATH`.

## Architecture

### Two-crate workspace

- **`mocida-sys`** — raw `extern "C"` bindings. `build.rs` runs `bindgen` over `wrapper.h` against `$MOCIDA_INCLUDE_DIR`. To stay buildable without SDL3 on disk, `build.rs` synthesizes a `SDL3/` + `SDL3_ttf/` + `SDL3_image/` stub tree in `$OUT_DIR` with opaque typedefs (see the `sdl_shim()` function). The `--features sdl3-headers` flag swaps the stubs for the real install at `$SDL3_INCLUDE_DIR`. The build script also surfaces every Win32 system lib mocida pulls in (mfplat, dwmapi, ...), so consumers don't repeat them.
- **`mocida`** — safe wrapper. Every C type becomes a Rust struct with a `moved` flag on widgets (flipped to `true` once ownership transfers to a parent collection / app, suppressing the `Drop` destructor to avoid double-free).

### Critical patterns

- **Widget ownership transfer.** `widget.into_raw()` consumes the wrapper and hands the `*mut UIWidget` to mocida (the parent will free it). Wrappers that own a typed payload (`Rectangle`, `Text`, `Button`, ...) have a `moved` flag they flip when lifted into a `Widget` via `into_widget`/`into_widget_sized`, so the C destructor only runs once. **Never** keep a raw `*mut UI*` past the owning app's `Drop`.
- **Callback trampolines.** C callbacks (`on_click`, `on_change`, event callbacks, signal subscriptions, ...) are routed through `extern "C"` trampoline functions. The Rust closure lives in a `Box<TrampolineState>`, and `Box::as_ref(&state)` is passed as the `void* userdata`. The box is owned by the wrapper struct so the address stays stable; transferring the wrapper into a `Widget` and dropping the original handle leaks the closure (see "Known sharp edges" in the README).
- **Global trampolines** (debug log handler, crash callback) use `AtomicPtr<HandlerState>` to manage replacement and teardown thread-safely.
- **Single-threaded.** Like mocida itself, every UI call assumes the main thread. Wrappers intentionally don't implement `Send`/`Sync`. `App::on_event` uses a TLS slot because the C event callback signature has no `userdata` pointer.
- **`Signal<T>` is generic** over a `SignalValue` trait. Implemented for `i32`, `f32`, `bool` (transparently sharing `UI_SIGNAL_INT`), `String` (auto-`CString`), and `Opaque(*mut c_void)` for the pointer case. The subscription trampoline is type-erased (`Box<dyn FnMut(*mut UISignal)>`) so a single `extern "C"` function handles all `T`.
- **bindgen enum style.** The `build.rs` uses `EnumVariation::NewType { is_global: true }`, so C enums become tuple structs like `sys::UIRenderQuality(pub i32)`. When forwarding Rust enums to the FFI, construct the newtype: `sys::UIRenderQuality(quality as i32)`.

### Workflow notes from prior sessions

- The workspace defaults to **Brazilian Portuguese** for chat replies (user is `rodrigo.mcu@gmail.com`).
- When the user says "port", they mean **bindings + idiomatic wrappers**, not a Rust reimplementation. Adding coverage means adding more wrappers, never rewriting widget logic.
- Header `mocida_alloc.h` is intentionally **not wrapped** — it's a macro-redefinition file for mimalloc with no public API.

## Memory and persistence

Free-form session notes live at `C:\Users\hcsbr\.claude\projects\C--Users-hcsbr-Documents-mocida-rs\memory\`. The auto-memory index `MEMORY.md` is loaded into every session and lists pointers to user/project/reference memories.
