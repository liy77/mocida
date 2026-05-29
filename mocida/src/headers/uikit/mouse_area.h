#ifndef UIKIT_MOUSE_AREA_H
#define UIKIT_MOUSE_AREA_H

#include <SDL3/SDL_stdinc.h>
#include <uikit/widget.h>
#include <uikit/children.h>
#include <uikit/cursor.h>

#define UI_WIDGET_MOUSE_AREA "@uikit/mouse_area"

// Forward declaration so callbacks can take a UIMouseArea*.
typedef struct UIMouseArea UIMouseArea;

/**
 * Data passed to every mouse-area callback. Coordinates are in renderer
 * space (already adjusted for SDL_LOGICAL_PRESENTATION). Deltas are
 * relative to the previous event of the same type.
 */
typedef struct {
    float x;       /**< Mouse X (renderer coords) when the event fired. */
    float y;       /**< Mouse Y (renderer coords) when the event fired. */
    float dx;      /**< Horizontal delta since the previous event of the same type. */
    float dy;      /**< Vertical delta since the previous event of the same type. */
    float startX;  /**< Mouse X at the start of the drag (drag events only). */
    float startY;  /**< Mouse Y at the start of the drag (drag events only). */
    int button;    /**< Mouse button (1 = left, 2 = middle, 3 = right). */
} UIMouseEvent;

typedef void (*UIMouseAreaCallback)(UIMouseArea* area, UIMouseEvent ev, void* userdata);

/**
 * Invisible interaction surface. Captures mouse hover, press, release
 * and drag events inside its bounds. When `draggable` is on, dragging
 * automatically updates the position of `dragTarget` (if set) AND of
 * the area's own widget, keeping them in sync.
 *
 * Pattern (similar to QML MouseArea):
 *   - Create the visible widget you want to drag (rect, image, ...).
 *   - Create a UIMouseArea sized to match it and put it on top
 *     (higher z-index) so it captures clicks before any visual widget.
 *   - Set dragTarget to the visible widget and draggable to 1.
 *   - Both will move together.
 */
struct UIMouseArea {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_MOUSE_AREA). */

    float marginLeft;          /**< Left outer margin (pixels). */
    float marginTop;           /**< Top outer margin (pixels). */
    float marginRight;         /**< Right outer margin (pixels). */
    float marginBottom;        /**< Bottom outer margin (pixels). */

    int draggable;             /**< When != 0, the area can be dragged. */
    int hovered;               /**< Internal: 1 while the mouse is over the area. */
    int pressed;               /**< Internal: 1 between mouse-down and mouse-up. */
    int dragging;              /**< Internal: 1 while a drag is in progress. */
    int enabled;               /**< 0 disables hit-testing entirely. */
    UICursor cursor;           /**< Cursor shown while hovering this area. */

    /**
     * Optional sibling widget moved together with the area while
     * dragging. Not owned - the caller manages its lifetime.
     */
    UIWidget* dragTarget;

    int hasClamp;              /**< When != 0, the drag is clamped to (clampX..clampX+clampW). */
    float clampX;              /**< Clamp rect X (inclusive lower bound). */
    float clampY;              /**< Clamp rect Y (inclusive lower bound). */
    float clampW;              /**< Clamp rect width. */
    float clampH;              /**< Clamp rect height. */

    float lastMouseX;          /**< Internal: previous mouse X (used for delta calc). */
    float lastMouseY;          /**< Internal: previous mouse Y. */

    Uint64 lastClickMs;        /**< Internal: SDL_GetTicks() of the previous click. */
    float  lastClickX;         /**< Internal: X of the previous click. */
    float  lastClickY;         /**< Internal: Y of the previous click. */
    int    lastClickButton;    /**< Internal: button index of the previous click. */

    UIMouseAreaCallback onHoverEnter;  /**< Fires when the mouse first enters the bounds. */
    UIMouseAreaCallback onHoverExit;   /**< Fires when the mouse leaves the bounds. */
    UIMouseAreaCallback onMouseDown;   /**< Fires on any mouse-button press inside. */
    UIMouseAreaCallback onMouseUp;     /**< Fires on the matching mouse-button release. */
    UIMouseAreaCallback onMouseMove;   /**< Fires on every mouse motion while hovered. */
    UIMouseAreaCallback onDoubleClick; /**< Fires on a quick second click on the same spot. */
    UIMouseAreaCallback onDragStart;   /**< Fires once when a drag is initiated. */
    UIMouseAreaCallback onDrag;        /**< Fires repeatedly while dragging. */
    UIMouseAreaCallback onDragEnd;     /**< Fires once when the drag completes. */
    void* userdata;                    /**< Opaque pointer passed to every callback. */
};

/**
 * Creates an empty mouse area with sensible defaults: enabled,
 * non-draggable, no clamp, no callbacks. Wrap with widgcs() to give
 * it bounds.
 */
UIMouseArea* UIMouseArea_Create(void);

UIMouseArea* UIMouseArea_SetDraggable(UIMouseArea* area, int draggable);
UIMouseArea* UIMouseArea_SetEnabled  (UIMouseArea* area, int enabled);
UIMouseArea* UIMouseArea_SetCursor   (UIMouseArea* area, UICursor cursor);

/**
 * Sets the widget that moves together with the area while dragging.
 * Pass NULL to clear. The target is a borrowed pointer.
 */
UIMouseArea* UIMouseArea_SetDragTarget(UIMouseArea* area, UIWidget* target);

/**
 * Confines the dragged position to the given rectangle (in renderer
 * coords). Set w or h to <= 0 to disable.
 */
UIMouseArea* UIMouseArea_SetDragBounds(UIMouseArea* area, float x, float y, float w, float h);

UIMouseArea* UIMouseArea_OnHoverEnter (UIMouseArea* area, UIMouseAreaCallback cb, void* userdata);
UIMouseArea* UIMouseArea_OnHoverExit  (UIMouseArea* area, UIMouseAreaCallback cb, void* userdata);
UIMouseArea* UIMouseArea_OnMouseDown  (UIMouseArea* area, UIMouseAreaCallback cb, void* userdata);
UIMouseArea* UIMouseArea_OnMouseUp    (UIMouseArea* area, UIMouseAreaCallback cb, void* userdata);
UIMouseArea* UIMouseArea_OnMouseMove  (UIMouseArea* area, UIMouseAreaCallback cb, void* userdata);
UIMouseArea* UIMouseArea_OnDoubleClick(UIMouseArea* area, UIMouseAreaCallback cb, void* userdata);
UIMouseArea* UIMouseArea_OnDragStart  (UIMouseArea* area, UIMouseAreaCallback cb, void* userdata);
UIMouseArea* UIMouseArea_OnDrag       (UIMouseArea* area, UIMouseAreaCallback cb, void* userdata);
UIMouseArea* UIMouseArea_OnDragEnd    (UIMouseArea* area, UIMouseAreaCallback cb, void* userdata);

void UIMouseArea_Destroy(UIMouseArea* area);

// ---------------------------------------------------------------------
// Internal dispatchers used by app.c.
// ---------------------------------------------------------------------
void UIMouseArea_DispatchMouseMotion(UIChildren* children, float x, float y);
void UIMouseArea_DispatchMouseDown  (UIChildren* children, float x, float y, int button);
void UIMouseArea_DispatchMouseUp    (UIChildren* children, float x, float y, int button);

#endif // UIKIT_MOUSE_AREA_H
