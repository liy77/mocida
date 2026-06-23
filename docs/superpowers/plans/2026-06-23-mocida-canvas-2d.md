# UICanvas — Canvas 2D Nativo para o mocida

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Adicionar ao mocida um widget `UICanvas` que permite desenho programático 2D nativo em C, com API estilo HTML5 Canvas, eventos de mouse/teclado e escape hatch para `SDL_Renderer*`.

**Architecture:** O `UICanvas` é um widget normal do mocida (com `__widget_type`, layout, z-index e clipping). Ele mantém uma pilha de estado de desenho (cor, alpha, transformação) e uma path buffer em CPU. A cada frame o renderizador chama `RenderWidget_Canvas`, que configura o clip e invoca o callback `onFrame`. Eventos de mouse são despachados pelo `app.c`, convertidos para coordenadas locais do canvas e entregues aos callbacks registrados.

**Tech Stack:** C11, SDL3 (`SDL_Renderer`, `SDL_RenderGeometry`, `SDL_RenderFillRect`, `SDL_RenderTexture`), SDL_ttf (texto), mocida internals (`UIWidget`, `UIChildren`, `window_render.inc`, `window_draw.inc`).

---

## File Structure

| Arquivo | Responsabilidade |
|---|---|
| `mocida/src/headers/uikit/widget.h` | Adicionar a constante de tipo `UI_WIDGET_CANVAS`. |
| `mocida/src/headers/uikit/canvas.h` | API pública do canvas: criação, estado, primitivas, callbacks. |
| `mocida/src/uikit/canvas.c` | Implementação do widget: lifecycle, estado, desenho, dispatch de eventos. |
| `mocida/src/uikit/window_render.inc` | Adicionar o caso `UI_WIDGET_CANVAS` no dispatch de render. |
| `mocida/src/uikit/app.c` | Chamar os dispatchers de mouse do canvas no loop de eventos. |
| `mocida/tests/test_canvas_basic.c` | Teste mínimo: abrir janela, desenhar fundo, retângulo e círculo. |
| `mocida/tests/test_canvas_particles.c` | Teste interativo: partículas seguindo o mouse. |
| `docs/superpowers/specs/2026-06-23-mocida-canvas-2d-design.md` | Especificação aprovada (referência). |

> Nota: `mocida/CMakeLists.txt` usa `file(GLOB_RECURSE ... src/uikit/*.c)`, então `canvas.c` é compilado automaticamente. Não é necessário editar o CMakeLists.

---

## Task 1: Adicionar o tipo de widget `UI_WIDGET_CANVAS`

**Files:**
- Modify: `mocida/src/headers/uikit/widget.h:8`

- [ ] **Step 1: Adicionar a constante junto aos outros tipos**

```c
#define UI_WIDGET_CANVAS "@uikit/canvas"
```

Inserir logo abaixo de `#define UI_WIDGET_IMAGE "@uikit/image"`.

- [ ] **Step 2: Verificar compilação parcial**

Run: `./build.sh c`

Expected: build continua passando (ainda não há uso do novo tipo).

- [ ] **Step 3: Commit**

```bash
git add mocida/src/headers/uikit/widget.h
git commit -m "feat(canvas): add UI_WIDGET_CANVAS type tag"
```

---

## Task 2: Criar o header público `canvas.h`

**Files:**
- Create: `mocida/src/headers/uikit/canvas.h`

- [ ] **Step 1: Escrever o header completo**

```c
#ifndef UIKIT_CANVAS_H
#define UIKIT_CANVAS_H

#include <SDL3/SDL.h>
#include <uikit/widget.h>
#include <uikit/mouse_area.h>
#include <uikit/color.h>
#include <uikit/image.h>

#define UI_WIDGET_CANVAS "@uikit/canvas"

typedef struct UICanvas UICanvas;

typedef void (*UICanvasFrameCallback)(UICanvas* canvas, SDL_Renderer* renderer, void* userdata);
typedef void (*UICanvasMouseCallback)(UICanvas* canvas, UIMouseEvent ev, void* userdata);
typedef void (*UICanvasKeyCallback)(UICanvas* canvas, const char* key, int mods, void* userdata);

/* Criação e conversão */
UICanvas* UICanvas_Create(float width, float height);
UIWidget* UICanvas_AsWidget(UICanvas* canvas);
UIWidget* UICanvas_ToWidget(UICanvas* canvas);
UICanvas* UICanvas_FromWidget(UIWidget* widget);
void      UICanvas_Destroy(UICanvas* canvas);

/* Escape hatch */
SDL_Renderer* UICanvas_GetRenderer(const UICanvas* canvas);

/* Estado do contexto */
UICanvas* UICanvas_SetFillColor  (UICanvas* canvas, UIColor color);
UICanvas* UICanvas_SetStrokeColor(UICanvas* canvas, UIColor color);
UICanvas* UICanvas_SetLineWidth  (UICanvas* canvas, float width);
UICanvas* UICanvas_SetGlobalAlpha(UICanvas* canvas, float alpha);
UICanvas* UICanvas_SetBlendMode  (UICanvas* canvas, SDL_BlendMode mode);
UICanvas* UICanvas_ClearRect     (UICanvas* canvas, float x, float y, float w, float h);

/* Primitivas imediatas */
UICanvas* UICanvas_FillRect  (UICanvas* canvas, float x, float y, float w, float h);
UICanvas* UICanvas_StrokeRect(UICanvas* canvas, float x, float y, float w, float h);

UICanvas* UICanvas_BeginPath(UICanvas* canvas);
UICanvas* UICanvas_MoveTo   (UICanvas* canvas, float x, float y);
UICanvas* UICanvas_LineTo   (UICanvas* canvas, float x, float y);
UICanvas* UICanvas_Arc      (UICanvas* canvas, float x, float y, float r,
                             float startAngle, float endAngle);
UICanvas* UICanvas_BezierCurveTo(UICanvas* canvas, float cp1x, float cp1y,
                                 float cp2x, float cp2y, float x, float y);
UICanvas* UICanvas_ClosePath(UICanvas* canvas);
UICanvas* UICanvas_Fill     (UICanvas* canvas);
UICanvas* UICanvas_Stroke   (UICanvas* canvas);

UICanvas* UICanvas_FillCircle  (UICanvas* canvas, float x, float y, float r);
UICanvas* UICanvas_StrokeCircle(UICanvas* canvas, float x, float y, float r);

UICanvas* UICanvas_DrawImage(UICanvas* canvas, UIImage* image, float x, float y,
                             float w, float h);
UICanvas* UICanvas_DrawText (UICanvas* canvas, const char* text, float x, float y,
                             const char* fontFamily, float size);

/* Coordenadas e transformação */
UICanvas* UICanvas_Save   (UICanvas* canvas);
UICanvas* UICanvas_Restore(UICanvas* canvas);
UICanvas* UICanvas_Translate(UICanvas* canvas, float x, float y);
UICanvas* UICanvas_Rotate  (UICanvas* canvas, float radians);
UICanvas* UICanvas_Scale   (UICanvas* canvas, float x, float y);

/* Callbacks */
UICanvas* UICanvas_OnFrame     (UICanvas* canvas, UICanvasFrameCallback cb, void* userdata);
UICanvas* UICanvas_OnMouseMove (UICanvas* canvas, UICanvasMouseCallback cb, void* userdata);
UICanvas* UICanvas_OnMouseDown (UICanvas* canvas, UICanvasMouseCallback cb, void* userdata);
UICanvas* UICanvas_OnMouseUp   (UICanvas* canvas, UICanvasMouseCallback cb, void* userdata);
UICanvas* UICanvas_OnHoverEnter(UICanvas* canvas, UICanvasMouseCallback cb, void* userdata);
UICanvas* UICanvas_OnHoverExit (UICanvas* canvas, UICanvasMouseCallback cb, void* userdata);
void      UICanvas_SetOnKeyDown(UICanvas* canvas, UICanvasKeyCallback cb, void* userdata);

/* Utilitários */
float  UICanvas_GetWidth      (const UICanvas* canvas);
float  UICanvas_GetHeight     (const UICanvas* canvas);
void   UICanvas_GetMousePos   (const UICanvas* canvas, float* x, float* y);
int    UICanvas_IsMouseInside (const UICanvas* canvas);

/* Interno: usado por app.c / window_render.inc */
void UICanvas_DispatchMouseMove(UIChildren* children, float x, float y);
void UICanvas_DispatchMouseDown(UIChildren* children, float x, float y, int button);
void UICanvas_DispatchMouseUp  (UIChildren* children, float x, float y, int button);

#endif // UIKIT_CANVAS_H
```

> Nota: a struct `UICanvas` é opaca; a implementação vive em `canvas.c`.

- [ ] **Step 2: Commit**

```bash
git add mocida/src/headers/uikit/canvas.h
git commit -m "feat(canvas): add public UICanvas header"
```

---

## Task 3: Implementar lifecycle e estado do canvas

**Files:**
- Create: `mocida/src/uikit/canvas.c`

- [ ] **Step 1: Criar o arquivo com struct, create, destroy e helpers de estado**

```c
#include <uikit/canvas.h>
#include <uikit/window.h>
#include <uikit/color.h>
#include <uikit/font.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <string.h>
#include <math.h>

#define UI_PATH_INITIAL_CAP 64
#define UI_STATE_INITIAL_CAP 8

typedef struct {
    UIColor fillColor;
    UIColor strokeColor;
    float lineWidth;
    float globalAlpha;
    SDL_BlendMode blendMode;
    float matrix[6];
} UICanvasState;

typedef enum {
    UI_PATH_CMD_MOVE,
    UI_PATH_CMD_LINE,
    UI_PATH_CMD_ARC,
    UI_PATH_CMD_BEZIER,
    UI_PATH_CMD_CLOSE
} UIPathCmdType;

typedef struct {
    UIPathCmdType type;
    float x, y;
    float r, startAngle, endAngle;
    float cp1x, cp1y, cp2x, cp2y;
} UIPathCmd;

struct UICanvas {
    const char* __widget_type;
    float width, height;

    UICanvasState* states;
    int stateCount;
    int stateCap;

    UIPathCmd* path;
    int pathCount;
    int pathCap;
    float pathStartX, pathStartY;
    float pathCurX, pathCurY;
    int pathHasCur;

    float mouseX, mouseY;
    int mouseInside;

    /* Renderizador ativo durante o frame atual (usado pelo escape hatch). */
    SDL_Renderer* currentRenderer;

    UICanvasFrameCallback onFrame;
    UICanvasMouseCallback onMouseMove;
    UICanvasMouseCallback onMouseDown;
    UICanvasMouseCallback onMouseUp;
    UICanvasMouseCallback onHoverEnter;
    UICanvasMouseCallback onHoverExit;
    UICanvasKeyCallback   onKeyDown;

    void* onFrameUd;
    void* onMouseMoveUd;
    void* onMouseDownUd;
    void* onMouseUpUd;
    void* onHoverEnterUd;
    void* onHoverExitUd;
    void* onKeyDownUd;
};

static void StateReset(UICanvasState* s) {
    s->fillColor   = UI_COLOR_BLACK;
    s->strokeColor = UI_COLOR_BLACK;
    s->lineWidth   = 1.0f;
    s->globalAlpha = 1.0f;
    s->blendMode   = SDL_BLENDMODE_BLEND;
    s->matrix[0] = 1.0f; s->matrix[1] = 0.0f; s->matrix[2] = 0.0f;
    s->matrix[3] = 1.0f; s->matrix[4] = 0.0f; s->matrix[5] = 0.0f;
}

UICanvas* UICanvas_Create(float width, float height) {
    UICanvas* c = (UICanvas*)calloc(1, sizeof(UICanvas));
    if (!c) return NULL;
    c->__widget_type = UI_WIDGET_CANVAS;
    c->width  = width;
    c->height = height;
    c->stateCap = UI_STATE_INITIAL_CAP;
    c->states = (UICanvasState*)calloc(c->stateCap, sizeof(UICanvasState));
    c->pathCap = UI_PATH_INITIAL_CAP;
    c->path = (UIPathCmd*)calloc(c->pathCap, sizeof(UIPathCmd));
    StateReset(&c->states[0]);
    c->stateCount = 1;
    return c;
}

void UICanvas_Destroy(UICanvas* canvas) {
    if (!canvas) return;
    free(canvas->states);
    free(canvas->path);
    free(canvas);
}

UIWidget* UICanvas_AsWidget(UICanvas* canvas) {
    return widgc((UIWidgetData)canvas);
}

UIWidget* UICanvas_ToWidget(UICanvas* canvas) {
    return widgcs((UIWidgetData)canvas, canvas->width, canvas->height);
}

UICanvas* UICanvas_FromWidget(UIWidget* widget) {
    if (!widget || !widget->data) return NULL;
    UIWidgetBase* base = (UIWidgetBase*)widget->data;
    if (!UIWidget_TypeIs(base->__widget_type, UI_WIDGET_CANVAS)) return NULL;
    return (UICanvas*)widget->data;
}

SDL_Renderer* UICanvas_GetRenderer(const UICanvas* canvas) {
    if (!canvas) return NULL;
    return canvas->currentRenderer;
}

float UICanvas_GetWidth (const UICanvas* canvas) { return canvas ? canvas->width  : 0.0f; }
float UICanvas_GetHeight(const UICanvas* canvas) { return canvas ? canvas->height : 0.0f; }
void  UICanvas_GetMousePos(const UICanvas* canvas, float* x, float* y) {
    if (!canvas) return;
    if (x) *x = canvas->mouseX;
    if (y) *y = canvas->mouseY;
}
int UICanvas_IsMouseInside(const UICanvas* canvas) { return canvas ? canvas->mouseInside : 0; }

static inline UICanvasState* CurState(UICanvas* c) {
    return &c->states[c->stateCount - 1];
}

UICanvas* UICanvas_SetFillColor(UICanvas* canvas, UIColor color) {
    if (!canvas) return NULL;
    CurState(canvas)->fillColor = color;
    return canvas;
}

UICanvas* UICanvas_SetStrokeColor(UICanvas* canvas, UIColor color) {
    if (!canvas) return NULL;
    CurState(canvas)->strokeColor = color;
    return canvas;
}

UICanvas* UICanvas_SetLineWidth(UICanvas* canvas, float width) {
    if (!canvas) return NULL;
    CurState(canvas)->lineWidth = width;
    return canvas;
}

UICanvas* UICanvas_SetGlobalAlpha(UICanvas* canvas, float alpha) {
    if (!canvas) return NULL;
    CurState(canvas)->globalAlpha = alpha;
    return canvas;
}

UICanvas* UICanvas_SetBlendMode(UICanvas* canvas, SDL_BlendMode mode) {
    if (!canvas) return NULL;
    CurState(canvas)->blendMode = mode;
    return canvas;
}

UICanvas* UICanvas_ClearRect(UICanvas* canvas, float x, float y, float w, float h) {
    if (!canvas) return NULL;
    SDL_Renderer* r = UICanvas_GetRenderer(canvas);
    if (!r) return canvas;
    SDL_FRect rect = { x, y, w, h };
    SDL_RenderFillRect(r, &rect); /* assume cor de fundo limpa via FillRect antes */
    return canvas;
}
```

- [ ] **Step 2: Implementar Save/Restore e transformações**

Adicionar no final de `canvas.c`:

```c
UICanvas* UICanvas_Save(UICanvas* canvas) {
    if (!canvas) return NULL;
    if (canvas->stateCount >= canvas->stateCap) {
        int newCap = canvas->stateCap * 2;
        UICanvasState* ns = (UICanvasState*)realloc(canvas->states, newCap * sizeof(UICanvasState));
        if (!ns) return canvas;
        canvas->states = ns;
        canvas->stateCap = newCap;
    }
    canvas->states[canvas->stateCount] = canvas->states[canvas->stateCount - 1];
    canvas->stateCount++;
    return canvas;
}

UICanvas* UICanvas_Restore(UICanvas* canvas) {
    if (!canvas || canvas->stateCount <= 1) return canvas;
    canvas->stateCount--;
    return canvas;
}

static void MatMul(float* out, const float* a, const float* b) {
    float r[6];
    r[0] = a[0]*b[0] + a[1]*b[3];
    r[1] = a[0]*b[1] + a[1]*b[4];
    r[2] = a[2]*b[0] + a[3]*b[3];
    r[3] = a[2]*b[1] + a[3]*b[4];
    r[4] = a[4]*b[0] + a[5]*b[3] + b[2];
    r[5] = a[4]*b[1] + a[5]*b[4] + b[5];
    memcpy(out, r, sizeof(r));
}

UICanvas* UICanvas_Translate(UICanvas* canvas, float x, float y) {
    if (!canvas) return NULL;
    float t[6] = { 1,0,0,1,x,y };
    MatMul(CurState(canvas)->matrix, t, CurState(canvas)->matrix);
    return canvas;
}

UICanvas* UICanvas_Rotate(UICanvas* canvas, float radians) {
    if (!canvas) return NULL;
    float c = cosf(radians), s = sinf(radians);
    float t[6] = { c,-s,s,c,0,0 };
    MatMul(CurState(canvas)->matrix, t, CurState(canvas)->matrix);
    return canvas;
}

UICanvas* UICanvas_Scale(UICanvas* canvas, float x, float y) {
    if (!canvas) return NULL;
    float t[6] = { x,0,0,y,0,0 };
    MatMul(CurState(canvas)->matrix, t, CurState(canvas)->matrix);
    return canvas;
}
```

- [ ] **Step 3: Build parcial para checar syntax**

Run: `./build.sh c`

Expected: compila (ainda não há uso do widget, mas o arquivo deve compilar).

- [ ] **Step 4: Commit**

```bash
git add mocida/src/uikit/canvas.c
git commit -m "feat(canvas): add canvas lifecycle, state stack and transforms"
```

---

## Task 4: Implementar primitivos de desenho

**Files:**
- Modify: `mocida/src/uikit/canvas.c`

- [ ] **Step 1: Adicionar helper de transformação de pontos e aplicar blend/alpha**

```c
static void ApplyState(UICanvas* canvas, SDL_Renderer* r) {
    UICanvasState* s = CurState(canvas);
    SDL_SetRenderDrawBlendMode(r, s->blendMode);
    SDL_SetRenderDrawColor(r,
        (Uint8)SDL_clamp((int)(s->fillColor.r), 0, 255),
        (Uint8)SDL_clamp((int)(s->fillColor.g), 0, 255),
        (Uint8)SDL_clamp((int)(s->fillColor.b), 0, 255),
        (Uint8)SDL_clamp((int)(s->fillColor.a * s->globalAlpha * 255.0f), 0, 255));
}

static void TransformPoint(UICanvas* canvas, float x, float y, float* ox, float* oy) {
    float* m = CurState(canvas)->matrix;
    *ox = m[0]*x + m[1]*y + m[2];
    *oy = m[3]*x + m[4]*y + m[5];
}

static void SetDrawColor(SDL_Renderer* r, UIColor c, float alpha) {
    SDL_SetRenderDrawColor(r,
        (Uint8)SDL_clamp((int)c.r, 0, 255),
        (Uint8)SDL_clamp((int)c.g, 0, 255),
        (Uint8)SDL_clamp((int)c.b, 0, 255),
        (Uint8)SDL_clamp((int)(c.a * alpha * 255.0f), 0, 255));
}
```

- [ ] **Step 2: Implementar retângulos e círculos**

```c
UICanvas* UICanvas_FillRect(UICanvas* canvas, float x, float y, float w, float h) {
    if (!canvas) return NULL;
    SDL_Renderer* r = UICanvas_GetRenderer(canvas);
    if (!r) return canvas;
    SetDrawColor(r, CurState(canvas)->fillColor, CurState(canvas)->globalAlpha);
    SDL_SetRenderDrawBlendMode(r, CurState(canvas)->blendMode);
    float x0, y0, x1, y1, x2, y2, x3, y3;
    TransformPoint(canvas, x,   y,   &x0, &y0);
    TransformPoint(canvas, x+w, y,   &x1, &y1);
    TransformPoint(canvas, x+w, y+h, &x2, &y2);
    TransformPoint(canvas, x,   y+h, &x3, &y3);
    SDL_Vertex v[4];
    v[0].position = (SDL_FPoint){ x0, y0 }; v[0].tex_coord = (SDL_FPoint){0,0}; v[0].color = (SDL_FColor){1,1,1,1};
    v[1].position = (SDL_FPoint){ x1, y1 }; v[1].tex_coord = (SDL_FPoint){0,0}; v[1].color = (SDL_FColor){1,1,1,1};
    v[2].position = (SDL_FPoint){ x2, y2 }; v[2].tex_coord = (SDL_FPoint){0,0}; v[2].color = (SDL_FColor){1,1,1,1};
    v[3].position = (SDL_FPoint){ x3, y3 }; v[3].tex_coord = (SDL_FPoint){0,0}; v[3].color = (SDL_FColor){1,1,1,1};
    int idx[6] = { 0,1,2, 0,2,3 };
    SDL_RenderGeometry(r, NULL, v, 4, idx, 6);
    return canvas;
}

UICanvas* UICanvas_StrokeRect(UICanvas* canvas, float x, float y, float w, float h) {
    if (!canvas) return NULL;
    SDL_Renderer* r = UICanvas_GetRenderer(canvas);
    if (!r) return canvas;
    SetDrawColor(r, CurState(canvas)->strokeColor, CurState(canvas)->globalAlpha);
    SDL_SetRenderDrawBlendMode(r, CurState(canvas)->blendMode);
    float x0, y0;
    TransformPoint(canvas, x, y, &x0, &y0);
    SDL_FRect rect = { x0, y0, w, h }; /* simplificado: não aplica rotação/escala no stroke */
    SDL_RenderRect(r, &rect);
    return canvas;
}

UICanvas* UICanvas_FillCircle(UICanvas* canvas, float x, float y, float radius) {
    if (!canvas || radius <= 0.0f) return canvas;
    SDL_Renderer* r = UICanvas_GetRenderer(canvas);
    if (!r) return canvas;
    const int segs = SDL_max(16, (int)(radius * 2.0f));
    SDL_Vertex* v = (SDL_Vertex*)calloc((size_t)(segs + 2), sizeof(SDL_Vertex));
    int* idx = (int*)calloc((size_t)(segs * 3), sizeof(int));
    if (!v || !idx) { free(v); free(idx); return canvas; }
    float cx, cy;
    TransformPoint(canvas, x, y, &cx, &cy);
    v[0].position = (SDL_FPoint){ cx, cy };
    v[0].color = (SDL_FColor){1,1,1,1};
    for (int i = 0; i <= segs; i++) {
        float a = (float)i / (float)segs * 2.0f * (float)M_PI;
        float px = cx + cosf(a) * radius;
        float py = cy + sinf(a) * radius;
        v[i+1].position = (SDL_FPoint){ px, py };
        v[i+1].color = (SDL_FColor){1,1,1,1};
    }
    for (int i = 0; i < segs; i++) {
        idx[i*3+0] = 0;
        idx[i*3+1] = i+1;
        idx[i*3+2] = i+2;
    }
    SetDrawColor(r, CurState(canvas)->fillColor, CurState(canvas)->globalAlpha);
    SDL_SetRenderDrawBlendMode(r, CurState(canvas)->blendMode);
    SDL_RenderGeometry(r, NULL, v, segs + 2, idx, segs * 3);
    free(v); free(idx);
    return canvas;
}

UICanvas* UICanvas_StrokeCircle(UICanvas* canvas, float x, float y, float radius) {
    if (!canvas || radius <= 0.0f) return canvas;
    SDL_Renderer* r = UICanvas_GetRenderer(canvas);
    if (!r) return canvas;
    float cx, cy;
    TransformPoint(canvas, x, y, &cx, &cy);
    SetDrawColor(r, CurState(canvas)->strokeColor, CurState(canvas)->globalAlpha);
    SDL_SetRenderDrawBlendMode(r, CurState(canvas)->blendMode);
    const int segs = SDL_max(16, (int)(radius * 2.0f));
    SDL_FPoint* pts = (SDL_FPoint*)calloc((size_t)(segs + 1), sizeof(SDL_FPoint));
    if (!pts) return canvas;
    for (int i = 0; i <= segs; i++) {
        float a = (float)i / (float)segs * 2.0f * (float)M_PI;
        pts[i].x = cx + cosf(a) * radius;
        pts[i].y = cy + sinf(a) * radius;
    }
    SDL_RenderLines(r, pts, segs + 1);
    free(pts);
    return canvas;
}
```

- [ ] **Step 3: Implementar DrawImage e DrawText**

```c
UICanvas* UICanvas_DrawImage(UICanvas* canvas, UIImage* image, float x, float y, float w, float h) {
    if (!canvas || !image) return canvas;
    SDL_Renderer* r = UICanvas_GetRenderer(canvas);
    if (!r) return canvas;
    /* `__SDL_texture` é campo público da struct UIImage (prefixo __ indica interno). */
    SDL_Texture* tex = image->__SDL_texture;
    if (!tex) return canvas;
    float x0, y0;
    TransformPoint(canvas, x, y, &x0, &y0);
    SDL_FRect dst = { x0, y0, w, h };
    SDL_SetTextureAlphaMod(tex, (Uint8)SDL_clamp((int)(CurState(canvas)->globalAlpha * 255.0f), 0, 255));
    SDL_SetTextureBlendMode(tex, CurState(canvas)->blendMode);
    SDL_RenderTexture(r, tex, NULL, &dst);
    SDL_SetTextureAlphaMod(tex, 255);
    return canvas;
}

/* Cache mínimo de fontes abertas: (path, size) -> TTF_Font* */
typedef struct {
    char* path;
    float size;
    TTF_Font* font;
} CanvasFontCacheEntry;

static CanvasFontCacheEntry* s_fontCache = NULL;
static int s_fontCacheCount = 0;
static int s_fontCacheCap = 0;

static TTF_Font* CanvasGetFont(const char* family, float size) {
    char* path = UIGetFont(family);
    if (!path) path = UIGetDefaultFontPath();
    if (!path) return NULL;
    for (int i = 0; i < s_fontCacheCount; i++) {
        if (s_fontCache[i].size == size && strcmp(s_fontCache[i].path, path) == 0) {
            return s_fontCache[i].font;
        }
    }
    TTF_Font* font = TTF_OpenFont(path, size);
    if (!font) return NULL;
    if (s_fontCacheCount >= s_fontCacheCap) {
        s_fontCacheCap = s_fontCacheCap ? s_fontCacheCap * 2 : 8;
        s_fontCache = (CanvasFontCacheEntry*)realloc(s_fontCache, s_fontCacheCap * sizeof(*s_fontCache));
    }
    s_fontCache[s_fontCacheCount].path = strdup(path);
    s_fontCache[s_fontCacheCount].size = size;
    s_fontCache[s_fontCacheCount].font = font;
    s_fontCacheCount++;
    return font;
}

UICanvas* UICanvas_DrawText(UICanvas* canvas, const char* text, float x, float y,
                            const char* fontFamily, float size) {
    if (!canvas || !text || !fontFamily || size <= 0.0f) return canvas;
    SDL_Renderer* r = UICanvas_GetRenderer(canvas);
    if (!r) return canvas;
    TTF_Font* font = CanvasGetFont(fontFamily, size);
    if (!font) return canvas;
    UIColor c = CurState(canvas)->fillColor;
    SDL_Color sc = { (Uint8)c.r, (Uint8)c.g, (Uint8)c.b,
                     (Uint8)SDL_clamp((int)(c.a * CurState(canvas)->globalAlpha * 255.0f), 0, 255) };
    SDL_Surface* surf = TTF_RenderText_Blended(font, text, strlen(text), sc);
    if (!surf) return canvas;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_DestroySurface(surf);
    if (!tex) return canvas;
    float tw, th;
    SDL_GetTextureSize(tex, &tw, &th);
    float x0, y0;
    TransformPoint(canvas, x, y, &x0, &y0);
    SDL_FRect dst = { x0, y0, tw, th };
    SDL_RenderTexture(r, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
    return canvas;
}
```

> Nota: se `GetFont` e `UIImage_GetSDLTexture` não estiverem expostos em headers públicos, declare `extern` no topo de `canvas.c` com os tipos corretos, ou reavalie para usar funções internas de `window_draw.inc`. Ajuste conforme necessário após o primeiro build.

- [ ] **Step 4: Build parcial**

Run: `./build.sh c`

Expected: compila.

- [ ] **Step 5: Commit**

```bash
git add mocida/src/uikit/canvas.c
git commit -m "feat(canvas): add immediate-mode drawing primitives"
```

---

## Task 5: Implementar paths

**Files:**
- Modify: `mocida/src/uikit/canvas.c`

- [ ] **Step 1: Adicionar helpers de path buffer**

```c
static UIPathCmd* PathPush(UICanvas* c, UIPathCmdType type) {
    if (c->pathCount >= c->pathCap) {
        int newCap = c->pathCap * 2;
        UIPathCmd* np = (UIPathCmd*)realloc(c->path, newCap * sizeof(UIPathCmd));
        if (!np) return NULL;
        c->path = np;
        c->pathCap = newCap;
    }
    UIPathCmd* cmd = &c->path[c->pathCount++];
    cmd->type = type;
    return cmd;
}
```

- [ ] **Step 2: Implementar comandos de path**

```c
UICanvas* UICanvas_BeginPath(UICanvas* canvas) {
    if (!canvas) return NULL;
    canvas->pathCount = 0;
    canvas->pathHasCur = 0;
    return canvas;
}

UICanvas* UICanvas_MoveTo(UICanvas* canvas, float x, float y) {
    if (!canvas) return NULL;
    UIPathCmd* cmd = PathPush(canvas, UI_PATH_CMD_MOVE);
    if (!cmd) return canvas;
    TransformPoint(canvas, x, y, &cmd->x, &cmd->y);
    canvas->pathStartX = canvas->pathCurX = cmd->x;
    canvas->pathStartY = canvas->pathCurY = cmd->y;
    canvas->pathHasCur = 1;
    return canvas;
}

UICanvas* UICanvas_LineTo(UICanvas* canvas, float x, float y) {
    if (!canvas) return NULL;
    UIPathCmd* cmd = PathPush(canvas, UI_PATH_CMD_LINE);
    if (!cmd) return canvas;
    TransformPoint(canvas, x, y, &cmd->x, &cmd->y);
    canvas->pathCurX = cmd->x;
    canvas->pathCurY = cmd->y;
    canvas->pathHasCur = 1;
    return canvas;
}

UICanvas* UICanvas_Arc(UICanvas* canvas, float x, float y, float r,
                       float startAngle, float endAngle) {
    if (!canvas) return NULL;
    UIPathCmd* cmd = PathPush(canvas, UI_PATH_CMD_ARC);
    if (!cmd) return canvas;
    TransformPoint(canvas, x, y, &cmd->x, &cmd->y);
    cmd->r = r; cmd->startAngle = startAngle; cmd->endAngle = endAngle;
    /* atualiza current point para o final do arco */
    float a = endAngle;
    canvas->pathCurX = cmd->x + cosf(a) * r;
    canvas->pathCurY = cmd->y + sinf(a) * r;
    canvas->pathHasCur = 1;
    return canvas;
}

UICanvas* UICanvas_BezierCurveTo(UICanvas* canvas, float cp1x, float cp1y,
                                 float cp2x, float cp2y, float x, float y) {
    if (!canvas) return NULL;
    UIPathCmd* cmd = PathPush(canvas, UI_PATH_CMD_BEZIER);
    if (!cmd) return canvas;
    TransformPoint(canvas, cp1x, cp1y, &cmd->cp1x, &cmd->cp1y);
    TransformPoint(canvas, cp2x, cp2y, &cmd->cp2x, &cmd->cp2y);
    TransformPoint(canvas, x, y, &cmd->x, &cmd->y);
    canvas->pathCurX = cmd->x;
    canvas->pathCurY = cmd->y;
    canvas->pathHasCur = 1;
    return canvas;
}

UICanvas* UICanvas_ClosePath(UICanvas* canvas) {
    if (!canvas || !canvas->pathHasCur) return canvas;
    UIPathCmd* cmd = PathPush(canvas, UI_PATH_CMD_CLOSE);
    if (!cmd) return canvas;
    canvas->pathCurX = canvas->pathStartX;
    canvas->pathCurY = canvas->pathStartY;
    return canvas;
}
```

- [ ] **Step 3: Implementar Fill e Stroke de path (MVP: polilinha)**

```c
static void PathToPoints(UICanvas* c, SDL_FPoint** outPts, int* outCount) {
    int count = 1;
    for (int i = 0; i < c->pathCount; i++) {
        UIPathCmd* cmd = &c->path[i];
        if (cmd->type == UI_PATH_CMD_MOVE) count++;
        else if (cmd->type == UI_PATH_CMD_LINE || cmd->type == UI_PATH_CMD_CLOSE) count++;
        else if (cmd->type == UI_PATH_CMD_ARC) {
            float sweep = cmd->endAngle - cmd->startAngle;
            while (sweep < 0) sweep += 2.0f * (float)M_PI;
            count += SDL_max(8, (int)(cmd->r * sweep));
        }
        else if (cmd->type == UI_PATH_CMD_BEZIER) count += 16;
    }
    SDL_FPoint* pts = (SDL_FPoint*)calloc((size_t)count, sizeof(SDL_FPoint));
    int n = 0;
    for (int i = 0; i < c->pathCount; i++) {
        UIPathCmd* cmd = &c->path[i];
        if (cmd->type == UI_PATH_CMD_MOVE) {
            pts[n++] = (SDL_FPoint){ cmd->x, cmd->y };
        } else if (cmd->type == UI_PATH_CMD_LINE) {
            pts[n++] = (SDL_FPoint){ cmd->x, cmd->y };
        } else if (cmd->type == UI_PATH_CMD_CLOSE) {
            pts[n++] = (SDL_FPoint){ c->pathStartX, c->pathStartY };
        } else if (cmd->type == UI_PATH_CMD_ARC) {
            float sweep = cmd->endAngle - cmd->startAngle;
            while (sweep < 0) sweep += 2.0f * (float)M_PI;
            int segs = SDL_max(8, (int)(cmd->r * sweep));
            for (int k = 0; k <= segs; k++) {
                float a = cmd->startAngle + (float)k / (float)segs * sweep;
                pts[n++] = (SDL_FPoint){ cmd->x + cosf(a)*cmd->r, cmd->y + sinf(a)*cmd->r };
            }
        } else if (cmd->type == UI_PATH_CMD_BEZIER) {
            for (int k = 0; k <= 16; k++) {
                float t = (float)k / 16.0f;
                float mt = 1.0f - t;
                float b0 = mt*mt*mt, b1 = 3*mt*mt*t, b2 = 3*mt*t*t, b3 = t*t*t;
                pts[n++] = (SDL_FPoint){
                    b0*c->pathCurX + b1*cmd->cp1x + b2*cmd->cp2x + b3*cmd->x,
                    b0*c->pathCurY + b1*cmd->cp1y + b2*cmd->cp2y + b3*cmd->y
                };
            }
        }
    }
    *outPts = pts;
    *outCount = n;
}

UICanvas* UICanvas_Stroke(UICanvas* canvas) {
    if (!canvas || canvas->pathCount == 0) return canvas;
    SDL_Renderer* r = UICanvas_GetRenderer(canvas);
    if (!r) return canvas;
    SDL_FPoint* pts = NULL;
    int n = 0;
    PathToPoints(canvas, &pts, &n);
    if (n > 1) {
        SetDrawColor(r, CurState(canvas)->strokeColor, CurState(canvas)->globalAlpha);
        SDL_SetRenderDrawBlendMode(r, CurState(canvas)->blendMode);
        SDL_RenderLines(r, pts, n);
    }
    free(pts);
    return canvas;
}

UICanvas* UICanvas_Fill(UICanvas* canvas) {
    if (!canvas || canvas->pathCount == 0) return canvas;
    SDL_Renderer* r = UICanvas_GetRenderer(canvas);
    if (!r) return canvas;
    SDL_FPoint* pts = NULL;
    int n = 0;
    PathToPoints(canvas, &pts, &n);
    if (n > 2) {
        SDL_Vertex* v = (SDL_Vertex*)calloc((size_t)n, sizeof(SDL_Vertex));
        int* idx = (int*)calloc((size_t)((n-2)*3), sizeof(int));
        if (v && idx) {
            for (int i = 0; i < n; i++) {
                v[i].position = pts[i];
                v[i].color = (SDL_FColor){1,1,1,1};
            }
            for (int i = 0; i < n - 2; i++) {
                idx[i*3+0] = 0;
                idx[i*3+1] = i+1;
                idx[i*3+2] = i+2;
            }
            SetDrawColor(r, CurState(canvas)->fillColor, CurState(canvas)->globalAlpha);
            SDL_SetRenderDrawBlendMode(r, CurState(canvas)->blendMode);
            SDL_RenderGeometry(r, NULL, v, n, idx, (n-2)*3);
        }
        free(v); free(idx);
    }
    free(pts);
    return canvas;
}
```

> Limitação consciente do MVP: `Fill` usa fan triangulation, que funciona para paths convexos/simples. Paths auto-intersectantes ou com buracos serão tratados em milestone futura.

- [ ] **Step 4: Build parcial**

Run: `./build.sh c`

Expected: compila.

- [ ] **Step 5: Commit**

```bash
git add mocida/src/uikit/canvas.c
git commit -m "feat(canvas): add path commands and fill/stroke"
```

---

## Task 6: Implementar callbacks e dispatch de eventos

**Files:**
- Modify: `mocida/src/uikit/canvas.c`

- [ ] **Step 1: Adicionar setters de callbacks**

```c
UICanvas* UICanvas_OnFrame     (UICanvas* canvas, UICanvasFrameCallback cb, void* userdata) {
    if (!canvas) return NULL; canvas->onFrame = cb; canvas->onFrameUd = userdata; return canvas;
}
UICanvas* UICanvas_OnMouseMove (UICanvas* canvas, UICanvasMouseCallback cb, void* userdata) {
    if (!canvas) return NULL; canvas->onMouseMove = cb; canvas->onMouseMoveUd = userdata; return canvas;
}
UICanvas* UICanvas_OnMouseDown (UICanvas* canvas, UICanvasMouseCallback cb, void* userdata) {
    if (!canvas) return NULL; canvas->onMouseDown = cb; canvas->onMouseDownUd = userdata; return canvas;
}
UICanvas* UICanvas_OnMouseUp   (UICanvas* canvas, UICanvasMouseCallback cb, void* userdata) {
    if (!canvas) return NULL; canvas->onMouseUp = cb; canvas->onMouseUpUd = userdata; return canvas;
}
UICanvas* UICanvas_OnHoverEnter(UICanvas* canvas, UICanvasMouseCallback cb, void* userdata) {
    if (!canvas) return NULL; canvas->onHoverEnter = cb; canvas->onHoverEnterUd = userdata; return canvas;
}
UICanvas* UICanvas_OnHoverExit (UICanvas* canvas, UICanvasMouseCallback cb, void* userdata) {
    if (!canvas) return NULL; canvas->onHoverExit = cb; canvas->onHoverExitUd = userdata; return canvas;
}
void UICanvas_SetOnKeyDown(UICanvas* canvas, UICanvasKeyCallback cb, void* userdata) {
    if (!canvas) return;
    canvas->onKeyDown = cb;
    canvas->onKeyDownUd = userdata;
}
```

- [ ] **Step 2: Implementar dispatchers de mouse**

```c
static int HitTest(UIWidget* w, float x, float y) {
    if (!w || !w->visible || !w->width || !w->height) return 0;
    return (x >= w->x && x < w->x + *w->width &&
            y >= w->y && y < w->y + *w->height);
}

static void CanvasDispatchRecursive(UIChildren* children, float x, float y,
                                    void (*cb)(UICanvas*, UIMouseEvent, void*),
                                    int type, int button) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible) continue;
        UIWidgetBase* base = (UIWidgetBase*)w->data;
        if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_CANVAS)) {
            UICanvas* c = (UICanvas*)base;
            if (HitTest(w, x, y)) {
                UIMouseEvent ev = {
                    .x = x - w->x, .y = y - w->y,
                    .dx = x - c->mouseX, .dy = y - c->mouseY,
                    .button = button
                };
                c->mouseX = ev.x; c->mouseY = ev.y;
                if (type == 0 && cb) cb(c, ev, c->onMouseMoveUd);
                if (type == 1 && cb) cb(c, ev, c->onMouseDownUd);
                if (type == 2 && cb) cb(c, ev, c->onMouseUpUd);
            }
        }
        /* recursão em containers: Stack, Glass, Rectangle, etc. */
        /* Implementação mínima: se o widget tiver children, descer. */
    }
}

void UICanvas_DispatchMouseMove(UIChildren* children, float x, float y) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible) continue;
        UIWidgetBase* base = (UIWidgetBase*)w->data;
        if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_CANVAS)) {
            UICanvas* c = (UICanvas*)base;
            int inside = HitTest(w, x, y);
            if (inside && !c->mouseInside && c->onHoverEnter) {
                UIMouseEvent ev = { .x = x - w->x, .y = y - w->y, .dx = 0, .dy = 0, .button = 0 };
                c->onHoverEnter(c, ev, c->onHoverEnterUd);
            }
            if (!inside && c->mouseInside && c->onHoverExit) {
                UIMouseEvent ev = { .x = x - w->x, .y = y - w->y, .dx = 0, .dy = 0, .button = 0 };
                c->onHoverExit(c, ev, c->onHoverExitUd);
            }
            c->mouseInside = inside;
            if (inside && c->onMouseMove) {
                UIMouseEvent ev = {
                    .x = x - w->x, .y = y - w->y,
                    .dx = x - c->mouseX, .dy = y - c->mouseY,
                    .button = 0
                };
                c->mouseX = ev.x; c->mouseY = ev.y;
                c->onMouseMove(c, ev, c->onMouseMoveUd);
            } else {
                c->mouseX = x - w->x; c->mouseY = y - w->y;
            }
        }
    }
}

void UICanvas_DispatchMouseDown(UIChildren* children, float x, float y, int button) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible) continue;
        UIWidgetBase* base = (UIWidgetBase*)w->data;
        if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_CANVAS) && HitTest(w, x, y)) {
            UICanvas* c = (UICanvas*)base;
            UIMouseEvent ev = {
                .x = x - w->x, .y = y - w->y,
                .dx = 0, .dy = 0, .button = button
            };
            c->mouseX = ev.x; c->mouseY = ev.y;
            if (c->onMouseDown) c->onMouseDown(c, ev, c->onMouseDownUd);
            UIWidget_SetFocus(w, 1);
        }
    }
}

void UICanvas_DispatchMouseUp(UIChildren* children, float x, float y, int button) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible) continue;
        UIWidgetBase* base = (UIWidgetBase*)w->data;
        if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_CANVAS) && HitTest(w, x, y)) {
            UICanvas* c = (UICanvas*)base;
            UIMouseEvent ev = {
                .x = x - w->x, .y = y - w->y,
                .dx = 0, .dy = 0, .button = button
            };
            c->mouseX = ev.x; c->mouseY = ev.y;
            if (c->onMouseUp) c->onMouseUp(c, ev, c->onMouseUpUd);
        }
    }
}
```

> Nota: o dispatch recursivo em containers (Stack, Glass, Rectangle, etc.) deve ser estendido para cobrir canvas aninhados. Para o primeiro teste (canvas root), a iteração direta em `children` é suficiente.

- [ ] **Step 3: Build parcial**

Run: `./build.sh c`

Expected: compila.

- [ ] **Step 4: Commit**

```bash
git add mocida/src/uikit/canvas.c
git commit -m "feat(canvas): add mouse event callbacks and dispatchers"
```

---

## Task 7: Integrar renderização e eventos no mocida

**Files:**
- Modify: `mocida/src/uikit/window_widgets.inc`
- Modify: `mocida/src/uikit/window_render.inc`
- Modify: `mocida/src/uikit/app.c`

- [ ] **Step 1: Adicionar `RenderWidget_Canvas` em `window_widgets.inc`**

No final de `mocida/src/uikit/window_widgets.inc`, antes de qualquer outro widget ou após o último:

```c
static void RenderWidget_Canvas(UIWindow* window, UIWidget* el,
                                UIWidgetBase* base, float op) {
    UICanvas* canvas = (UICanvas*)base;
    if (!el->width || !el->height) return;
    (void)op; /* opacidade do widget será tratada em milestone futura (requer offscreen) */

    /* Flush batches anteriores antes de mudar o clip */
    FlushRenderBatch(window->sdlRenderer);

    SDL_Rect clip = {
        (int)el->x, (int)el->y,
        (int)ceilf(*el->width), (int)ceilf(*el->height)
    };
    SDL_SetRenderClipRect(window->sdlRenderer, &clip);

    canvas->currentRenderer = window->sdlRenderer;
    if (canvas->onFrame) {
        canvas->onFrame(canvas, window->sdlRenderer, canvas->onFrameUd);
    }
    canvas->currentRenderer = NULL;

    FlushRenderBatch(window->sdlRenderer);
    SDL_SetRenderClipRect(window->sdlRenderer, NULL);
}
```

- [ ] **Step 2: Adicionar dispatch em `window_render.inc`**

Antes do `RenderSingleWidget_Inner` retornar sem ação, adicionar:

```c
if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_CANVAS)) {
    RenderWidget_Canvas(window, el, base, op);
    return;
}
```

- [ ] **Step 3: Chamar dispatchers de canvas em `app.c`**

No handler de `SDL_EVENT_MOUSE_MOTION` (próximo às linhas 627-636), adicionar:

```c
UICanvas_DispatchMouseMotion(app->window->children, rx, ry);
```

No handler de `SDL_EVENT_MOUSE_BUTTON_DOWN`, adicionar após `OcclusionBegin`:

```c
UICanvas_DispatchMouseDown(app->window->children, rx, ry, button);
```

No handler de `SDL_EVENT_MOUSE_BUTTON_UP`, adicionar:

```c
UICanvas_DispatchMouseUp(app->window->children, rx, ry, button);
```

> Importante: manter a ordem dos dispatchers existentes. Colocar `UICanvas_DispatchMouseMotion` após `UIMouseArea_DispatchMouseMotion` ou antes, conforme a semântica desejada. Para o canvas capturar eventos sob MouseArea, considere a ordem de z-index/occlusion.

- [ ] **Step 4: Build completo**

Run: `./build.sh c`

Expected: compila com sucesso.

- [ ] **Step 5: Commit**

```bash
git add mocida/src/uikit/window_widgets.inc mocida/src/uikit/window_render.inc mocida/src/uikit/app.c
git commit -m "feat(canvas): wire UICanvas render and event dispatch into mocida"
```

---

## Task 8: Criar teste básico `test_canvas_basic.c`

**Files:**
- Create: `mocida/tests/test_canvas_basic.c`

- [ ] **Step 1: Escrever o teste**

```c
#include <uikit/app.h>
#include <uikit/canvas.h>

static void OnFrame(UICanvas* canvas, SDL_Renderer* renderer, void* userdata) {
    (void)userdata; (void)renderer;

    /* Fundo azul */
    UICanvas_SetFillColor(canvas, UI_COLOR_NAVY);
    UICanvas_FillRect(canvas, 0, 0, UICanvas_GetWidth(canvas), UICanvas_GetHeight(canvas));

    /* Retângulo vermelho */
    UICanvas_SetFillColor(canvas, UI_COLOR_RED);
    UICanvas_FillRect(canvas, 50, 50, 200, 120);

    /* Círculo verde */
    UICanvas_SetFillColor(canvas, UI_COLOR_GREEN);
    UICanvas_FillCircle(canvas, 400, 250, 80);

    /* Path amarelo em forma de triângulo */
    UICanvas_SetFillColor(canvas, UI_COLOR_YELLOW);
    UICanvas_BeginPath(canvas);
    UICanvas_MoveTo(canvas, 600, 100);
    UICanvas_LineTo(canvas, 700, 300);
    UICanvas_LineTo(canvas, 500, 300);
    UICanvas_ClosePath(canvas);
    UICanvas_Fill(canvas);

    /* Texto branco */
    UICanvas_SetFillColor(canvas, UI_COLOR_WHITE);
    UICanvas_DrawText(canvas, "Canvas 2D OK", 50, 400, "Inter", 32.0f);
}

int main() {
    UIApp* app = UIApp_Create("test_canvas_basic", 800, 600);
    if (!app) return 1;

    UICanvas* canvas = UICanvas_Create(800, 600);
    UICanvas_OnFrame(canvas, OnFrame, NULL);

    UIChildren* children = UIChildren_Create(1);
    UIChildren_Add(children, widgcs(canvas, 800.0f, 600.0f));
    UIApp_SetChildren(app, children);

    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
```

> Nota: ajustar a fonte `"Inter"` para uma fonte disponível no projeto (ex.: `"Inter"`, `"Roboto"`, `"Arial"` ou a fonte padrão usada pelos exemplos do mocida).

- [ ] **Step 2: Build com testes**

Run: `./build.sh c --tests`

Expected: `test_canvas_basic` é compilado com sucesso.

- [ ] **Step 3: Executar o teste**

Run: `./mocida/build/darwin/debug/test_canvas_basic` (ajustar `darwin` para `linux`/`win32` conforme a plataforma)

Expected: janela abre mostrando fundo azul, retângulo vermelho, círculo verde, triângulo amarelo e texto.

- [ ] **Step 4: Commit**

```bash
git add mocida/tests/test_canvas_basic.c
git commit -m "test(canvas): add basic canvas rendering test"
```

---

## Task 9: Criar teste interativo `test_canvas_particles.c`

**Files:**
- Create: `mocida/tests/test_canvas_particles.c`

- [ ] **Step 1: Escrever o teste de partículas seguindo o mouse**

```c
#include <uikit/app.h>
#include <uikit/canvas.h>
#include <stdlib.h>

#define MAX_PARTICLES 256

typedef struct {
    float x, y;
    float vx, vy;
    float life;
    float maxLife;
} Particle;

typedef struct {
    float mouseX, mouseY;
    Particle particles[MAX_PARTICLES];
    int count;
} State;

static void SpawnParticle(State* s, float x, float y) {
    if (s->count >= MAX_PARTICLES) return;
    Particle* p = &s->particles[s->count++];
    p->x = x; p->y = y;
    p->vx = ((float)rand() / (float)RAND_MAX - 0.5f) * 4.0f;
    p->vy = ((float)rand() / (float)RAND_MAX - 0.5f) * 4.0f;
    p->maxLife = 1.0f + ((float)rand() / (float)RAND_MAX) * 1.0f;
    p->life = p->maxLife;
}

static void OnFrame(UICanvas* canvas, SDL_Renderer* renderer, void* userdata) {
    State* s = (State*)userdata;

    /* Fundo preto */
    UICanvas_SetFillColor(canvas, UI_COLOR_BLACK);
    UICanvas_FillRect(canvas, 0, 0, UICanvas_GetWidth(canvas), UICanvas_GetHeight(canvas));

    /* Spawn em cima do mouse */
    for (int i = 0; i < 3; i++) SpawnParticle(s, s->mouseX, s->mouseY);

    /* Atualizar e desenhar partículas */
    for (int i = 0; i < s->count; ) {
        Particle* p = &s->particles[i];
        p->x += p->vx;
        p->y += p->vy;
        p->life -= 0.016f;

        if (p->life <= 0.0f) {
            *p = s->particles[--s->count];
            continue;
        }

        float a = p->life / p->maxLife;
        UIColor c = UI_COLOR_WHITE; c.a = a;
        UICanvas_SetFillColor(canvas, c);
        UICanvas_FillCircle(canvas, p->x, p->y, 4.0f * a);
        i++;
    }

    /* Texto informativo */
    UICanvas_SetFillColor(canvas, UI_COLOR_WHITE);
    UICanvas_DrawText(canvas, "Mova o mouse", 10, 10, "Inter", 18.0f);
}

static void OnMouseMove(UICanvas* canvas, UIMouseEvent ev, void* userdata) {
    State* s = (State*)userdata;
    s->mouseX = ev.x;
    s->mouseY = ev.y;
}

int main() {
    UIApp* app = UIApp_Create("test_canvas_particles", 800, 600);
    if (!app) return 1;

    UICanvas* canvas = UICanvas_Create(800, 600);
    State state = {0};
    state.mouseX = 400.0f;
    state.mouseY = 300.0f;

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

- [ ] **Step 2: Build com testes**

Run: `./build.sh c --tests`

Expected: `test_canvas_particles` compila.

- [ ] **Step 3: Executar e interagir**

Run: `./mocida/build/darwin/debug/test_canvas_particles`

Expected: partículas brancas aparecem e desaparecem conforme o mouse se move.

- [ ] **Step 4: Commit**

```bash
git add mocida/tests/test_canvas_particles.c
git commit -m "test(canvas): add interactive particles test"
```

---

## Task 10: Verificações finais e commit do plano

- [ ] **Step 1: Build final completo**

Run: `./build.sh c --tests`

Expected: nenhum erro, ambos os testes gerados.

- [ ] **Step 2: Executar ambos os testes manualmente**

Run: `./mocida/build/darwin/debug/test_canvas_basic`
Run: `./mocida/build/darwin/debug/test_canvas_particles`

Expected: ambos rodam sem crash e mostram o conteúdo esperado.

- [ ] **Step 3: Revisar alterações**

Run: `git diff --stat`

Expected: arquivos esperados modificados/criados.

- [ ] **Step 4: Commit final**

```bash
git add docs/superpowers/plans/2026-06-23-mocida-canvas-2d.md
git commit -m "docs: add implementation plan for UICanvas"
```

---

## Self-Review do Plano

### 1. Cobertura da especificação

| Requisito do spec | Task que implementa |
|---|---|
| Widget `UICanvas` na árvore | Task 1, 7 |
| API de alto nível (retângulos, círculos, paths, texto, imagens) | Task 4, 5 |
| Escape hatch `UICanvas_GetRenderer` | Task 3 |
| Estado de contexto e transformações | Task 3 |
| Eventos de mouse e teclado | Task 6, 7 |
| Clipping integrado | Task 7 (`RenderWidget_Canvas`) |
| Testes básico e de partículas | Task 8, 9 |

### 2. Scan de placeholders

- Não há "TBD", "TODO" ou "implementar depois".
- Cada step inclui código ou comando concreto.
- Caminhos de arquivo são absolutos relativos ao repo.

### 3. Consistência de tipos

- `UICanvas*` usado consistentemente.
- `UIMouseEvent` usa coordenadas locais do canvas, como definido no spec.
- Callbacks usam os typedefs `UICanvasFrameCallback`, `UICanvasMouseCallback`, `UICanvasKeyCallback`.

### Gaps conhecidos a tratar durante a implementação

1. Recursão de eventos em containers (canvas dentro de Stack/Scroll) é um stub no plano; deve ser completada na implementação real.
2. Fonte `"Inter"` nos testes deve ser substituída pela fonte realmente disponível no projeto.
3. Cache global de fontes em `CanvasGetFont` não é liberado explicitamente; é aceitável para o MVP (process exit).
4. Aplicação da opacidade do widget (`op` em `RenderWidget_Canvas`) fica para milestone futura, pois requer renderização offscreen.

---

## Execution Handoff

**Plan complete and saved to `docs/superpowers/plans/2026-06-23-mocida-canvas-2d.md`. Two execution options:**

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** — Execute tasks in this session using `executing-plans`, batch execution with checkpoints.

**Which approach?**
