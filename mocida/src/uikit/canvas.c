#include <uikit/canvas.h>
#include <uikit/window.h>
#include <uikit/color.h>
#include <uikit/font.h>
#include <uikit/stack.h>
#include <uikit/glass.h>
#include <uikit/container.h>
#include <uikit/rect.h>
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

/* ----------------------------------------------------------------------
 * Helpers de renderização
 * ---------------------------------------------------------------------- */

static void SetDrawColor(SDL_Renderer* r, UIColor c, float alpha) {
    SDL_SetRenderDrawColor(r,
        (Uint8)SDL_clamp((int)c.r, 0, 255),
        (Uint8)SDL_clamp((int)c.g, 0, 255),
        (Uint8)SDL_clamp((int)c.b, 0, 255),
        (Uint8)SDL_clamp((int)(c.a * alpha * 255.0f), 0, 255));
}

static void TransformPoint(UICanvas* canvas, float x, float y, float* ox, float* oy) {
    float* m = CurState(canvas)->matrix;
    *ox = m[0]*x + m[1]*y + m[2];
    *oy = m[3]*x + m[4]*y + m[5];
}

/* ----------------------------------------------------------------------
 * Cache mínimo de fontes abertas: (path, size) -> TTF_Font*
 * ---------------------------------------------------------------------- */

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
    if (!path) path = (char*)UIGetDefaultFontPath();
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

/* ----------------------------------------------------------------------
 * Primitivas imediatas
 * ---------------------------------------------------------------------- */

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
    SDL_FRect rect = { x0, y0, w, h };
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

UICanvas* UICanvas_DrawImage(UICanvas* canvas, UIImage* image, float x, float y, float w, float h) {
    if (!canvas || !image) return canvas;
    SDL_Renderer* r = UICanvas_GetRenderer(canvas);
    if (!r) return canvas;
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

/* ----------------------------------------------------------------------
 * Paths
 * ---------------------------------------------------------------------- */

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

/* ----------------------------------------------------------------------
 * Callbacks
 * ---------------------------------------------------------------------- */

UICanvas* UICanvas_OnFrame(UICanvas* canvas, UICanvasFrameCallback cb, void* userdata) {
    if (!canvas) return NULL;
    canvas->onFrame = cb;
    canvas->onFrameUd = userdata;
    return canvas;
}

UICanvas* UICanvas_OnMouseMove(UICanvas* canvas, UICanvasMouseCallback cb, void* userdata) {
    if (!canvas) return NULL;
    canvas->onMouseMove = cb;
    canvas->onMouseMoveUd = userdata;
    return canvas;
}

UICanvas* UICanvas_OnMouseDown(UICanvas* canvas, UICanvasMouseCallback cb, void* userdata) {
    if (!canvas) return NULL;
    canvas->onMouseDown = cb;
    canvas->onMouseDownUd = userdata;
    return canvas;
}

UICanvas* UICanvas_OnMouseUp(UICanvas* canvas, UICanvasMouseCallback cb, void* userdata) {
    if (!canvas) return NULL;
    canvas->onMouseUp = cb;
    canvas->onMouseUpUd = userdata;
    return canvas;
}

UICanvas* UICanvas_OnHoverEnter(UICanvas* canvas, UICanvasMouseCallback cb, void* userdata) {
    if (!canvas) return NULL;
    canvas->onHoverEnter = cb;
    canvas->onHoverEnterUd = userdata;
    return canvas;
}

UICanvas* UICanvas_OnHoverExit(UICanvas* canvas, UICanvasMouseCallback cb, void* userdata) {
    if (!canvas) return NULL;
    canvas->onHoverExit = cb;
    canvas->onHoverExitUd = userdata;
    return canvas;
}

void UICanvas_SetOnKeyDown(UICanvas* canvas, UICanvasKeyCallback cb, void* userdata) {
    if (!canvas) return;
    canvas->onKeyDown = cb;
    canvas->onKeyDownUd = userdata;
}

/* ----------------------------------------------------------------------
 * Event dispatch
 * ---------------------------------------------------------------------- */

static int CanvasHitTest(UIWidget* w, float x, float y) {
    if (!w || !w->visible || !w->width || !w->height) return 0;
    return (x >= w->x && x < w->x + *w->width &&
            y >= w->y && y < w->y + *w->height);
}

static void CanvasDispatchToWidget(UIWidget* w, float x, float y,
                                   void (*cb)(UICanvas*, UIMouseEvent, void*),
                                   void* ud, int type, int button) {
    UICanvas* c = (UICanvas*)w->data;
    UIMouseEvent ev = {
        .x = x - w->x, .y = y - w->y,
        .dx = x - c->mouseX, .dy = y - c->mouseY,
        .startX = 0, .startY = 0,
        .button = button
    };
    c->mouseX = ev.x;
    c->mouseY = ev.y;
    if (cb) cb(c, ev, ud);
}

static void CanvasDispatchRecursive(UIChildren* children, float x, float y,
                                    void (*cb)(UICanvas*, UIMouseEvent, void*),
                                    void* ud, int type, int button) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible) continue;
        UIWidgetBase* base = (UIWidgetBase*)w->data;
        if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_CANVAS)) {
            if (CanvasHitTest(w, x, y)) {
                CanvasDispatchToWidget(w, x, y, cb, ud, type, button);
            }
        } else if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_STACK)) {
            UIStack* s = (UIStack*)base;
            CanvasDispatchRecursive(s->items, x, y, cb, ud, type, button);
        } else if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_GLASS)) {
            UIGlass* g = (UIGlass*)base;
            CanvasDispatchRecursive(g->items, x, y, cb, ud, type, button);
        } else if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_RECTANGLE)) {
            UIRectangle* r = (UIRectangle*)base;
            CanvasDispatchRecursive((UIChildren*)r->children, x, y, cb, ud, type, button);
        } else if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_GRID)) {
            UIGrid* g = (UIGrid*)base;
            CanvasDispatchRecursive(g->items, x, y, cb, ud, type, button);
        } else if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_SCROLL)) {
            UIScroll* s = (UIScroll*)base;
            CanvasDispatchRecursive((UIChildren*)s->content, x, y, cb, ud, type, button);
        }
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
            int inside = CanvasHitTest(w, x, y);
            if (inside && !c->mouseInside && c->onHoverEnter) {
                UIMouseEvent ev = { .x = x - w->x, .y = y - w->y, .dx = 0, .dy = 0, .startX = 0, .startY = 0, .button = 0 };
                c->onHoverEnter(c, ev, c->onHoverEnterUd);
            }
            if (!inside && c->mouseInside && c->onHoverExit) {
                UIMouseEvent ev = { .x = x - w->x, .y = y - w->y, .dx = 0, .dy = 0, .startX = 0, .startY = 0, .button = 0 };
                c->onHoverExit(c, ev, c->onHoverExitUd);
            }
            c->mouseInside = inside;
            if (inside && c->onMouseMove) {
                UIMouseEvent ev = {
                    .x = x - w->x, .y = y - w->y,
                    .dx = x - c->mouseX, .dy = y - c->mouseY,
                    .startX = 0, .startY = 0, .button = 0
                };
                c->mouseX = ev.x; c->mouseY = ev.y;
                c->onMouseMove(c, ev, c->onMouseMoveUd);
            } else {
                c->mouseX = x - w->x; c->mouseY = y - w->y;
            }
        } else if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_STACK)) {
            UIStack* s = (UIStack*)base;
            UICanvas_DispatchMouseMove(s->items, x, y);
        } else if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_GLASS)) {
            UIGlass* g = (UIGlass*)base;
            UICanvas_DispatchMouseMove(g->items, x, y);
        } else if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_RECTANGLE)) {
            UIRectangle* r = (UIRectangle*)base;
            UICanvas_DispatchMouseMove((UIChildren*)r->children, x, y);
        } else if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_GRID)) {
            UIGrid* g = (UIGrid*)base;
            UICanvas_DispatchMouseMove(g->items, x, y);
        } else if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_SCROLL)) {
            UIScroll* s = (UIScroll*)base;
            UICanvas_DispatchMouseMove((UIChildren*)s->content, x, y);
        }
    }
}

void UICanvas_DispatchMouseDown(UIChildren* children, float x, float y, int button) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible) continue;
        UIWidgetBase* base = (UIWidgetBase*)w->data;
        if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_CANVAS)) {
            if (CanvasHitTest(w, x, y)) {
                CanvasDispatchToWidget(w, x, y, NULL, NULL, 0, button);
                UICanvas* c = (UICanvas*)base;
                if (c->onMouseDown) {
                    UIMouseEvent ev = { .x = x - w->x, .y = y - w->y, .dx = 0, .dy = 0, .startX = 0, .startY = 0, .button = button };
                    c->onMouseDown(c, ev, c->onMouseDownUd);
                }
                UIWidget_SetFocus(w, 1);
            }
        } else if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_STACK)) {
            UIStack* s = (UIStack*)base;
            UICanvas_DispatchMouseDown(s->items, x, y, button);
        } else if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_GLASS)) {
            UIGlass* g = (UIGlass*)base;
            UICanvas_DispatchMouseDown(g->items, x, y, button);
        } else if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_RECTANGLE)) {
            UIRectangle* r = (UIRectangle*)base;
            UICanvas_DispatchMouseDown((UIChildren*)r->children, x, y, button);
        } else if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_GRID)) {
            UIGrid* g = (UIGrid*)base;
            UICanvas_DispatchMouseDown(g->items, x, y, button);
        } else if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_SCROLL)) {
            UIScroll* s = (UIScroll*)base;
            UICanvas_DispatchMouseDown((UIChildren*)s->content, x, y, button);
        }
    }
}

void UICanvas_DispatchMouseUp(UIChildren* children, float x, float y, int button) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible) continue;
        UIWidgetBase* base = (UIWidgetBase*)w->data;
        if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_CANVAS)) {
            if (CanvasHitTest(w, x, y)) {
                UICanvas* c = (UICanvas*)base;
                if (c->onMouseUp) {
                    UIMouseEvent ev = { .x = x - w->x, .y = y - w->y, .dx = 0, .dy = 0, .startX = 0, .startY = 0, .button = button };
                    c->onMouseUp(c, ev, c->onMouseUpUd);
                }
            }
        } else if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_STACK)) {
            UIStack* s = (UIStack*)base;
            UICanvas_DispatchMouseUp(s->items, x, y, button);
        } else if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_GLASS)) {
            UIGlass* g = (UIGlass*)base;
            UICanvas_DispatchMouseUp(g->items, x, y, button);
        } else if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_RECTANGLE)) {
            UIRectangle* r = (UIRectangle*)base;
            UICanvas_DispatchMouseUp((UIChildren*)r->children, x, y, button);
        } else if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_GRID)) {
            UIGrid* g = (UIGrid*)base;
            UICanvas_DispatchMouseUp(g->items, x, y, button);
        } else if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_SCROLL)) {
            UIScroll* s = (UIScroll*)base;
            UICanvas_DispatchMouseUp((UIChildren*)s->content, x, y, button);
        }
    }
}
