# Mocida {#mainpage}

> Modular UI toolkit in C, built on top of
> [SDL3](https://github.com/libsdl-org/SDL). Focused on visual quality
> (analytic-coverage anti-aliasing, SDF-based drop shadows, configurable
> MSAA) and a simple, typed API.

---

## Overview

A Mocida program is built around these pieces:

| Concept                     | Header                | What it does                                                                                |
|-----------------------------|-----------------------|---------------------------------------------------------------------------------------------|
| `UIApp`                     | @ref app.h            | Application lifecycle, window, event loop, FPS target, render quality.                      |
| `UIWindow`                  | @ref window.h         | SDL window + renderer; owns the per-frame render pass.                                      |
| `UIWidget`                  | @ref widget.h         | Generic wrapper (position, size, z-index, alignment, parent).                               |
| `UIRectangle`               | @ref rect.h           | Rounded rectangle with optional border and drop shadow.                                     |
| `UIText`                    | @ref text.h           | TTF text with background rect, padding, margins.                                            |
| `UIImage`                   | @ref image.h          | Image widget (PNG/JPG/BMP/SVG) with fill modes and tint.                                    |
| `UIButton`                  | @ref button.h         | Button with NORMAL/HOVER/PRESSED/DISABLED states and an `onClick` callback.                 |
| `UIShadow`                  | @ref shadow.h         | CSS-like drop shadow (offset, blur, spread, color).                                         |
| `UIChildren`                | @ref children.h       | Z-index ordered list of widgets attached to a window.                                       |
| `UIAlignment`               | @ref alignment.h      | Alignment of one widget relative to another (V/H + target widget).                          |

---

## Building

First time on a fresh PC:

```bat
:: from the project root
setup.bat
```

That will install (if missing) Git, CMake, LLVM/clang, Ninja, and Make
via winget; bootstrap a local vcpkg + libcurl; clone SDL/SDL_image/SDL_ttf
at pinned commits; and run the first build.

After that, incremental builds are just:

```bat
build.bat            :: Debug
release.bat          :: Release + distribution zip
build.bat --clean    :: force CMake reconfiguration
```

Each file in `tests/` becomes its own executable:

```bat
.\build\win32\demo.exe              :: original demo
.\build\win32\test_image.exe        :: fill-mode gallery + tint
.\build\win32\test_shadows.exe      :: drop shadows + icon
.\build\win32\test_quality.exe      :: cycles MSAA LOW/MEDIUM/HIGH/ULTRA
.\build\win32\test_button.exe       :: interactive buttons
.\build\win32\test_fps.exe          :: FPS cap cycle
```

---

## Hello World

```c
#include <uikit/app.h>

int main(void) {
    UIApp* app = UIApp_Create("My app", 800, 600);

    UIApp_SetWindowIcon(app, "assets/logo.svg");
    UISearchFonts();

    UIChildren* children = UIChildren_Create(8);

    UIButton* btn = UIButton_Create("Click me", 22.0f);
    UIButton_SetFontFamily(btn, UIGetFont("Arial"));
    UIButton_SetRadius(btn, 8.0f);
    UIButton_SetShadow(btn, UI_SHADOW_DEFAULT);

    UIWidget* w = widgcs(btn, 220.0f, 56.0f);
    UIWidget_SetPosition(w, 290.0f, 270.0f);
    UIChildren_Add(children, w);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, (UIColor){ 248, 250, 252, 1.0f });

    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
```

---

## Visual quality pipeline

- **Analytic-coverage AA on the CPU.** For circles and rounded corners,
  the renderer computes `coverage = samples_inside / N^2` per pixel of
  the cached texture. N is tunable via `UIApp_SetRenderQuality`. The
  result matches what 16x or 64x MSAA would produce, but is
  mathematically exact and consistent across backends (D3D11/12,
  Vulkan, OpenGL, Metal).
- **SDF-based drop shadows.** `RasterizeShadowMask` in `window.c` uses
  the signed-distance function of a rounded rect with a smoothstep
  falloff. Soft edges in one pass — no multi-pass CPU/GPU blur needed.
- **Texture cache.** Circles cached by display size; shadows cached by
  `(w, h, radius, blur, spread)`. Color is applied via
  `SDL_SetTextureColorMod` at draw time — re-coloring is free.

---

## Generating these docs

If `doxygen` is on the PATH:

```bat
docs.bat                                 :: from the repo root
:: or, from a configured build dir:
cmake --build build --target docs
```

The site lands in `docs/generated/html/index.html`.

To install Doxygen on Windows:

```bat
winget install --id DimitriVanHeesch.Doxygen
```
