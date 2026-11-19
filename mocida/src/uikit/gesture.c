#include <uikit/gesture.h>
#include <uikit/container.h>
#include <uikit/stack.h>
#include <uikit/glass.h>
#include <uikit/rect.h>
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define UI_GESTURE_DOUBLE_TAP_MS 420
#define UI_GESTURE_DOUBLE_TAP_PX 12.0f

static int InsideWidget(const UIWidget* w, float x, float y) {
    if (!w || !w->visible || !w->width || !w->height) return 0;
    const float ww = *w->width;
    const float hh = *w->height;
    return (x >= w->x && x < w->x + ww && y >= w->y && y < w->y + hh);
}

static UIChildren* ContainerChildren(UIWidget* w) {
    if (!w || !w->data || !w->visible) return NULL;
    UIWidgetBase* base = (UIWidgetBase*)w->data;
    const char* t = base->__widget_type;
    if (strcmp(t, UI_WIDGET_STACK) == 0) return ((UIStack*)base)->items;
    if (strcmp(t, UI_WIDGET_GLASS) == 0) return ((UIGlass*)base)->items;
    if (strcmp(t, UI_WIDGET_GRID) == 0) return ((UIGrid*)base)->items;
    if (strcmp(t, UI_WIDGET_RECTANGLE) == 0) return (UIChildren*)((UIRectangle*)base)->children;
    if (strcmp(t, UI_WIDGET_SCROLL) == 0) {
        UIWidget* content = ((UIScroll*)base)->content;
        return content ? ContainerChildren(content) : NULL;
    }
    return NULL;
}

static UIChildren* PressChildren(UIWidget* w, float x, float y) {
    if (w && w->visible && w->data &&
        strcmp(((UIWidgetBase*)w->data)->__widget_type, UI_WIDGET_SCROLL) == 0) {
        const float sw = w->width ? *w->width : 0.0f;
        const float sh = w->height ? *w->height : 0.0f;
        if (x < w->x || y < w->y || x >= w->x + sw || y >= w->y + sh) return NULL;
    }
    return ContainerChildren(w);
}

static UIGestureEvent MakeEv(UIGestureArea* g, float x, float y, int button) {
    UIGestureEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.x = x;
    ev.y = y;
    ev.dx = x - g->lastX;
    ev.dy = y - g->lastY;
    ev.startX = g->pressX;
    ev.startY = g->pressY;
    ev.translationX = x - g->pressX;
    ev.translationY = y - g->pressY;
    ev.velocityX = g->velX;
    ev.velocityY = g->velY;
    ev.distance = sqrtf(ev.translationX * ev.translationX +
                        ev.translationY * ev.translationY);
    ev.button = button;
    return ev;
}

static void SampleVelocity(UIGestureArea* g, float x, float y, Uint64 now) {
    float dt = (float)(now - g->lastSampleMs);
    if (dt < 1.0f) dt = 1.0f;
    float ix = (x - g->lastX) / dt;
    float iy = (y - g->lastY) / dt;
    /* Light EMA so a single jitter sample doesn't dominate swipe. */
    g->velX = g->velX * 0.55f + ix * 0.45f;
    g->velY = g->velY * 0.55f + iy * 0.45f;
    g->lastSampleMs = now;
}

static int SwipeDir(float tx, float ty) {
    float ax = fabsf(tx), ay = fabsf(ty);
    if (ax < 1.0f && ay < 1.0f) return UI_GESTURE_DIR_NONE;
    if (ax >= ay) return tx < 0.0f ? UI_GESTURE_DIR_LEFT : UI_GESTURE_DIR_RIGHT;
    return ty < 0.0f ? UI_GESTURE_DIR_UP : UI_GESTURE_DIR_DOWN;
}

/* ------------------------------------------------------------------ */

UIGestureArea* UIGestureArea_Create(void) {
    UIGestureArea* g = (UIGestureArea*)calloc(1, sizeof(UIGestureArea));
    if (!g) return NULL;
    g->__widget_type = UI_WIDGET_GESTURE_AREA;
    g->enabled = 1;
    g->cursor = UI_CURSOR_POINTER;
    g->longPressMs = 500;
    g->dragMinDistance = 8.0f;
    g->tapMaxDistance = 10.0f;
    g->swipeMinDistance = 48.0f;
    g->swipeMinVelocity = 0.35f;
    g->exclusive = 1;
    return g;
}

UIGestureArea* UIGestureArea_SetEnabled(UIGestureArea* g, int enabled) {
    if (g) g->enabled = enabled ? 1 : 0;
    return g;
}
UIGestureArea* UIGestureArea_SetCursor(UIGestureArea* g, UICursor cursor) {
    if (g) g->cursor = cursor;
    return g;
}
UIGestureArea* UIGestureArea_SetNestedFirst(UIGestureArea* g, int nestedFirst) {
    if (g) g->nestedFirst = nestedFirst ? 1 : 0;
    return g;
}
UIGestureArea* UIGestureArea_SetExclusive(UIGestureArea* g, int exclusive) {
    if (g) g->exclusive = exclusive ? 1 : 0;
    return g;
}
UIGestureArea* UIGestureArea_SetLongPressMs(UIGestureArea* g, int ms) {
    if (g) g->longPressMs = ms < 0 ? 0 : ms;
    return g;
}
UIGestureArea* UIGestureArea_SetDragMinDistance(UIGestureArea* g, float px) {
    if (g) g->dragMinDistance = px < 0.0f ? 0.0f : px;
    return g;
}
UIGestureArea* UIGestureArea_SetTapMaxDistance(UIGestureArea* g, float px) {
    if (g) g->tapMaxDistance = px < 0.0f ? 0.0f : px;
    return g;
}
UIGestureArea* UIGestureArea_SetSwipeMinDistance(UIGestureArea* g, float px) {
    if (g) g->swipeMinDistance = px < 0.0f ? 0.0f : px;
    return g;
}
UIGestureArea* UIGestureArea_SetSwipeMinVelocity(UIGestureArea* g, float pxPerMs) {
    if (g) g->swipeMinVelocity = pxPerMs < 0.0f ? 0.0f : pxPerMs;
    return g;
}

#define SET_CB(NAME, FIELD, UD) \
UIGestureArea* UIGestureArea_On##NAME(UIGestureArea* g, UIGestureCallback cb, void* userdata) { \
    if (!g) return g; g->FIELD = cb; g->UD = userdata; return g; \
}
SET_CB(Tap, onTap, onTapUd)
SET_CB(DoubleTap, onDoubleTap, onDoubleTapUd)
SET_CB(LongPress, onLongPress, onLongPressUd)
SET_CB(LongPressProgress, onLongPressProgress, onLongPressProgressUd)
SET_CB(DragStart, onDragStart, onDragStartUd)
SET_CB(DragChanged, onDragChanged, onDragChangedUd)
SET_CB(DragEnded, onDragEnded, onDragEndedUd)
SET_CB(Swipe, onSwipe, onSwipeUd)
SET_CB(HoverEnter, onHoverEnter, onHoverEnterUd)
SET_CB(HoverExit, onHoverExit, onHoverExitUd)
#undef SET_CB

void UIGestureArea_Destroy(UIGestureArea* g) {
    free(g);
}

/* ------------------------------------------------------------------ */

static void TryLongPress(UIGestureArea* g, UIWidget* w, float x, float y) {
    if (!g || !g->pressed || g->longPressed || g->cancelled || g->dragging) return;
    if (g->longPressMs <= 0) return;
    Uint64 now = SDL_GetTicks();
    float elapsed = (float)(now - g->pressMs);
    float progress = elapsed / (float)g->longPressMs;
    if (progress > 1.0f) progress = 1.0f;

    float dist = sqrtf((x - g->pressX) * (x - g->pressX) +
                       (y - g->pressY) * (y - g->pressY));
    if (dist > g->tapMaxDistance) return;

    if (g->onLongPressProgress) {
        UIGestureEvent ev = MakeEv(g, x, y, 1);
        ev.progress = progress;
        g->onLongPressProgress(g, ev, g->onLongPressProgressUd);
    }
    if (progress >= 1.0f && g->onLongPress) {
        g->longPressed = 1;
        if (g->exclusive) g->cancelled = 1; /* tap won't fire */
        UIGestureEvent ev = MakeEv(g, x, y, 1);
        ev.progress = 1.0f;
        g->onLongPress(g, ev, g->onLongPressUd);
        (void)w;
    }
}

static void HandleMotion(UIGestureArea* g, UIWidget* w, float x, float y) {
    if (!g || !g->enabled) return;
    int inside = InsideWidget(w, x, y);
    if (inside && !g->hovered) {
        g->hovered = 1;
        if (g->onHoverEnter) {
            UIGestureEvent ev = MakeEv(g, x, y, 0);
            g->onHoverEnter(g, ev, g->onHoverEnterUd);
        }
    } else if (!inside && g->hovered) {
        g->hovered = 0;
        if (g->onHoverExit) {
            UIGestureEvent ev = MakeEv(g, x, y, 0);
            g->onHoverExit(g, ev, g->onHoverExitUd);
        }
    }

    if (!g->pressed) {
        g->lastX = x;
        g->lastY = y;
        return;
    }

    Uint64 now = SDL_GetTicks();
    SampleVelocity(g, x, y, now);

    float dist = sqrtf((x - g->pressX) * (x - g->pressX) +
                       (y - g->pressY) * (y - g->pressY));

    if (!g->dragging && dist >= g->dragMinDistance &&
        (g->onDragStart || g->onDragChanged || g->onDragEnded)) {
        g->dragging = 1;
        if (g->exclusive) {
            g->cancelled = 1;
            g->longPressed = 1; /* suppress later long-press */
        }
        if (g->onDragStart) {
            UIGestureEvent ev = MakeEv(g, x, y, 1);
            g->onDragStart(g, ev, g->onDragStartUd);
        }
    }

    if (g->dragging && g->onDragChanged) {
        UIGestureEvent ev = MakeEv(g, x, y, 1);
        g->onDragChanged(g, ev, g->onDragChangedUd);
    } else {
        TryLongPress(g, w, x, y);
    }

    g->lastX = x;
    g->lastY = y;
}

void UIGestureArea_DispatchMouseMotion(UIChildren* children, float x, float y) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || !w->data) continue;
        UIChildren* kids = ContainerChildren(w);
        if (kids) UIGestureArea_DispatchMouseMotion(kids, x, y);
        UIWidgetBase* base = (UIWidgetBase*)w->data;
        if (strcmp(base->__widget_type, UI_WIDGET_GESTURE_AREA) != 0) continue;
        UIGestureArea* g = (UIGestureArea*)base;
        if (!g->enabled) continue;
        HandleMotion(g, w, x, y);
    }
}

void UIGestureArea_DispatchMouseDown(UIChildren* children, float x, float y, int button) {
    if (!children || button != SDL_BUTTON_LEFT) return;
    /* Back-to-front so topmost wins. */
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible || !w->data) continue;
        UIChildren* kids = PressChildren(w, x, y);
        if (kids) {
            UIGestureArea_DispatchMouseDown(kids, x, y, button);
            /* Still allow sibling areas under nestedFirst semantics by continuing. */
        }
        UIWidgetBase* base = (UIWidgetBase*)w->data;
        if (strcmp(base->__widget_type, UI_WIDGET_GESTURE_AREA) != 0) continue;
        UIGestureArea* g = (UIGestureArea*)base;
        if (!g->enabled) continue;
        if (!UIWidget_EventOcclusionAllows(w)) continue;
        if (!InsideWidget(w, x, y)) continue;

        if (g->nestedFirst && kids) {
            /* Content already had a chance; still claim the press for gestures
             * over empty regions of this area. */
        }

        g->pressed = 1;
        g->dragging = 0;
        g->longPressed = 0;
        g->cancelled = 0;
        g->pressX = x;
        g->pressY = y;
        g->lastX = x;
        g->lastY = y;
        g->pressMs = SDL_GetTicks();
        g->lastSampleMs = g->pressMs;
        g->velX = 0.0f;
        g->velY = 0.0f;
        return;
    }
}

void UIGestureArea_DispatchMouseUp(UIChildren* children, float x, float y, int button) {
    if (!children || button != SDL_BUTTON_LEFT) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || !w->data) continue;
        UIChildren* kids = ContainerChildren(w);
        if (kids) UIGestureArea_DispatchMouseUp(kids, x, y, button);

        UIWidgetBase* base = (UIWidgetBase*)w->data;
        if (strcmp(base->__widget_type, UI_WIDGET_GESTURE_AREA) != 0) continue;
        UIGestureArea* g = (UIGestureArea*)base;
        if (!g->pressed) continue;

        Uint64 now = SDL_GetTicks();
        SampleVelocity(g, x, y, now);
        UIGestureEvent ev = MakeEv(g, x, y, button);

        if (g->dragging) {
            if (g->onDragEnded) g->onDragEnded(g, ev, g->onDragEndedUd);
        } else {
            TryLongPress(g, w, x, y);
            int isSwipe = 0;
            float speed = sqrtf(g->velX * g->velX + g->velY * g->velY);
            if (!g->longPressed &&
                (ev.distance >= g->swipeMinDistance || speed >= g->swipeMinVelocity) &&
                g->onSwipe) {
                isSwipe = 1;
                ev.direction = SwipeDir(ev.translationX, ev.translationY);
                g->onSwipe(g, ev, g->onSwipeUd);
            }
            if (!g->cancelled && !g->longPressed && !isSwipe &&
                ev.distance <= g->tapMaxDistance) {
                /* Double-tap detection. */
                int isDouble = 0;
                if (g->onDoubleTap && g->lastTapMs > 0 &&
                    (now - g->lastTapMs) <= UI_GESTURE_DOUBLE_TAP_MS) {
                    float ddx = x - g->lastTapX;
                    float ddy = y - g->lastTapY;
                    if (sqrtf(ddx * ddx + ddy * ddy) <= UI_GESTURE_DOUBLE_TAP_PX) {
                        isDouble = 1;
                    }
                }
                if (isDouble) {
                    ev.tapCount = 2;
                    g->onDoubleTap(g, ev, g->onDoubleTapUd);
                    g->lastTapMs = 0;
                    g->tapStreak = 0;
                } else if (g->onTap) {
                    ev.tapCount = 1;
                    g->onTap(g, ev, g->onTapUd);
                    g->lastTapMs = now;
                    g->lastTapX = x;
                    g->lastTapY = y;
                    g->tapStreak = 1;
                }
            }
        }

        g->pressed = 0;
        g->dragging = 0;
        g->lastX = x;
        g->lastY = y;
    }
}

void UIGestureArea_Tick(UIChildren* children, float dtMs) {
    if (!children) return;
    (void)dtMs;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || !w->data) continue;
        UIChildren* kids = ContainerChildren(w);
        if (kids) UIGestureArea_Tick(kids, dtMs);
        UIWidgetBase* base = (UIWidgetBase*)w->data;
        if (strcmp(base->__widget_type, UI_WIDGET_GESTURE_AREA) != 0) continue;
        UIGestureArea* g = (UIGestureArea*)base;
        if (!g->enabled || !g->pressed) continue;
        TryLongPress(g, w, g->lastX, g->lastY);
    }
}
