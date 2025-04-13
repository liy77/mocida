#include <uikit/rect.h>
#include <stdio.h>

UIRectangle* UIRectangle_Create() {
    UIRectangle* rect = (UIRectangle*)malloc(sizeof(UIRectangle));
    if (rect == NULL) {
        fprintf(stderr, "Failed to allocate memory for UIRectangle\n");
        return NULL; // Memory allocation failed
    }

    rect->color = UIColorWhite;
    rect->borderColor = UIColorBlack;
    rect->marginLeft = 0;
    rect->marginTop = 0;
    rect->marginRight = 0;
    rect->marginBottom = 0;
    rect->radius = 0;
    rect->borderWidth = 0;
    rect->__widget_type = UI_WIDGET_RECTANGLE; // Set the widget type

    return rect;
}

UIRectangle* UIRectangle_SetMargins(UIRectangle* rect, float left, float top, float right, float bottom) {
    rect->marginLeft = left;
    rect->marginTop = top;
    rect->marginRight = right;
    rect->marginBottom = bottom;
    return rect;
}

UIRectangle* UIRectangle_SetRadius(UIRectangle* rect, float radius) {
    rect->radius = radius;
    return rect;
}

UIRectangle* UIRectangle_SetBorderWidth(UIRectangle* rect, float width) {
    rect->borderWidth = width;
    return rect;
}

UIRectangle* UIRectangle_SetColor(UIRectangle* rect, UIColor color) {
    rect->color = color;
    return rect;
}

UIRectangle* UIRectangle_SetBorderColor(UIRectangle* rect, UIColor color) {
    rect->borderColor = color;
    return rect;
}