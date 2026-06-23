# Design: Simulador de Dispositivos para o mui-dev

**Data:** 2026-06-23  
**Status:** Aprovado para implementação  
**Escopo:** MVP de simulação visual de iPhones e Galaxy S em `mui-dev`. Rotação, safe area e transformação de input. Multitouch, sensores e teclado virtual ficam para milestones futuras.

---

## 1. Objetivo

Permitir que o `mui-dev` execute uma aplicação mocida dentro da moldura visual de um dispositivo móvel real (iPhone 15/16 e Galaxy S24/S25), respeitando:

- Dimensões lógicas da tela do aparelho.
- Safe area (notch, dynamic island, barra de navegação).
- Device pixel ratio (`scale`).
- Orientação portrait/landscape, trocável em runtime.
- Transformação de input de mouse do host para coordenadas do dispositivo.

O resultado é uma janela do `mui-dev` mostrando o aparelho ao redor da app, com a app renderizada em tempo real dentro da tela.

---

## 2. Contexto do Projeto

O mocida é um toolkit UI em C11 sobre SDL3, com bindings Rust e linguagem declarativa MUI:

- `mocida/src/uikit` é a biblioteca C do runtime (widgets, render, eventos).
- `mocida-rs/mui-runtime` expõe a API Rust que gera a árvore de widgets.
- `mocida-rs/mui-dev` é o host executável que cria janela, chama o runtime e faz hot-reload.
- Hoje `Window.width/height` e `Screen.safe_area()` refletem o desktop; não há simulação de dispositivo.
- O render é feito diretamente no backbuffer da janela pelo `SDL_Renderer`.

Para o simulador, o runtime precisa renderizar a app em uma textura do tamanho do dispositivo, enquanto o host desenha essa textura dentro da moldura.

---

## 3. Decisões de Design

### 3.1 Local da simulação

A lógica principal fica no **runtime C** (`mocida/src/uikit`), não no host Rust. Assim:

- Toda a UI (widgets, `Screen`, `Window`) vê as métricas do dispositivo.
- O host apenas carrega o asset da moldura, cria a janela grande o suficiente e chama a API de ativação.
- Hot-reload continua funcionando porque o runtime re-renderiza a cada frame.

### 3.2 Render-to-textura

Quando o simulador está ativo:

1. `UIApp_Present` redireciona o `SDL_Renderer` para uma textura do tamanho lógico do dispositivo.
2. A árvore de widgets é renderizada normalmente nessa textura.
3. O renderer volta para o backbuffer da janela.
4. O runtime desenha a moldura PNG (ou fallback) e blita a textura do dispositivo na posição correta.

Isso evita duas árvores de renderização e reaproveita todo o pipeline existente.

### 3.3 Transformação de input

Coordenadas do mouse são recebidas da janela do host. Antes de serem entregues aos widgets, são convertidas para o espaço do dispositivo considerando:

- Posição do viewport do device dentro da janela.
- Escala visual (`hostScale`) escolhida pelo usuário.
- Orientação portrait/landscape.

### 3.4 Rotação

A rotação é suportada de duas formas:

- **CLI:** `--landscape` inicia no modo paisagem.
- **Runtime:** tecla `R` alterna entre portrait e landscape.

A moldura PNG (asset vertical) é rotacionada automaticamente pelo SDL (`SDL_RenderTextureRotated`). O recorte da tela é recalculado e as safe areas são trocadas conforme a regra da seção 5.5. A janela do host é redimensionada para `(profile.frame_h × hostScale) × (profile.frame_w × hostScale)`. A troca é instantânea, sem animação.

### 3.5 Assets de moldura

Cada dispositivo tem uma imagem PNG realista (`mocida-rs/assets/devices/<name>.png`) com área transparente/recortada para a tela. Se o asset não existir, o simulador usa um fallback de bezel simples (retângulo arredondado com bordas).

---

## 4. API Pública em C

Header: `mocida/src/headers/uikit/device_simulator.h`

### 4.1 Tipos

```c
typedef enum {
    UI_DEVICE_ORIENTATION_PORTRAIT = 0,
    UI_DEVICE_ORIENTATION_LANDSCAPE
} UIDeviceOrientation;

typedef struct {
    const char* name;
    int width;        // pixels lógicos da tela
    int height;       // pixels lógicos da tela
    int safeTop;
    int safeBottom;
    int safeLeft;
    int safeRight;
    float scale;      // device pixel ratio

    // Posição da tela dentro da imagem da moldura (asset vertical/portrait)
    int frameScreenX;
    int frameScreenY;
    int frameScreenW;
    int frameScreenH;

    // Dimensões totais da imagem da moldura
    int frameW;
    int frameH;
} UIDeviceProfile;
```

### 4.2 Ativação e configuração

```c
void UIApp_EnableDeviceSimulator(
    UIApp* app,
    const UIDeviceProfile* profile,
    SDL_Texture* frameTexture,  // pode ser NULL para fallback de bezel
    float hostScale,             // escala visual da janela (ex: 2.0)
    UIDeviceOrientation orientation);

void UIApp_DisableDeviceSimulator(UIApp* app);
bool UIApp_IsDeviceSimulatorEnabled(UIApp* app);

const UIDeviceProfile* UIApp_GetCurrentDeviceProfile(UIApp* app);
UIDeviceOrientation UIApp_GetCurrentDeviceOrientation(UIApp* app);
void UIApp_SetDeviceOrientation(UIApp* app, UIDeviceOrientation orientation);
```

### 4.3 Transformação de coordenadas

```c
void UIApp_DeviceSimulatorWindowToDevice(
    UIApp* app,
    float wx, float wy,
    float* dx, float* dy);

void UIApp_DeviceSimulatorDeviceToWindow(
    UIApp* app,
    float dx, float dy,
    float* wx, float* wy);
```

A transformação leva em conta a escala visual do host, a posição da tela dentro da moldura (`frameScreenX/Y/W/H`) e a orientação atual.

### 4.4 Acesso interno (usado por window/app)

```c
bool UIApp_GetDeviceSimulatorViewport(UIApp* app, SDL_FRect* outViewport);
SDL_Texture* UIApp_GetDeviceSimulatorRenderTarget(UIApp* app);
```

---

## 5. Integração com o Runtime C

### 5.1 `UIApp`

`struct UIApp` ganha campos opcionais:

```c
bool deviceSimulatorEnabled;
UIDeviceProfile deviceProfile;
UIDeviceOrientation deviceOrientation;
SDL_Texture* deviceRenderTarget;
SDL_Texture* deviceFrameTexture;
float hostScale;
```

### 5.2 `UIWindow_GetSize` / `UIScreen_GetSize`

Quando o simulador está ativo, retornam as dimensões do profile considerando a orientação:

- Portrait: `width × height`
- Landscape: `height × width`

### 5.3 `UIApp_Present`

```c
void UIApp_Present(UIApp* app) {
    if (app->deviceSimulatorEnabled) {
        // 1. Renderiza a app na textura do device
        SDL_SetRenderTarget(renderer, app->deviceRenderTarget);
        SDL_SetRenderScale(renderer, 1.0f, 1.0f);
        SDL_SetRenderViewport(renderer, NULL);
        RenderApp(app);

        // 2. Volta ao backbuffer
        SDL_SetRenderTarget(renderer, NULL);

        // 3. Limpa a janela
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderClear(renderer);

        // 4. Desenha a moldura e a textura do device
        DeviceSimulator_PresentFrame(app);
    } else {
        RenderApp(app);
    }

    SDL_RenderPresent(renderer);
}
```

### 5.4 Eventos de mouse

`app.c` transforma coordenadas antes de chamar os dispatchers:

```c
float dx, dy;
UIApp_DeviceSimulatorWindowToDevice(app, event->motion.x, event->motion.y, &dx, &dy);
UIWindow_DispatchMouseMove(app->window, dx, dy);
```

Quando o simulador não está ativo, a transformação é identidade.

### 5.5 Safe area

`UIScreen_SafeArea` retorna (considerando rotação de 90° no sentido horário):

- Portrait:
  - `top = profile.safeTop`
  - `bottom = profile.safeBottom`
  - `left = profile.safeLeft`
  - `right = profile.safeRight`
- Landscape:
  - `top = profile.safeRight`
  - `bottom = profile.safeLeft`
  - `left = profile.safeTop`
  - `right = profile.safeBottom`

### 5.6 Posicionamento da tela na moldura

O campo `frameScreenX/Y/W/H` define, no asset vertical, onde a textura do device deve ser blitada. Para o fallback (sem PNG), o runtime centraliza a tela dentro do `frameW × frameH` com uma margem fixa.

No modo landscape, esse retângulo é rotacionado 90° em torno do centro da moldura para posicionar a textura corretamente.

---

## 6. Integração com o `mui-dev` (Rust)

### 6.1 CLI

```bash
mui-dev --device iphone15pro --device-scale 2 --landscape meu_app.mui
```

Flags:

- `--device <name>` — ativa o simulador com o profile nomeado.
- `--device-scale <fator>` — escala visual da janela (padrão: 2.0).
- `--landscape` — inicia em orientação paisagem.

### 6.2 Profiles

`mocida-rs/mui-runtime/src/device_profiles.rs` define:

```rust
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

pub const IPHONE_15: DeviceProfile = DeviceProfile { ... };
pub const IPHONE_15_PRO: DeviceProfile = DeviceProfile { ... };
pub const IPHONE_15_PRO_MAX: DeviceProfile = DeviceProfile { ... };
pub const IPHONE_16: DeviceProfile = DeviceProfile { ... };
pub const GALAXY_S24: DeviceProfile = DeviceProfile { ... };
pub const GALAXY_S24_PLUS: DeviceProfile = DeviceProfile { ... };
pub const GALAXY_S25: DeviceProfile = DeviceProfile { ... };
pub const GALAXY_S25_ULTRA: DeviceProfile = DeviceProfile { ... };
```

### 6.3 Ciclo de vida no host

1. Parse dos argumentos.
2. Se `--device` foi passado:
   - Carrega `assets/devices/<name>.png` via SDL_image (se faltar, passa `NULL`).
   - Calcula tamanho da janela: `profile.frame_w * hostScale` × `profile.frame_h * hostScale`.
   - Cria a janela com esse tamanho.
   - Chama `UIApp_EnableDeviceSimulator(app, &profile, frameTexture, hostScale, orientation)`.
3. No loop principal:
   - Processa eventos SDL.
   - Detecta tecla `R` e chama `UIApp_SetDeviceOrientation(...)`.
   - Redimensiona a janela se a orientação mudar.
   - Chama o runtime normalmente.

### 6.4 Hot-reload

A app compilada continua usando as mesmas métricas do dispositivo. Ao trocar de orientação em runtime, o runtime re-avalia `Window.width/height` e a UI é re-layoutada automaticamente.

---

## 7. Exemplo de Uso

### C

```c
#include <uikit/app.h>
#include <uikit/device_simulator.h>

int main() {
    UIApp* app = UIApp_Create("Device Sim", 100, 100);

    UIDeviceProfile profile = {
        .name = "iphone15pro",
        .width = 393, .height = 852,
        .safeTop = 59, .safeBottom = 34,
        .safeLeft = 0, .safeRight = 0,
        .scale = 3.0f
    };

    UIApp_EnableDeviceSimulator(app, &profile, NULL,
                                2.0f,      // hostScale
                                UI_DEVICE_ORIENTATION_PORTRAIT);

    UIApp_SetChildren(app, BuildMyApp());
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
```

### Rust / CLI

```bash
mui-dev --device iphone15pro --device-scale 2 examples/counter.mui
# Apertar R dentro da janela alterna para landscape.
```

---

## 8. Testes

- **Teste de build**: compilar `mocida` com `device_simulator.h` e `device_simulator.c`.
- **Teste visual**: executar `mui-dev --device iphone15pro` e verificar que a app aparece dentro da moldura.
- **Teste de safe area**: app que pinta a safe area de verde; verificar que fica dentro da área visível.
- **Teste de input**: botão na parte inferior da tela; clicar dentro da moldura e verificar que o evento chega no lugar certo.
- **Teste de rotação**: apertar `R` e verificar que a janela redimensiona, a moldura gira e a app se adapta ao novo aspecto.
- **Teste de fallback**: renomear o asset PNG e verificar que o simulador ainda exibe um bezel simples.

---

## 9. Milestones Futuras

1. **Rotação animada**: transição suave entre portrait e landscape.
2. **Multitouch simulado**: mapear gestos de trackpad/mouse para toques múltiplos.
3. **Teclado virtual simulado**: mostrar teclado na tela quando um campo de texto ganha foco.
4. **Dispositivos adicionais**: iPad, tablets Android, dispositivos dobráveis.
5. **Cutout visual do notch/dynamic island**: recorte adicional sobre a moldura, não apenas safe area.

---

## 10. Arquivos a Serem Criados/Modificados

### Novos arquivos

- `mocida/src/headers/uikit/device_simulator.h`
- `mocida/src/uikit/device_simulator.c`
- `mocida/tests/test_device_simulator.c`
- `mocida-rs/mui-runtime/src/device_profiles.rs`
- `mocida-rs/assets/devices/iphone15.png`
- `mocida-rs/assets/devices/iphone15pro.png`
- `mocida-rs/assets/devices/iphone15promax.png`
- `mocida-rs/assets/devices/iphone16.png`
- `mocida-rs/assets/devices/galaxy_s24.png`
- `mocida-rs/assets/devices/galaxy_s24plus.png`
- `mocida-rs/assets/devices/galaxy_s25.png`
- `mocida-rs/assets/devices/galaxy_s25ultra.png`

### Arquivos modificados

- `mocida/src/headers/uikit/app.h` — adicionar declarações do simulador.
- `mocida/src/uikit/app.c` — campos no `UIApp`, `UIApp_Present`, transformação de input.
- `mocida/src/uikit/window.c` — `UIWindow_GetSize` respeita o simulador.
- `mocida/src/uikit/screen.c` — `UIScreen_GetSize` e `UIScreen_SafeArea` respeitam o simulador.
- `mocida-rs/mui-runtime/src/lib.rs` — expor profiles e helpers.
- `mocida-rs/mui-dev/src/main.rs` — parse de CLI, carregar asset, ativar simulador, tecla `R`.
- `mocida-rs/mui-dev/Cargo.toml` — dependência de SDL_image se ainda não estiver presente.

---

## 11. Riscos e Mitigações

| Risco | Mitigação |
|-------|-----------|
| Render-to-textura aumentar uso de VRAM | Usar textura do tamanho lógico do device; escala é apenas visual no host. |
| Asset PNG da moldura ficar desalinhado | Definir no profile as coordenadas exatas do recorte da tela; testar visualmente. |
| Rotação trocar safe area de forma confusa | Documentar regra de troca; testar com app que pinta safe area. |
| Input do mouse não bater com a escala | Centralizar transformação em `UIApp_DeviceSimulatorWindowToDevice`; testar hit em todos os cantos. |
| Hot-reload quebrar ao trocar orientação | Garantir que `Window.width/height` seja re-lido a cada frame/rebuild. |
