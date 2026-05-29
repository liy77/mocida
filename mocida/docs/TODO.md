# Mocida — TODO

Inventory of gaps, bugs and proposed improvements. Each item links to
the relevant file(s) and gives a rough effort tag (S / M / L).

> **2026-05-21 status:** P0 items are all done. P1 widgets (textfield,
> checkbox, slider, progressbar, spinner, tooltip, dialog, menu,
> dropdown, tabview) are implemented. Text alignment, word-wrap,
> UIImage rounded corners + border, font-cache LRU, widget culling,
> double-click + right/middle mouse, clipboard wrapper, and the
> bug-tail cleanup pass (USE_BATCHED_RENDERING, dead globals,
> Doxyfile, alignment.c double-malloc, setup.ps1 helper) all landed.
> The remaining gaps below are the larger features (multi-window,
> hot reload, a11y, i18n, dirty-rect tracking) and the macOS/Linux
> cross-compilation work, which is intentionally still pending.

---

## P0 — Correctness bugs / data-loss risks

These are real bugs that will hurt users in normal use. Fix first.

- **[S] `UIWidget_GetParent` returns NULL when parent has dynamic size.**
  Lines in `src/uikit/widget.c` reject `parent->width == NULL || parent->height == NULL`,
  even though a valid parent without explicit size should still be returned.
  Should return `parent` and let callers handle dynamic sizing.

- **[M] `widget->opacity` and `widget->rotation` are stored but ignored by the renderer.**
  See `src/headers/uikit/widget.h` and `RenderSingleWidget` in
  `src/uikit/window.c`. Need to thread these through every render
  branch (alpha mod for opacity, `SDL_RenderTextureRotated` for rotation).

- **[M] Window resize doesn't reposition / resize widgets.**
  `HandleEvent(SDL_EVENT_WINDOW_RESIZED)` in `src/uikit/app.c` only
  updates the main widget and the renderer logical size. Buttons,
  scrolls and grids stay at their old positions. Need a layout-pass
  hook or `UIWidget_SetAnchors(...)`.

- **[S] Text rendering uses `SDL_SCALEMODE_NEAREST` in `window.c`.**
  Buttons use `LINEAR` (good) but `UIText` uses `NEAREST` — text looks
  blocky when the widget is scaled. Change to `LINEAR`.

- **[S] `UIWindow_SetProperty` does not check for / merge duplicate keys.**
  See `src/uikit/window.c`. Setting the same key twice leaks the old
  entry. Either replace the value in place or destroy the old prop.

- **[S] `__ui_props` teardown frees `key` as if it were heap-owned.**
  `UI_PROP_MAX_EVENTS` is a string literal — calling `free()` on it is
  UB. Confirm whether keys are always `_strdup`'d at insert; if not,
  fix the destroy path.

---

## P1 — Missing foundational widgets

The library can't realistically be used to build an app without
these. Order roughly by how often they show up in real UIs.

- **[L] `UITextField` / `UITextInput`** — typed text entry.
  Needs: focus tracking, caret rendering, SDL_StartTextInput()
  integration in `app.c`, IME handling, clipboard
  cut/copy/paste, selection range, basic key handling
  (arrows / home / end / backspace).

- **[M] `UICheckbox` / `UIToggle`** — boolean state widget with click
  toggling. Implementation mostly mirrors `UIButton`, just renders a
  checkmark / switch knob.

- **[M] `UISlider` / `UIRange`** — drag-to-scrub continuous value.
  Reuse `UIMouseArea` for the input layer.

- **[M] `UIProgressBar`** — read-only progress indicator (determinate +
  indeterminate variants).

- **[M] `UIDropdown` / `UIComboBox`** — needs popup window or
  overlay support. Touches the windowing model (see P3).

- **[M] `UITooltip`** — hover-triggered floating label. Needs a
  hover-with-delay primitive (timer-driven) and an overlay layer
  that draws above everything.

- **[L] `UIDialog` / `UIModal`** — blocks input to widgets below,
  draws over the whole window with a dimmed backdrop. Needs an
  "overlay children" list in `UIWindow`.

- **[L] `UIMenu` / `UIMenuItem`** — vertical pop-up menu, used by
  context menus and dropdowns. Often built on top of `UIDialog`.

- **[S] `UISpinner`** — animated loading indicator. Requires the
  animation system (P2).

- **[M] `UITabView`** — common navigation pattern.

---

## P1 — Input gaps

- **[M] No keyboard event dispatch.**
  `HandleEvent` in `src/uikit/app.c` ignores `SDL_EVENT_KEY_DOWN/UP`
  entirely. Need an event type (`UI_EVENT_KEY_PRESSED`?) with a
  widget-level focus model so `UITextField` can receive characters.

- **[M] No focus management.**
  Decide whether focus is per-widget or per-MouseArea-like overlay,
  and add a tab order. Without focus there is no keyboard navigation.

- **[S] No double-click detection.**
  Track last-click timestamp + position in `UIMouseArea` / `UIButton`
  and synthesise `UI_EVENT_DOUBLE_CLICK`.

- **[S] `UIMouseArea` only routes left-click.**
  See the `SDL_BUTTON_LEFT` filter in `HandleEvent` (`src/uikit/app.c`).
  Pass the button index to the dispatchers and let widgets opt into
  right / middle / extra buttons.

- **[M] No clipboard API.**
  Should expose `UIClipboard_GetText` / `SetText` wrapping
  `SDL_GetClipboardText` / `SDL_SetClipboardText`.

---

## P1 — Text rendering

- **[M] No word wrap / multiline.**
  `TTF_RenderText_Blended` renders a single line. Need
  `TTF_RenderText_Blended_Wrapped` integration plus text bounds /
  baseline awareness.

- **[S] No text alignment (left / center / right).**
  Add `UIText_SetAlign(text, UI_TEXT_ALIGN_LEFT/CENTER/RIGHT)` and
  honor it in the renderer.

- **[L] No font fallback chain.**
  When a glyph isn't in the font, render `?`. A real chain would try
  multiple fonts. Useful for international text.

- **[L] No emoji / colored glyph support.**
  Requires either a separate emoji font with COLR/CPAL or a fallback
  path that switches fonts per glyph.

---

## P1 — Visual / rendering features

- **[M] `UIImage` ignores `radius` and `borderWidth`.**
  Fields exist in `src/headers/uikit/image.h` but the renderer in
  `window.c` doesn't clip the image to a rounded shape. Easiest fix:
  reuse the existing AA circle texture as an alpha mask via
  render-to-texture + multiplicative blend.

- **[L] Nine-slice scaling is not implemented.**
  `UIImage.nineSliceMargins` exists but the renderer always uses
  the configured `fillMode`. Common for window chrome.

- **[L] No animated GIF support.**
  `UIImage.animated` flag exists but is unused. SDL3_image supports
  this via `IMG_LoadAnimation`.

- **[M] No gradient fills.**
  Even a 2-stop linear gradient (CPU-rasterized like the shadow
  pipeline) would cover most UI needs.

- **[L] No animation / tween system.**
  Critical for transitions (hover, button press, dialog enter/exit).
  Suggested API: `UIAnim_To(widget, &widget->x, target, duration_ms, ease)`.
  Internally a tweener registered on `UIApp_Run`'s per-frame tick.

- **[M] No transitions on state changes.**
  Buttons snap between NORMAL / HOVER / PRESSED. With the animation
  system this becomes a 80–120ms ease-out fade.

- **[M] No widget culling.**
  `RenderSingleWidget` always processes every child, even when its
  bounds are outside the window. Bounds-vs-window early-out would
  cut work in a window with off-screen scroll content.

- **[L] No dirty-rectangle tracking.**
  Every frame redraws the entire scene. For a fairly static UI we
  could track which widgets changed and redraw only their bounds,
  saving GPU draw calls.

---

## P2 — Layout

- **[M] Flex / stack layout.**
  Right now positions are pixel-perfect or via `UIAlignment` (which
  ties one widget to another). A vertical / horizontal stack with
  spacing and flex grow factors would be huge.

- **[M] Padding per cell in `UIGrid`.**
  Today the gap is uniform; an individual cell can't request more
  space.

- **[L] Constraint solver / aspect-ratio locks.**
  Bigger lift; would supersede most of `UIAlignment`.

- **[S] "Fill parent" sizing.**
  Today widgets need explicit dimensions. A sentinel like
  `UI_DYNAMIC_SIZE` already exists but isn't wired through the render
  path consistently.

- **[M] Layout invalidation.**
  Today widget size changes don't propagate. A simple "layout dirty"
  flag walked once per frame would be enough.

---

## P2 — Theme / styling

- **[M] Theme primitives.**
  Define `UITheme` with a colour palette (primary, surface, on-surface,
  ...), font sizes, spacing scale, border radii. Widgets read defaults
  from the active theme rather than the current hard-coded values.

- **[M] Dark / light mode switch.**
  Once themes exist, hook into the system preference (Windows
  registry / Apple `NSAppearance` / GTK setting) and emit
  `UI_EVENT_THEME_CHANGED`.

- **[L] CSS-like cascading.**
  Probably overkill at this scale; a flat theme is enough.

---

## P2 — Performance / memory

- **[S] Font cache is unbounded.**
  `g_fontCache` in `src/uikit/window.c` grows with every unique
  (path, size) pair and never evicts. Add an LRU cap (e.g. 32
  entries) or hook into `UIWindow_TrimCaches`.

- **[S] `UIText` keeps its own glyph texture forever.**
  Each `UIText_SetText` invalidates and recreates - fine. But after a
  text widget is hidden, the texture stays around. Optional: drop
  the texture on hide and rebuild on show.

- **[M] No widget pool.**
  Every `UIRectangle_Create` is its own malloc. For lists / grids
  that create hundreds of identical-shape widgets this is wasteful.
  A pool keyed by struct size (or per-type) would help.

- **[S] Heap stats hook.**
  Expose `UIApp_GetMemoryStats(...)` that consults mimalloc
  (`mi_stats_get_default`) and returns peak / current usage.

- **[M] Multi-threaded rasterization for shadows / coverage AA.**
  Both rasterizers are embarrassingly parallel per row. SDL3 has
  thread primitives; sharding the loop across `nproc` workers would
  halve the cost on multi-core machines.

---

## P2 — Audio

- **[L] Basic audio.**
  SDL3 has full audio support but Mocida doesn't expose it.
  `UISound_Play(path)` for one-shot clicks/notifications would be a
  small but appreciated addition.

---

## P3 — Multi-window / overlays

- **[L] Multiple windows.**
  `UIApp` owns a single `UIWindow`. Real apps want secondary windows
  (preferences, dialogs, tool palettes). The event dispatch needs to
  know which window an event targets.

- **[M] Overlay layer per window.**
  For dropdowns / tooltips / modals. Either a second `UIChildren`
  list with a higher z-index, or an explicit `UIWindow_AddOverlay`.

- **[L] System tray icon / notifications.**
  Cross-platform support is messy but the value is high for desktop
  apps. SDL3 doesn't directly cover this - probably platform-specific
  glue.

---

## P3 — Internationalization / accessibility

- **[L] RTL text.**
  Wide topic. Requires bidirectional algorithm + mirrored layout.

- **[L] String translation infrastructure.**
  Simple `gettext`-style `UIT(key)` lookup with a JSON / TOML strings
  file would be enough for most desktop apps.

- **[L] Accessibility hooks.**
  Windows UI Automation, AT-SPI on Linux, NSAccessibility on macOS.
  Massive lift, but without it blind users can't navigate the app.

- **[M] Keyboard navigation.**
  Tab / Shift-Tab to walk focusable widgets, Enter to activate. Cheap
  once focus exists.

---

## P3 — Tooling / project health

- **[M] Real unit tests.**
  `tests/test_*.c` are demos, not assertions. Switch a subset to a
  framework like µunit / Unity / cmocka and assert behaviour
  (rendering output checked against a golden image, geometry asserts
  on layouts, etc.).

- **[M] CI on push (GitHub Actions).**
  Compile + smoke test on Windows, Linux, macOS. Mocida is currently
  Windows-only de facto - CI would prove cross-platform claims.

- **[S] `pkg-config` / CMake package config.**
  Today consumers `add_subdirectory(mocida)` only. A proper
  `install()` and `mocidaConfig.cmake` make the library
  installable / findable.

- **[M] Fuzzing.**
  Pipe random asset paths / text into the loaders. Catches issues
  like the FXAA pixel-format bug we hit earlier.

- **[S] Code coverage with gcov / OpenCppCoverage.**
  Helpful to see which branches actually run during tests.

- **[L] cross-compilation validated.**
  macOS/Linux builds, Wayland support, package on flatpak / brew /
  homebrew tap.

---

## P3 — Persistence / runtime introspection

- **[M] Widget tree serialization.**
  Save / load layouts as JSON or a compact binary. Powers tools like
  an in-app editor / live reload.

- **[M] Hot reload.**
  Watch assets/ and source files; reload textures or recompile UI on
  change. Big DX win during development.

- **[M] Debug overlay (F12).**
  Toggleable HUD showing FPS, widget count, memory stats, last AA
  mode, etc. Independent of `OnFps` callback so any app gets it for
  free.

- **[L] Inspector.**
  Click-to-pick a widget at runtime and edit its properties live.
  Combine with hot reload for full devloop bliss.

---

## Bug-tail / minor cleanup

Things that aren't urgent but should be cleaned up the next time
their area is touched.

- **[S]** `RenderSingleWidget` is monolithic (~300 LOC). Split each
  widget type into its own static `RenderRect`, `RenderText`, etc.
  for parity with `RenderGrid` / `RenderScroll`.

- **[S]** `UICleanupAll` exists but is never called from anywhere -
  decide whether to expose it as part of the public API or remove.

- **[S]** `g_drawBorder`, `g_borderWidth`, `g_borderColor` globals at
  the top of `window.c` are dead. Remove.

- **[S]** `USE_BATCHED_RENDERING` is a `#define` but always 1.
  Either expose as a runtime option or remove the `#if` branches.

- **[S]** `UIWidget_SetAlignment(widget, UIAlignment value)` takes by
  value but the field is a pointer - it mallocs each call. Either
  store by value or assert non-NULL caller-owned input.

- **[S]** The `Doxyfile` has `EXTRACT_ALL = YES`, so every undocumented
  function shows up in the generated docs with no description. Flip
  to `NO` and rely on `WARN_NO_PARAMDOC` to surface what's still
  undocumented.

- **[S]** `tests/test_aa.c`'s custom run loop reimplements frame
  pacing inline. Replace with `UIApp_Run` now that motion-mask TAA
  removes most of the demo's reason to handle the loop manually.

- **[M]** Dead-code block in `src/uikit/window.c` from the render
  extraction was removed but there are still stray `g_*` globals
  for AA / borders that should be cleaned up.

- **[S]** `setup.ps1` re-uses `Invoke-ClonePinned` for SDL pins but
  bypasses it for mimalloc / vcpkg (those use `git clone --depth 1`
  inline). Extract a single helper.

---

## Suggested next-up order

If I had to pick the next three things to ship:

1. **`UITextField`** + keyboard dispatch + focus tracking. Without this
   Mocida is a "viewer" not an "app" framework.
2. **Animation / tween system**. Cheap to build (1 small file) and
   unlocks transitions, spinner, and a dozen other niceties.
3. **Theme primitives + dark/light mode**. Lifts everything visually
   without forcing every callsite to specify colours.

Everything else can wait; those three pay off across the entire
catalogue of widgets.
