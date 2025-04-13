#ifndef UIKIT_RECT_H
#define UIKIT_RECT_H

#include "color.h"
#include <uikit/widget.h>

typedef struct {
    const char* __widget_type;

    float marginLeft;
    float marginTop;
    float marginRight;
    float marginBottom;
    float radius;
    float borderWidth;

    UIColor color;
    UIColor borderColor;
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

#endif // UIKIT_RECT_H