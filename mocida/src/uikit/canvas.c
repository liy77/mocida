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

static inline UICanvasState* CurState(UICanvas* c) {
    return &c->states[c->stateCount - 1];
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
    SDL_RenderFillRect(r, &rect);
    return canvas;
}

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
