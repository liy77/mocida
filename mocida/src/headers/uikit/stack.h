#ifndef UIKIT_STACK_H
#define UIKIT_STACK_H

#include <uikit/widget.h>
#include <uikit/children.h>
#include <uikit/color.h>

#define UI_WIDGET_STACK "@uikit/stack"

typedef enum {
    UI_STACK_VERTICAL   = 0,
    UI_STACK_HORIZONTAL = 1
} UIStackOrientation;

/** Cross-axis alignment of items within the stack's content box. */
typedef enum {
    UI_STACK_ALIGN_START  = 0, /**< Left (vertical stack) / top (horizontal). */
    UI_STACK_ALIGN_CENTER = 1, /**< Centered on the cross axis. */
    UI_STACK_ALIGN_END    = 2  /**< Right (vertical) / bottom (horizontal). */
} UIStackAlign;

/** Main-axis distribution of items along the stack's content box. Needs the
 *  stack to be larger than its content on the main axis (give it an explicit
 *  size or let the runtime fill the parent). */
typedef enum {
    UI_STACK_JUSTIFY_START   = 0, /**< Packed at the start (default). */
    UI_STACK_JUSTIFY_CENTER  = 1, /**< Packed, centered on the main axis. */
    UI_STACK_JUSTIFY_END     = 2, /**< Packed at the end. */
    UI_STACK_JUSTIFY_BETWEEN = 3  /**< First/last at the edges, even gaps. */
} UIStackJustify;

/**
 * Lays its children sequentially along one axis with constant spacing.
 * Children keep whatever explicit width/height they were given; the
 * stack only sets their position. Use UIStack inside a UIScroll for
 * scrollable lists with variable item sizes.
 */
typedef struct {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_STACK). */

    int   orientation;         /**< UI_STACK_VERTICAL or UI_STACK_HORIZONTAL. */
    int   align;               /**< UIStackAlign — cross-axis alignment of items. */
    int   justify;             /**< UIStackJustify — main-axis distribution of items. */
    float spacing;             /**< Constant gap between consecutive items (pixels). */
    float paddingLeft;         /**< Inner left padding (pixels). */
    float paddingTop;          /**< Inner top padding (pixels). */
    float paddingRight;        /**< Inner right padding (pixels). */
    float paddingBottom;       /**< Inner bottom padding (pixels). */

    UIColor bgColor;           /**< Background fill (alpha 0 == no fill). */
    float   radius;            /**< Corner radius of the background fill. */
    UIColor borderColor;       /**< Border color (drawn when borderWidth > 0). */
    float   borderWidth;       /**< Border thickness (pixels). */

    UIChildren* items;         /**< Owned items. Destroyed with the stack. */
} UIStack;

UIStack* UIStack_Create(UIStackOrientation orientation);
UIStack* UIStack_SetSpacing(UIStack* s, float spacing);
UIStack* UIStack_SetPadding(UIStack* s, float l, float t, float r, float b);
UIStack* UIStack_SetAlign  (UIStack* s, UIStackAlign align);
UIStack* UIStack_SetJustify(UIStack* s, UIStackJustify justify);
UIStack* UIStack_SetBackground(UIStack* s, UIColor color);
UIStack* UIStack_SetRadius (UIStack* s, float radius);
UIStack* UIStack_SetBorder (UIStack* s, UIColor color, float width);
int      UIStack_AddItem   (UIStack* s, UIWidget* item);
void     UIStack_Destroy   (UIStack* s);

#endif // UIKIT_STACK_H
