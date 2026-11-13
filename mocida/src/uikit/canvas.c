#include "canvas_internal.h"
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

/* Width of the anti-aliasing fringe around fills and strokes: 0.9 device
 * pixel, expressed in logical points for the current draw density. */
#define UI_CANVAS_AA (0.9f / UIWindow__DrawDensity())
/* Frames a cached text texture may go unused before it's evicted (~2 s). */
#define UI_CANVAS_TEXT_TTL 120u

typedef struct UICanvasTextEntry {
    char*        text;
    TTF_Font*    font;
    Uint32       rgba;
    SDL_Texture* tex;
    float        w, h;
    unsigned     lastFrame;
} UICanvasTextEntry;

static void StateReset(UICanvasState* s) {
    s->fillColor   = UI_COLOR_BLACK;
    s->strokeColor = UI_COLOR_BLACK;
    s->lineWidth   = 1.0f;
    s->globalAlpha = 1.0f;
    s->blendMode   = SDL_BLENDMODE_BLEND;
    // Affine 2x3 [a b c; d e f] mapping (x,y) -> (a*x+b*y+c, d*x+e*y+f).
    s->matrix[0] = 1.0f; s->matrix[1] = 0.0f; s->matrix[2] = 0.0f;
    s->matrix[3] = 0.0f; s->matrix[4] = 1.0f; s->matrix[5] = 0.0f;
}

static inline UICanvasState* CurState(UICanvas* c) {
    return &c->states[c->stateCount - 1];
}

/* Grow `*buf` to hold at least `need` elements (amortised doubling). */
static int Grow(void** buf, int* cap, int need, size_t elem) {
    if (need <= *cap) return 1;
    int nc = *cap > 0 ? *cap : 64;
    while (nc < need) nc *= 2;
    void* nb = realloc(*buf, (size_t)nc * elem);
    if (!nb) return 0;
    *buf = nb;
    *cap = nc;
    return 1;
}

UICanvas* UICanvas_Create(float width, float height) {
    UICanvas* c = (UICanvas*)calloc(1, sizeof(UICanvas));
    if (!c) return NULL;
    c->__widget_type = UI_WIDGET_CANVAS;
    c->width  = width;
    c->height = height;
    c->opacity = 1.0f;
    c->stateCap = UI_STATE_INITIAL_CAP;
    c->states = (UICanvasState*)calloc(c->stateCap, sizeof(UICanvasState));
    c->pathCap = UI_PATH_INITIAL_CAP;
    c->path = (UIPathCmd*)calloc(c->pathCap, sizeof(UIPathCmd));
    if (!c->states || !c->path) {
        free(c->states);
        free(c->path);
        free(c);
        return NULL;
    }
    StateReset(&c->states[0]);
    c->stateCount = 1;
    return c;
}

static void TextCacheClear(UICanvas* c) {
    for (int i = 0; i < c->textCount; i++) {
        if (c->texts[i].tex) SDL_DestroyTexture(c->texts[i].tex);
        free(c->texts[i].text);
    }
    c->textCount = 0;
}

void UICanvas_Destroy(UICanvas* canvas) {
    if (!canvas) return;
    TextCacheClear(canvas);
    free(canvas->texts);
    free(canvas->states);
    free(canvas->path);
    free(canvas->pts);
    free(canvas->subs);
    free(canvas->verts);
    free(canvas->idx);
    free(canvas->work);
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

/* m = m · t, both in the [a b c; d e f] layout TransformPoint reads. Post-
 * multiplying makes a transform apply to subsequent drawing in the current
 * (already transformed) space — the HTML canvas semantics. */
static void Concat(float* m, const float* t) {
    float r[6];
    r[0] = m[0]*t[0] + m[1]*t[3];
    r[1] = m[0]*t[1] + m[1]*t[4];
    r[2] = m[0]*t[2] + m[1]*t[5] + m[2];
    r[3] = m[3]*t[0] + m[4]*t[3];
    r[4] = m[3]*t[1] + m[4]*t[4];
    r[5] = m[3]*t[2] + m[4]*t[5] + m[5];
    memcpy(m, r, sizeof(r));
}

UICanvas* UICanvas_Translate(UICanvas* canvas, float x, float y) {
    if (!canvas) return NULL;
    const float t[6] = { 1, 0, x,  0, 1, y };
    Concat(CurState(canvas)->matrix, t);
    return canvas;
}

UICanvas* UICanvas_Rotate(UICanvas* canvas, float radians) {
    if (!canvas) return NULL;
    const float c = cosf(radians), s = sinf(radians);
    const float t[6] = { c, -s, 0,  s, c, 0 };
    Concat(CurState(canvas)->matrix, t);
    return canvas;
}

UICanvas* UICanvas_Scale(UICanvas* canvas, float x, float y) {
    if (!canvas) return NULL;
    const float t[6] = { x, 0, 0,  0, y, 0 };
    Concat(CurState(canvas)->matrix, t);
    return canvas;
}

/* ----------------------------------------------------------------------
 * Render helpers
 * ---------------------------------------------------------------------- */

static float Alpha(UICanvas* c) {
    return CurState(c)->globalAlpha * c->opacity;
}

/* UIColor (0..255 rgb, 0..1 a) -> SDL_FColor (0..1) for per-vertex coloring.
 * SDL_RenderGeometry with a NULL texture multiplies by the vertex color, so the
 * geometry fills MUST carry the real color here. */
static SDL_FColor FColorOf(UIColor c, float alpha) {
    return (SDL_FColor){
        SDL_clamp(c.r, 0.0f, 255.0f) / 255.0f,
        SDL_clamp(c.g, 0.0f, 255.0f) / 255.0f,
        SDL_clamp(c.b, 0.0f, 255.0f) / 255.0f,
        SDL_clamp(c.a * alpha, 0.0f, 1.0f),
    };
}

static void TransformPoint(UICanvas* canvas, float x, float y, float* ox, float* oy) {
    const float* m = CurState(canvas)->matrix;
    *ox = m[0]*x + m[1]*y + m[2] + canvas->originX;
    *oy = m[3]*x + m[4]*y + m[5] + canvas->originY;
}

/* Uniform scale of the current transform (for radii / line widths). */
static float MatrixScale(UICanvas* canvas) {
    const float* m = CurState(canvas)->matrix;
    const float det = fabsf(m[0]*m[4] - m[1]*m[3]);
    return det > 0.0f ? sqrtf(det) : 1.0f;
}

/* Internal: set the window-space origin of the canvas widget. All draw
 * coordinates are local to this. */
void UICanvas_SetOrigin(UICanvas* canvas, float x, float y) {
    if (!canvas) return;
    canvas->originX = x;
    canvas->originY = y;
}

void UICanvas_BeginFrame(UICanvas* canvas, SDL_Renderer* r, float x, float y, float opacity) {
    if (!canvas) return;
    canvas->currentRenderer = r;
    canvas->originX = x;
    canvas->originY = y;
    canvas->opacity = SDL_clamp(opacity, 0.0f, 1.0f);
    canvas->frameNo++;
    // Each frame starts from the base state with an empty path, so an
    // unbalanced Save() in a frame callback can't grow the stack forever and a
    // half-built path from the previous frame never leaks into this one.
    canvas->stateCount = 1;
    canvas->pathCount = 0;
    canvas->pathHasCur = 0;
    if (canvas->textRenderer && canvas->textRenderer != r) {
        TextCacheClear(canvas);   // textures belong to a different renderer
    }
    canvas->textRenderer = r;
}

void UICanvas_EndFrame(UICanvas* canvas) {
    if (!canvas) return;
    canvas->currentRenderer = NULL;
    canvas->originX = canvas->originY = 0.0f;
    canvas->opacity = 1.0f;
    // Evict text textures not drawn for a while.
    int keep = 0;
    for (int i = 0; i < canvas->textCount; i++) {
        UICanvasTextEntry* e = &canvas->texts[i];
        if (canvas->frameNo - e->lastFrame > UI_CANVAS_TEXT_TTL) {
            if (e->tex) SDL_DestroyTexture(e->tex);
            free(e->text);
        } else {
            canvas->texts[keep++] = *e;
        }
    }
    canvas->textCount = keep;
}

/* ----------------------------------------------------------------------
 * Font cache: (path, size) -> TTF_Font*
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
    if (!TTF_WasInit() && !TTF_Init()) return NULL;
    TTF_Font* font = TTF_OpenFont(path, size);
    if (!font) return NULL;
    if (s_fontCacheCount >= s_fontCacheCap) {
        int nc = s_fontCacheCap ? s_fontCacheCap * 2 : 8;
        CanvasFontCacheEntry* grown =
            (CanvasFontCacheEntry*)realloc(s_fontCache, (size_t)nc * sizeof(*s_fontCache));
        if (!grown) { TTF_CloseFont(font); return NULL; }
        s_fontCache = grown;
        s_fontCacheCap = nc;
    }
    char* dup = strdup(path);
    if (!dup) { TTF_CloseFont(font); return NULL; }
    s_fontCache[s_fontCacheCount].path = dup;
    s_fontCache[s_fontCacheCount].size = size;
    s_fontCache[s_fontCacheCount].font = font;
    s_fontCacheCount++;
    return font;
}

/* ----------------------------------------------------------------------
 * Geometry: anti-aliased polygon fill + polyline stroke
 * ---------------------------------------------------------------------- */

/* Twice the signed area (shoelace). */
static float PolyArea2(const SDL_FPoint* p, int n) {
    float a = 0.0f;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        a += (p[j].x * p[i].y) - (p[i].x * p[j].y);
    }
    return a;
}

static int PointInTri(SDL_FPoint p, SDL_FPoint a, SDL_FPoint b, SDL_FPoint c) {
    float d1 = (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
    float d2 = (p.x - c.x) * (b.y - c.y) - (b.x - c.x) * (p.y - c.y);
    float d3 = (p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y);
    int hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    int hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(hasNeg && hasPos);
}

/* True when every turn has the same sign as the polygon's winding. */
static int IsConvex(const SDL_FPoint* p, int n, float area2) {
    for (int i = 0; i < n; i++) {
        const SDL_FPoint a = p[i], b = p[(i + 1) % n], c = p[(i + 2) % n];
        const float cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
        if (cross * area2 < -1e-3f) return 0;
    }
    return 1;
}

/* Ear-clipping triangulation (handles concave outlines, e.g. browser-tab
 * silhouettes). Writes index triples into c->idx starting at `idxBase`;
 * returns the triangle count, 0 if it stalled (caller falls back to a fan). */
static int TriangulateEarClip(UICanvas* c, const SDL_FPoint* pts, int n, int idxBase, float area2) {
    if (n < 3 || !Grow((void**)&c->work, &c->workCap, n, sizeof(int))) return 0;
    int* V = c->work;
    // Index order with positive area so the convexity test below is uniform.
    for (int i = 0; i < n; i++) V[i] = area2 >= 0.0f ? i : n - 1 - i;

    int* out = c->idx + idxBase;
    int nv = n, tris = 0;
    int guard = 3 * nv;
    int v = nv - 1;
    while (nv > 2) {
        if (guard-- <= 0) return 0;
        int u = v % nv;
        v = (u + 1) % nv;
        int w = (v + 1) % nv;
        SDL_FPoint A = pts[V[u]], B = pts[V[v]], C = pts[V[w]];
        float cross = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
        int isEar = (cross >= 0.0f);
        if (isEar) {
            for (int k = 0; k < nv; k++) {
                if (k == u || k == v || k == w) continue;
                if (PointInTri(pts[V[k]], A, B, C)) { isEar = 0; break; }
            }
        }
        if (isEar) {
            out[tris*3+0] = V[u];
            out[tris*3+1] = V[v];
            out[tris*3+2] = V[w];
            tris++;
            memmove(&V[v], &V[v + 1], (size_t)(nv - 1 - v) * sizeof(int));
            nv--;
            guard = 3 * nv;
        }
    }
    return tris;
}

/* Fill a simple polygon (screen-space points) with an anti-aliased edge: the
 * interior is triangulated, then a thin fringe fading to transparent is laid
 * along every edge, offset outward along the vertex normals. One draw call. */
static void FillPolygonAA(UICanvas* c, const SDL_FPoint* p, int n, SDL_FColor fc) {
    SDL_Renderer* r = c->currentRenderer;
    if (!r || n < 3 || fc.a <= 0.0f) return;
    const float area2 = PolyArea2(p, n);
    if (fabsf(area2) < 1e-4f) return;

    const int maxIdx = (n - 2) * 3 + n * 6;
    if (!Grow((void**)&c->verts, &c->vertsCap, 2 * n, sizeof(SDL_Vertex)) ||
        !Grow((void**)&c->idx, &c->idxCap, maxIdx, sizeof(int))) return;

    int tris = IsConvex(p, n, area2) ? 0 : TriangulateEarClip(c, p, n, 0, area2);
    if (tris <= 0) {   // convex (or ear-clipping stalled): fan
        for (int i = 0; i < n - 2; i++) {
            c->idx[i*3+0] = 0;
            c->idx[i*3+1] = i + 1;
            c->idx[i*3+2] = i + 2;
        }
        tris = n - 2;
    }

    SDL_Vertex* v = c->verts;
    SDL_FColor clear = fc;
    clear.a = 0.0f;
    const float sgn = area2 > 0.0f ? 1.0f : -1.0f;
    for (int i = 0; i < n; i++) {
        const SDL_FPoint prev = p[(i + n - 1) % n], cur = p[i], next = p[(i + 1) % n];
        // Outward unit normals of the two edges meeting here.
        float e0x = cur.x - prev.x, e0y = cur.y - prev.y;
        float e1x = next.x - cur.x, e1y = next.y - cur.y;
        float l0 = sqrtf(e0x*e0x + e0y*e0y), l1 = sqrtf(e1x*e1x + e1y*e1y);
        float n0x = 0, n0y = 0, n1x = 0, n1y = 0;
        if (l0 > 1e-6f) { n0x = sgn * e0y / l0; n0y = -sgn * e0x / l0; }
        if (l1 > 1e-6f) { n1x = sgn * e1y / l1; n1y = -sgn * e1x / l1; }
        float mx = n0x + n1x, my = n0y + n1y;
        float ml = sqrtf(mx*mx + my*my);
        float ox = 0, oy = 0;
        if (ml > 1e-6f) {
            mx /= ml; my /= ml;
            // Miter length so the fringe keeps its width at corners (capped).
            float d = mx * n1x + my * n1y;
            float k = UI_CANVAS_AA / SDL_max(d, 0.35f);
            ox = mx * k; oy = my * k;
        }
        v[i]     = (SDL_Vertex){ cur, fc, {0, 0} };
        v[n + i] = (SDL_Vertex){ { cur.x + ox, cur.y + oy }, clear, {0, 0} };
    }
    int* idx = c->idx;
    int m = tris * 3;
    for (int i = 0; i < n; i++) {
        const int j = (i + 1) % n;
        idx[m++] = i;     idx[m++] = j;     idx[m++] = n + j;
        idx[m++] = i;     idx[m++] = n + j; idx[m++] = n + i;
    }
    SDL_SetRenderDrawBlendMode(r, CurState(c)->blendMode);
    SDL_RenderGeometry(r, NULL, v, 2 * n, idx, m);
}

/* Stroke a polyline as an anti-aliased ribbon: four rails across the line
 * (transparent edge, solid core, solid core, transparent edge) with mitred
 * joins, so `lineWidth` is honoured and thin lines stay smooth. */
static void StrokePolylineAA(UICanvas* c, const SDL_FPoint* p, int n, int closed,
                             float width, SDL_FColor fc) {
    SDL_Renderer* r = c->currentRenderer;
    if (!r || n < 2 || fc.a <= 0.0f) return;
    if (closed && n > 2 && p[0].x == p[n - 1].x && p[0].y == p[n - 1].y) n--;
    if (n < 2) return;

    // Hairlines narrower than a pixel draw at 1px with proportionally less alpha.
    if (width < 1.0f) { fc.a *= SDL_max(width, 0.0f); width = 1.0f; }
    const float core = SDL_max(width * 0.5f - UI_CANVAS_AA * 0.5f, 0.0f);
    const float edge = width * 0.5f + UI_CANVAS_AA * 0.5f;
    SDL_FColor clear = fc;
    clear.a = 0.0f;

    const int segs = closed ? n : n - 1;
    if (!Grow((void**)&c->verts, &c->vertsCap, 4 * n, sizeof(SDL_Vertex)) ||
        !Grow((void**)&c->idx, &c->idxCap, segs * 18, sizeof(int))) return;

    SDL_Vertex* v = c->verts;
    for (int i = 0; i < n; i++) {
        const int hasPrev = closed || i > 0, hasNext = closed || i < n - 1;
        const SDL_FPoint cur = p[i];
        float nx = 0, ny = 0, k = 1.0f;
        float n0x = 0, n0y = 0, n1x = 0, n1y = 0;
        if (hasPrev) {
            const SDL_FPoint a = p[(i + n - 1) % n];
            float dx = cur.x - a.x, dy = cur.y - a.y, l = sqrtf(dx*dx + dy*dy);
            if (l > 1e-6f) { n0x = -dy / l; n0y = dx / l; }
        }
        if (hasNext) {
            const SDL_FPoint b = p[(i + 1) % n];
            float dx = b.x - cur.x, dy = b.y - cur.y, l = sqrtf(dx*dx + dy*dy);
            if (l > 1e-6f) { n1x = -dy / l; n1y = dx / l; }
        }
        nx = n0x + n1x; ny = n0y + n1y;
        float nl = sqrtf(nx*nx + ny*ny);
        if (nl > 1e-6f) {
            nx /= nl; ny /= nl;
            const float refx = hasNext ? n1x : n0x, refy = hasNext ? n1y : n0y;
            k = 1.0f / SDL_max(nx * refx + ny * refy, 0.35f);   // miter, capped
        }
        v[i*4+0] = (SDL_Vertex){ { cur.x - nx*edge*k, cur.y - ny*edge*k }, clear, {0,0} };
        v[i*4+1] = (SDL_Vertex){ { cur.x - nx*core*k, cur.y - ny*core*k }, fc,    {0,0} };
        v[i*4+2] = (SDL_Vertex){ { cur.x + nx*core*k, cur.y + ny*core*k }, fc,    {0,0} };
        v[i*4+3] = (SDL_Vertex){ { cur.x + nx*edge*k, cur.y + ny*edge*k }, clear, {0,0} };
    }
    int* idx = c->idx;
    int m = 0;
    for (int s = 0; s < segs; s++) {
        const int a = s * 4, b = ((s + 1) % n) * 4;
        for (int rail = 0; rail < 3; rail++) {
            idx[m++] = a + rail; idx[m++] = b + rail;     idx[m++] = b + rail + 1;
            idx[m++] = a + rail; idx[m++] = b + rail + 1; idx[m++] = a + rail + 1;
        }
    }
    SDL_SetRenderDrawBlendMode(r, CurState(c)->blendMode);
    SDL_RenderGeometry(r, NULL, v, 4 * n, idx, m);
}

/* ----------------------------------------------------------------------
 * Immediate primitives
 * ---------------------------------------------------------------------- */

UICanvas* UICanvas_ClearRect(UICanvas* canvas, float x, float y, float w, float h) {
    if (!canvas) return NULL;
    SDL_Renderer* r = UICanvas_GetRenderer(canvas);
    if (!r) return canvas;
    // The canvas draws straight into the window, so "clear" paints the area
    // with the current fill colour (there's no backing store to make
    // transparent).
    SDL_FPoint q[4];
    TransformPoint(canvas, x,     y,     &q[0].x, &q[0].y);
    TransformPoint(canvas, x + w, y,     &q[1].x, &q[1].y);
    TransformPoint(canvas, x + w, y + h, &q[2].x, &q[2].y);
    TransformPoint(canvas, x,     y + h, &q[3].x, &q[3].y);
    const SDL_FColor fc = FColorOf(CurState(canvas)->fillColor, Alpha(canvas));
    SDL_Vertex v[4] = { { q[0], fc, {0,0} }, { q[1], fc, {0,0} },
                        { q[2], fc, {0,0} }, { q[3], fc, {0,0} } };
    const int idx[6] = { 0,1,2, 0,2,3 };
    SDL_SetRenderDrawBlendMode(r, CurState(canvas)->blendMode);
    SDL_RenderGeometry(r, NULL, v, 4, idx, 6);
    return canvas;
}

UICanvas* UICanvas_FillRect(UICanvas* canvas, float x, float y, float w, float h) {
    if (!canvas) return NULL;
    SDL_Renderer* r = UICanvas_GetRenderer(canvas);
    if (!r || w <= 0.0f || h <= 0.0f) return canvas;
    const SDL_FColor fc = FColorOf(CurState(canvas)->fillColor, Alpha(canvas));
    if (fc.a <= 0.0f) return canvas;
    const float* m = CurState(canvas)->matrix;
    SDL_FPoint q[4];
    TransformPoint(canvas, x,     y,     &q[0].x, &q[0].y);
    TransformPoint(canvas, x + w, y,     &q[1].x, &q[1].y);
    TransformPoint(canvas, x + w, y + h, &q[2].x, &q[2].y);
    TransformPoint(canvas, x,     y + h, &q[3].x, &q[3].y);
    if (m[1] != 0.0f || m[3] != 0.0f) {
        // Rotated/skewed rect: diagonal edges need the AA fringe.
        FillPolygonAA(canvas, q, 4, fc);
        return canvas;
    }
    // Axis-aligned: a plain quad is pixel-exact and cheapest.
    SDL_Vertex v[4] = { { q[0], fc, {0,0} }, { q[1], fc, {0,0} },
                        { q[2], fc, {0,0} }, { q[3], fc, {0,0} } };
    const int idx[6] = { 0,1,2, 0,2,3 };
    SDL_SetRenderDrawBlendMode(r, CurState(canvas)->blendMode);
    SDL_RenderGeometry(r, NULL, v, 4, idx, 6);
    return canvas;
}

UICanvas* UICanvas_StrokeRect(UICanvas* canvas, float x, float y, float w, float h) {
    if (!canvas || !UICanvas_GetRenderer(canvas)) return canvas;
    SDL_FPoint q[4];
    TransformPoint(canvas, x,     y,     &q[0].x, &q[0].y);
    TransformPoint(canvas, x + w, y,     &q[1].x, &q[1].y);
    TransformPoint(canvas, x + w, y + h, &q[2].x, &q[2].y);
    TransformPoint(canvas, x,     y + h, &q[3].x, &q[3].y);
    StrokePolylineAA(canvas, q, 4, 1,
                     CurState(canvas)->lineWidth * MatrixScale(canvas),
                     FColorOf(CurState(canvas)->strokeColor, Alpha(canvas)));
    return canvas;
}

/* Circle outline points into c->pts (screen space). Returns the count. */
static int CirclePoints(UICanvas* c, float x, float y, float radius) {
    float cx, cy;
    TransformPoint(c, x, y, &cx, &cy);
    const float rr = radius * MatrixScale(c);
    // ~1 segment per 2.5px of circumference: smooth at any size, no waste.
    const int segs = SDL_clamp((int)(2.0f * (float)M_PI * rr / 2.5f), 12, 256);
    if (!Grow((void**)&c->pts, &c->ptsCap, segs, sizeof(SDL_FPoint))) return 0;
    for (int i = 0; i < segs; i++) {
        const float a = (float)i / (float)segs * 2.0f * (float)M_PI;
        c->pts[i] = (SDL_FPoint){ cx + cosf(a) * rr, cy + sinf(a) * rr };
    }
    return segs;
}

UICanvas* UICanvas_FillCircle(UICanvas* canvas, float x, float y, float radius) {
    if (!canvas || radius <= 0.0f || !UICanvas_GetRenderer(canvas)) return canvas;
    const int n = CirclePoints(canvas, x, y, radius);
    FillPolygonAA(canvas, canvas->pts, n,
                  FColorOf(CurState(canvas)->fillColor, Alpha(canvas)));
    return canvas;
}

UICanvas* UICanvas_StrokeCircle(UICanvas* canvas, float x, float y, float radius) {
    if (!canvas || radius <= 0.0f || !UICanvas_GetRenderer(canvas)) return canvas;
    const int n = CirclePoints(canvas, x, y, radius);
    StrokePolylineAA(canvas, canvas->pts, n, 1,
                     CurState(canvas)->lineWidth * MatrixScale(canvas),
                     FColorOf(CurState(canvas)->strokeColor, Alpha(canvas)));
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
    const float s = MatrixScale(canvas);
    SDL_FRect dst = { x0, y0, w * s, h * s };
    SDL_SetTextureAlphaModFloat(tex, SDL_clamp(Alpha(canvas), 0.0f, 1.0f));
    SDL_SetTextureBlendMode(tex, CurState(canvas)->blendMode);
    SDL_RenderTexture(r, tex, NULL, &dst);
    SDL_SetTextureAlphaModFloat(tex, 1.0f);
    return canvas;
}

UICanvas* UICanvas_DrawImageRegion(UICanvas* canvas, UIImage* image,
                                   float sx, float sy, float sw, float sh,
                                   float x, float y, float w, float h,
                                   float angle, int flip) {
    if (!canvas || !image) return canvas;
    SDL_Renderer* r = UICanvas_GetRenderer(canvas);
    if (!r) return canvas;
    SDL_Texture* tex = image->__SDL_texture;
    if (!tex) return canvas;
    if (sw <= 0.0f || sh <= 0.0f) {
        float tw = 0.0f, th = 0.0f;
        SDL_GetTextureSize(tex, &tw, &th);
        sx = 0.0f; sy = 0.0f; sw = tw; sh = th;
    }
    float x0, y0;
    TransformPoint(canvas, x, y, &x0, &y0);
    const float s = MatrixScale(canvas);
    const SDL_FRect src = { sx, sy, sw, sh };
    const SDL_FRect dst = { x0, y0, w * s, h * s };
    const SDL_FlipMode mode = (SDL_FlipMode)(((flip & 1) ? SDL_FLIP_HORIZONTAL : 0) |
                                             ((flip & 2) ? SDL_FLIP_VERTICAL : 0));
    SDL_SetTextureAlphaModFloat(tex, SDL_clamp(Alpha(canvas), 0.0f, 1.0f));
    SDL_SetTextureBlendMode(tex, CurState(canvas)->blendMode);
    SDL_RenderTextureRotated(r, tex, &src, &dst, angle, NULL, mode);
    SDL_SetTextureAlphaModFloat(tex, 1.0f);
    return canvas;
}

/* Cached texture for (font, text, color) — rasterised once, reused every frame. */
static UICanvasTextEntry* TextEntry(UICanvas* c, SDL_Renderer* r, TTF_Font* font,
                                    const char* text, SDL_Color sc) {
    const Uint32 rgba = ((Uint32)sc.r << 24) | ((Uint32)sc.g << 16) | ((Uint32)sc.b << 8) | sc.a;
    for (int i = 0; i < c->textCount; i++) {
        UICanvasTextEntry* e = &c->texts[i];
        if (e->font == font && e->rgba == rgba && strcmp(e->text, text) == 0) {
            e->lastFrame = c->frameNo;
            return e;
        }
    }
    SDL_Surface* surf = TTF_RenderText_Blended(font, text, strlen(text), sc);
    if (!surf) return NULL;
    // Upload as RGBA32: some drivers drop the coverage alpha when converting
    // the ARGB8888 TTF surface implicitly (text drew as solid boxes).
    SDL_Surface* rgba32 = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surf);
    if (!rgba32) return NULL;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, rgba32);
    SDL_DestroySurface(rgba32);
    if (!tex) return NULL;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    char* dup = strdup(text);
    if (!dup || !Grow((void**)&c->texts, &c->textCap, c->textCount + 1, sizeof(UICanvasTextEntry))) {
        free(dup);
        SDL_DestroyTexture(tex);
        return NULL;
    }
    UICanvasTextEntry* e = &c->texts[c->textCount++];
    e->text = dup;
    e->font = font;
    e->rgba = rgba;
    e->tex = tex;
    SDL_GetTextureSize(tex, &e->w, &e->h);
    e->lastFrame = c->frameNo;
    return e;
}

UICanvas* UICanvas_DrawText(UICanvas* canvas, const char* text, float x, float y,
                            const char* fontFamily, float size) {
    if (!canvas || !text || !*text || !fontFamily || size <= 0.0f) return canvas;
    SDL_Renderer* r = UICanvas_GetRenderer(canvas);
    if (!r) return canvas;
    /* Rasterize at the display's pixel density (and the current transform's
     * scale) and draw back at logical size, so text is as sharp as UIText on
     * Retina instead of a 1x bitmap stretched 2x. */
    const float scale = MatrixScale(canvas);
    const float density = SDL_max(1.0f, UIWindow__DrawDensity());
    const float px = roundf(size * scale * density * 2.0f) / 2.0f;
    TTF_Font* font = CanvasGetFont(fontFamily, px);
    if (!font) return canvas;
    const UIColor c = CurState(canvas)->fillColor;
    const SDL_Color sc = {
        (Uint8)SDL_clamp((int)c.r, 0, 255), (Uint8)SDL_clamp((int)c.g, 0, 255),
        (Uint8)SDL_clamp((int)c.b, 0, 255),
        (Uint8)SDL_clamp((int)(c.a * Alpha(canvas) * 255.0f), 0, 255) };
    if (sc.a == 0) return canvas;
    UICanvasTextEntry* e = TextEntry(canvas, r, font, text, sc);
    if (!e) return canvas;
    float x0, y0;
    TransformPoint(canvas, x, y, &x0, &y0);
    /* Snap to device pixels: a half-pixel offset blurs the glyphs. */
    x0 = roundf(x0 * density) / density;
    y0 = roundf(y0 * density) / density;
    SDL_FRect dst = { x0, y0, e->w / density, e->h / density };
    SDL_RenderTexture(r, e->tex, NULL, &dst);
    return canvas;
}

/* ----------------------------------------------------------------------
 * Paths
 * ---------------------------------------------------------------------- */

static UIPathCmd* PathPush(UICanvas* c, UIPathCmdType type) {
    if (!Grow((void**)&c->path, &c->pathCap, c->pathCount + 1, sizeof(UIPathCmd))) return NULL;
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
    if (!canvas->pathHasCur) return UICanvas_MoveTo(canvas, x, y);
    UIPathCmd* cmd = PathPush(canvas, UI_PATH_CMD_LINE);
    if (!cmd) return canvas;
    TransformPoint(canvas, x, y, &cmd->x, &cmd->y);
    canvas->pathCurX = cmd->x;
    canvas->pathCurY = cmd->y;
    return canvas;
}

UICanvas* UICanvas_Arc(UICanvas* canvas, float x, float y, float r,
                       float startAngle, float endAngle) {
    if (!canvas) return NULL;
    UIPathCmd* cmd = PathPush(canvas, UI_PATH_CMD_ARC);
    if (!cmd) return canvas;
    TransformPoint(canvas, x, y, &cmd->x, &cmd->y);
    // Store the radius in screen space (uniform-scale approximation).
    cmd->r = r * MatrixScale(canvas);
    cmd->startAngle = startAngle;
    cmd->endAngle = endAngle;
    const float sx = cmd->x + cosf(startAngle) * cmd->r;
    const float sy = cmd->y + sinf(startAngle) * cmd->r;
    if (!canvas->pathHasCur) {
        // An arc with no current point starts a subpath at its first point.
        canvas->pathStartX = sx;
        canvas->pathStartY = sy;
    }
    canvas->pathCurX = cmd->x + cosf(endAngle) * cmd->r;
    canvas->pathCurY = cmd->y + sinf(endAngle) * cmd->r;
    canvas->pathHasCur = 1;
    return canvas;
}

UICanvas* UICanvas_BezierCurveTo(UICanvas* canvas, float cp1x, float cp1y,
                                 float cp2x, float cp2y, float x, float y) {
    if (!canvas) return NULL;
    if (!canvas->pathHasCur) UICanvas_MoveTo(canvas, cp1x, cp1y);
    UIPathCmd* cmd = PathPush(canvas, UI_PATH_CMD_BEZIER);
    if (!cmd) return canvas;
    TransformPoint(canvas, cp1x, cp1y, &cmd->cp1x, &cmd->cp1y);
    TransformPoint(canvas, cp2x, cp2y, &cmd->cp2x, &cmd->cp2y);
    TransformPoint(canvas, x, y, &cmd->x, &cmd->y);
    canvas->pathCurX = cmd->x;
    canvas->pathCurY = cmd->y;
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

/* Append a flattened point, skipping near-duplicates of the previous point in
 * the same subpath (they stall ear-clipping and create zero-length segments). */
static void PushPt(UICanvas* c, int* n, int subStart, float x, float y) {
    if (*n > subStart) {
        const SDL_FPoint q = c->pts[*n - 1];
        if (fabsf(q.x - x) < 0.01f && fabsf(q.y - y) < 0.01f) return;
    }
    if (!Grow((void**)&c->pts, &c->ptsCap, *n + 1, sizeof(SDL_FPoint))) return;
    c->pts[(*n)++] = (SDL_FPoint){ x, y };
}

static void PushSub(UICanvas* c, int* subs, int start) {
    if (!Grow((void**)&c->subs, &c->subsCap, *subs + 2, sizeof(int))) return;
    c->subs[(*subs)++] = start;
}

/* Flatten the recorded (already screen-space) path into c->pts, split into
 * subpaths: subpath s spans [c->subs[s], c->subs[s+1]). `closedMask` bit s is
 * set when subpath s ended with ClosePath. Returns the subpath count. */
static int FlattenPath(UICanvas* c, unsigned* closedMask) {
    int n = 0, subs = 0, subStart = 0;
    float curX = 0, curY = 0, startX = 0, startY = 0;
    *closedMask = 0;
    for (int i = 0; i < c->pathCount; i++) {
        const UIPathCmd* cmd = &c->path[i];
        switch (cmd->type) {
        case UI_PATH_CMD_MOVE:
            if (n > subStart) PushSub(c, &subs, subStart);
            subStart = n;
            PushPt(c, &n, subStart, cmd->x, cmd->y);
            curX = startX = cmd->x;
            curY = startY = cmd->y;
            break;
        case UI_PATH_CMD_LINE:
            // After a ClosePath the next subpath starts at the closed point.
            if (n == subStart) PushPt(c, &n, subStart, curX, curY);
            PushPt(c, &n, subStart, cmd->x, cmd->y);
            curX = cmd->x; curY = cmd->y;
            break;
        case UI_PATH_CMD_CLOSE:
            if (n > subStart) {
                if (subs < 32) *closedMask |= 1u << subs;
                PushSub(c, &subs, subStart);
                subStart = n;
            }
            curX = startX; curY = startY;
            break;
        case UI_PATH_CMD_ARC: {
            float sweep = cmd->endAngle - cmd->startAngle;
            while (sweep < 0) sweep += 2.0f * (float)M_PI;
            if (sweep > 2.0f * (float)M_PI) sweep = 2.0f * (float)M_PI;
            const int segs = SDL_clamp((int)(cmd->r * sweep / 2.5f), 6, 256);
            if (n == subStart) {   // arc opens the subpath
                startX = cmd->x + cosf(cmd->startAngle) * cmd->r;
                startY = cmd->y + sinf(cmd->startAngle) * cmd->r;
            }
            for (int k = 0; k <= segs; k++) {
                const float a = cmd->startAngle + (float)k / (float)segs * sweep;
                PushPt(c, &n, subStart, cmd->x + cosf(a) * cmd->r, cmd->y + sinf(a) * cmd->r);
            }
            curX = cmd->x + cosf(cmd->endAngle) * cmd->r;
            curY = cmd->y + sinf(cmd->endAngle) * cmd->r;
            break;
        }
        case UI_PATH_CMD_BEZIER: {
            // Adaptive subdivision from the control-polygon length.
            const float len = hypotf(cmd->cp1x - curX, cmd->cp1y - curY)
                            + hypotf(cmd->cp2x - cmd->cp1x, cmd->cp2y - cmd->cp1y)
                            + hypotf(cmd->x - cmd->cp2x, cmd->y - cmd->cp2y);
            const int segs = SDL_clamp((int)(len / 3.0f), 3, 48);
            if (n == subStart) PushPt(c, &n, subStart, curX, curY);
            for (int k = 1; k <= segs; k++) {
                const float t = (float)k / (float)segs, mt = 1.0f - t;
                const float b0 = mt*mt*mt, b1 = 3*mt*mt*t, b2 = 3*mt*t*t, b3 = t*t*t;
                PushPt(c, &n, subStart,
                       b0*curX + b1*cmd->cp1x + b2*cmd->cp2x + b3*cmd->x,
                       b0*curY + b1*cmd->cp1y + b2*cmd->cp2y + b3*cmd->y);
            }
            curX = cmd->x; curY = cmd->y;
            break;
        }
        }
    }
    if (n > subStart) PushSub(c, &subs, subStart);
    if (Grow((void**)&c->subs, &c->subsCap, subs + 1, sizeof(int))) c->subs[subs] = n;
    return subs;
}

UICanvas* UICanvas_Stroke(UICanvas* canvas) {
    if (!canvas || canvas->pathCount == 0 || !UICanvas_GetRenderer(canvas)) return canvas;
    unsigned closed = 0;
    const int subs = FlattenPath(canvas, &closed);
    const SDL_FColor fc = FColorOf(CurState(canvas)->strokeColor, Alpha(canvas));
    const float width = CurState(canvas)->lineWidth * MatrixScale(canvas);
    for (int s = 0; s < subs; s++) {
        const int a = canvas->subs[s], b = canvas->subs[s + 1];
        // Stroking reuses c->verts/idx but reads c->pts — no aliasing.
        StrokePolylineAA(canvas, canvas->pts + a, b - a, s < 32 && (closed >> s) & 1u, width, fc);
    }
    return canvas;
}

UICanvas* UICanvas_Fill(UICanvas* canvas) {
    if (!canvas || canvas->pathCount == 0 || !UICanvas_GetRenderer(canvas)) return canvas;
    unsigned closed = 0;
    const int subs = FlattenPath(canvas, &closed);
    const SDL_FColor fc = FColorOf(CurState(canvas)->fillColor, Alpha(canvas));
    for (int s = 0; s < subs; s++) {
        const int a = canvas->subs[s];
        int n = canvas->subs[s + 1] - a;
        const SDL_FPoint* p = canvas->pts + a;
        // Fills are implicitly closed; drop an explicit closing duplicate.
        if (n > 3 && fabsf(p[0].x - p[n-1].x) < 0.01f && fabsf(p[0].y - p[n-1].y) < 0.01f) n--;
        FillPolygonAA(canvas, p, n, fc);
    }
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
