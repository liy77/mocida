# Changelog

All notable changes to this monorepo.

## [v0.5.0] - 2026-06-03

This release introduces **MUI**, a declarative UI layer over mocida — write a
`.mui`/`.crm` file and render it as a live mocida window — plus media
components (Video, WebView) and a round of text-rendering quality fixes.

### Added
- **MUI runtime + `mui-dev` host**: render declarative `.mui`/`.crm` documents
  as real mocida UIs (`cforge run x.mui`). Reactive `signal()` state with live
  text/structural updates, hot-reload on save, components with params +
  cross-file `import`, styling + anchors, and an `app { … }` window/bundle
  block. Widget parity for the visible widget set.
- **`onKeyInput` keyboard handlers** on any widget (`{ |event| … }`), keyed off
  SDL key names; supports signal mutations and `println!` debugging.
- **MUI `Video` component** wrapping `UIVideo` — `source`, `width`/`height`,
  `fillMode`, `autoplay`, `repeat` (looping; `loop` is a reserved word),
  `muted`, `volume`.
- **MUI `WebView` component** wrapping `UIWebView` (WebView2 / WKWebView /
  WebKitGTK) — initial `url`, `radius`, `border`.
- **Cross-platform `Video` corner radius** (`UIVideo_SetRadius` /
  `UIVideo_GetRadius` across the Windows / Linux / macOS / fallback backends);
  the rounded composite runs in the shared SDL render path, so it looks the
  same on every platform.
- Expanded Rust bindings for the new C surface (stack justify, margins, key
  callbacks, min/max size, renderer selection).

### Changed
- **Text rendering quality**: glyph textures are snapped to the integer pixel
  grid and carry a transparent border, so a fractional or scaled (HiDPI/SSAA)
  blit no longer shaves descenders or edge AA rows; centered text now stays
  centered when a reactive label grows past its estimated width box; default
  font hinting is `LIGHT` (rounder glyph apexes, still crisp).
- Split the 5063-line `window.c` into focused include units.
- Registered the vendored SDL/SDL_image/SDL_ttf trees as git submodules.

### Fixed
- **macOS**: disabled mimalloc's `MI_OVERRIDE` (process-wide malloc-zone
  interpose) which was hijacking the Rust side's allocations and aborting every
  `.mui` with "pointer being freed was not allocated" (SIGABRT). mimalloc stays
  enabled for the C library on all platforms.
- Rust bindings: use inferred casts for bindgen newtype enums.

## [v0.4.1] - 2026-05-30

Mocida agora é verdadeiramente multiplataforma: além do Windows, esta release
traz backends nativos completos para macOS, Linux e iOS, um instalador e um
fluxo de release que cobrem os três desktops, e a reorganização do projeto em
monorepo (toolkit C + bindings Rust).

### Added
- **macOS**: backend de webview (WKWebView), backend de vídeo (AVFoundation) e
  seleção explícita do render driver Metal com dicas de baixo consumo de GPU.
- **Linux**: backend de webview WebKitGTK completo (navegação, scripts,
  cookies, callbacks) com fallback gracioso quando nenhum backend foi compilado;
  vídeo via GStreamer; enumeração de fontes do sistema; `UIApp_SetMinSize` /
  `SetMaxSize`.
- **iOS**: build de `.ipa` (não assinado) do demo, backend WKWebView via UIKit
  com toque funcionando no simulador, ícone do app, componente `Screen`,
  layouts móveis responsivos, fontes customizadas empacotadas, trava de
  orientação e barra de status opcional.
- **Safe-area insets** (notch) cross-platform, com relayout em rotação.
- **Sistema de bundle/assets** (`mocida://`), manifesto `app.bundle`, nome do
  app, background e ícone de tray.
- **Instalador GUI single-file** e empacotador de release cross-platform
  (Windows / Linux / macOS), com SDK + instalador por plataforma.
- **Workflow de CI de release** (GitHub Actions): build nativo em
  windows/linux/macos anexando todos os assets a uma única tag.
- **Apps de exemplo** standalone e paridade de features nos bindings Rust
  (binding de safe-area, color setters, font styles, helpers de widget, etc.).

### Changed
- **Reestruturação em monorepo**: toolkit C em `mocida/`, bindings Rust em
  `mocida-rs/`, com tooling na raiz (`.clang-format`, `rustfmt.toml`,
  pre-commit).
- **Tooling portado para Python** cross-platform (`build.py`, `setup.py`,
  `release.py`, `docs.py`); orquestrador de build em estilo task-runner;
  scripts `.bat`/`.ps1`/`.sh` antigos removidos.
- Debug overlay agora é opt-in em runtime (`UIDebugOverlay_SetEnabled`) e
  funciona em qualquer build.
- Otimizações de render: TAA/FXAA residentes na GPU, batching de
  RenderGeometry, cache de texturas de labels de abas e de imagens com cantos
  arredondados.

### Fixed
- **macOS**: texto não renderizava (fontes resolvidas contra caminhos mortos);
  vazamento de `CGColor` por frame na borda do WKWebView.
- **iOS**: app preenche a tela inteira (layouts proporcionais); relayout
  correto quando a safe-area se estabiliza um frame depois.
- **Linux**: segfault do GStreamer no init do vídeo; vazamentos de memória em
  destroy de containers (grid/scroll) e na enumeração de fontes no shutdown.
- Auto-recuperação de diretórios vazios de SDL/SDL_image/SDL_ttf/mimalloc em
  clone novo.
