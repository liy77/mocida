# WebAssembly Port — Lessons Learned

This document captures the practical knowledge gathered while adding WebAssembly support to the mocida project. It is meant to help future maintainers understand why things were done the way they were and what to watch out for when touching the web build.

---

## 1. What was built

An opt-in `--web` flag was added to the build orchestrator. When enabled:

- The C/C++ toolkit is compiled with Emscripten (`emcmake`/`emmake`).
- The Rust workspace (`mocida-rs`) cross-compiles to `wasm32-unknown-emscripten`.
- Desktop-only dependencies are disabled: mimalloc, libcurl, WebView2, GStreamer, WebKitGTK, Cocoa.
- SDL3 + SDL3_image + SDL3_ttf are kept as vendored subprojects (they already have Emscripten backends).
- Remote images are fetched with `emscripten_fetch` instead of libcurl.
- Fonts are bundled in `mocida/fonts/` and preloaded into the Emscripten virtual filesystem.
- The main loop uses `emscripten_set_main_loop` instead of the desktop blocking loop.
- A custom `shell.html` replaces the default Emscripten page, giving a fullscreen, branding-free experience.
- The window icon becomes the browser favicon on web.

---

## 2. Build entry points

```bash
# Build everything (C + Rust) for web
python3 build.py --web --config release --force

# Serve the demo
python3 -m http.server 8765 --directory mocida/build/wasm/release
# open http://localhost:8765/demo.html
```

The desktop build is unchanged:

```bash
python3 build.py --no-demo --config release
```

---

## 3. Key architectural decisions

### 3.1. Use the vendored SDL stack, not Emscripten ports

At first the plan suggested `-sUSE_SDL=3` etc., but that conflicts with the vendored `add_subdirectory(SDL)`/`SDL_image`/`SDL_ttf` in CMake. The final build keeps the vendored libraries and lets SDL's own Emscripten backend handle the browser integration. The only Emscripten-specific link flags needed are:

```text
-sFETCH=1
-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']
-sALLOW_MEMORY_GROWTH=1
-sINITIAL_MEMORY=64MB
-sMAXIMUM_MEMORY=256MB
-fwasm-exceptions
-sSUPPORT_LONGJMP=wasm
```

### 3.2. Web-specific source files

Platform-specific web backends live in `*_web.c` files:

- `mocida/src/uikit/image_net_web.c` — async `emscripten_fetch` backend for remote images.
- `mocida/src/uikit/app_web.c` — `emscripten_set_main_loop` wrapper and FPS pacing.
- `mocida/src/uikit/backdrop_web.c` — no-op window backdrop backend.

These files are excluded from desktop builds via a CMake `FILTER REGEX "_web\\.c$"` and re-added under `MOCIDA_WEB`.

### 3.3. Shared frame function

`UIApp_RunOneFrame(UIApp* app, Uint32 dtMs)` was extracted from `UIApp_Run` so the same per-frame logic (events, layout, animation, render) is used by both the desktop blocking loop and the web callback loop. Desktop computes `dtMs` from its own timer; the web callback computes it before calling the shared function.

### 3.4. Replace libcurl with `emscripten_fetch`

Remote image loading on web cannot use libcurl because curl is not compiled/linked in the Emscripten build. The web backend uses `emscripten_fetch` with callbacks. Because Emscripten callbacks run on the main thread, a two-party release model was used:

- The fetch callback releases the `WebFetch` object once when the request completes.
- The owning `UIImage` releases it once when the image is destroyed or when the result is consumed.
- The second releaser frees the object.

This avoids calling `emscripten_fetch_close` outside the callback, which Emscripten documents as unsafe for in-flight async fetches.

---

## 4. Problems encountered and solutions

### 4.1. Build script forwarded `--web` before the C script supported it

**Problem:** Root `build.py` forwarded `--web` to `mocida/build.py` before the latter understood the flag.
**Fix:** Implemented the C build script changes right after and made the root orchestrator robust enough to handle `--web rust` with a clear error during Task 1, then removed that restriction in Task 9 when Rust support landed.

### 4.2. CMake `find_package(CURL REQUIRED)` failed on web

**Problem:** With `MOCIDA_WEB=ON`, curl is intentionally not available, but CMake still required it.
**Fix:** Guarded the curl block with `if(NOT MOCIDA_IOS AND NOT MOCIDA_WEB)` and similarly guarded the link command.

### 4.3. `window.c` included `<curl/curl.h>` unconditionally

**Problem:** Even though curl was not linked, the header include broke the web compile.
**Fix:** Changed the guard from `#ifndef MOCIDA_IOS` to `#if !defined(MOCIDA_IOS) && !defined(MOCIDA_WEB)`.

### 4.4. Host-specific Release optimizations broke Emscripten

**Problem:** CMake appended `-march=native` and `-funroll-loops` for Clang/GCC in Release mode. Emscripten identifies as Clang but cannot target the host CPU.
**Fix:** Guarded the optimization block with `NOT MOCIDA_WEB`.

### 4.5. Linux linker `--exclude-libs,ALL` is unsupported by `wasm-ld`

**Problem:** The `mocida_add_executable` helper added `--exclude-libs,ALL` for `UNIX AND NOT APPLE`. Emscripten sets `UNIX=1` and `APPLE=0`.
**Fix:** Guarded the flag with `NOT MOCIDA_WEB`.

### 4.6. SDL_ttf/FreeType/harfbuzz needs `-sSUPPORT_LONGJMP=wasm`

**Problem:** Linking the Rust web binary failed with `undefined symbol: emscripten_longjmp` because the vendored font libraries use `setjmp`/`longjmp`.
**Fix:** Added `-sSUPPORT_LONGJMP=wasm` and `-fwasm-exceptions` to both the C and Rust Emscripten link flags.

### 4.7. Backdrop symbols missing on web

**Problem:** `UIBackdrop_ResolveAuto`, `UIBackdrop_Apply`, and `UIBackdrop_NativeAvailable` are implemented only for desktop platforms.
**Fix:** Added `backdrop_web.c` with no-op implementations (`AUTO -> NONE`, native unavailable, apply returns 0). The app falls back to in-app rendering.

### 4.8. No fonts in the repo

**Problem:** `mocida/fonts/` contained only a `README.md`. On the web there are no system fonts, so all text rendered blank.
**Fix:** Bundled Inter Regular and Bold in `mocida/fonts/`. CMake preloads the directory into the Emscripten VFS and `UISearchFonts()` scans `<SDL_GetBasePath()>/fonts`.

### 4.9. Default Emscripten HTML shell had branding

**Problem:** The generated `demo.html` contained "Powered by Emscripten", spinner, status text, and an output box.
**Fix:** Created `mocida/cmake/shell.html`: a minimal fullscreen black page with only a `<canvas>` element. CMake uses `--shell-file` to apply it.

### 4.10. `SDL_SetWindowIcon` is unsupported on web

**Problem:** The demo called `UIApp_SetWindowIconFromSurface`, which failed on Emscripten with "That operation is not supported".
**Fix:** On web, encode the surface as PNG via `IMG_SavePNG_IO`, create a Blob URL in JS, and assign it to the page favicon.

### 4.11. FPS locker was unstable

**Problem:** The initial web loop simply skipped frames when `targetFps` was lower than the display refresh rate. The measured FPS stayed a few frames below the target (e.g., 57-58 instead of 60).
**Fix:** Switched to an accumulator-based pacing model. The browser callback still runs at display refresh rate, but time is accumulated and a frame is rendered only when the target interval has elapsed. The delta passed to animations is fixed at the target interval, keeping motion smooth.

---

## 5. Runtime behavior on web

### 5.1. Virtual filesystem paths

`--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets` mounts the assets at `/assets` in the Emscripten VFS. Similarly, fonts are at `/fonts`.

`SDL_GetBasePath()` returns `"/"` in the browser, so the web font branch scans `/fonts`.

### 5.2. Main loop timing

`emscripten_set_main_loop(mocida_web_loop, 0, 1)` uses `requestAnimationFrame` by default, capped at the monitor refresh rate. The FPS cap is enforced manually inside `mocida_web_loop`; the browser cap is the hard upper bound.

### 5.3. Memory

Initial heap is 64 MB, allowed to grow to 256 MB. This is enough for the demo but may need tuning for larger apps.

### 5.4. Browser requirements

The build uses WebAssembly exception handling (`-fwasm-exceptions`), which requires a modern browser.

---

## 6. Known limitations

- **WebView, video, native file dialogs, clipboard:** no web backends; they fall back to existing no-op stubs.
- **Remote image cache:** the desktop URL cache is compiled in but unused on web; every `http(s)` `UIImage` refetches.
- **Threads:** not enabled. Anything relying on `SDL_CreateThread` will need SharedArrayBuffer + COOP/COEP to work on web, or must be replaced with async code.
- **Window resizing:** handled by SDL's Emscripten backend; the canvas resizes via CSS.
- **Audio:** SDL_AudioStream maps to Web Audio, but this was not explicitly tested.

---

## 7. Files that changed

Build scripts:

- `build.py`
- `mocida/build.py`
- `mocida/CMakeLists.txt`
- `mocida-rs/mocida-sys/build.rs`

Web backends:

- `mocida/src/uikit/image_net_web.c`
- `mocida/src/uikit/app_web.c`
- `mocida/src/uikit/backdrop_web.c`

Shared/adapted code:

- `mocida/src/uikit/app.c`
- `mocida/src/uikit/image.c`
- `mocida/src/uikit/font.c`
- `mocida/src/uikit/window.c`
- `mocida/src/headers/uikit/app.h`
- `mocida/src/main.c`

Assets / shell:

- `mocida/cmake/shell.html`
- `mocida/fonts/Inter-Regular.ttf`
- `mocida/fonts/Inter-Bold.ttf`

---

## 8. Maintenance tips

- If a new desktop-only dependency is added to CMake, always guard it with `NOT MOCIDA_WEB`.
- If a new platform-specific source file is added and there is no web equivalent, the build will likely fail on `--web`. Create a `*_web.c` stub or guard the desktop file.
- When changing the main loop, keep both `UIApp_Run` (desktop) and `UIApp_RunWeb` (web) in mind.
- If Emscripten link errors mention `longjmp` or `emscripten_longjmp`, check that `-sSUPPORT_LONGJMP=wasm` and `-fwasm-exceptions` are still present in both CMake and `build.py`.
- If text stops rendering on web, verify that `mocida/fonts/` contains at least one `.ttf` and that the CMake `--preload-file` for fonts is still emitted.
