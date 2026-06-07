# CLAUDE.md — mocida workspace (agent operating guide)

Authoritative guide for working on **mocida** (the C UI toolkit), its **Rust
bindings + runtime** (`mocida-rs`), and the **OndaEngine** editor that consumes
them. Read this first. It is written to make non-trivial UI/runtime tasks (new
widgets, editor features, debugging "impossible" bugs) fast and safe.

Companion docs:
- `mocida-rs/CLAUDE.md` — the Rust FFI + wrapper internals (ownership, trampolines, signals).
- `mocida/README.md`, `mocida/docs/` — the C library API surface.
- OndaEngine: `C:\Users\hcsbr\Documents\OndaEngine\CLAUDE.md` — the editor/host.

Chat replies default to **Brazilian Portuguese** (user `rodrigo.mcu@gmail.com`);
code, comments, docs and commit messages stay in **English**.

---

## 1. The stack (who renders what)

```
mocida (C, SDL3)            real rendering + event dispatch + widgets
  └─ mocida-sys  (Rust)     raw bindgen FFI over the C headers
      └─ mocida  (Rust)     safe idiomatic wrappers (ownership, callbacks)
          └─ mui-runtime    interprets .mui markup → builds a live widget tree
              └─ onda-launcher (host)  OndaEngine's app: owns signals, file I/O, LSP
```

mocida does ALL rendering and input. The Rust side never re-implements widgets —
it wraps them. OndaEngine's UI is authored in `.mui`; the host and `.mui`
communicate **only through signals**.

Paths (this machine):
- C library: `C:\Users\hcsbr\Documents\mocida\mocida`
- Rust workspace: `C:\Users\hcsbr\Documents\mocida\mocida-rs` (`mocida-sys`, `mocida`, `mui-runtime`, `mui-dev`)
- Editor: `C:\Users\hcsbr\Documents\OndaEngine` (`onda.mui`, `ui/*.mui`, `onda-launcher/`)

---

## 2. Build & deploy — the exact workflow

Set these once per shell (PowerShell):
```powershell
$env:LIBCLANG_PATH      = "C:\Program Files\LLVM\bin"          # bindgen needs clang
$env:PATH               = "C:\Program Files\LLVM\bin;$env:PATH"
$env:MOCIDA_INCLUDE_DIR = "C:\Users\hcsbr\Documents\mocida\mocida\src\headers"
$env:MOCIDA_LIB_DIR     = "C:\Users\hcsbr\Documents\mocida\mocida\build\win32\release-shared"
```
> `MOCIDA_LIB_DIR` **must** be `release-shared` (fresh `mocida.dll` + `.lib`).
> The older `build/win32/release` import lib is stale and misses newer symbols
> (e.g. `UIText_SetGradient` → `LNK2019`).

**Decision table — what to rebuild after a change:**

| You changed… | Do this |
| --- | --- |
| `mocida/src/uikit/*.c` | rebuild DLL + redeploy (below). No Rust relink — just copy the DLL. |
| `mocida/src/uikit/*.inc` (e.g. `window_widgets.inc`) | **`touch` the `.c` that `#include`s it** (usually `window.c`), then rebuild DLL. The build skips the `.inc` otherwise — silent stale binary. |
| `mocida/src/headers/**/*.h` | rebuild DLL **and** `cargo clean -p mocida-sys` (regen bindgen) before `cargo build`. |
| `mocida-rs/**` (wrapper / `mui-runtime`) | `cargo build --release` (host). |
| `onda.mui`, embedded `ui/*.mui` | `cargo build --release` (they're `include_str!`-baked). |
| disk-loaded `ui/*.mui` (`color_picker.mui`, `agents.mui`, `git_panel.mui`) | nothing — relaunch picks them up in dev. (NOT in `EMBEDDED_COMPONENTS`; the standalone bundle build would miss them — add them there before shipping.) |

**Build the DLL + deploy** (from `mocida/mocida`):
```powershell
# touch window.c if you edited a .inc:
(Get-Item .\src\uikit\window.c).LastWriteTime = Get-Date
python build.py --release --shared --no-tests --no-demo
$src = ".\build\win32\release-shared\mocida.dll"
Copy-Item $src "C:\Users\hcsbr\Documents\OndaEngine\onda-launcher\target\release\mocida.dll"      -Force
Copy-Item $src "C:\Users\hcsbr\Documents\OndaEngine\onda-launcher\target\release\deps\mocida.dll" -Force
```
The DLL must be staged next to `onda-launcher.exe` **and** in `deps/`.

**Build the host** (from `OndaEngine/onda-launcher`): `cargo build --release`.

### ⚠️ Incremental-build corruption (read this before chasing ghosts)
Many rapid `cargo build`s in a row — especially after a **failed/aborted build**
(e.g. you reverted a file mid-iteration) — can leave the incremental artifacts
inconsistent. Symptom: **"impossible" runtime breakage that doesn't match the
source** — most notably `for`-loops silently rendering nothing (file tree empty,
recent-projects empty) even though the data is present and seeded.

This is NOT a code bug. **Fix: `cargo clean -p mui-runtime -p onda-launcher` then
rebuild.** When a rendering bug makes no sense given the diff, do a clean rebuild
FIRST, before reverting code or blaming the DB.

---

## 3. mui-runtime — how the tree is built (and its limits)

`build_node` builds one element, then wires the live (no-rebuild) reactive hooks:
- `subscribe_reactive_size` — `width:`/`height:` bound to a signal/expr updates the
  widget's size pointer in place each frame (smooth drag-resize).
- `subscribe_reactive_position` — `x:`/`y:` bound to a signal moves the widget in place.
- `subscribe_reactive_visible` — `visible: <signal>` shows/hides in place
  (string signal truthy unless ""/"0"/"false"; int signal ≠ 0).
- `subscribe_reactive_rect_color` — a `Rectangle`'s `background`/`gradientTo` bound
  to string signals re-colors / re-gradients in place.

### THE central limitation: structural rebuilds are coarse
A change to a signal used in a structural `if`/`for` **rebuilds the entire view**
that contains it. In the editor that means the `TextArea` is destroyed and
recreated, which:
- steals focus, resets caret/scroll/selection,
- **wipes the TextArea undo stack** (so Ctrl+Z can't undo a programmatic edit made before the rebuild),
- flickers everything (a frame of re-rasterization).

**Rules of thumb:**
- Per-keystroke / live UI over the editor → use **reactive** props (`${text}`,
  reactive size/position/visible), never a structural `if`/`for` toggle.
- A popup that overlays the editor (color picker, autocomplete) should be
  **always-rendered and hidden via `visible:`**, not gated by `if open == "1"` —
  otherwise opening/closing it rebuilds the editor (flicker + lost undo + lost focus).
- Subscriptions capture raw widget pointers. If the widget can be freed by a
  rebuild while a subscription on a still-live signal could still fire, that's a
  **use-after-free** (silent crash / memory corruption). Only attach
  enable/disable-style subscriptions to widgets that PERSIST (always-rendered).

### `dismiss: "signal"` (reusable click-outside-to-close)
On a popup container, `dismiss: "signalName"` injects a full-window transparent
`MouseArea` catcher behind the content; a press outside the first child's bounds
sets the signal to `"0"`. Pair it with always-render + a `visible:`-gated card so
closing doesn't rebuild the editor; gate the catcher's `enabled` on the signal so
it doesn't eat editor clicks while hidden.

---

## 4. TextArea & inline color swatches (C internals)

- `UITextArea_SetColorSwatches(offsets, colors, count)` reserves an inline gap and
  **draws the swatch box inline during the same render pass**, so boxes track the
  text exactly while scrolling/zooming (no overlay, no position race). Box/gap scale
  with `fontSize`. `UITextArea_SetOnSwatchClick` fires on a box click with the byte.
- **Per-line gap fold:** the offset table must add only gaps **on the same line**
  (`TA_GapInLine(segStart, b)`), not all gaps `<= b` — otherwise a gap on an earlier
  line shifts later lines' glyphs (box lands on the `#`).
- **Color-only updates must not force a full line-cache rebuild.** `SetColorSwatches`
  only bumps the version / sets `__cachedTextLen = -1` when the gap layout (offsets/
  widths) changes; a pure color change just rewrites `__swatchColor` (the draw reads
  it live) → no whole-editor flash while dragging a picker.
- **Undo-friendly programmatic edits:** `InsertText` / `ReplaceBeforeCaret` push an
  undo step; `SetText` calls `HistoryReset` (wipes undo). To replace a token undoably:
  `SetCaretByte(end)` **first** (it collapses the selection), then `SetSelAnchor(start)`
  to form the selection, then `InsertText(new)`. Do NOT also `set_str("file_content")`
  afterwards — the two-way binding round-trips it back as `SetText` and wipes the undo.

### Event dispatch & cursor (C)
- Each input type has its own dispatcher (`UIButton_/UIMouseArea_/UITextArea_DispatchMouseDown`).
  They must **recurse into containers unconditionally** via `ContainerChildren`
  (Stack/Glass/Grid/Rectangle/Scroll) and bounds-test only leaf widgets — otherwise
  a widget nested in layout containers gets no input (see `mocida-nested-dispatch`).
- Hover-cursor picking (`app.c PickHoverCursorImpl`) mirrors the dispatch: recurse
  unconditionally, bounds-test leaves, propagate a `blocked` flag so an opaque overlay
  blocks widgets behind it. The TextArea branch hit-tests the per-frame swatch rects →
  `UI_CURSOR_POINTER` over a swatch, else `ta->cursor` (default `UI_CURSOR_TEXT`).
- `UIButton` defaults to `UI_CURSOR_POINTER`; free-layout children are **window-
  absolute** positioned (moving a container does NOT move free-layout children).

---

## 5. GUI testing harness (OndaEngine)

Driver: `C:\Users\hcsbr\Documents\OndaEngine\_drive.ps1` (DPI-aware SendInput).
`. .\_drive.ps1` then: `Get-Hwnd`, `Front $h`, `Place $h` (1290×760), `Shot $h <png>`,
`Crop <src> <dst> x y w h scale`, `[Win]::Click(x,y)`, `[Win]::ScanKey(scancode,ctrl)`,
`[Win]::TypeText("...")`.

**Hard-won testing facts:**
- **Window placement is racy.** After `Place`, verify `GetWindowRect` width == 1290
  and re-`Place` until it sticks; an un-placed window (1298×767) drifts every click ~8px.
- **Click coords are SCREEN coords** (window at 0,0); mocida's renderer coords are
  client-relative, offset by ~**(+9, +38)** (border + title bar). A click 1px off a
  15px-tall bar misses — locate small targets from a screenshot, don't trust math.
- **Intermittent silent crash (~1/3)** on project/file open and editor rebuilds. It's
  a C segfault, NOT a Rust panic, so stderr shows only the startup banner. Make every
  test flow **retry from launch on `Get-Hwnd` null**; never trust a single run.
- **List item Y positions shift** (the Scene section collapses, moving the Files list
  up). Screenshot and locate; don't hardcode Y for `recent`/file-tree clicks.
- **Screenshots do NOT capture the OS cursor.** Verify cursor shape with
  `GetCursorInfo` + `LoadCursor(0, IDC_HAND=32649 / IDC_IBEAM=32513 / IDC_ARROW=32512)`,
  driven by real `SendInput` absolute motion glided in small steps. Reads still go
  stale between glides — probe one position right after a fresh hover.
- A single screenshot can't catch a 1-frame flicker; reason about it from the
  rebuild model instead (§3).

---

## 6. Debugging playbook (in order)

1. **Rendering breakage that contradicts the source** (empty lists, missing widgets,
   but data is present) → **clean rebuild** (`cargo clean -p mui-runtime -p onda-launcher`).
   Incremental corruption is the usual cause (§2).
2. **Is the data there?** Add a one-line `eprintln!` in the host where the signal is
   set/seeded (e.g. print `recent.len()`, `file_tree.len()`). Data present but rendered
   empty ⇒ runtime/build problem; data absent ⇒ host/IO/DB problem.
3. **DB suspected?** `onda.sqlite` integrity: `python -c "import sqlite3; print(sqlite3.connect('onda.sqlite').execute('PRAGMA integrity_check').fetchone())"`.
   It rarely is — list rendering is filesystem/seed driven, not the DB.
4. **Silent crash (no Rust panic in stderr)** = C segfault. Suspect a use-after-free
   in a reactive subscription firing on a widget freed by a structural rebuild (§3),
   or a stale pointer in a per-frame rect/array. Bisect by reverting the most recent
   C/runtime change; a clean rebuild also rules out corruption.
5. **`.inc` edit "didn't take"** → you forgot to `touch window.c` (§2).
6. **Link error for a brand-new C symbol** → `MOCIDA_LIB_DIR` points at the stale
   `release` lib; use `release-shared` and rebuild the DLL first (§2).

---

## 7. Conventions

- Adding a widget/prop usually touches 3 layers across 2 repos: the C widget +
  header, the `mocida` wrapper (+ `mocida-sys` regen on header change), and
  `mui-runtime`'s `build_*`. For OndaEngine features add the host signal wiring too.
- "port" means **bindings + idiomatic wrappers**, never a Rust reimplementation.
- `mocida_alloc.h` is intentionally not wrapped (mimalloc macros, no public API).
- Persistent agent memory: `C:\Users\hcsbr\.claude\projects\C--Users-hcsbr\memory\`
  (`MEMORY.md` index). Record non-obvious gotchas there as you find them.
