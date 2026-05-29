#include <uikit/mouse_area.h>
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

#define MOCIDA_DEFINE_CB_SETTER(name, field) \
    UIMouseArea* UIMouseArea_##name(UIMouseArea* area, UIMouseAreaCallback cb, void* userdata) { \
        if (!area) return area; \
        area->field = cb; \
        area->userdata = userdata; \
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
    free(area);
}

// ---------------------------------------------------------------------
// Event dispatch
// ---------------------------------------------------------------------
// Iteration order: front to back (highest z first) on press/release so
// the topmost area captures the event. On motion we visit everyone so
// hover state is kept consistent across overlapping areas.

static void FireCallback(UIMouseArea* area, UIMouseAreaCallback cb,
                         UIMouseEvent ev) {
    if (cb) cb(area, ev, area->userdata);
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
        if (!area || !area->enabled) continue;

        const int inside = InsideWidget(w, x, y);

        // Hover transitions.
        if (inside && !area->hovered) {
            area->hovered = 1;
            FireCallback(area, area->onHoverEnter,
                         MakeEvent(x, y, 0, 0, 0, 0, 0));
        } else if (!inside && area->hovered) {
            area->hovered = 0;
            FireCallback(area, area->onHoverExit,
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
                FireCallback(area, area->onDragStart,
                             MakeEvent(x, y, 0, 0, x, y, 0));
            }

            if (area->dragging) {
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
                FireCallback(area, area->onDrag,
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
                              area->userdata);
        }
    }
}

void UIMouseArea_DispatchMouseDown(UIChildren* children, float x, float y, int button) {
    if (!children) return;

    // Walk back-to-front (highest z first) so the topmost area wins.
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w = children->children[i];
        UIMouseArea* area = AsArea(w);
        if (!area || !area->enabled) continue;

        if (InsideWidget(w, x, y)) {
            area->pressed = 1;
            area->lastMouseX = x;
            area->lastMouseY = y;
            FireCallback(area, area->onMouseDown,
                         MakeEvent(x, y, 0, 0, x, y, button));

            const Uint64 now = SDL_GetTicks();
            const float dxClick = x - area->lastClickX;
            const float dyClick = y - area->lastClickY;
            if (area->lastClickMs != 0 &&
                (now - area->lastClickMs) <= UI_MOUSE_DOUBLECLICK_MS &&
                area->lastClickButton == button &&
                dxClick * dxClick + dyClick * dyClick <=
                    UI_MOUSE_DOUBLECLICK_PX * UI_MOUSE_DOUBLECLICK_PX) {
                FireCallback(area, area->onDoubleClick,
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
        if (!area || !area->enabled || !area->pressed) continue;

        if (area->dragging) {
            FireCallback(area, area->onDragEnd,
                         MakeEvent(x, y, 0, 0, 0, 0, button));
        }
        FireCallback(area, area->onMouseUp,
                     MakeEvent(x, y, 0, 0, 0, 0, button));

        area->pressed = 0;
        area->dragging = 0;
    }
}
