#ifndef UIKIT_STACK_H
#define UIKIT_STACK_H

#include <uikit/widget.h>
#include <uikit/children.h>

#define UI_WIDGET_STACK "@uikit/stack"

typedef enum {
    UI_STACK_VERTICAL   = 0,
    UI_STACK_HORIZONTAL = 1
} UIStackOrientation;

/**
 * Lays its children sequentially along one axis with constant spacing.
 * Children keep whatever explicit width/height they were given; the
 * stack only sets their position. Use UIStack inside a UIScroll for
 * scrollable lists with variable item sizes.
 */
typedef struct {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_STACK). */

    int   orientation;         /**< UI_STACK_VERTICAL or UI_STACK_HORIZONTAL. */
    float spacing;             /**< Constant gap between consecutive items (pixels). */
    float paddingLeft;         /**< Inner left padding (pixels). */
    float paddingTop;          /**< Inner top padding (pixels). */
    float paddingRight;        /**< Inner right padding (pixels). */
    float paddingBottom;       /**< Inner bottom padding (pixels). */

    UIChildren* items;         /**< Owned items. Destroyed with the stack. */
} UIStack;

UIStack* UIStack_Create(UIStackOrientation orientation);
UIStack* UIStack_SetSpacing(UIStack* s, float spacing);
UIStack* UIStack_SetPadding(UIStack* s, float l, float t, float r, float b);
int      UIStack_AddItem   (UIStack* s, UIWidget* item);
void     UIStack_Destroy   (UIStack* s);

#endif // UIKIT_STACK_H
