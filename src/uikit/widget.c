#include <uikit/widget.h>
#include <stdio.h>
#include <stdlib.h>

UIWidget* UIWidget_Create(UIWidgetData data)  {
    if (data == NULL) {
        return NULL;
    }
    
    UIWidget* widget = (UIWidget*)malloc(sizeof(UIWidget));
    if (widget == NULL) {
        return NULL; 
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
        return NULL;
    }

    widget->x = x;
    widget->y = y;
    return widget;
}

UIWidget* UIWidget_SetZIndex(UIWidget* widget, int z) {
    if (widget == NULL) {
        return NULL;
    }

    widget->z = z;
    return widget;
}

UIWidget* UIWidget_SetVisible(UIWidget* widget, int visible) {
    if (widget == NULL) {
        return NULL;
    }

    widget->visible = visible;
    return widget;
}

void UIWidget_Destroy(UIWidget* widget) {
    if (widget == NULL) {
        return;
    }

    if (widget->data != NULL) {
        free(widget->data); // Free the data pointer
    }

    free(widget); // Free the widget itself
}
