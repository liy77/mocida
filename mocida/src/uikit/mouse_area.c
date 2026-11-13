#include <uikit/mouse_area.h>
#include <uikit/container.h>
#include <uikit/stack.h>
#include <uikit/glass.h>
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#define UI_MOUSE_DOUBLECLICK_MS 500
#define UI_MOUSE_DOUBLECLICK_PX 5.0f

// ---------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------

static int InsideWidget(const UIWidget* w, float x, float y) {
    if (!w || !w->visible || !w->width || !w->height) return 0;
    const float ww = *w->width;
    const float hh = *w->height;
    return (x >= w->x && x < w->x + ww &&
            y >= w->y && y < w->y + hh);
}

static UIMouseArea* AsArea(UIWidget* w) {
    if (!w || !w->data) return NULL;
    UIWidgetBase* base = (UIWidgetBase*)w->data;
    if (strcmp(base->__widget_type, UI_WIDGET_MOUSE_AREA) != 0) return NULL;
    return (UIMouseArea*)base;
}

// If `w` is a layout container (Stack / Grid / Rectangle), return its child
// collection so the mouse dispatch can recurse into NESTED mouse areas. The
// container's render pass already set each child's absolute x/y, so the same
// absolute InsideWidget hit-test works at any depth. Other widgets return NULL.
// (Mirrors button.c's ContainerChildren — without this a MouseArea inside a
// Stack/Rectangle never receives events.)
static UIChildren* ContainerChildren(UIWidget* w) {
    // A hidden container hides its subtree from input too (it isn't laid out,
    // so its children sit at stale positions and would steal clicks/focus).
    if (!w || !w->data || !w->visible) return NULL;
    UIWidgetBase* base = (UIWidgetBase*)w->data;
    const char* t = base->__widget_type;
    if (strcmp(t, UI_WIDGET_STACK) == 0)     return ((UIStack*)base)->items;
    if (strcmp(t, UI_WIDGET_GLASS) == 0)     return ((UIGlass*)base)->items;
    if (strcmp(t, UI_WIDGET_GRID) == 0)      return ((UIGrid*)base)->items;
    if (strcmp(t, UI_WIDGET_RECTANGLE) == 0) return (UIChildren*)((UIRectangle*)base)->children;
    // Descend into a UIScroll's content (its children are re-laid-out to
    // absolute on-screen coords at render, so the same hit-test point applies)
    // — otherwise nothing inside a Scroll receives mouse events.
    if (strcmp(t, UI_WIDGET_SCROLL) == 0) {
        UIWidget* content = ((UIScroll*)base)->content;
        return content ? ContainerChildren(content) : NULL;
    }
    return NULL;
}

// A press only reaches a Scroll's content inside the scroll's viewport, so
// rows scrolled (or clipped) out of view can't catch clicks meant for the
// panels around them.
static UIChildren* PressChildren(UIWidget* w, float x, float y) {
    if (w && w->visible && w->data &&
        strcmp(((UIWidgetBase*)w->data)->__widget_type, UI_WIDGET_SCROLL) == 0) {
        const float sw = w->width ? *w->width : 0.0f;
        const float sh = w->height ? *w->height : 0.0f;
        if (x < w->x || y < w->y || x >= w->x + sw || y >= w->y + sh) return NULL;
    }
    return ContainerChildren(w);
}

// Clamps the (x, y) origin of a w x h rectangle to live inside the
// area's clamp rect. Returns the corrected position via out params.
static void ApplyClamp(const UIMouseArea* area,
                       float w, float h,
                       float* x, float* y) {
    if (!area || !area->hasClamp) return;
    if (area->clampW <= 0.0f || area->clampH <= 0.0f) return;

    const float maxX = area->clampX + area->clampW - w;
    const float maxY = area->clampY + area->clampH - h;

    if (*x < area->clampX) *x = area->clampX;
    if (*y < area->clampY) *y = area->clampY;
    if (*x > maxX)         *x = maxX;
    if (*y > maxY)         *y = maxY;
}

// ---------------------------------------------------------------------
// Construction / configuration
// ---------------------------------------------------------------------

UIMouseArea* UIMouseArea_Create(void) {
    UIMouseArea* a = (UIMouseArea*)calloc(1, sizeof(UIMouseArea));
    if (!a) return NULL;
    a->__widget_type = UI_WIDGET_MOUSE_AREA;
    a->enabled = 1;
    a->cursor  = UI_CURSOR_POINTER;
    return a;
}

UIMouseArea* UIMouseArea_SetCursor(UIMouseArea* area, UICursor cursor) {
    if (area) area->cursor = cursor;
    return area;
}

UIMouseArea* UIMouseArea_SetDraggable(UIMouseArea* area, int draggable) {
    if (!area) return area;
    area->draggable = draggable ? 1 : 0;
    return area;
}

UIMouseArea* UIMouseArea_SetEnabled(UIMouseArea* area, int enabled) {
    if (!area) return area;
    area->enabled = enabled ? 1 : 0;
    return area;
}

UIMouseArea* UIMouseArea_SetDragTarget(UIMouseArea* area, UIWidget* target) {
    if (!area) return area;
    area->dragTarget = target;
    return area;
}

UIMouseArea* UIMouseArea_SetDragBounds(UIMouseArea* area, float x, float y, float w, float h) {
    if (!area) return area;
    if (w <= 0.0f || h <= 0.0f) {
        area->hasClamp = 0;
    } else {
        area->hasClamp = 1;
        area->clampX = x;
        area->clampY = y;
        area->clampW = w;
        area->clampH = h;
    }
    return area;
}

// ---------------------------------------------------------------------
// Internal drag-and-drop
// ---------------------------------------------------------------------
// A source carries `dragData` (the payload). The active drag payload is held in
// `g_activeDrag` (a strdup'd copy) for the lifetime of the gesture so the drop
// target found at mouse-up can receive it even after the source's own state is
// reset.

static char* g_activeDrag = NULL;

static char* dup_or_null(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char* d = (char*)malloc(n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}

UIMouseArea* UIMouseArea_SetDragData(UIMouseArea* area, const char* payload) {
    if (!area) return area;
    free(area->dragData);
    area->dragData = dup_or_null(payload);
    if (area->dragData) area->draggable = 1; // a drag source must emit drag events
    return area;
}

UIMouseArea* UIMouseArea_SetDropData(UIMouseArea* area, const char* dropData) {
    if (!area) return area;
    free(area->dropData);
    area->dropData = dup_or_null(dropData);
    return area;
}

UIMouseArea* UIMouseArea_OnDrop(UIMouseArea* area, UIMouseAreaDropCallback cb, void* userdata) {
    if (!area) return area;
    area->onDrop = cb;
    area->onDropUd = userdata;
    return area;
}

int UIMouseArea_DragActive(void) { return g_activeDrag != NULL; }

void UIMouseArea_ClearDrag(void) {
    free(g_activeDrag);
    g_activeDrag = NULL;
}

// Recurse front-to-back; fire onDrop on the topmost drop target containing (x,y).
int UIMouseArea_DispatchDrop(UIChildren* children, float x, float y) {
    if (!children || !g_activeDrag) return 0;
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w = children->children[i];
        UIMouseArea* area = AsArea(w);
        if (!area) {
            UIChildren* kids = PressChildren(w, x, y);
            if (kids && UIMouseArea_DispatchDrop(kids, x, y)) return 1;
            continue;
        }
        if (!area->enabled || !area->dropData || !area->onDrop) continue;
        if (InsideWidget(w, x, y)) {
            area->onDrop(area, g_activeDrag, area->dropData, area->onDropUd);
            return 1;
        }
    }
    return 0;
}

#define MOCIDA_DEFINE_CB_SETTER(name, field) \
    UIMouseArea* UIMouseArea_##name(UIMouseArea* area, UIMouseAreaCallback cb, void* userdata) { \
        if (!area) return area; \
        area->field = cb; \
        area->field##Ud = userdata; \
        area->userdata = userdata; /* keep legacy field in sync (last setter wins) */ \
        return area; \
    }

MOCIDA_DEFINE_CB_SETTER(OnHoverEnter, onHoverEnter)
MOCIDA_DEFINE_CB_SETTER(OnHoverExit,  onHoverExit)
MOCIDA_DEFINE_CB_SETTER(OnMouseDown,  onMouseDown)
MOCIDA_DEFINE_CB_SETTER(OnMouseUp,    onMouseUp)
MOCIDA_DEFINE_CB_SETTER(OnMouseMove,  onMouseMove)
MOCIDA_DEFINE_CB_SETTER(OnDoubleClick, onDoubleClick)
MOCIDA_DEFINE_CB_SETTER(OnDragStart,  onDragStart)
MOCIDA_DEFINE_CB_SETTER(OnDrag,       onDrag)
MOCIDA_DEFINE_CB_SETTER(OnDragEnd,    onDragEnd)

#undef MOCIDA_DEFINE_CB_SETTER

void UIMouseArea_Destroy(UIMouseArea* area) {
    if (!area) return;
    // dragTarget is a borrowed pointer - do not free it.
    free(area->dragData);
    free(area->dropData);
    free(area);
}

// ---------------------------------------------------------------------
// Event dispatch
// ---------------------------------------------------------------------
// Iteration order: front to back (highest z first) on press/release so
// the topmost area captures the event. On motion we visit everyone so
// hover state is kept consistent across overlapping areas.

static void FireCallback(UIMouseArea* area, UIMouseAreaCallback cb,
                         void* ud, UIMouseEvent ev) {
    if (cb) cb(area, ev, ud);
}

static UIMouseEvent MakeEvent(float x, float y,
                              float dx, float dy,
                              float startX, float startY,
                              int button) {
    UIMouseEvent ev;
    ev.x = x; ev.y = y;
    ev.dx = dx; ev.dy = dy;
    ev.startX = startX; ev.startY = startY;
    ev.button = button;
    return ev;
}

void UIMouseArea_DispatchMouseMotion(UIChildren* children, float x, float y) {
    if (!children) return;

    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        UIMouseArea* area = AsArea(w);
        if (!area) {
            UIChildren* kids = ContainerChildren(w);
            if (kids) UIMouseArea_DispatchMouseMotion(kids, x, y);
            continue;
        }
        if (!area->enabled) continue;

        // Occlusion: an area hidden behind an overlay gets no hover.
        const int inside = InsideWidget(w, x, y) && UIWidget_EventOcclusionAllows(w);

        // Hover transitions.
        if (inside && !area->hovered) {
            area->hovered = 1;
            FireCallback(area, area->onHoverEnter, area->onHoverEnterUd,
                         MakeEvent(x, y, 0, 0, 0, 0, 0));
        } else if (!inside && area->hovered) {
            area->hovered = 0;
            FireCallback(area, area->onHoverExit, area->onHoverExitUd,
                         MakeEvent(x, y, 0, 0, 0, 0, 0));
        }

        // Drag while button is held.
        if (area->pressed && area->draggable) {
            const float dx = x - area->lastMouseX;
            const float dy = y - area->lastMouseY;
            area->lastMouseX = x;
            area->lastMouseY = y;

            if (!area->dragging && (dx != 0.0f || dy != 0.0f)) {
                area->dragging = 1;
                FireCallback(area, area->onDragStart, area->onDragStartUd,
                             MakeEvent(x, y, 0, 0, x, y, 0));
            }

            if (area->dragging) {
                if (area->dragData) {
                    // Internal DnD source: do NOT move its own widget — it just
                    // drives the global drag (a ghost rendered elsewhere follows
                    // the cursor; the drop target is resolved at mouse-up).
                    if (!g_activeDrag) g_activeDrag = dup_or_null(area->dragData);
                } else {
                    // Move the area widget by delta, then clamp.
                    if (w->width && w->height) {
                        w->x += dx;
                        w->y += dy;
                        ApplyClamp(area, *w->width, *w->height, &w->x, &w->y);
                    }
                    // Move the drag target by the same delta, then clamp.
                    if (area->dragTarget) {
                        UIWidget* t = area->dragTarget;
                        t->x += dx;
                        t->y += dy;
                        if (t->width && t->height) {
                            ApplyClamp(area, *t->width, *t->height, &t->x, &t->y);
                        }
                    }
                }
                FireCallback(area, area->onDrag, area->onDragUd,
                             MakeEvent(x, y, dx, dy, 0, 0, 0));
            }
        }

        // Plain mouse-move (always fires when callback is set).
        if (area->onMouseMove) {
            const float dx = x - area->lastMouseX;
            const float dy = y - area->lastMouseY;
            if (!area->pressed) {
                area->lastMouseX = x;
                area->lastMouseY = y;
            }
            area->onMouseMove(area,
                              MakeEvent(x, y, dx, dy, 0, 0, 0),
                              area->onMouseMoveUd);
        }
    }
}

// MOCIDA_TRACE_INPUT=1: log mouse-down arbitration (which areas contain the
// point, and whether z-order occlusion let them have it).
static int TraceInput(void) {
    static int on = -1;
    if (on < 0) on = SDL_getenv("MOCIDA_TRACE_INPUT") != NULL;
    return on;
}

void UIMouseArea_DispatchMouseDown(UIChildren* children, float x, float y, int button) {
    if (!children) return;

    // Walk back-to-front (highest z first) so the topmost area wins.
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w = children->children[i];
        UIMouseArea* area = AsArea(w);
        if (!area) {
            UIChildren* kids = PressChildren(w, x, y);
            if (kids) UIMouseArea_DispatchMouseDown(kids, x, y, button);
            continue;
        }
        if (TraceInput()) {
            fprintf(stderr, "[mocida.input] down (%.0f,%.0f) area %p box=(%.0f,%.0f %.0fx%.0f) vis=%d enabled=%d inside=%d occlusion=%d\n",
                    x, y, (void*)area, w->x, w->y, w->width ? *w->width : -1.0f,
                    w->height ? *w->height : -1.0f, w->visible, area->enabled,
                    InsideWidget(w, x, y), UIWidget_EventOcclusionAllows(w));
        }
        if (!area->enabled) continue;

        if (InsideWidget(w, x, y) && UIWidget_EventOcclusionAllows(w)) {
            area->pressed = 1;
            area->lastMouseX = x;
            area->lastMouseY = y;
            FireCallback(area, area->onMouseDown, area->onMouseDownUd,
                         MakeEvent(x, y, 0, 0, x, y, button));

            const Uint64 now = SDL_GetTicks();
            const float dxClick = x - area->lastClickX;
            const float dyClick = y - area->lastClickY;
            if (area->lastClickMs != 0 &&
                (now - area->lastClickMs) <= UI_MOUSE_DOUBLECLICK_MS &&
                area->lastClickButton == button &&
                dxClick * dxClick + dyClick * dyClick <=
                    UI_MOUSE_DOUBLECLICK_PX * UI_MOUSE_DOUBLECLICK_PX) {
                FireCallback(area, area->onDoubleClick, area->onDoubleClickUd,
                             MakeEvent(x, y, 0, 0, x, y, button));
                // Reset so a triple-click doesn't fire as another
                // double-click.
                area->lastClickMs = 0;
            } else {
                area->lastClickMs     = now;
                area->lastClickX      = x;
                area->lastClickY      = y;
                area->lastClickButton = button;
            }
            return; // capture - don't propagate to widgets below
        }
    }
}

void UIMouseArea_DispatchMouseUp(UIChildren* children, float x, float y, int button) {
    if (!children) return;

    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        UIMouseArea* area = AsArea(w);
        if (!area) {
            UIChildren* kids = ContainerChildren(w);
            if (kids) UIMouseArea_DispatchMouseUp(kids, x, y, button);
            continue;
        }
        if (!area->enabled || !area->pressed) continue;

        if (area->dragging) {
            FireCallback(area, area->onDragEnd, area->onDragEndUd,
                         MakeEvent(x, y, 0, 0, 0, 0, button));
        }
        // A drag on an internal DnD source is NOT a click — suppress onMouseUp
        // (which the runtime maps to onClick) so dragging a file row to move it
        // doesn't also "open" it. Plain draggable areas keep firing onMouseUp.
        if (!(area->dragging && area->dragData)) {
            FireCallback(area, area->onMouseUp, area->onMouseUpUd,
                         MakeEvent(x, y, 0, 0, 0, 0, button));
        }

        area->pressed = 0;
        area->dragging = 0;
    }
}
