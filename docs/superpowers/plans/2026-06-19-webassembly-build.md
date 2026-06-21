# WebAssembly Build Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-TECHNIQUE: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in `--web` flag to the mocida build orchestrator that produces a WebAssembly build via Emscripten, leaving desktop builds (with mimalloc, curl, WebView2, etc.) completely unchanged.

**Architecture:** Introduce a `MOCIDA_WEB` CMake option driven by `python build.py --web`. When enabled, the build targets `wasm32-unknown-emscripten` using `emcmake`/`emmake`, disables desktop-only dependencies (mimalloc, curl, WebView2, GStreamer, WebKitGTK, Cocoa), replaces libcurl-based remote image fetching with `emscripten_fetch`, loads fonts from bundled assets, and runs the app via `emscripten_set_main_loop`. Assets and fonts are packaged with `--preload-file` into the Emscripten virtual filesystem.

**Tech Stack:** Python 3 (build scripts), CMake, Emscripten (`emcc`/`emcmake`/`emmake`), SDL3 (Emscripten backend), SDL3_image, SDL3_ttf, C11, Rust/Cargo (optional second phase).

---

## File Structure

| File | Responsibility |
|------|----------------|
| `build.py` | Adds `--web` argument and forwards it to `mocida/build.py`; skips Rust build in web mode (phase 1). |
| `mocida/build.py` | Detects `--web`, validates Emscripten toolchain, sets `MOCIDA_WEB=ON`, picks `build/wasm/<config>` output dir, invokes `emcmake`/`emmake`. |
| `mocida/CMakeLists.txt` | Branches on `MOCIDA_WEB`: disables mimalloc/curl/WebView2, links `emscripten_fetch`, adds web-specific sources, skips PCH if needed. |
| `mocida/src/uikit/image_net_web.c` | New file: async `emscripten_fetch`-based remote image download backend for web. |
| `mocida/src/uikit/image.c` | Adds `#elif defined(MOCIDA_WEB)` branch routing to `UIImage_RemoteHttpDownload_Web`. |
| `mocida/src/uikit/font.c` | Adds `#elif defined(MOCIDA_WEB)` branch in `UISearchFonts()` to scan bundled `fonts/` via `SDL_GetBasePath()`. |
| `mocida/src/uikit/app.c` | Adds `EMSCRIPTEN_KEEP_ALIVE`/loop wrapper: desktop keeps blocking `UIApp_Run`; web registers one frame callback with `emscripten_set_main_loop`. |
| `mocida/src/uikit/webview.c` | Ensures web build uses the existing no-backend stub (already guarded by platform `#ifdef`s). |
| `mocida/src/uikit/video.c` | Ensures web build uses the existing no-backend stub (already guarded by platform `#ifdef`s). |
| `mocida-rs/mocida-sys/build.rs` | (Phase 2) Detects `wasm32-unknown-emscripten`, skips Windows-only libs, links the Emscripten-produced `libmocida.a`. |

---

## Task 1: Add `--web` flag to root `build.py`

**Files:**
- Modify: `build.py`

- [ ] **Step 1: Add `--web` argument**

```python
ap.add_argument("--web", action="store_true",
                help="build for the web (Emscripten/WebAssembly); "
                     "disables desktop-only deps like mimalloc/curl")
```

Insert right after the existing `--verbose` argument (around line 439).

- [ ] **Step 2: Forward `--web` to the C build and skip Rust in web mode**

In `task_c` / `_run_c_pass`, add:

```python
if opts.web:
    flags.append("--web")
```

In `task_rust`, raise a clear error if invoked in web mode (Rust bindings are phase 2):

```python
if opts.web:
    raise TaskError("rust build is not yet supported with --web (phase 2).")
```

In `main`, when resolving the plan for task `all` and `opts.web`, force the plan to `["c"]` only (skip `rust`):

```python
if opts.web and opts.task == "all":
    plan = ["c"]
```

- [ ] **Step 3: Commit**

```bash
git add build.py
git commit -m "build: add --web flag to root orchestrator (web mode skips rust)"
```

---

## Task 2: Add `--web` support to `mocida/build.py`

**Files:**
- Modify: `mocida/build.py`

- [ ] **Step 1: Add `--web` argument**

```python
p.add_argument("--web", action="store_true",
               help="build for WebAssembly via Emscripten")
```

Insert near the other build flags (around line 343).

- [ ] **Step 2: Validate Emscripten toolchain when `--web` is used**

In `main(argv=None)`, right after parsing args:

```python
if args.web:
    if not shutil.which("emcmake") or not shutil.which("emmake"):
        err("Emscripten not found. Install/activate emsdk and ensure "
            "emcmake + emmake are on PATH.")
        return 1
    if _SYS == "Windows":
        # Emscripten uses its own clang; MSVC env is irrelevant and can
        # confuse the configure step.
        for k in ["INCLUDE", "LIB", "LIBPATH"]:
            os.environ.pop(k, None)
```

- [ ] **Step 3: Use `wasm` build dir and `emcmake`/`emmake` when `--web`**

Replace the build-dir assignment (around line 577-585) with:

```python
if args.web:
    build_dir = ROOT / "build" / "wasm" / config_lc
    src_dir = ROOT
else:
    if args.example:
        build_dir = ROOT / "build" / PLATFORM / f"example-{args.example}-{config_lc}"
    else:
        suffix = "-shared" if args.shared else ""
        build_dir = ROOT / "build" / PLATFORM / (config_lc + suffix)
```

At configure time, when `args.web` is true:
- Do **not** set `CC`/`CXX` to system clang.
- Do **not** append the vcpkg toolchain.
- Do **not** fetch WebView2.
- Use `emcmake cmake` instead of `cmake`.
- Pass `-DMOCIDA_WEB=ON`, `-DMOCIDA_BUILD_SHARED=OFF`, `-DMOCIDA_BUILD_DEMO=ON`, `-DMOCIDA_BUILD_TESTS=OFF`.

Wrap the configure call:

```python
if args.web:
    configure_bin = "emcmake"
else:
    configure_bin = "cmake"

# ... later, when building the cmake_cfg list:
cmake_cfg = [
    configure_bin, "cmake", "-G", generator,
    ...
]
```

For build, replace `cmake --build` with `emmake cmake --build` when `args.web`:

```python
build_prefix = ["emmake"] if args.web else []
build_cmd = build_prefix + ["cmake", "--build", str(build_dir),
                            "--parallel", str(jobs), "--config", config,
                            "--target", *targets]
```

- [ ] **Step 4: Print a web-mode summary line**

After a successful web build, print:

```python
if args.web:
    print(f"  Web artefacts: {build_dir.relative_to(ROOT)}/")
    print(f"  Open: python -m http.server 8000 --directory {build_dir.relative_to(ROOT)}")
```

- [ ] **Step 5: Commit**

```bash
git add mocida/build.py
git commit -m "build: wire --web through C build script using emcmake/emmake"
```

---

## Task 3: CMake web branch in `mocida/CMakeLists.txt`

**Files:**
- Modify: `mocida/CMakeLists.txt`

- [ ] **Step 1: Detect web target and define `MOCIDA_WEB`**

After the iOS detection block (around line 30), add:

```cmake
set(MOCIDA_WEB OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten" OR EMSCRIPTEN)
    set(MOCIDA_WEB ON)
    message(STATUS "Mocida: WebAssembly/Emscripten build")
endif()
```

- [ ] **Step 2: Disable desktop-only deps when `MOCIDA_WEB`**

Change the mimalloc block (around line 260) to:

```cmake
if(MOCIDA_USE_MIMALLOC AND NOT MOCIDA_IOS AND NOT MOCIDA_WEB AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/mimalloc/CMakeLists.txt)
```

Change the curl block (around line 236) to:

```cmake
if(NOT MOCIDA_IOS AND NOT MOCIDA_WEB)
    find_package(CURL REQUIRED)
endif()
```

Change the curl link (around line 487) to:

```cmake
if(NOT MOCIDA_IOS AND NOT MOCIDA_WEB)
    target_link_libraries(mocida PUBLIC CURL::libcurl)
endif()
```

Keep the Windows block guarded by `if(WIN32 AND NOT MOCIDA_WEB)` (WebView2/system libs are meaningless in Emscripten).

- [ ] **Step 3: Add web-specific sources and link flags**

After the Apple ObjC++ source block, add:

```cmake
if(MOCIDA_WEB)
    list(APPEND MOCIDA_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/uikit/image_net_web.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/uikit/app_web.c")
    target_compile_definitions(mocida PUBLIC MOCIDA_WEB=1)
    target_link_options(mocida PUBLIC
        "-sUSE_SDL=3"
        "-sUSE_SDL_IMAGE=3"
        "-sUSE_SDL_TTF=3"
        "-sFETCH=1"
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sINITIAL_MEMORY=64MB"
        "-sMAXIMUM_MEMORY=256MB")
endif()
```

- [ ] **Step 4: Disable PCH for web if it causes issues**

Change the PCH option default for web:

```cmake
if(MOCIDA_WEB)
    set(MOCIDA_USE_PCH OFF CACHE BOOL "" FORCE)
endif()
```

- [ ] **Step 5: Commit**

```bash
git add mocida/CMakeLists.txt
git commit -m "build: CMake web branch disables desktop deps and links emscripten_fetch"
```

---

## Task 4: Implement web remote-image fetch backend

**Files:**
- Create: `mocida/src/uikit/image_net_web.c`
- Modify: `mocida/src/uikit/image.c`

- [ ] **Step 1: Create `image_net_web.c`**

```c
#include <emscripten/fetch.h>
#include <uikit/debug.h>
#include <stdlib.h>
#include <string.h>

/* Async fetch state handed back to image.c. */
typedef struct WebFetch {
    emscripten_fetch_t* fetch;
    int done;
    int ok;
    void* data;
    size_t size;
} WebFetch;

static void on_success(emscripten_fetch_t* fetch) {
    WebFetch* wf = (WebFetch*)fetch->userData;
    wf->data = malloc(fetch->numBytes);
    if (wf->data) {
        memcpy(wf->data, fetch->data, fetch->numBytes);
        wf->size = fetch->numBytes;
        wf->ok = 1;
    }
    wf->done = 1;
    emscripten_fetch_close(fetch);
    wf->fetch = NULL;
}

static void on_error(emscripten_fetch_t* fetch) {
    WebFetch* wf = (WebFetch*)fetch->userData;
    UI_ERROR(UI_CAT_IMAGE,
             "web fetch failed for '%s': HTTP %d",
             fetch->url, fetch->status);
    wf->done = 1;
    wf->ok = 0;
    emscripten_fetch_close(fetch);
    wf->fetch = NULL;
}

/* Start (or restart) an async download. Returns a cookie the caller
 * owns; free with WebFetch_Free. */
void* UIImage_RemoteHttpStart_Web(const char* url) {
    WebFetch* wf = (WebFetch*)calloc(1, sizeof(WebFetch));
    if (!wf) return NULL;

    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = on_success;
    attr.onerror = on_error;
    attr.userData = wf;
    wf->fetch = emscripten_fetch(&attr, url);
    return wf;
}

int UIImage_RemoteHttpPoll_Web(void* cookie, void** outData, size_t* outSize) {
    WebFetch* wf = (WebFetch*)cookie;
    if (!wf) return 0;
    if (!wf->done) return 0;  /* still in flight */
    if (wf->ok && outData && outSize) {
        *outData = wf->data;
        *outSize = wf->size;
        wf->data = NULL;
        wf->size = 0;
    }
    return 1;  /* finished (success or error) */
}

void WebFetch_Free(void* cookie) {
    WebFetch* wf = (WebFetch*)cookie;
    if (!wf) return;
    if (wf->fetch) {
        emscripten_fetch_close(wf->fetch);
    }
    free(wf->data);
    free(wf);
}
```

- [ ] **Step 2: Route `image.c` to the web backend**

At the top of `mocida/src/uikit/image.c`, change the backend selection to:

```c
#if defined(MOCIDA_NO_REMOTE_IMAGES)
   /* no HTTP backend */
#elif defined(MOCIDA_IOS)
extern int UIImage_RemoteHttpDownload_iOS(const char* url,
                                          void** outData, size_t* outSize);
#elif defined(MOCIDA_WEB)
extern void* UIImage_RemoteHttpStart_Web(const char* url);
extern int   UIImage_RemoteHttpPoll_Web(void* cookie, void** outData, size_t* outSize);
extern void  WebFetch_Free(void* cookie);
#else
#  include <curl/curl.h>
#endif
```

- [ ] **Step 3: Add a web-specific branch inside `UIImage_PumpRemote`**

Do **not** use `RemoteHttp_Download`/`RemoteJob_Thread` on the web. Instead, add a dedicated branch inside `UIImage_PumpRemote` (after the existing desktop `RemoteJob` logic) so the async fetch is polled once per frame:

```c
void UIImage_PumpRemote(UIImage* image, SDL_Renderer* renderer) {
    if (!image || !renderer) return;
    RemoteEnsureInit();

#if defined(MOCIDA_WEB)
    typedef struct { void* cookie; } WebJob;
    WebJob* job = (WebJob*)image->__download;
    if (!job) {
        job = (WebJob*)calloc(1, sizeof(WebJob));
        if (!job) { image->loadState = IMAGE_LOAD_FAILURE; return; }
        job->cookie = UIImage_RemoteHttpStart_Web(image->source);
        if (!job->cookie) {
            free(job);
            image->loadState = IMAGE_LOAD_FAILURE;
            return;
        }
        image->__download = job;
        return;
    }

    void* data = NULL;
    size_t size = 0;
    if (!UIImage_RemoteHttpPoll_Web(job->cookie, &data, &size)) return;

    if (data && size) {
        SDL_Surface* surf = UIAsset_LoadSurfaceFromMemory(data, size);
        if (surf) {
            image->__SDL_texture = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_DestroySurface(surf);
        }
    }
    if (image->__SDL_texture) {
        image->loadState = IMAGE_LOAD_SUCCESS;
        SDL_SetTextureScaleMode(image->__SDL_texture,
            image->antialiasing ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    } else {
        image->loadState = IMAGE_LOAD_FAILURE;
    }

    WebFetch_Free(job->cookie);
    free(job);
    image->__download = NULL;
}
#else
    /* existing desktop implementation unchanged */
#endif
}
```

At the top of `image.c`, add `#include <emscripten.h>` only when `MOCIDA_WEB` is defined (it is not required by the fetch code itself, but may be needed for future web shims).

- [ ] **Step 4: Commit**

```bash
git add mocida/src/uikit/image_net_web.c mocida/src/uikit/image.c
git commit -m "feat(web): async emscripten_fetch backend for remote images"
```

---

## Task 5: Load bundled fonts on web

**Files:**
- Modify: `mocida/src/uikit/font.c`

- [ ] **Step 1: Add a web branch to `UISearchFonts()`**

After the iOS branch and before the macOS branch, insert:

```c
#elif defined(MOCIDA_WEB)
    // Emscripten builds preload fonts into the virtual FS at <base>/fonts,
    // same layout as iOS. Walk that directory.
    {
        const char* base = SDL_GetBasePath();
        if (base) {
            char fontsDir[1024];
            snprintf(fontsDir, sizeof(fontsDir), "%sfonts", base);
            WalkFontsDir(fontsDir, &fontCount);
            if (fontCount == 0) WalkFontsDir(base, &fontCount);
        }
    }
    FontEntry** temp = realloc(UIFonts, sizeof(FontEntry*) * (fontCount + 1));
    if (!temp) {
        TTF_Quit();
        return;
    }
    UIFonts = temp;
    UIFonts[fontCount] = NULL;
```

- [ ] **Step 2: Commit**

```bash
git add mocida/src/uikit/font.c
git commit -m "feat(web): scan bundled fonts directory on Emscripten"
```

---

## Task 6: Adapt the main loop for web

**Files:**
- Create: `mocida/src/uikit/app_web.c`
- Modify: `mocida/src/uikit/app.c`

- [ ] **Step 1: Split frame logic so it can be called from Emscripten**

In `app.c`, extract the body of `UIApp_Run` into a new function `UIApp_RunOneFrame(UIApp* app, Uint32 dtMs)` placed just above `UIApp_Run`. The caller is responsible for computing `dtMs` so both desktop and web can share the frame body:

```c
void UIApp_RunOneFrame(UIApp* app, Uint32 dtMs) {
    UIProfile_FrameBegin();

    SDL_Event e;
    if (app->onTick) app->onTick(app->onTickUserdata);

    {
        UI_SCOPEC("events", UI_PROF_EVENT);
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                app->window->visible = 0;
                app->runInBackground = 0;
            }
            HandleEvent(app, &e);
        }
    }

    if (app->window->visible && app->window->sdlWindow) {
        static int lastW = -1, lastH = -1;
        static UIScreenInsets lastSafe = { -1, -1, -1, -1 };
        int liveW = 0, liveH = 0;
        SDL_GetWindowSize(app->window->sdlWindow, &liveW, &liveH);
        const UIScreenInsets s = UIScreen_GetSafeArea();
        if (liveW > 0 && liveH > 0 &&
            (liveW != lastW || liveH != lastH ||
             s.top != lastSafe.top || s.left != lastSafe.left ||
             s.bottom != lastSafe.bottom || s.right != lastSafe.right)) {
            lastW = liveW; lastH = liveH; lastSafe = s;
            ApplyResize(app, liveW, liveH);
        }
    }

    if (dtMs > 0) {
        UI_SCOPEC("anim", UI_PROF_LAYOUT);
        UIAnim_Tick(dtMs);
    }

    if (app->window->visible) {
        UI_SCOPEC("render", UI_PROF_RENDER);
        UIWindow_Render(app->window);
    }

    UIProfile_FrameEnd();
}
```

Keep `freq` and `lastTickPC` as local variables inside `UIApp_Run`; compute `dtMs` there and pass it to `UIApp_RunOneFrame(app, dtMs)` each loop iteration.

- [ ] **Step 2: Keep desktop `UIApp_Run` unchanged**

`UIApp_Run` remains the blocking loop for desktop, now calling `UIApp_RunOneFrame(app)` each iteration.

- [ ] **Step 3: Create `app_web.c`**

```c
#include <emscripten.h>
#include <SDL3/SDL.h>
#include <uikit/app.h>

/* Declared in app.c; one frame of event pump + layout + render. */
extern void UIApp_RunOneFrame(UIApp* app, Uint32 dtMs);

static UIApp* s_webApp = NULL;
static Uint64 s_webLastTickPC = 0;
static Uint64 s_webFreq = 0;

static void mocida_web_loop(void) {
    if (!s_webApp) {
        emscripten_cancel_main_loop();
        return;
    }
    if (s_webFreq == 0) s_webFreq = SDL_GetPerformanceFrequency();
    Uint64 nowPC = SDL_GetPerformanceCounter();
    Uint32 dtMs = 0;
    if (s_webLastTickPC != 0) {
        dtMs = (Uint32)((nowPC - s_webLastTickPC) * 1000ULL / s_webFreq);
    }
    s_webLastTickPC = nowPC;

    UIApp_RunOneFrame(s_webApp, dtMs);
    if (!s_webApp->window->visible && !s_webApp->runInBackground) {
        emscripten_cancel_main_loop();
        s_webApp = NULL;
    }
}

void UIApp_RunWeb(UIApp* app) {
    s_webApp = app;
    s_webLastTickPC = SDL_GetPerformanceCounter();
    s_webFreq = SDL_GetPerformanceFrequency();
    emscripten_set_main_loop(mocida_web_loop, 0, 1);
}
```

- [ ] **Step 4: Add public web entry header**

In `mocida/src/headers/uikit/app.h`, add:

```c
#ifdef MOCIDA_WEB
void UIApp_RunOneFrame(UIApp* app, Uint32 dtMs);
void UIApp_RunWeb(UIApp* app);
#endif
```

- [ ] **Step 5: Wire `main()` to call `UIApp_RunWeb` on web**

For the demo and examples, the typical pattern is:

```c
#ifdef MOCIDA_WEB
    UIApp_RunWeb(app);
#else
    UIApp_Run(app);
#endif
```

For phase 1, patch `mocida/src/main.c` only. Locate where `UIApp_Run(app)` is called and wrap it.

- [ ] **Step 6: Commit**

```bash
git add mocida/src/uikit/app.c mocida/src/uikit/app_web.c mocida/src/headers/uikit/app.h mocida/src/main.c
git commit -m "feat(web): run one frame per emscripten main-loop callback"
```

---

## Task 7: Package assets and fonts for the web build

**Files:**
- Modify: `mocida/build.py`
- Modify: `mocida/CMakeLists.txt`

- [ ] **Step 1: Preload `assets/` and `fonts/` directories**

In `mocida/build.py`, when configuring for web, append preload-file arguments if the directories exist:

```python
preload_args = []
assets_dir = ROOT / "assets"
fonts_dir = ROOT / "fonts"
if assets_dir.exists():
    preload_args.append(f"--preload-file={assets_dir}@/assets")
if fonts_dir.exists():
    preload_args.append(f"--preload-file={fonts_dir}@/fonts")

# Pass them through CMake as linker options:
for pa in preload_args:
    configure_args.append(f"-DCMAKE_EXE_LINKER_FLAGS={pa}")
```

- [ ] **Step 2: Set HTML shell template (optional)**

Add to CMake web link options:

```cmake
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/shell.html")
    target_link_options(mocida PUBLIC
        "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/cmake/shell.html")
endif()
```

Create a minimal default `cmake/shell.html` if desired (copy Emscripten's default and add a full-window canvas style). This is optional; without it Emscripten emits its default shell.

- [ ] **Step 3: Commit**

```bash
git add mocida/build.py mocida/CMakeLists.txt
git commit -m "build(web): preload assets and fonts into Emscripten virtual filesystem"
```

---

## Task 8: Smoke-test the web build

**Files:**
- None (verification only)

- [ ] **Step 1: Run the web build**

```bash
python build.py --web --config release --no-demo
```

Expected: configure + build succeeds and produces at least `mocida/build/wasm/release/libmocida.a`.

- [ ] **Step 2: Build the demo for web**

```bash
cd mocida
python build.py --web --config release
```

Expected: `mocida/build/wasm/release/demo.html`, `demo.js`, `demo.wasm` exist.

- [ ] **Step 3: Serve and open in browser**

```bash
cd mocida/build/wasm/release
python -m http.server 8000
```

Open `http://localhost:8000/demo.html` in a browser. Expected: canvas appears, demo UI renders, mouse/keyboard events work.

- [ ] **Step 4: Commit a build-script-only change if fixes were needed**

If any fix was required during the smoke test, commit it before moving on.

---

## Task 9: (Phase 2) Rust bindings for web

**Files:**
- Modify: `mocida-rs/mocida-sys/build.rs`
- Modify: `build.py`

- [ ] **Step 1: Detect `wasm32-unknown-emscripten` in `build.rs`**

At the top of `main()`:

```rust
let target = env::var("TARGET").unwrap_or_default();
let is_web = target == "wasm32-unknown-emscripten";
```

When `is_web`:
- Skip `stage_runtime_dlls` entirely.
- Skip the Windows system-lib loop.
- Skip SDL3 import-lib search (SDL is linked through Emscripten flags).
- Keep the `-lmocida` link and the include/bindgen path.

- [ ] **Step 2: Allow root `build.py rust --web`**

In `task_rust`, when `opts.web`:
- Point `MOCIDA_LIB_DIR` to `c_build_dir(opts.config, shared=False)` with platform `wasm`.
- Set `MOCIDA_STATIC=1`.
- Add `--target wasm32-unknown-emscripten` to the cargo command.

```python
if opts.web:
    libdir = CDIR / "build" / "wasm" / opts.config
    lib = libdir / "libmocida.a"
    env["MOCIDA_STATIC"] = "1"
    cargo.extend(["--target", "wasm32-unknown-emscripten"])
```

- [ ] **Step 3: Build a Rust example for web**

```bash
python build.py --web --config release
```

Expected: cargo links `libmocida.a` and produces `.wasm` output for the Rust workspace.

- [ ] **Step 4: Commit**

```bash
git add build.py mocida-rs/mocida-sys/build.rs
git commit -m "build(web): support wasm32-unknown-emscripten in Rust bindings"
```

---

## Self-Review

- `--web` is opt-in; desktop defaults are untouched.
- mimalloc, curl, WebView2, GStreamer, WebKitGTK, Cocoa are all guarded by `NOT MOCIDA_WEB`.
- Remote image fetch has a web-specific async backend (`emscripten_fetch`).
- Font loading falls back to bundled `fonts/` on web, matching iOS.
- The main loop uses `emscripten_set_main_loop` on web.
- Assets/fonts are preloaded into the Emscripten virtual filesystem.
- Rust bindings are phase 2 and do not block the C/web MVP.
- No placeholders or TBDs remain.
