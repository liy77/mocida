<div align="center">

<img src="mocida/assets/banner.svg" alt="Mocida" width="100%"/>

<p>
  <img alt="C11" src="https://img.shields.io/badge/C-11-3949ab?style=flat-square">
  <img alt="Rust" src="https://img.shields.io/badge/Rust-bindings-ce422b?style=flat-square&logo=rust">
  <img alt="SDL3" src="https://img.shields.io/badge/SDL-3-2c5f9e?style=flat-square">
  <img alt="Platforms" src="https://img.shields.io/badge/platform-Windows%20%C2%B7%20macOS%20%C2%B7%20Linux%20%C2%B7%20Web-0078d4?style=flat-square">
  <img alt="License" src="https://img.shields.io/badge/license-MIT-22c55e?style=flat-square">
</p>

<p><b>Mocida</b> — a modular UI and graphics toolkit on SDL3.<br/>
One C core, idiomatic Rust bindings, and a declarative MUI runtime for apps and 2.5D games.</p>

</div>

---

## Why Mocida

Mocida is the host surface for desktop apps, MUI views, and Mocidad games. The same widget tree, canvas, shaders, and animation clock run on Windows, macOS, Linux, and WebAssembly — with GPU acceleration when the device allows it, and a software path when it does not.

| Layer | Role |
| ----- | ---- |
| **C toolkit** (`mocida/`) | Widgets, layout, render, GPU/shaders, 2.5D scene, animation |
| **Rust** (`mocida-rs/`) | Safe FFI (`mocida-sys`) + builders (`mocida`) |
| **MUI** (`mui-runtime`, `mui-dev`) | Declarative `.mui` → live window; hot-reload; signals |

```
┌─────────────┐     FFI      ┌──────────────────┐
│  mocida (C) │ ───────────► │ mocida-rs        │
│  UIWidget   │  headers +   │  mocida-sys      │
│  UIScene3D  │  libmocida   │  mocida          │
│  UIAnim     │              │  mui-runtime     │
└─────────────┘              └──────────────────┘
        ▲                            ▲
        └──────── build.py ──────────┘
```

---

## Capabilities

### UI

- Component model on one `UIWidget` envelope: stacks, text, buttons, inputs, dialogs, tabs, scroll/grid, image, video, glass/backdrop, file drop, WebView.
- Analytic-coverage anti-aliasing, SDF shadows, MSAA / SSAA / FXAA / TAA.
- Hardware render drivers when available; software + light profile on weak or headless machines.

### Graphics — shaders & 2.5D / 3D

Built for Mocidad-style **2D with native 2.5D** (and 3D-aux props), exposed to C, Rust, and MUI:

| API | Purpose |
| --- | ------- |
| `UIGpu` | Soft SDL_GPU device — never hard-fails; reports driver & shader support |
| `UIShader` | Library with built-ins `unlit`, `lit_sprite`, `hd2d` + custom Metal/HLSL/GLSL/WGSL/SPIR-V passes |
| `UIScene3D` | Depth-sorted sprites, point lights, camera, meshes; paints via GPU path or CPU lighting fallback |

```c
UIScene3D* scene = UIScene3D_Create(960, 540);
UIScene3D_UseShader(scene, "hd2d");
UIScene3D_AddSprite(scene, "hero.png", 120, 80, 12, 64, 64);
UIScene3D_AddPointLight(scene, 200, 100, 0, 220, 1.4f, UI_COLOR_ORANGE);
```

```mui
Scene3D(width: 960, height: 540, shader: "hd2d") {
  Sprite3D("hero.png", x: 120, y: 80, z: 12, width: 64, height: 64, billboard: "yAxis", castsShadow: true)
  PointLight(x: 200, y: 100, radius: 220, intensity: 1.4, color: #ff9900)
  ShaderPass("water", vertex: "shaders/water.vert", fragment: "shaders/water.frag")
}
```

Modes: `Mode2D` · `Mode25D` (HD-2D) · `Mode3DAux`. Custom `ShaderPass` registrations stay selectable even when a device falls back to software — the scene approximates lighting instead of aborting the frame.

### Animation

Tween and timeline system shared by UI and scenes:

- Float tweens (`UIAnim_To` / `FromTo`) with Penner easings, sine/expo/bounce/back
- Timelines: delayed tracks, stagger, loop, per-timeline pause, callbacks
- Global pause / time scale (256 concurrent tweens)

```c
UIAnimTimeline* intro = UIAnimTimeline_Create();
UIAnimTimeline_AddFromTo(intro, &opacity, 0.f, 1.f, 0, 400, UI_EASE_OUT_CUBIC);
UIAnimTimeline_Stagger(intro, targets, n, 1.f, 300, 0, 40, UI_EASE_OUT_BACK);
UIAnimTimeline_Play(intro);
```

### Debug & quality

Logger (terminal / file / TCP), Chrome-Trace profiler, bounds/FPS/overdraw overlays (F8–F12), crash dumps, optional mimalloc and sanitizers.

---

## Quick start

```sh
python mocida/setup.py          # first machine: tools + deps
python build.py                 # debug: C → Rust
python build.py --config release
```

| Command | Effect |
| ------- | ------ |
| `python build.py` | Full graph, Debug |
| `python build.py c` / `rust` | C only / Rust only |
| `python build.py --force` | Clean rebuild |
| `python build.py --verbose` | Full compiler / cargo log |
| `python mocida/docs.py --serve` | Doxygen locally |

Environment wired by `build.py`: `MOCIDA_INCLUDE_DIR`, `MOCIDA_LIB_DIR`, optional `MOCIDA_LIB_NAME` / `MOCIDA_STATIC`.

---

## Rust

```rust
use mocida::{Button, FontStyle, Scene3D, ShaderLibrary, Ease, Timeline};

let button = Button::new("Play", 18.0)?
    .on_click(|_| { /* … */ })
    .into_widget()?;

let mut scene = Scene3D::new(960.0, 540.0)?
    .own_shader_library(ShaderLibrary::new(None)?)
    .use_shader("hd2d")?;
scene.add_sprite("hero.png", 120.0, 80.0, 12.0, 64.0, 64.0)?;
```

Docs: [`mocida-rs/README.md`](mocida-rs/README.md) · [`mocida-rs/EXAMPLES.md`](mocida-rs/EXAMPLES.md)

---

## Repository layout

| Path | Contents |
| ---- | -------- |
| [`mocida/`](mocida/README.md) | C toolkit (SDL3, headers under `src/headers/uikit/`) |
| [`mocida-rs/`](mocida-rs/README.md) | `mocida-sys`, `mocida`, `mui-runtime`, `mui-dev` |
| `build.py` · `setup.py` | Cross-platform orchestrator and bootstrap |
| `.github/` | CI |

---

## Status

**Shipped**

- [x] Widget model, layout, AA / shadows / post-AA, debug stack
- [x] Canvas 2D, fonts, assets, video/audio (platform backends)
- [x] `UIGpu` / `UIShader` / `UIScene3D` foundations + MUI `Scene3D`
- [x] Tween + timeline animation (`UIAnim` / `UIAnimTimeline`)
- [x] Rust FFI + builders; MUI reactive views & hot-reload

**Next**

- [ ] Full GPU shader compile/upload for custom passes (Metal/DXIL/SPIR-V)
- [ ] Soft/RT shadow maps and GPU particles on the Scene3D path
- [ ] MUI reactive diff/reconcile; richer handler interpreter
- [ ] Scroll input recursion into nested content

---

## Contributing hygiene

Pre-commit formats **staged files only**:

```sh
pipx install pre-commit && pre-commit install
```

- **C** — `clang-format` on `mocida/src` (vendored SDL trees excluded)
- **Rust** — `cargo fmt` on `mocida-rs/` (`rust-toolchain.toml` pins `rustfmt`)

WSL sync helper: [`sync-from-windows.sh`](sync-from-windows.sh).

---

<div align="center"><sub>MIT · C11 + Rust · SDL3 · UI · shaders · 2.5D · animation</sub></div>
