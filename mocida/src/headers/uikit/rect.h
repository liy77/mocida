#ifndef UIKIT_RECT_H
#define UIKIT_RECT_H

#include "uikit/color.h"
#include <uikit/widget.h>
#include <uikit/shadow.h>
#include <stdio.h>

/**
 * UIRectangle structure representing a rectangle widget.
 * It contains properties for margins, border radius,
 * border width, color, and border color.
 */
typedef struct {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_RECTANGLE). */

    float marginLeft;          /**< Left outer margin (pixels). */
    float marginTop;           /**< Top outer margin (pixels). */
    float marginRight;         /**< Right outer margin (pixels). */
    float marginBottom;        /**< Bottom outer margin (pixels). */
    float radius;              /**< Corner radius. Equals min(w,h)/2 to render a circle. */
    float borderWidth;         /**< Border thickness (pixels). 0 = no border. */

    UIColor color;             /**< Fill color. */
    UIColor borderColor;       /**< Border color. */

    /** When 0 the renderer skips the shadow pass entirely. Set via UIRectangle_SetShadow. */
    int hasShadow;
    UIShadow shadow;           /**< Shadow parameters (offset, blur, spread, color). */

    /**
     * Optional inner content. A rectangle is the most basic container:
     * any widget added via UIRectangle_AddChild is rendered *inside* the
     * rect's bounds, laid out top-to-bottom from the inner padding box
     * with `gap` between consecutive children (mouse events recurse into
     * them as well). NULL when the rectangle is a plain leaf (the common
     * case), so leaf rectangles stay zero-overhead. The rectangle OWNS
     * this collection and frees it (and the children) on Destroy.
     *
     * Typed `void*` (a `UIChildren*`) to avoid a circular include with
     * children.h, which already includes rect.h — same convention as
     * `UIScroll.background`. Cast to `UIChildren*` at the use sites.
     */
    void* children;
    float paddingLeft;         /**< Inner left padding for children (pixels). */
    float paddingTop;          /**< Inner top padding for children (pixels). */
    float paddingRight;        /**< Inner right padding for children (pixels). */
    float paddingBottom;       /**< Inner bottom padding for children (pixels). */
    float gap;                 /**< Vertical gap between consecutive children (pixels). */
} UIRectangle;

/**
 * Creates a UIRectangle object with the specified width and height.
 * @return A pointer to the UIRectangle object.
 */
UIRectangle* UIRectangle_Create();

/**
 * Sets the margins of the rectangle.
 * @param rect Pointer to the UIRectangle object.
 * @param left Left margin.
 * @param top Top margin.
 * @param right Right margin.
 * @param bottom Bottom margin.
 * @return Pointer to the updated UIRectangle object.
 */
UIRectangle* UIRectangle_SetMargins(UIRectangle* rect, float left, float top, float right, float bottom);

/**
 * Sets the border radius of the rectangle.
 * @param rect Pointer to the UIRectangle object.
 * @param radius Border radius.
 * @return Pointer to the updated UIRectangle object.
 */
UIRectangle* UIRectangle_SetRadius(UIRectangle* rect, float radius);

/**
 * Sets the border width of the rectangle.
 * @param rect Pointer to the UIRectangle object.
 * @param width Border width.
 * @return Pointer to the updated UIRectangle object.
 */
UIRectangle* UIRectangle_SetBorderWidth(UIRectangle* rect, float width);

/**
 * Sets the color of the rectangle.
 * @param rect Pointer to the UIRectangle object.
 * @param color Color of the rectangle.
 * @return Pointer to the updated UIRectangle object.
 */
UIRectangle* UIRectangle_SetColor(UIRectangle* rect, UIColor color);

/**
 * Sets the border color of the rectangle.
 * @param rect Pointer to the UIRectangle object.
 * @param color Border color of the rectangle.
 * @return Pointer to the updated UIRectangle object.
 */
UIRectangle* UIRectangle_SetBorderColor(UIRectangle* rect, UIColor color);

/**
 * Sets a drop shadow on the rectangle.
 * Use UI_SHADOW_DEFAULT as a sensible starting point.
 *
 * @param rect   Pointer to the UIRectangle.
 * @param shadow Shadow configuration (offset, blur, spread, color).
 * @return The same UIRectangle pointer (for chaining).
 */
UIRectangle* UIRectangle_SetShadow(UIRectangle* rect, UIShadow shadow);

/**
 * Clears any previously configured shadow.
 *
 * @param rect Pointer to the UIRectangle.
 * @return The same UIRectangle pointer (for chaining).
 */
UIRectangle* UIRectangle_ClearShadow(UIRectangle* rect);

/**
 * Adds a child widget to the rectangle, turning it into a container.
 * The children list is created lazily on the first call. Children are
 * laid out top-to-bottom inside the rect's inner padding box, separated
 * by `gap`. The rectangle takes ownership of `child` and frees it on
 * Destroy.
 *
 * @param rect  Pointer to the UIRectangle.
 * @param child Child widget (ownership transferred).
 * @return The same UIRectangle pointer (for chaining), or NULL on error.
 */
UIRectangle* UIRectangle_AddChild(UIRectangle* rect, UIWidget* child);

/**
 * Sets the inner padding applied to the rectangle's children.
 * @return The same UIRectangle pointer (for chaining).
 */
UIRectangle* UIRectangle_SetPadding(UIRectangle* rect, float left, float top, float right, float bottom);

/**
 * Sets the vertical gap inserted between consecutive children.
 * @return The same UIRectangle pointer (for chaining).
 */
UIRectangle* UIRectangle_SetGap(UIRectangle* rect, float gap);

/**
 * Destroys the UIRectangle object and frees its memory.
 * @param rect Pointer to the UIRectangle object to be destroyed.
 */
void UIRectangle_Destroy(UIRectangle* rect);

#endif // UIKIT_RECT_H