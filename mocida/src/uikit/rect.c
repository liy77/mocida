#include <uikit/rect.h>
#include <uikit/children.h>
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

    // Container support: leaf by default (no children allocated).
    rect->children = NULL;
    rect->paddingLeft = 0;
    rect->paddingTop = 0;
    rect->paddingRight = 0;
    rect->paddingBottom = 0;
    rect->gap = 0;

    return rect;
}

UIRectangle* UIRectangle_AddChild(UIRectangle* rect, UIWidget* child) {
    if (!rect || !child) return NULL;
    if (!rect->children) {
        rect->children = UIChildren_Create(8);
        if (!rect->children) {
            UI_ERROR(UI_CAT_WIDGET, "out of memory allocating UIRectangle children");
            return NULL;
        }
    }
    UIChildren_Add((UIChildren*)rect->children, child);
    return rect;
}

UIRectangle* UIRectangle_SetPadding(UIRectangle* rect, float left, float top, float right, float bottom) {
    if (!rect) return NULL;
    rect->paddingLeft = left;
    rect->paddingTop = top;
    rect->paddingRight = right;
    rect->paddingBottom = bottom;
    return rect;
}

UIRectangle* UIRectangle_SetGap(UIRectangle* rect, float gap) {
    if (!rect) return NULL;
    rect->gap = gap;
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
        // Free owned children (the list also destroys each child widget).
        if (rect->children) {
            UIChildren_Destroy((UIChildren*)rect->children);
            rect->children = NULL;
        }
        // __widget_type points at a string literal (UI_WIDGET_RECTANGLE);
        // calling free() on it would be undefined behaviour.
        free(rect);
    }
}