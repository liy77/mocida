# Changelog

All notable changes to this monorepo.

## [v0.4.0] - 2026-05-30

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
