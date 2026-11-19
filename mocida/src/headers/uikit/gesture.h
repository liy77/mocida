#ifndef UIKIT_GESTURE_H
#define UIKIT_GESTURE_H

#include <SDL3/SDL_stdinc.h>
#include <uikit/widget.h>
#include <uikit/children.h>
#include <uikit/cursor.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_WIDGET_GESTURE_AREA "@uikit/gesture_area"

typedef struct UIGestureArea UIGestureArea;

/** Cardinal swipe direction (matches SwiftUI-ish axis). */
typedef enum {
    UI_GESTURE_DIR_NONE  = 0,
    UI_GESTURE_DIR_LEFT  = 1,
    UI_GESTURE_DIR_RIGHT = 2,
    UI_GESTURE_DIR_UP    = 3,
    UI_GESTURE_DIR_DOWN  = 4,
} UIGestureDirection;

/**
 * Payload for every gesture callback. Coordinates are renderer-space
 * (same as UIMouseEvent). Translation / velocity / distance are relative
 * to the press that started the current gesture.
 */
typedef struct {
    float x;
    float y;
    float dx;
    float dy;
    float startX;
    float startY;
    float translationX;   /**< Total delta from press (x - startX). */
    float translationY;   /**< Total delta from press (y - startY). */
    float velocityX;      /**< px/ms estimate from recent samples. */
    float velocityY;
    float distance;       /**< Euclidean distance from press. */
    float progress;       /**< 0..1 for long-press progress; else 0/1. */
    int   button;
    int   tapCount;       /**< 1 = single, 2 = double, … */
    int   direction;      /**< UIGestureDirection for swipe events. */
} UIGestureEvent;

typedef void (*UIGestureCallback)(UIGestureArea* area, UIGestureEvent ev, void* userdata);

/**
 * Invisible SwiftUI-style gesture surface. Exclusive recognizers by default:
 *   tap → fires on release if the pointer barely moved and long-press didn't win
 *   longPress → fires once after `longPressMs` while still pressed (and not dragged)
 *   drag → starts after `dragMinDistance`, then streams changed / ended
 *   swipe → on release, if distance/velocity crossed thresholds and drag wasn't
 *           claimed as a continuous drag destination
 *
 * Nest content as children of the wrapping widget (MUI builds a Rectangle + area).
 */
struct UIGestureArea {
    const char* __widget_type;

    int enabled;
    int hovered;
    int pressed;
    int dragging;
    int longPressed;       /**< 1 after onLongPress has fired for this press. */
    int cancelled;         /**< Exclusive cancel (drag won over tap/longpress). */
    UICursor cursor;

    int longPressMs;       /**< Default 500. */
    float dragMinDistance; /**< Default 8 px before Drag starts. */
    float tapMaxDistance;  /**< Default 10 px — beyond this, tap is cancelled. */
    float swipeMinDistance;/**< Default 48 px. */
    float swipeMinVelocity;/**< Default 0.35 px/ms (optional boost). */
    int exclusive;         /**< Default 1 — drag cancels tap/longpress. */

    float lastX;
    float lastY;
    float pressX;
    float pressY;
    Uint64 pressMs;
    Uint64 lastSampleMs;
    float  velX;
    float  velY;

    Uint64 lastTapMs;
    float  lastTapX;
    float  lastTapY;
    int    tapStreak;

    UIGestureCallback onTap;
    UIGestureCallback onDoubleTap;
    UIGestureCallback onLongPress;
    UIGestureCallback onLongPressProgress; /**< Optional; fires while holding. */
    UIGestureCallback onDragStart;
    UIGestureCallback onDragChanged;
    UIGestureCallback onDragEnded;
    UIGestureCallback onSwipe;
    UIGestureCallback onHoverEnter;
    UIGestureCallback onHoverExit;

    void* onTapUd;
    void* onDoubleTapUd;
    void* onLongPressUd;
    void* onLongPressProgressUd;
    void* onDragStartUd;
    void* onDragChangedUd;
    void* onDragEndedUd;
    void* onSwipeUd;
    void* onHoverEnterUd;
    void* onHoverExitUd;

    int nestedFirst;
};

UIGestureArea* UIGestureArea_Create(void);
UIGestureArea* UIGestureArea_SetEnabled(UIGestureArea* g, int enabled);
UIGestureArea* UIGestureArea_SetCursor(UIGestureArea* g, UICursor cursor);
UIGestureArea* UIGestureArea_SetNestedFirst(UIGestureArea* g, int nestedFirst);
UIGestureArea* UIGestureArea_SetExclusive(UIGestureArea* g, int exclusive);
UIGestureArea* UIGestureArea_SetLongPressMs(UIGestureArea* g, int ms);
UIGestureArea* UIGestureArea_SetDragMinDistance(UIGestureArea* g, float px);
UIGestureArea* UIGestureArea_SetTapMaxDistance(UIGestureArea* g, float px);
UIGestureArea* UIGestureArea_SetSwipeMinDistance(UIGestureArea* g, float px);
UIGestureArea* UIGestureArea_SetSwipeMinVelocity(UIGestureArea* g, float pxPerMs);

UIGestureArea* UIGestureArea_OnTap(UIGestureArea* g, UIGestureCallback cb, void* userdata);
UIGestureArea* UIGestureArea_OnDoubleTap(UIGestureArea* g, UIGestureCallback cb, void* userdata);
UIGestureArea* UIGestureArea_OnLongPress(UIGestureArea* g, UIGestureCallback cb, void* userdata);
UIGestureArea* UIGestureArea_OnLongPressProgress(UIGestureArea* g, UIGestureCallback cb, void* userdata);
UIGestureArea* UIGestureArea_OnDragStart(UIGestureArea* g, UIGestureCallback cb, void* userdata);
UIGestureArea* UIGestureArea_OnDragChanged(UIGestureArea* g, UIGestureCallback cb, void* userdata);
UIGestureArea* UIGestureArea_OnDragEnded(UIGestureArea* g, UIGestureCallback cb, void* userdata);
UIGestureArea* UIGestureArea_OnSwipe(UIGestureArea* g, UIGestureCallback cb, void* userdata);
UIGestureArea* UIGestureArea_OnHoverEnter(UIGestureArea* g, UIGestureCallback cb, void* userdata);
UIGestureArea* UIGestureArea_OnHoverExit(UIGestureArea* g, UIGestureCallback cb, void* userdata);

void UIGestureArea_Destroy(UIGestureArea* g);

void UIGestureArea_DispatchMouseMotion(UIChildren* children, float x, float y);
void UIGestureArea_DispatchMouseDown(UIChildren* children, float x, float y, int button);
void UIGestureArea_DispatchMouseUp(UIChildren* children, float x, float y, int button);
/** Call once per frame so long-press can fire without pointer motion. */
void UIGestureArea_Tick(UIChildren* children, float dtMs);

#ifdef __cplusplus
}
#endif

#endif /* UIKIT_GESTURE_H */
