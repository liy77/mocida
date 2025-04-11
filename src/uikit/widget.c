#include <uikit/widget.h>
#include <stdio.h>
#include <stdlib.h>

UIWidget* UIWidget_Create(UIWidgetData data)  {
    if (data == NULL) {
        return NULL; // Invalid argument
    }
    
    UIWidget* widget = (UIWidget*)malloc(sizeof(UIWidget));
    if (widget == NULL) {
        return NULL; // Memory allocation failed
    }

    widget->x = 0;
    widget->y = 0;
    widget->z = 0;
    widget->visible = 1;
    widget->data = data; // Set the data pointer

    return widget;
}

UIWidget* UIWidget_SetPosition(UIWidget* widget, int x, int y) {
    if (widget == NULL) {
        return NULL; // Invalid argument
    }

    widget->x = x;
    widget->y = y;
    return widget;
}

UIWidget* UIWidget_SetZIndex(UIWidget* widget, int z) {
    if (widget == NULL) {
        return NULL; // Invalid argument
    }

    widget->z = z;
    return widget;
}

UIWidget* UIWidget_SetVisible(UIWidget* widget, int visible) {
    if (widget == NULL) {
        return NULL; // Invalid argument
    }

    widget->visible = visible;
    return widget;
}

void UIWidget_Destroy(UIWidget* widget) {
    if (widget == NULL) {
        return; // Invalid argument
    }

    if (widget->data != NULL) {
        free(widget->data); // Free the data pointer
    }

    free(widget); // Free the widget itself
}
