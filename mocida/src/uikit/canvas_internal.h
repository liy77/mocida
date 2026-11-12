#ifndef CANVAS_INTERNAL_H
#define CANVAS_INTERNAL_H

#include <uikit/canvas.h>

/* Internal struct definition - kept private from public API users. */
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

    /* Window-space origin of the canvas widget, set by RenderWidget_Canvas each
     * frame. All path/immediate draw coordinates are offset by this so the
     * canvas draws in its OWN local space (0,0 = top-left of the widget) instead
     * of raw window space. */
    float originX, originY;

    SDL_Renderer* currentRenderer;

    /* Widget opacity for the frame being drawn (RenderWidget_Canvas), folded
     * into every fill/stroke/text/image alpha. 1 outside a frame. */
    float opacity;
    /* Frame counter (bumped per render) — ages the text-texture cache. */
    unsigned frameNo;

    /* Grow-only scratch buffers reused by Fill/Stroke/circles, so steady-state
     * drawing does no heap allocation. */
    SDL_FPoint* pts;    int ptsCap;    /* flattened path points */
    int*        subs;   int subsCap;   /* subpath start offsets (+ sentinel) */
    SDL_Vertex* verts;  int vertsCap;
    int*        idx;    int idxCap;
    int*        work;   int workCap;   /* ear-clipping vertex list */

    /* DrawText cache: one texture per (font, text, color), reused across frames
     * and evicted after ~2 s unused — text used to be re-rasterised into a
     * fresh texture on every call. */
    struct UICanvasTextEntry* texts; int textCount; int textCap;
    SDL_Renderer* textRenderer;   /* renderer owning the cached textures */

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

/* Called by RenderWidget_Canvas around the onFrame callback: bind renderer +
 * origin + opacity and reset the per-frame path/state; then release them and
 * evict stale cached text textures. */
void UICanvas_BeginFrame(UICanvas* canvas, SDL_Renderer* r, float x, float y, float opacity);
void UICanvas_EndFrame(UICanvas* canvas);

#endif // CANVAS_INTERNAL_H
