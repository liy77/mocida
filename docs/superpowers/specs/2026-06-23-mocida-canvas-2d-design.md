# Design: Canvas 2D Nativo para o mocida

**Data:** 2026-06-23  
**Status:** Aprovado para implementação  
**Escopo:** Primeiro marco de componentes gráficos no mocida (API C nativa, sem HTML). Partículas, shaders e 3D serão tratados em milestones futuras.

---

## 1. Objetivo

Adicionar ao mocida um componente `UICanvas` que permita desenho programático 2D nativo em C, com sensação parecida com o HTML5 Canvas, mas sem depender de HTML/JS. O componente deve:

- Viver na árvore de widgets do mocida (layout, z-index, clipping, opacidade).
- Oferecer API de alto nível para primitivos 2D (retângulos, círculos, paths, texto, imagens).
- Oferecer escape hatch para o `SDL_Renderer*` quando o usuário precisar de controle baixo nível.
- Capturar eventos de mouse e teclado em coordenadas locais do canvas.
- Servir de base para partículas, efeitos e (futuramente) shaders/3D.

---

## 2. Contexto do Projeto

O mocida é um toolkit UI em C11 sobre SDL3, com bindings Rust e linguagem declarativa MUI. Sua arquitetura atual inclui:

- Árvore de widgets retida (`UIWidget`, `UIChildren`).
- Renderização via `SDL_Renderer` com AA, sombras SDF, MSAA/SSAA/FXAA/TAA.
- Eventos de mouse já convertidos de janela física para coordenadas do renderer.
- Vários widgets (Rectangle, Button, Image, Stack, Scroll, MouseArea, etc.).
- Não existe hoje nenhum widget ou API para desenho customizado, partículas ou geometria arbitrária.

A base existe; falta um widget de canvas/surface customizada com callbacks de render e eventos.

---

## 3. Decisões de Design

### 3.1 Forma do componente

O componente será um **widget `UICanvas`**, não um hook global de render. Isso permite que ele participe de layout, z-index, clipping e hit-test da mesma forma que outros widgets.

### 3.2 Estilo da API

A API será **híbrida**:

- **Alto nível**: funções imediatas no estilo HTML5 Canvas (`UICanvas_FillRect`, `UICanvas_BeginPath`, `UICanvas_Arc`, `UICanvas_Fill`, etc.).
- **Baixo nível**: `UICanvas_GetRenderer(canvas)` devolve o `SDL_Renderer*` para desenho direto via SDL3.

### 3.3 Eventos

O canvas terá callbacks para:

- `onFrame` — chamado a cada render pass.
- `onMouseMove`, `onMouseDown`, `onMouseUp` — com coordenadas locais.
- `onHoverEnter`, `onHoverExit`.
- `onKeyDown` — quando o canvas tem foco.

### 3.4 Coordenadas

Coordenadas de mouse são convertidas de janela física → renderer → canvas local antes de chamar os callbacks. O desenho acontece no espaço local do canvas.

---

## 4. API Pública em C

Header: `mocida/src/headers/uikit/canvas.h`

### 4.1 Criação e conversão

```c
UICanvas* UICanvas_Create(float width, float height);
UIWidget* UICanvas_AsWidget(UICanvas* canvas);   // empréstimo
UIWidget* UICanvas_ToWidget(UICanvas* canvas);   // transferência de ownership
UICanvas* UICanvas_FromWidget(UIWidget* widget);
void UICanvas_Destroy(UICanvas* canvas);
```

### 4.2 Escape hatch

```c
SDL_Renderer* UICanvas_GetRenderer(const UICanvas* canvas);
```

### 4.3 Estado do contexto

```c
void UICanvas_SetFillColor(UICanvas* canvas, UIColor color);
void UICanvas_SetStrokeColor(UICanvas* canvas, UIColor color);
void UICanvas_SetLineWidth(UICanvas* canvas, float width);
void UICanvas_SetGlobalAlpha(UICanvas* canvas, float alpha);
void UICanvas_SetBlendMode(UICanvas* canvas, UIBlendMode mode);
void UICanvas_ClearRect(UICanvas* canvas, float x, float y, float w, float h);
```

### 4.4 Primitivas imediatas

```c
void UICanvas_FillRect(UICanvas* canvas, float x, float y, float w, float h);
void UICanvas_StrokeRect(UICanvas* canvas, float x, float y, float w, float h);

void UICanvas_BeginPath(UICanvas* canvas);
void UICanvas_MoveTo(UICanvas* canvas, float x, float y);
void UICanvas_LineTo(UICanvas* canvas, float x, float y);
void UICanvas_Arc(UICanvas* canvas, float x, float y, float r, float startAngle, float endAngle);
void UICanvas_BezierCurveTo(UICanvas* canvas, float cp1x, float cp1y, float cp2x, float cp2y, float x, float y);
void UICanvas_ClosePath(UICanvas* canvas);
void UICanvas_Fill(UICanvas* canvas);
void UICanvas_Stroke(UICanvas* canvas);

void UICanvas_FillCircle(UICanvas* canvas, float x, float y, float r);
void UICanvas_StrokeCircle(UICanvas* canvas, float x, float y, float r);

void UICanvas_DrawImage(UICanvas* canvas, UIImage* image, float x, float y, float w, float h);
void UICanvas_DrawText(UICanvas* canvas, const char* text, float x, float y, UIFont* font, float size);
```

### 4.5 Coordenadas e transformação

```c
void UICanvas_Save(UICanvas* canvas);
void UICanvas_Restore(UICanvas* canvas);
void UICanvas_Translate(UICanvas* canvas, float x, float y);
void UICanvas_Rotate(UICanvas* canvas, float radians);
void UICanvas_Scale(UICanvas* canvas, float x, float y);
```

### 4.6 Callbacks

```c
typedef void (*UICanvasFrameCallback)(UICanvas* canvas, SDL_Renderer* renderer, void* userdata);
typedef void (*UICanvasMouseCallback)(UICanvas* canvas, UIMouseEvent ev, void* userdata);
typedef void (*UICanvasKeyCallback)(UICanvas* canvas, const char* key, int mods, void* userdata);

// Setters seguem a convenção do mocida (retornam o próprio objeto para chaining).
UICanvas* UICanvas_OnFrame(UICanvas* canvas, UICanvasFrameCallback cb, void* userdata);
UICanvas* UICanvas_OnMouseMove(UICanvas* canvas, UICanvasMouseCallback cb, void* userdata);
UICanvas* UICanvas_OnMouseDown(UICanvas* canvas, UICanvasMouseCallback cb, void* userdata);
UICanvas* UICanvas_OnMouseUp(UICanvas* canvas, UICanvasMouseCallback cb, void* userdata);
UICanvas* UICanvas_OnHoverEnter(UICanvas* canvas, UICanvasMouseCallback cb, void* userdata);
UICanvas* UICanvas_OnHoverExit(UICanvas* canvas, UICanvasMouseCallback cb, void* userdata);
void      UICanvas_SetOnKeyDown(UICanvas* canvas, UICanvasKeyCallback cb, void* userdata);
```

### 4.7 Utilitários

```c
float UICanvas_GetWidth(const UICanvas* canvas);
float UICanvas_GetHeight(const UICanvas* canvas);
void UICanvas_GetMousePos(const UICanvas* canvas, float* x, float* y);
bool UICanvas_IsMouseInside(const UICanvas* canvas);
```

---

## 5. Integração com o Sistema de Widgets

### 5.1 Novo tipo

```c
#define UI_WIDGET_CANVAS "@uikit/canvas"
```

`mocida/src/headers/uikit/canvas.h` define a struct `UICanvas` com estado de desenho, callbacks e userdata.

### 5.2 Renderização

`mocida/src/uikit/canvas.c` implementa `RenderWidget_Canvas`, com a mesma assinatura dos outros renderers:

```c
static void RenderWidget_Canvas(UIWindow* window, UIWidget* el,
                                UIWidgetBase* base, float op) {
    UICanvas* canvas = (UICanvas*)base;
    // Configura clipping para a área do canvas e chama onFrame.
    // A opacidade `op` é aplicada ao alpha global do contexto.
}
```

O dispatch em `mocida/src/uikit/window_render.inc` ganha o caso:

```c
if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_CANVAS)) {
    RenderWidget_Canvas(window, el, base, op);
    return;
}
```

### 5.3 Eventos

`mocida/src/uikit/canvas.c` implementa:

- `UICanvas_DispatchMouseMove`
- `UICanvas_DispatchMouseDown`
- `UICanvas_DispatchMouseUp`

Essas funções verificam hit-test, convertem coordenadas e chamam os callbacks registrados. Elas são chamadas a partir do loop de eventos em `mocida/src/uikit/app.c`, ao lado dos dispatchers existentes.

### 5.4 Layout e clipping

Como `UICanvas` é um `UIWidget` normal, ele herda automaticamente:

- layout via `UIStack`, `UIGrid`, `UIScroll`;
- clipping de containers/scroll;
- z-index e opacidade;
- foco de teclado via `UIWidget_SetFocus`.

---

## 6. Implementação dos Primitivos

| Função | Implementação |
|--------|---------------|
| `FillRect` / `StrokeRect` | `SDL_RenderFillRect` / `SDL_RenderRect` |
| `FillCircle` / `StrokeCircle` | `SDL_RenderGeometry` com vértices gerados em CPU |
| Paths | Path builder em CPU + triangulação para `SDL_RenderGeometry` (MVP: paths simples/convexos) |
| `DrawImage` | `SDL_RenderTexture` / `SDL_RenderTextureRotated` com transformação atual |
| `DrawText` | `TTF_RenderText_Blended` + `SDL_CreateTextureFromSurface`, com cache opcional |
| Estado | Pilha de estado (`Save`/`Restore`) com cor, alpha, largura, blend e matriz de transformação |

Batching de geometria é deixado como otimização futura; o MVP envia cada primitivo imediatamente sem mudar a API pública.

---

## 7. Exemplo de Uso

```c
#include <uikit/app.h>
#include <uikit/canvas.h>

typedef struct {
    float mouseX, mouseY;
    int particleCount;
} CanvasState;

void OnFrame(UICanvas* canvas, SDL_Renderer* renderer, void* userdata) {
    CanvasState* state = (CanvasState*)userdata;

    // Fundo
    UICanvas_SetFillColor(canvas, UI_COLOR_BLACK);
    UICanvas_FillRect(canvas, 0, 0, UICanvas_GetWidth(canvas), UICanvas_GetHeight(canvas));

    // Partícula no mouse
    UICanvas_SetFillColor(canvas, UI_COLOR_WHITE);
    UICanvas_FillCircle(canvas, state->mouseX, state->mouseY, 8.0f);
}

void OnMouseMove(UICanvas* canvas, UIMouseEvent ev, void* userdata) {
    CanvasState* state = (CanvasState*)userdata;
    state->mouseX = ev.x;
    state->mouseY = ev.y;
}

int main() {
    UIApp* app = UIApp_Create("Canvas Demo", 800, 600);

    UICanvas* canvas = UICanvas_Create(800, 600);
    CanvasState state = {0};

    UICanvas_OnFrame(canvas, OnFrame, &state);
    UICanvas_OnMouseMove(canvas, OnMouseMove, &state);

    UIChildren* children = UIChildren_Create(1);
    UIChildren_Add(children, widgcs(canvas, 800.0f, 600.0f));
    UIApp_SetChildren(app, children);

    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
```

---

## 8. Bindings Rust e MUI (futuro)

O primeiro marco foca na API C. Após validação, serão adicionados:

- `mocida-rs/mocida/src/canvas.rs` com wrappers seguros.
- Suporte a `<Canvas>` no runtime MUI em `mocida-rs/mui-runtime/src/lib.rs`.

A API Rust espelhará a API C de forma idiomática.

---

## 9. Testes

- **Teste de build**: compilar exemplo mínimo com `UICanvas_Create` e `UICanvas_FillRect`.
- **Teste visual**: exemplo de partículas seguindo o mouse.
- **Teste de eventos**: log de `onMouseMove`, `onMouseDown`, `onHoverEnter`, `onHoverExit`.
- **Teste de clipping**: canvas dentro de um `UIScroll` deve respeitar o clip.
- **Teste de escape hatch**: exemplo desenhando diretamente com `SDL_RenderGeometry` via `UICanvas_GetRenderer`.

---

## 10. Próximos Passos (milestones futuros)

1. **Engine de partículas**: componente `UIParticleSystem` de alto nível construído sobre `UICanvas`.
2. **Shaders customizados**: acesso ao backend nativo (OpenGL/Vulkan/Metal/D3D) através de SDL3.
3. **Objetos 3D**: widget `UICanvas3D` ou integração com biblioteca 3D via textura compartilhada.

---

## 11. Arquivos a Serem Criados/Modificados

### Novos arquivos
- `mocida/src/headers/uikit/canvas.h`
- `mocida/src/uikit/canvas.c`
- `mocida/src/uikit/canvas_internal.h` — definição interna da struct `UICanvas` (compartilhada entre `canvas.c` e `window.c`).
- `mocida/tests/test_canvas_basic.c`
- `mocida/tests/test_canvas_particles.c`

### Arquivos modificados
- `mocida/src/headers/uikit/widget.h` — adicionar `#define UI_WIDGET_CANVAS "@uikit/canvas"` junto aos outros tipos.
- `mocida/src/uikit/window.c` — incluir `canvas_internal.h` para acesso à struct `UICanvas` no `RenderWidget_Canvas`.
- `mocida/src/uikit/window_widgets.inc` — adicionar `RenderWidget_Canvas` e registrá-lo em `RenderSingleWidget_Inner`.
- `mocida/src/uikit/window_render.inc` — adicionar caso `UI_WIDGET_CANVAS` no dispatch de render.
- `mocida/src/uikit/app.c` — incluir `canvas.h` e chamar `UICanvas_DispatchMouseMove`, `UICanvas_DispatchMouseDown` e `UICanvas_DispatchMouseUp` no loop de eventos.

---

## 12. Riscos e Mitigações

| Risco | Mitigação |
|-------|-----------|
| Paths complexos ficarem lentos | MVP limita a paths simples; triangulação robusta vem depois. |
| Texto sem cache causar queda de FPS | Cache de texturas por `(string, fonte, tamanho)` no canvas. |
| SDL_Renderer não expõe shaders | Escape hatch SDL é suficiente para 2D; shaders ficam para milestone futura. |
| Mudar arquitetura de widgets | Canvas segue o padrão existente; mudanças são locais. |
