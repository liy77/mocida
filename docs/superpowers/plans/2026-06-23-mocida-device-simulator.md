# Device Simulator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a device simulator to `mui-dev` that renders mocida apps inside realistic iPhone/Galaxy frames, with safe-area simulation, orientation switching, and correct mouse input mapping.

**Architecture:** The simulator state lives in `UIWindow` as an opaque pointer. `UIWindow_Render` redirects widget rendering to a device-sized SDL render-target, then draws the device frame and the rendered texture onto the host window. The public API is exposed through `UIApp_*` wrappers. `mui-dev` selects a profile from Rust, sizes the host window accordingly, and enables the simulator before the first build.

**Tech Stack:** C11 / SDL3 / mocida-sys (bindgen) / Rust / Pillow (for placeholder frame assets).

---

## File Structure

| File | Responsibility |
|------|----------------|
| `mocida/src/headers/uikit/device_simulator.h` | Public C API: profile, orientation, enable/disable, coordinate transform. |
| `mocida/src/uikit/device_simulator.c` | Implementation of simulator state, render-target management, frame drawing, coordinate math. |
| `mocida/src/uikit/device_simulator_internal.h` | Internal functions called by `app.c` and `window.c`. |
| `mocida/src/headers/uikit/app.h` | Include `device_simulator.h` so consumers see the API. |
| `mocida/src/headers/uikit/window.h` | Add `void* __deviceSimulator` to `UIWindow`. |
| `mocida/src/uikit/app.c` | Wrappers, input transform, layout sizing, R-key orientation toggle, cleanup. |
| `mocida/src/uikit/window.c` | `UIWindow_Render` hook, `UIScreen_GetSafeArea` override. |
| `mocida/src/uikit/screen.c` | `UIScreen_GetSize` override when simulator is active. |
| `mocida/tests/test_device_simulator.c` | Headless-ish C test: enable simulator, verify sizes, draw a frame. |
| `mocida-rs/mui-runtime/src/device_profiles.rs` | Rust constants for all supported devices + lookup helper. |
| `mocida-rs/mui-runtime/src/lib.rs` | Add module + expose `Screen.safeTop/Left/Bottom/Right`. |
| `mocida-rs/mocida/src/device_simulator.rs` | Safe Rust wrappers (`DeviceProfile`, `DeviceOrientation`, `App` methods). |
| `mocida-rs/mocida/src/lib.rs` | Expose the new `device_simulator` module. |
| `mocida-rs/mui-dev/src/main.rs` | Parse `--device`, `--device-scale`, `--landscape`; enable simulator; fix host window size. |
| `mocida-rs/assets/devices/generate_placeholders.py` | Generate frame PNGs for the MVP device list. |

---

## Task 1: Public Header + Stub Implementation

**Files:**
- Create: `mocida/src/headers/uikit/device_simulator.h`
- Create: `mocida/src/uikit/device_simulator.c`
- Create: `mocida/src/uikit/device_simulator_internal.h`

- [ ] **Step 1: Write `device_simulator.h`**

```c
#ifndef UIKIT_DEVICE_SIMULATOR_H
#define UIKIT_DEVICE_SIMULATOR_H

#include <SDL3/SDL.h>

typedef enum {
    UI_DEVICE_ORIENTATION_PORTRAIT = 0,
    UI_DEVICE_ORIENTATION_LANDSCAPE
} UIDeviceOrientation;

typedef struct {
    const char* name;
    int width;
    int height;
    int safeTop;
    int safeBottom;
    int safeLeft;
    int safeRight;
    float scale;
    int frameScreenX;
    int frameScreenY;
    int frameScreenW;
    int frameScreenH;
    int frameW;
    int frameH;
} UIDeviceProfile;

typedef struct UIApp UIApp;

#ifdef __cplusplus
extern "C" {
#endif

void UIApp_EnableDeviceSimulator(UIApp* app, const UIDeviceProfile* profile,
                                 SDL_Texture* frameTexture, float hostScale,
                                 UIDeviceOrientation orientation);
void UIApp_EnableDeviceSimulatorFromFile(UIApp* app, const UIDeviceProfile* profile,
                                         const char* framePath, float hostScale,
                                         UIDeviceOrientation orientation);
void UIApp_DisableDeviceSimulator(UIApp* app);
int  UIApp_IsDeviceSimulatorEnabled(UIApp* app);
const UIDeviceProfile* UIApp_GetCurrentDeviceProfile(UIApp* app);
UIDeviceOrientation UIApp_GetCurrentDeviceOrientation(UIApp* app);
void UIApp_SetDeviceOrientation(UIApp* app, UIDeviceOrientation orientation);

void UIApp_DeviceSimulatorWindowToDevice(UIApp* app, float wx, float wy,
                                         float* dx, float* dy);
void UIApp_DeviceSimulatorDeviceToWindow(UIApp* app, float dx, float dy,
                                         float* wx, float* wy);

#ifdef __cplusplus
}
#endif

#endif
```

- [ ] **Step 2: Write the internal header `device_simulator_internal.h`**

```c
#ifndef DEVICE_SIMULATOR_INTERNAL_H
#define DEVICE_SIMULATOR_INTERNAL_H

#include <uikit/device_simulator.h>
#include <uikit/screen.h>
#include <SDL3/SDL.h>

typedef struct UIWindow UIWindow;
typedef struct UIDeviceSimulatorState UIDeviceSimulatorState;

void UIWindow_EnableDeviceSimulator(UIWindow* window,
                                    const UIDeviceProfile* profile,
                                    SDL_Texture* frameTexture,
                                    int ownsFrameTexture,
                                    float hostScale,
                                    UIDeviceOrientation orientation);
void UIWindow_DisableDeviceSimulator(UIWindow* window);
int  UIWindow_IsDeviceSimulatorEnabled(UIWindow* window);
UIDeviceSimulatorState* UIWindow_GetDeviceSimulatorState(UIWindow* window);

void UIWindow_DeviceSimulatorGetDeviceSize(UIWindow* window, int* w, int* h);
void UIWindow_DeviceSimulatorGetSafeArea(UIWindow* window, UIScreenInsets* out);
SDL_Texture* UIWindow_DeviceSimulatorGetRenderTarget(UIWindow* window);
float UIWindow_DeviceSimulatorGetHostScale(UIWindow* window);

void UIWindow_DeviceSimulatorSetOrientation(UIWindow* window,
                                            UIDeviceOrientation orientation);
void UIWindow_DeviceSimulatorToggleOrientation(UIWindow* window);

void UIWindow_DeviceSimulatorWindowToDevice(UIWindow* window,
                                            float wx, float wy,
                                            float* dx, float* dy);
void UIWindow_DeviceSimulatorDeviceToWindow(UIWindow* window,
                                            float dx, float dy,
                                            float* wx, float* wy);

void UIWindow_DeviceSimulatorPresentFrame(UIWindow* window,
                                          int hostW, int hostH);

#endif
```

- [ ] **Step 3: Write a stub `device_simulator.c` that compiles**

```c
#include "device_simulator_internal.h"
#include <uikit/app.h>
#include <uikit/window.h>

int UIWindow_IsDeviceSimulatorEnabled(UIWindow* window) {
    (void)window;
    return 0;
}

void UIApp_EnableDeviceSimulator(UIApp* app, const UIDeviceProfile* profile,
                                 SDL_Texture* frameTexture, float hostScale,
                                 UIDeviceOrientation orientation) {
    (void)app; (void)profile; (void)frameTexture; (void)hostScale; (void)orientation;
}

void UIApp_EnableDeviceSimulatorFromFile(UIApp* app, const UIDeviceProfile* profile,
                                         const char* framePath, float hostScale,
                                         UIDeviceOrientation orientation) {
    (void)app; (void)profile; (void)framePath; (void)hostScale; (void)orientation;
}

void UIApp_DisableDeviceSimulator(UIApp* app) { (void)app; }
int  UIApp_IsDeviceSimulatorEnabled(UIApp* app) { (void)app; return 0; }
const UIDeviceProfile* UIApp_GetCurrentDeviceProfile(UIApp* app) { (void)app; return NULL; }
UIDeviceOrientation UIApp_GetCurrentDeviceOrientation(UIApp* app) { (void)app; return UI_DEVICE_ORIENTATION_PORTRAIT; }
void UIApp_SetDeviceOrientation(UIApp* app, UIDeviceOrientation orientation) { (void)app; (void)orientation; }
void UIApp_DeviceSimulatorWindowToDevice(UIApp* app, float wx, float wy, float* dx, float* dy) { (void)app; *dx=wx; *dy=wy; }
void UIApp_DeviceSimulatorDeviceToWindow(UIApp* app, float dx, float dy, float* wx, float* wy) { (void)app; *wx=dx; *wy=dy; }
```

- [ ] **Step 4: Add `#include <uikit/device_simulator.h>` to `mocida/src/headers/uikit/app.h`**

Place it after `#include <uikit/screen.h>` (around line 17).

- [ ] **Step 5: Add `void* __deviceSimulator;` to `UIWindow` in `mocida/src/headers/uikit/window.h`**

Add after `int backdropNative;` (around line 78).

- [ ] **Step 6: Build the C library to check compilation**

Run: `./build.sh c --force`

Expected: builds without errors.

- [ ] **Step 7: Commit**

```bash
git add mocida/src/headers/uikit/device_simulator.h \
        mocida/src/uikit/device_simulator.c \
        mocida/src/uikit/device_simulator_internal.h \
        mocida/src/headers/uikit/app.h \
        mocida/src/headers/uikit/window.h
git commit -m "feat(device-simulator): add public header, stubs, and window slot"
```

---

## Task 2: Core Simulator State (Enable / Disable / Orientation / Render Target)

**Files:**
- Modify: `mocida/src/uikit/device_simulator.c`

- [ ] **Step 1: Replace the stub with the full state implementation**

```c
#include "device_simulator_internal.h"
#include <uikit/app.h>
#include <uikit/window.h>
#include <uikit/asset.h>
#include <stdlib.h>
#include <string.h>

struct UIDeviceSimulatorState {
    UIDeviceProfile profile;
    UIDeviceOrientation orientation;
    SDL_Texture* frameTexture;
    SDL_Texture* renderTarget;
    float hostScale;
    int ownsFrameTexture;
};

static UIDeviceSimulatorState* GetState(UIWindow* window) {
    return window ? (UIDeviceSimulatorState*)window->__deviceSimulator : NULL;
}

static void GetDeviceSize(const UIDeviceSimulatorState* state, int* w, int* h) {
    if (!state || !w || !h) return;
    if (state->orientation == UI_DEVICE_ORIENTATION_LANDSCAPE) {
        *w = state->profile.height;
        *h = state->profile.width;
    } else {
        *w = state->profile.width;
        *h = state->profile.height;
    }
}

static void GetSafeArea(const UIDeviceSimulatorState* state, UIScreenInsets* out) {
    if (!state || !out) return;
    const UIDeviceProfile* p = &state->profile;
    if (state->orientation == UI_DEVICE_ORIENTATION_LANDSCAPE) {
        out->top    = p->safeRight;
        out->bottom = p->safeLeft;
        out->left   = p->safeTop;
        out->right  = p->safeBottom;
    } else {
        out->top    = p->safeTop;
        out->bottom = p->safeBottom;
        out->left   = p->safeLeft;
        out->right  = p->safeRight;
    }
}

static int RecreateRenderTarget(UIWindow* window, UIDeviceSimulatorState* state) {
    SDL_Renderer* r = window ? window->sdlRenderer : NULL;
    if (!r || !state) return 0;
    if (state->renderTarget) {
        SDL_DestroyTexture(state->renderTarget);
        state->renderTarget = NULL;
    }
    int w = 0, h = 0;
    GetDeviceSize(state, &w, &h);
    state->renderTarget = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_TARGET, w, h);
    if (!state->renderTarget) return 0;
    SDL_SetTextureScaleMode(state->renderTarget, SDL_SCALEMODE_LINEAR);
    return 1;
}

static void DestroyState(UIDeviceSimulatorState* state) {
    if (!state) return;
    if (state->renderTarget) SDL_DestroyTexture(state->renderTarget);
    if (state->frameTexture && state->ownsFrameTexture) SDL_DestroyTexture(state->frameTexture);
    free((void*)state->profile.name);
    free(state);
}

void UIWindow_EnableDeviceSimulator(UIWindow* window,
                                    const UIDeviceProfile* profile,
                                    SDL_Texture* frameTexture,
                                    int ownsFrameTexture,
                                    float hostScale,
                                    UIDeviceOrientation orientation) {
    if (!window || !profile) return;
    UIWindow_DisableDeviceSimulator(window);
    UIDeviceSimulatorState* state = (UIDeviceSimulatorState*)calloc(1, sizeof(*state));
    if (!state) return;
    state->profile = *profile;
    state->profile.name = profile->name ? strdup(profile->name) : NULL;
    state->orientation = orientation;
    state->frameTexture = frameTexture;
    state->ownsFrameTexture = ownsFrameTexture ? 1 : 0;
    state->hostScale = hostScale > 0.0f ? hostScale : 1.0f;
    if (!RecreateRenderTarget(window, state)) {
        DestroyState(state);
        return;
    }
    window->__deviceSimulator = state;
}

void UIWindow_DisableDeviceSimulator(UIWindow* window) {
    if (!window) return;
    UIDeviceSimulatorState* state = GetState(window);
    window->__deviceSimulator = NULL;
    DestroyState(state);
}

int UIWindow_IsDeviceSimulatorEnabled(UIWindow* window) {
    return GetState(window) != NULL;
}

UIDeviceSimulatorState* UIWindow_GetDeviceSimulatorState(UIWindow* window) {
    return GetState(window);
}

void UIWindow_DeviceSimulatorGetDeviceSize(UIWindow* window, int* w, int* h) {
    GetDeviceSize(GetState(window), w, h);
}

void UIWindow_DeviceSimulatorGetSafeArea(UIWindow* window, UIScreenInsets* out) {
    GetSafeArea(GetState(window), out);
}

SDL_Texture* UIWindow_DeviceSimulatorGetRenderTarget(UIWindow* window) {
    UIDeviceSimulatorState* state = GetState(window);
    return state ? state->renderTarget : NULL;
}

float UIWindow_DeviceSimulatorGetHostScale(UIWindow* window) {
    UIDeviceSimulatorState* state = GetState(window);
    return state ? state->hostScale : 1.0f;
}

void UIWindow_DeviceSimulatorSetOrientation(UIWindow* window,
                                            UIDeviceOrientation orientation) {
    UIDeviceSimulatorState* state = GetState(window);
    if (!state || state->orientation == orientation) return;
    state->orientation = orientation;
    RecreateRenderTarget(window, state);
}

void UIWindow_DeviceSimulatorToggleOrientation(UIWindow* window) {
    UIDeviceSimulatorState* state = GetState(window);
    if (!state) return;
    UIWindow_DeviceSimulatorSetOrientation(window,
        state->orientation == UI_DEVICE_ORIENTATION_PORTRAIT
            ? UI_DEVICE_ORIENTATION_LANDSCAPE
            : UI_DEVICE_ORIENTATION_PORTRAIT);
}

// UIApp wrappers
void UIApp_EnableDeviceSimulator(UIApp* app, const UIDeviceProfile* profile,
                                 SDL_Texture* frameTexture, float hostScale,
                                 UIDeviceOrientation orientation) {
    if (!app || !app->window) return;
    UIWindow_EnableDeviceSimulator(app->window, profile, frameTexture, 0,
                                   hostScale, orientation);
}

void UIApp_EnableDeviceSimulatorFromFile(UIApp* app, const UIDeviceProfile* profile,
                                         const char* framePath, float hostScale,
                                         UIDeviceOrientation orientation) {
    if (!app || !app->window) return;
    SDL_Texture* tex = NULL;
    if (framePath) {
        tex = UIAsset_LoadTexture(app->window->sdlRenderer, framePath);
    }
    UIWindow_EnableDeviceSimulator(app->window, profile, tex, 1,
                                   hostScale, orientation);
}

void UIApp_DisableDeviceSimulator(UIApp* app) {
    if (!app) return;
    UIWindow_DisableDeviceSimulator(app->window);
}

int UIApp_IsDeviceSimulatorEnabled(UIApp* app) {
    return app && app->window && UIWindow_IsDeviceSimulatorEnabled(app->window);
}

const UIDeviceProfile* UIApp_GetCurrentDeviceProfile(UIApp* app) {
    UIDeviceSimulatorState* state = app && app->window ? GetState(app->window) : NULL;
    return state ? &state->profile : NULL;
}

UIDeviceOrientation UIApp_GetCurrentDeviceOrientation(UIApp* app) {
    UIDeviceSimulatorState* state = app && app->window ? GetState(app->window) : NULL;
    return state ? state->orientation : UI_DEVICE_ORIENTATION_PORTRAIT;
}

void UIApp_SetDeviceOrientation(UIApp* app, UIDeviceOrientation orientation) {
    if (!app || !app->window) return;
    UIWindow_DeviceSimulatorSetOrientation(app->window, orientation);
}
```

- [ ] **Step 2: Build the C library**

Run: `./build.sh c --force`

Expected: compiles.

- [ ] **Step 3: Commit**

```bash
git add mocida/src/uikit/device_simulator.c
git commit -m "feat(device-simulator): state, enable/disable, orientation, render target"
```

---

## Task 3: Coordinate Transform

**Files:**
- Modify: `mocida/src/uikit/device_simulator.c`

- [ ] **Step 1: Append the transform and frame-drawing functions**

```c
void UIWindow_DeviceSimulatorWindowToDevice(UIWindow* window,
                                            float wx, float wy,
                                            float* dx, float* dy) {
    UIDeviceSimulatorState* state = GetState(window);
    if (!state || !dx || !dy) { if (dx) *dx = wx; if (dy) *dy = wy; return; }
    const UIDeviceProfile* p = &state->profile;
    const float scale = state->hostScale;
    int hostW = 0, hostH = 0;
    if (window && window->sdlWindow) SDL_GetWindowSize(window->sdlWindow, &hostW, &hostH);
    float cx = hostW * 0.5f;
    float cy = hostH * 0.5f;

    int cosA = (state->orientation == UI_DEVICE_ORIENTATION_LANDSCAPE) ? 0 : 1;
    int sinA = (state->orientation == UI_DEVICE_ORIENTATION_LANDSCAPE) ? 1 : 0;

    float fx = p->frameW * 0.5f + ((wx - cx) * cosA - (wy - cy) * sinA) / scale;
    float fy = p->frameH * 0.5f + ((wx - cx) * sinA + (wy - cy) * cosA) / scale;

    int dw = 0, dh = 0;
    GetDeviceSize(state, &dw, &dh);
    *dx = (fx - (float)p->frameScreenX) * ((float)dw / (float)p->frameScreenW);
    *dy = (fy - (float)p->frameScreenY) * ((float)dh / (float)p->frameScreenH);
}

void UIWindow_DeviceSimulatorDeviceToWindow(UIWindow* window,
                                            float dx, float dy,
                                            float* wx, float* wy) {
    UIDeviceSimulatorState* state = GetState(window);
    if (!state || !wx || !wy) { if (wx) *wx = dx; if (wy) *wy = dy; return; }
    const UIDeviceProfile* p = &state->profile;
    const float scale = state->hostScale;
    int dw = 0, dh = 0;
    GetDeviceSize(state, &dw, &dh);
    float fx = (float)p->frameScreenX + dx * ((float)p->frameScreenW / (float)dw);
    float fy = (float)p->frameScreenY + dy * ((float)p->frameScreenH / (float)dh);

    int hostW = 0, hostH = 0;
    if (window && window->sdlWindow) SDL_GetWindowSize(window->sdlWindow, &hostW, &hostH);
    float cx = hostW * 0.5f;
    float cy = hostH * 0.5f;

    int cosA = (state->orientation == UI_DEVICE_ORIENTATION_LANDSCAPE) ? 0 : 1;
    int sinA = (state->orientation == UI_DEVICE_ORIENTATION_LANDSCAPE) ? 1 : 0;

    *wx = cx + scale * ((fx - p->frameW * 0.5f) * cosA + (fy - p->frameH * 0.5f) * sinA);
    *wy = cy + scale * (-(fx - p->frameW * 0.5f) * sinA + (fy - p->frameH * 0.5f) * cosA);
}

static void DrawFallbackBezel(SDL_Renderer* r, int hostW, int hostH) {
    SDL_FRect full = { 0, 0, (float)hostW, (float)hostH };
    SDL_SetRenderDrawColor(r, 30, 30, 35, 255);
    SDL_RenderFillRect(r, &full);
    SDL_SetRenderDrawColor(r, 60, 60, 70, 255);
    SDL_RenderRect(r, &full);
}

void UIWindow_DeviceSimulatorPresentFrame(UIWindow* window, int hostW, int hostH) {
    UIDeviceSimulatorState* state = GetState(window);
    if (!state) return;
    SDL_Renderer* r = window->sdlRenderer;
    const UIDeviceProfile* p = &state->profile;
    const float scale = state->hostScale;
    float cx = hostW * 0.5f;
    float cy = hostH * 0.5f;
    double angle = state->orientation == UI_DEVICE_ORIENTATION_LANDSCAPE ? 90.0 : 0.0;
    SDL_FPoint center = { cx, cy };

    SDL_FRect frameDst = {
        cx - p->frameW * scale * 0.5f,
        cy - p->frameH * scale * 0.5f,
        p->frameW * scale,
        p->frameH * scale
    };
    SDL_FRect screenDst = {
        cx + (p->frameScreenX - p->frameW * 0.5f) * scale,
        cy + (p->frameScreenY - p->frameH * 0.5f) * scale,
        p->frameScreenW * scale,
        p->frameScreenH * scale
    };

    if (state->frameTexture) {
        SDL_RenderTextureRotated(r, state->frameTexture, NULL, &frameDst,
                                 angle, &center, SDL_FLIP_NONE);
    } else {
        DrawFallbackBezel(r, hostW, hostH);
    }
    SDL_RenderTextureRotated(r, state->renderTarget, NULL, &screenDst,
                             angle, &center, SDL_FLIP_NONE);
}
```

- [ ] **Step 2: Build**

Run: `./build.sh c --force`

Expected: compiles.

- [ ] **Step 3: Commit**

```bash
git add mocida/src/uikit/device_simulator.c
git commit -m "feat(device-simulator): coordinate transform and frame drawing"
```

---

## Task 4: Metrics Overrides (`UIApp_GetWidth/Height`, `UIScreen`, Safe Area)

**Files:**
- Modify: `mocida/src/uikit/app.c`
- Modify: `mocida/src/uikit/window.c`
- Modify: `mocida/src/uikit/screen.c`

- [ ] **Step 1: Update `UIApp_GetWidth` / `UIApp_GetHeight` in `mocida/src/uikit/app.c`**

```c
int UIApp_GetWidth(UIApp* app) {
    if (!app || !app->window) return 0;
    if (UIWindow_IsDeviceSimulatorEnabled(app->window)) {
        int w = 0, h = 0;
        UIWindow_DeviceSimulatorGetDeviceSize(app->window, &w, &h);
        return w;
    }
    return app->window->width;
}

int UIApp_GetHeight(UIApp* app) {
    if (!app || !app->window) return 0;
    if (UIWindow_IsDeviceSimulatorEnabled(app->window)) {
        int w = 0, h = 0;
        UIWindow_DeviceSimulatorGetDeviceSize(app->window, &w, &h);
        return h;
    }
    return app->window->height;
}
```

- [ ] **Step 2: Update `UIScreen_GetSize` in `mocida/src/uikit/screen.c`**

```c
#include <uikit/window.h>
#include "device_simulator_internal.h"

UIScreenSize UIScreen_GetSize(void) {
    UIWindow* active = UIWindow_GetActive();
    if (active && UIWindow_IsDeviceSimulatorEnabled(active)) {
        UIScreenSize s = {0, 0};
        UIWindow_DeviceSimulatorGetDeviceSize(active, &s.width, &s.height);
        return s;
    }
    UIScreenSize s = { 0, 0 };
    SDL_DisplayID disp = SDL_GetPrimaryDisplay();
    if (disp != 0) {
        SDL_Rect bounds;
        if (SDL_GetDisplayBounds(disp, &bounds)) {
            s.width  = bounds.w;
            s.height = bounds.h;
        }
    }
    return s;
}
```

- [ ] **Step 3: Update `UIScreen_GetSafeArea` in `mocida/src/uikit/window.c`**

Replace the body (around line 649) with:

```c
UIScreenInsets UIScreen_GetSafeArea(void) {
    UIScreenInsets ins = {0, 0, 0, 0};
    if (!g_activeWindow || !g_activeWindow->sdlWindow) return ins;
    if (UIWindow_IsDeviceSimulatorEnabled(g_activeWindow)) {
        UIWindow_DeviceSimulatorGetSafeArea(g_activeWindow, &ins);
        return ins;
    }
    SDL_Rect r;
    if (SDL_GetWindowSafeArea(g_activeWindow->sdlWindow, &r) && r.w > 0 && r.h > 0) {
        int ww = 0, wh = 0;
        SDL_GetWindowSize(g_activeWindow->sdlWindow, &ww, &wh);
        ins.top    = r.y;
        ins.bottom = wh - (r.y + r.h);
        ins.left   = r.x;
        ins.right  = ww - (r.x + r.w);
    }
    return ins;
}
```

- [ ] **Step 4: Include `device_simulator_internal.h` in `mocida/src/uikit/window.c`**

Add near the top with the other internal includes.

- [ ] **Step 5: Build**

Run: `./build.sh c --force`

Expected: compiles.

- [ ] **Step 6: Commit**

```bash
git add mocida/src/uikit/app.c mocida/src/uikit/window.c mocida/src/uikit/screen.c
git commit -m "feat(device-simulator): metric overrides for size and safe area"
```

---

## Task 5: Render-to-Texture Hook in `UIWindow_Render`

**Files:**
- Modify: `mocida/src/uikit/window.c`

- [ ] **Step 1: Rewrite the render loop body to support the simulator**

The diff below is for `UIWindow_Render` (`mocida/src/uikit/window.c:356`). Replace the size-query + logical-presentation block (lines 368-380) with:

```c
    int rw = 0, rh = 0;
    SDL_GetWindowSize(window->sdlWindow, &rw, &rh);
    int hostW = rw, hostH = rh;
    const int sim = UIWindow_IsDeviceSimulatorEnabled(window);
    if (sim) {
        UIWindow_DeviceSimulatorGetDeviceSize(window, &rw, &rh);
    }
    if (rw > 0 && rh > 0) {
        SDL_SetRenderLogicalPresentation(window->sdlRenderer, rw, rh,
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
    }
```

- [ ] **Step 2: Replace the offscreen target selection block**

After the `needsOffscreen` calculation, replace the existing `if (needsOffscreen) { ... } else { ... }` (lines 427-452) with:

```c
    SDL_Texture* simTarget = sim ? UIWindow_DeviceSimulatorGetRenderTarget(window) : NULL;

    if (needsOffscreen) {
        const int tw = rw * aaScale;
        const int th = rh * aaScale;
        if (!g_smoothTexture || g_smoothW != tw || g_smoothH != th) {
            if (g_smoothTexture) SDL_DestroyTexture(g_smoothTexture);
            g_smoothTexture = SDL_CreateTexture(window->sdlRenderer,
                SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, tw, th);
            if (!g_smoothTexture) {
                UI_ERROR(UI_CAT_RENDER, "failed to create AA target texture: %s", SDL_GetError());
                return -1;
            }
            g_smoothW = tw;
            g_smoothH = th;
            SDL_SetTextureScaleMode(g_smoothTexture, SDL_SCALEMODE_LINEAR);
            g_taaHistoryReady = 0;
        }
        SDL_SetRenderTarget(window->sdlRenderer, g_smoothTexture);
        SDL_SetRenderScale(window->sdlRenderer, (float)aaScale, (float)aaScale);
    } else {
        SDL_SetRenderTarget(window->sdlRenderer, simTarget);
        SDL_SetRenderScale(window->sdlRenderer, 1.0f, 1.0f);
    }
```

- [ ] **Step 3: Replace the offscreen resolve block**

Replace the existing post-process / blit block (lines 490-543) with:

```c
    if (needsOffscreen) {
        if (g_aaMode == 4) {
            FxaaPass(window->sdlRenderer, g_smoothTexture, g_smoothW, g_smoothH);
        } else if (g_aaMode == 5) {
            TaaPass(window->sdlRenderer, g_smoothTexture, g_smoothW, g_smoothH);
        }
        SDL_SetRenderTarget(window->sdlRenderer, simTarget);
        SDL_SetRenderScale(window->sdlRenderer, 1.0f, 1.0f);
        if (clearA == 0) {
            SDL_SetRenderDrawColor(window->sdlRenderer, 0, 0, 0, 0);
        } else {
            SDL_SetRenderDrawColor(window->sdlRenderer,
                (Uint8)window->backgroundColor.r,
                (Uint8)window->backgroundColor.g,
                (Uint8)window->backgroundColor.b,
                255);
        }
        SDL_RenderClear(window->sdlRenderer);
        SDL_SetTextureScaleMode(g_smoothTexture, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(g_smoothTexture, SDL_BLENDMODE_NONE);
        g_tempRect1.x = 0; g_tempRect1.y = 0;
        g_tempRect1.w = (float)rw; g_tempRect1.h = (float)rh;
        SDL_RenderTexture(window->sdlRenderer, g_smoothTexture, NULL, &g_tempRect1);
    }

    if (sim) {
        SDL_SetRenderTarget(window->sdlRenderer, NULL);
        SDL_SetRenderScale(window->sdlRenderer, 1.0f, 1.0f);
        SDL_SetRenderLogicalPresentation(window->sdlRenderer, hostW, hostH,
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
        SDL_SetRenderDrawColor(window->sdlRenderer, 30, 30, 30, 255);
        SDL_RenderClear(window->sdlRenderer);
        UIWindow_DeviceSimulatorPresentFrame(window, hostW, hostH);
    }
```

- [ ] **Step 4: Build**

Run: `./build.sh c --force`

Expected: compiles.

- [ ] **Step 5: Commit**

```bash
git add mocida/src/uikit/window.c
git commit -m "feat(device-simulator): render-to-texture hook in UIWindow_Render"
```

---

## Task 6: Input Transform and Orientation Hotkey

**Files:**
- Modify: `mocida/src/uikit/app.c`

- [ ] **Step 1: Add a helper to transform coords and include the internal header**

Add `#include "device_simulator_internal.h"` near the top of `mocida/src/uikit/app.c`.

Add before `HandleEvent`:

```c
static void ApplySimulatorMouseTransform(UIApp* app, float* rx, float* ry) {
    if (app && app->window && UIWindow_IsDeviceSimulatorEnabled(app->window)) {
        float dx = 0, dy = 0;
        UIWindow_DeviceSimulatorWindowToDevice(app->window, *rx, *ry, &dx, &dy);
        *rx = dx;
        *ry = dy;
    }
}
```

- [ ] **Step 2: Apply the transform after every `WindowToRenderCoords` call**

In `HandleEvent`, after each of these blocks:

```c
WindowToRenderCoords(app, event->motion.x, event->motion.y, &rx, &ry);
ApplySimulatorMouseTransform(app, &rx, &ry);
```

Do this for:
- `SDL_EVENT_MOUSE_MOTION`
- `SDL_EVENT_MOUSE_BUTTON_DOWN`
- `SDL_EVENT_MOUSE_BUTTON_UP`
- `SDL_EVENT_MOUSE_WHEEL`
- `SDL_EVENT_DROP_POSITION`
- `SDL_EVENT_DROP_FILE`

- [ ] **Step 3: Add the R-key orientation toggle**

Inside `SDL_EVENT_KEY_DOWN`, after the `UIDebugOverlay_HandleScancode` block, add:

```c
if (UIWindow_IsDeviceSimulatorEnabled(app->window) &&
    event->key.key == SDLK_R) {
    UIWidget* focused = UIWidget_GetFocused(app->window->children);
    int typing = focused && (
        strcmp(focused->__widget_type, UI_WIDGET_TEXTFIELD) == 0 ||
        strcmp(focused->__widget_type, UI_WIDGET_TEXTAREA) == 0 ||
        strcmp(focused->__widget_type, UI_WIDGET_TEXT) == 0);
    if (!typing) {
        UIWindow_DeviceSimulatorToggleOrientation(app->window);
        const UIDeviceProfile* p = UIApp_GetCurrentDeviceProfile(app);
        UIDeviceOrientation o = UIApp_GetCurrentDeviceOrientation(app);
        float scale = UIWindow_DeviceSimulatorGetHostScale(app->window);
        int newW = (int)((o == UI_DEVICE_ORIENTATION_LANDSCAPE ? p->frameH : p->frameW) * scale);
        int newH = (int)((o == UI_DEVICE_ORIENTATION_LANDSCAPE ? p->frameW : p->frameH) * scale);
        SDL_SetWindowSize(app->window->sdlWindow, newW, newH);
        break;
    }
}
```

- [ ] **Step 4: Make `ApplyResize` use device size when simulator is active**

In `ApplyResize` (`mocida/src/uikit/app.c:392`), before setting `mainWidget` size:

```c
    int layoutW = new_width;
    int layoutH = new_height;
    if (UIWindow_IsDeviceSimulatorEnabled(app->window)) {
        UIWindow_DeviceSimulatorGetDeviceSize(app->window, &layoutW, &layoutH);
    }

    if (app->mainWidget) {
        UIWidget_SetSize(app->mainWidget, (float)layoutW, (float)layoutH);
    }
```

Leave `app->window->width/height` as the host window size.

- [ ] **Step 5: Disable simulator on window destroy**

In `UIWindow_Destroy` (`mocida/src/uikit/window.c:1078`), right after the null check:

```c
    UIWindow_DisableDeviceSimulator(window);
```

- [ ] **Step 6: Build**

Run: `./build.sh c --force`

Expected: compiles.

- [ ] **Step 7: Commit**

```bash
git add mocida/src/uikit/app.c mocida/src/uikit/window.c
git commit -m "feat(device-simulator): input transform and R-key rotation"
```

---

## Task 7: C Smoke Test

**Files:**
- Create: `mocida/tests/test_device_simulator.c`

- [ ] **Step 1: Write the test**

```c
#include <uikit/app.h>
#include <uikit/device_simulator.h>
#include <uikit/widget.h>
#include <uikit/rect.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UIApp* app = UIApp_Create("Device Sim Test", 200, 200);
    if (!app) { fprintf(stderr, "failed to create app\n"); return 1; }

    UIDeviceProfile profile = {
        .name = "testphone",
        .width = 393, .height = 852,
        .safeTop = 59, .safeBottom = 34,
        .safeLeft = 0, .safeRight = 0,
        .scale = 3.0f,
        .frameScreenX = 20, .frameScreenY = 70,
        .frameScreenW = 393, .frameScreenH = 852,
        .frameW = 433, .frameH = 972
    };

    UIApp_EnableDeviceSimulator(app, &profile, NULL, 1.0f, UI_DEVICE_ORIENTATION_PORTRAIT);
    UIApp_SetWindowSize(app, profile.frameW, profile.frameH);
    if (!UIApp_IsDeviceSimulatorEnabled(app)) {
        fprintf(stderr, "simulator not enabled\n");
        UIApp_Destroy(app);
        return 1;
    }

    printf("device size: %dx%d\n", UIApp_GetWidth(app), UIApp_GetHeight(app));
    if (UIApp_GetWidth(app) != 393 || UIApp_GetHeight(app) != 852) {
        fprintf(stderr, "unexpected device size\n");
        UIApp_Destroy(app);
        return 1;
    }

    const UIDeviceProfile* p = UIApp_GetCurrentDeviceProfile(app);
    if (!p || p->width != 393) {
        fprintf(stderr, "profile mismatch\n");
        UIApp_Destroy(app);
        return 1;
    }

    UIApp_SetDeviceOrientation(app, UI_DEVICE_ORIENTATION_LANDSCAPE);
    if (UIApp_GetCurrentDeviceOrientation(app) != UI_DEVICE_ORIENTATION_LANDSCAPE) {
        fprintf(stderr, "orientation did not change\n");
        UIApp_Destroy(app);
        return 1;
    }
    if (UIApp_GetWidth(app) != 852 || UIApp_GetHeight(app) != 393) {
        fprintf(stderr, "landscape size wrong\n");
        UIApp_Destroy(app);
        return 1;
    }

    float wx = 0, wy = 0;
    UIApp_DeviceSimulatorDeviceToWindow(app, 0.0f, 0.0f, &wx, &wy);
    printf("device (0,0) -> window (%.1f, %.1f)\n", wx, wy);

    UIApp_Destroy(app);
    printf("OK\n");
    return 0;
}
```

- [ ] **Step 2: Build and run the test**

Run:

```bash
./build.sh c --tests --force
./mocida/build/darwin/debug/test_device_simulator
```

Expected output:

```text
device size: 393x852
device (0,0) -> window (20.0, 70.0)
OK
```

- [ ] **Step 4: Commit**

```bash
git add mocida/tests/test_device_simulator.c
git commit -m "test(device-simulator): add C smoke test"
```

---

## Task 8: Rust Device Profiles

**Files:**
- Create: `mocida-rs/mui-runtime/src/device_profiles.rs`
- Modify: `mocida-rs/mui-runtime/src/lib.rs`

- [ ] **Step 1: Write `device_profiles.rs`**

```rust
//! Pre-defined device profiles for the mui-dev simulator.

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct DeviceProfile {
    pub name: &'static str,
    pub width: i32,
    pub height: i32,
    pub safe_top: i32,
    pub safe_bottom: i32,
    pub safe_left: i32,
    pub safe_right: i32,
    pub scale: f32,
    pub frame_screen_x: i32,
    pub frame_screen_y: i32,
    pub frame_screen_w: i32,
    pub frame_screen_h: i32,
    pub frame_w: i32,
    pub frame_h: i32,
}

impl DeviceProfile {
    /// Generic frame geometry: 20 px side bezels, 70 px top bezel for camera.
    const fn framed(
        name: &'static str,
        width: i32,
        height: i32,
        safe_top: i32,
        safe_bottom: i32,
        safe_left: i32,
        safe_right: i32,
        scale: f32,
    ) -> Self {
        let frame_w = width + 40;
        let frame_h = height + 120;
        Self {
            name,
            width,
            height,
            safe_top,
            safe_bottom,
            safe_left,
            safe_right,
            scale,
            frame_screen_x: 20,
            frame_screen_y: 70,
            frame_screen_w: width,
            frame_screen_h: height,
            frame_w,
            frame_h,
        }
    }
}

pub const IPHONE_15: DeviceProfile = DeviceProfile::framed(
    "iphone15", 393, 852, 59, 34, 0, 0, 3.0,
);
pub const IPHONE_15_PRO: DeviceProfile = DeviceProfile::framed(
    "iphone15pro", 393, 852, 59, 34, 0, 0, 3.0,
);
pub const IPHONE_15_PRO_MAX: DeviceProfile = DeviceProfile::framed(
    "iphone15promax", 430, 932, 59, 34, 0, 0, 3.0,
);
pub const IPHONE_16: DeviceProfile = DeviceProfile::framed(
    "iphone16", 393, 852, 59, 34, 0, 0, 3.0,
);
pub const GALAXY_S24: DeviceProfile = DeviceProfile::framed(
    "galaxy_s24", 360, 780, 24, 24, 0, 0, 3.0,
);
pub const GALAXY_S24_PLUS: DeviceProfile = DeviceProfile::framed(
    "galaxy_s24plus", 384, 832, 24, 24, 0, 0, 3.2,
);
pub const GALAXY_S25: DeviceProfile = DeviceProfile::framed(
    "galaxy_s25", 360, 780, 24, 24, 0, 0, 3.0,
);
pub const GALAXY_S25_ULTRA: DeviceProfile = DeviceProfile::framed(
    "galaxy_s25ultra", 384, 832, 24, 24, 0, 0, 3.2,
);

pub fn lookup(name: &str) -> Option<&'static DeviceProfile> {
    Some(match name {
        "iphone15" => &IPHONE_15,
        "iphone15pro" => &IPHONE_15_PRO,
        "iphone15promax" => &IPHONE_15_PRO_MAX,
        "iphone16" => &IPHONE_16,
        "galaxy_s24" => &GALAXY_S24,
        "galaxy_s24plus" => &GALAXY_S24_PLUS,
        "galaxy_s25" => &GALAXY_S25,
        "galaxy_s25ultra" => &GALAXY_S25_ULTRA,
        _ => return None,
    })
}
```

- [ ] **Step 2: Wire the module in `lib.rs`**

Add:

```rust
pub mod device_profiles;
```

near the other `pub mod` lines.

- [ ] **Step 3: Expose `Screen.safe*` members**

In `screen_member` (`mocida-rs/mui-runtime/src/lib.rs`), add cases:

```rust
match (ns.as_str(), field) {
    ("Screen", "safeTop") => Some(unsafe { mocida::sys::UIScreen_GetSafeArea().top }.to_string()),
    ("Screen", "safeBottom") => Some(unsafe { mocida::sys::UIScreen_GetSafeArea().bottom }.to_string()),
    ("Screen", "safeLeft") => Some(unsafe { mocida::sys::UIScreen_GetSafeArea().left }.to_string()),
    ("Screen", "safeRight") => Some(unsafe { mocida::sys::UIScreen_GetSafeArea().right }.to_string()),
    _ => None,
}
```

- [ ] **Step 4: Build the Rust runtime**

Run: `cargo build -p mui-runtime`

Expected: compiles.

- [ ] **Step 5: Commit**

```bash
git add mocida-rs/mui-runtime/src/device_profiles.rs mocida-rs/mui-runtime/src/lib.rs
git commit -m "feat(device-simulator): add Rust device profiles and Screen.safe* members"
```

---

## Task 9: Safe Rust Wrappers in `mocida`

**Files:**
- Create: `mocida-rs/mocida/src/device_simulator.rs`
- Modify: `mocida-rs/mocida/src/lib.rs`

- [ ] **Step 1: Write `device_simulator.rs`**

```rust
//! Device-simulator wrappers.

use std::ffi::{c_float, c_int, CString};
use mocida_sys as sys;
use crate::app::App;
use crate::error::{Error, Result};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum DeviceOrientation {
    Portrait = 0,
    Landscape = 1,
}

#[derive(Debug, Clone, Copy)]
pub struct DeviceProfile {
    pub name: &'static str,
    pub width: i32,
    pub height: i32,
    pub safe_top: i32,
    pub safe_bottom: i32,
    pub safe_left: i32,
    pub safe_right: i32,
    pub scale: f32,
    pub frame_screen_x: i32,
    pub frame_screen_y: i32,
    pub frame_screen_w: i32,
    pub frame_screen_h: i32,
    pub frame_w: i32,
    pub frame_h: i32,
}

impl App {
    /// Enable the device simulator using a placeholder bezel (no frame PNG).
    pub fn enable_device_simulator(&mut self,
                                   profile: &DeviceProfile,
                                   host_scale: f32,
                                   orientation: DeviceOrientation) -> &mut Self {
        let name = CString::new(profile.name).unwrap_or_default();
        let c = sys::UIDeviceProfile {
            name: name.as_ptr(),
            width: profile.width,
            height: profile.height,
            safeTop: profile.safe_top,
            safeBottom: profile.safe_bottom,
            safeLeft: profile.safe_left,
            safeRight: profile.safe_right,
            scale: profile.scale,
            frameScreenX: profile.frame_screen_x,
            frameScreenY: profile.frame_screen_y,
            frameScreenW: profile.frame_screen_w,
            frameScreenH: profile.frame_screen_h,
            frameW: profile.frame_w,
            frameH: profile.frame_h,
        };
        unsafe {
            sys::UIApp_EnableDeviceSimulator(self.as_ptr(), &c,
                                             std::ptr::null_mut(),
                                             host_scale,
                                             sys::UIDeviceOrientation(orientation as i32));
        }
        self
    }

    /// Enable the device simulator and load a frame PNG from `frame_path`.
    pub fn enable_device_simulator_with_frame(&mut self,
                                              profile: &DeviceProfile,
                                              frame_path: &str,
                                              host_scale: f32,
                                              orientation: DeviceOrientation) -> Result<&mut Self> {
        let name = CString::new(profile.name).unwrap_or_default();
        let path = CString::new(frame_path)?;
        let c = sys::UIDeviceProfile {
            name: name.as_ptr(),
            width: profile.width,
            height: profile.height,
            safeTop: profile.safe_top,
            safeBottom: profile.safe_bottom,
            safeLeft: profile.safe_left,
            safeRight: profile.safe_right,
            scale: profile.scale,
            frameScreenX: profile.frame_screen_x,
            frameScreenY: profile.frame_screen_y,
            frameScreenW: profile.frame_screen_w,
            frameScreenH: profile.frame_screen_h,
            frameW: profile.frame_w,
            frameH: profile.frame_h,
        };
        unsafe {
            sys::UIApp_EnableDeviceSimulatorFromFile(self.as_ptr(), &c,
                                                     path.as_ptr(),
                                                     host_scale,
                                                     sys::UIDeviceOrientation(orientation as i32));
        }
        Ok(self)
    }

    pub fn disable_device_simulator(&mut self) -> &mut Self {
        unsafe { sys::UIApp_DisableDeviceSimulator(self.as_ptr()); }
        self
    }

    pub fn set_device_orientation(&mut self, orientation: DeviceOrientation) -> &mut Self {
        unsafe {
            sys::UIApp_SetDeviceOrientation(self.as_ptr(),
                                            sys::UIDeviceOrientation(orientation as i32));
        }
        self
    }
}
```

- [ ] **Step 2: Expose the module in `mocida/src/lib.rs`**

Add:

```rust
pub mod device_simulator;
```

- [ ] **Step 3: Build**

Run: `cargo build -p mocida`

Expected: compiles.

- [ ] **Step 4: Commit**

```bash
git add mocida-rs/mocida/src/device_simulator.rs mocida-rs/mocida/src/lib.rs
git commit -m "feat(device-simulator): add safe Rust wrappers"
```

---

## Task 10: `mui-dev` CLI Integration

**Files:**
- Modify: `mocida-rs/mui-dev/src/main.rs`

- [ ] **Step 1: Add CLI fields**

Add to `CliOptions`:

```rust
    device: Option<String>,
    device_scale: f32,
    landscape: bool,
```

Add defaults in `CliOptions::default()` (or initialize inline):

```rust
impl Default for CliOptions {
    fn default() -> Self {
        Self {
            minimize: false,
            display: None,
            path: None,
            device: None,
            device_scale: 2.0,
            landscape: false,
        }
    }
}
```

- [ ] **Step 2: Parse the new flags**

In `parse_args`, add:

```rust
"--device" => opts.device = it.next(),
"--device-scale" => {
    if let Some(v) = it.next() {
        if let Ok(s) = v.parse::<f32>() {
            opts.device_scale = s.max(0.5);
        }
    }
}
"--landscape" => opts.landscape = true,
```

Update `--help` text to include them.

- [ ] **Step 3: Add the conversion helper**

```rust
fn apply_device_profile(
    app: &mut mocida::App,
    opts: &CliOptions,
) -> Result<(), Box<dyn std::error::Error>> {
    let Some(name) = &opts.device else { return Ok(()); };
    let Some(profile) = mui_runtime::device_profiles::lookup(name) else {
        return Err(format!("unknown device profile: {name}").into());
    };

    use mocida::device_simulator::{DeviceOrientation, DeviceProfile};

    let orientation = if opts.landscape {
        DeviceOrientation::Landscape
    } else {
        DeviceOrientation::Portrait
    };

    let window_w = ((if opts.landscape { profile.frame_h } else { profile.frame_w } as f32)
        * opts.device_scale) as i32;
    let window_h = ((if opts.landscape { profile.frame_w } else { profile.frame_h } as f32)
        * opts.device_scale) as i32;

    app.set_window_size(window_w, window_h)
        .set_min_size(window_w, window_h)
        .set_max_size(window_w, window_h);

    let rust_profile = DeviceProfile {
        name: profile.name,
        width: profile.width,
        height: profile.height,
        safe_top: profile.safe_top,
        safe_bottom: profile.safe_bottom,
        safe_left: profile.safe_left,
        safe_right: profile.safe_right,
        scale: profile.scale,
        frame_screen_x: profile.frame_screen_x,
        frame_screen_y: profile.frame_screen_y,
        frame_screen_w: profile.frame_screen_w,
        frame_screen_h: profile.frame_screen_h,
        frame_w: profile.frame_w,
        frame_h: profile.frame_h,
    };

    let assets_dir = std::env::current_dir()
        .unwrap_or_else(|_| std::path::PathBuf::from("."))
        .join("mocida-rs/assets/devices");
    let png = assets_dir.join(format!("{name}.png"));

    if png.exists() {
        app.enable_device_simulator_with_frame(&rust_profile, png.to_str().unwrap_or(""),
                                               opts.device_scale, orientation)?;
    } else {
        app.enable_device_simulator(&rust_profile, opts.device_scale, orientation);
    }
    Ok(())
}
```

- [ ] **Step 4: Call it after `App::new` and before `build()`**

```rust
    let mut app = App::new(&cfg.title, cfg.width, cfg.height)?;
    if let Err(e) = apply_device_profile(&mut app, &opts) {
        return Err(e);
    }
```

- [ ] **Step 5: Build and smoke-test**

Run:

```bash
cargo build -p mui-dev
cargo run -p mui-dev -- --device iphone15pro --device-scale 1.5 examples/hello.mui
```

Expected: window opens at the host-frame size; the sample renders inside the device frame.

- [ ] **Step 6: Commit**

```bash
git add mocida-rs/mui-dev/src/main.rs
git commit -m "feat(device-simulator): mui-dev CLI flags and activation"
```

---

## Task 11: Placeholder Frame Assets

**Files:**
- Create: `mocida-rs/assets/devices/generate_placeholders.py`
- Create: 8 PNG files under `mocida-rs/assets/devices/`

- [ ] **Step 1: Write the generator script**

```python
#!/usr/bin/env python3
"""Generate placeholder device frame PNGs with a transparent screen cutout."""
from PIL import Image
import os

DEVICES = [
    ("iphone15", 393, 852),
    ("iphone15pro", 393, 852),
    ("iphone15promax", 430, 932),
    ("iphone16", 393, 852),
    ("galaxy_s24", 360, 780),
    ("galaxy_s24plus", 384, 832),
    ("galaxy_s25", 360, 780),
    ("galaxy_s25ultra", 384, 832),
]

OUT = os.path.dirname(os.path.abspath(__file__))

for name, w, h in DEVICES:
    frame_w = w + 40
    frame_h = h + 120
    img = Image.new("RGBA", (frame_w, frame_h), (40, 40, 45, 255))
    # camera / speaker bar at top
    for y in range(40):
        for x in range(frame_w):
            img.putpixel((x, y), (30, 30, 35, 255))
    # transparent screen area
    screen = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    img.paste(screen, (20, 70))
    out_path = os.path.join(OUT, f"{name}.png")
    img.save(out_path)
    print(f"wrote {out_path}")
```

- [ ] **Step 2: Run the generator**

```bash
python3 mocida-rs/assets/devices/generate_placeholders.py
```

Expected: 8 PNG files created.

- [ ] **Step 3: Commit the script and generated assets**

```bash
git add mocida-rs/assets/devices/generate_placeholders.py mocida-rs/assets/devices/*.png
git commit -m "assets(device-simulator): add placeholder device frames"
```

---

## Task 12: End-to-End Validation

**Files:** none (manual validation)

- [ ] **Step 1: Build everything**

```bash
./build.sh c --tests --force
cargo build -p mui-dev
```

Expected: no errors.

- [ ] **Step 2: Run the C test**

```bash
./mocida/build/darwin/debug/test_device_simulator
```

Expected: prints sizes and `OK`.

- [ ] **Step 3: Visual smoke test with mui-dev**

```bash
cargo run -p mui-dev -- --device iphone15pro --device-scale 1.5 <path/to/sample>.mui
```

Check:
- The host window is sized like a phone frame.
- The MUI app renders inside the transparent screen area.
- Mouse clicks on buttons inside the screen work.
- Pressing `R` rotates the frame and the app re-layouts to landscape.

- [ ] **Step 4: Test fallback bezel**

Rename `mocida-rs/assets/devices/iphone15pro.png` temporarily and rerun. Expected: a plain bezel is drawn and the app still works.

- [ ] **Step 5: Commit a changelog note (optional)**

Add a line to `CHANGELOG.md` under the unreleased section.

- [ ] **Step 6: Final commit**

```bash
git add -A
git commit -m "feat(device-simulator): complete MVP integration"
```

---

## Self-Review

### Spec coverage

| Spec requirement | Task |
|------------------|------|
| Public C API (`UIApp_EnableDeviceSimulator`, orientation, transform) | Task 1, 2, 3 |
| State stored in `UIWindow` as opaque pointer | Task 1, 2 |
| Render-to-texture in `UIWindow_Render` | Task 5 |
| `UIApp_GetWidth/Height` return device size | Task 4 |
| `UIScreen_GetSize` / `UIScreen_GetSafeArea` override | Task 4 |
| Mouse input transformed | Task 6 |
| R-key orientation toggle | Task 6 |
| Rust device profiles | Task 8 |
| Safe Rust wrappers | Task 9 |
| `mui-dev` CLI (`--device`, `--device-scale`, `--landscape`) | Task 10 |
| Placeholder frame assets | Task 11 |
| End-to-end validation | Task 12 |

### Placeholder scan

No `TBD`, `TODO`, "add appropriate error handling", or "write tests for the above" remain. Every step has exact code or commands.

### Type consistency

- `UIDeviceOrientation` is `enum` in C and `#[repr(i32)]` Rust enum.
- `UIScreenInsets` fields are used as `.top/.bottom/.left/.right` in both C and Rust.
- `DeviceProfile` conversion maps `safe_top` → `safeTop`, etc.
- `hostScale` is `float` in C and `f32` in Rust.

---

## Execution Handoff

**Plan complete and saved to `docs/superpowers/plans/2026-06-23-mocida-device-simulator.md`.**

Two execution options:

1. **Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.
2. **Inline Execution** — Execute tasks in this session using `executing-plans`, batch execution with checkpoints.

Which approach do you want?
