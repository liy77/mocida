#include <uikit/rect.h>
#include <uikit/debug.h>

UIRectangle* UIRectangle_Create() {
    UIRectangle* rect = (UIRectangle*)malloc(sizeof(UIRectangle));
    if (rect == NULL) {
        UI_ERROR(UI_CAT_WIDGET, "out of memory allocating UIRectangle");
        return NULL; // Memory allocation failed
    }

    rect->color = UI_COLOR_WHITE;
    rect->borderColor = UI_COLOR_BLACK;
    rect->marginLeft = 0;
    rect->marginTop = 0;
    rect->marginRight = 0;
    rect->marginBottom = 0;
    rect->radius = 0;
    rect->borderWidth = 0;
    rect->__widget_type = UI_WIDGET_RECTANGLE; // Set the widget type
    rect->hasShadow = 0;
    rect->shadow = UI_SHADOW_NONE;

    return rect;
}

UIRectangle* UIRectangle_SetShadow(UIRectangle* rect, UIShadow shadow) {
    if (!rect) return NULL;
    rect->hasShadow = 1;
    rect->shadow = shadow;
    return rect;
}

UIRectangle* UIRectangle_ClearShadow(UIRectangle* rect) {
    if (!rect) return NULL;
    rect->hasShadow = 0;
    rect->shadow = UI_SHADOW_NONE;
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

void UIRectangle_Destroy(UIRectangle* rect) {
    if (rect) {
        // __widget_type points at a string literal (UI_WIDGET_RECTANGLE);
        // calling free() on it would be undefined behaviour.
        free(rect);
    }
}