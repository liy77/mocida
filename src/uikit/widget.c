#include <uikit/widget.h>
#include <stdio.h>
#include <stdlib.h>

UIWidget* widgc(UIWidgetData data) {
    return UIWidget_Create(data);
}

UIWidget* widgcs(UIWidgetData data, float width, float height) {
    UIWidget* widget = UIWidget_Create(data);
    return UIWidget_SetSize(widget, width, height);
}

UIWidget* UIWidget_Create(UIWidgetData data)  {
    UIWidget* widget = (UIWidget*)malloc(sizeof(UIWidget));
    if (widget == NULL) {
        return NULL; 
    }

    widget->width = 0; // Default width (dynamic)
    widget->height = 0; // Default height (dynamic)
    widget->x = 0;
    widget->y = 0;
    widget->z = 0;
    widget->visible = 1;
    widget->opacity = 1.0f; // Default opacity
    widget->alignment = NULL; // Default alignment (NULL)
    widget->data = data; // Set the data pointer

    return widget;
}

UIWidget* UIWidget_SetSize(UIWidget* widget, float width, float height) {
    if (widget == NULL) {
        return NULL;
    }

    // Free previous width and height if allocated
    free(widget->width);
    free(widget->height);

    // Allocate or set width
    if (width <= UI_DYNAMIC_SIZE) {
        widget->width = NULL; // Dynamic size
    } else {
        widget->width = malloc(sizeof(float));
        if (widget->width == NULL) {
            fprintf(stderr, "Failed to allocate memory for width\n");
            return NULL;
        }
        *widget->width = width;
    }

    // Allocate or set height
    if (height <= UI_DYNAMIC_SIZE) {
        widget->height = NULL; // Dynamic size
    } else {
        widget->height = malloc(sizeof(float));
        if (widget->height == NULL) {
            fprintf(stderr, "Failed to allocate memory for height\n");
            free(widget->width); // Clean up width allocation
            return NULL;
        }
        *widget->height = height;
    }

    return widget;
}

UIWidget* UIWidget_SetPosition(UIWidget* widget, float x, float y) {
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

UIWidget* UIWidget_GetParent(UIWidget* widget) {
    if (widget == NULL) {
        return NULL;
    }

    // Assuming the parent is stored in the widget's data structure
    return (UIWidget*)widget->parent;
}

UIWidget* UIWidget_SetParent(UIWidget* widget, UIWidget* parent) {
    if (widget == NULL || parent == NULL) {
        return NULL;
    }

    widget->parent = parent;
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
