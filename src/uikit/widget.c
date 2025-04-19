#include <uikit/widget.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uikit/text.h>

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
    widget->id = NULL; // Default ID (NULL)

    return widget;
}

UIWidget* UIWidget_SetId(UIWidget* widget, const char* id) {
    if (widget == NULL || id == NULL) {
        return NULL;
    }

    // Free previous ID if allocated
    if (widget->id != NULL) {
        free(widget->id);
    }
    
    // Allocate and set new ID
    size_t len = strlen(id) + 1;
    widget->id = (char*)malloc(len);
    if (widget->id) {
        memcpy(widget->id, id, len);
    }
    widget->id = _strdup(id);
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

    // Check if the parent is a valid widget
    UIWidget* parent = (UIWidget*)widget->parent;
    if (parent != NULL && parent->width != NULL && parent->height != NULL) {
        return parent;
    }

    fprintf(stderr, "Parent widget is NULL or invalid\n");
    return NULL;
}

UIWidget* UIWidget_SetParent(UIWidget* widget, UIWidget* parent) {
    if (widget == NULL || parent == NULL) {
        return NULL;
    }

    widget->parent = (UIWidgetData)parent;
    return widget;
}

void UIWidget_Destroy(UIWidget* widget) {
    if (!widget) return;
    
    if (widget->data) {
        UIWidgetBase* base = (UIWidgetBase*)widget->data;
        if (!strcmp(base->__widget_type, UI_WIDGET_TEXT)) {
            UIText* text = (UIText*)base;
            UIText_DestroyTexture(text);
            if (text->background) free(text->background);
            if (text->text) free(text->text);
            if (text->fontFamily) free(text->fontFamily);
            free(text->__widget_type);
        }
        else if (!strcmp(base->__widget_type, UI_WIDGET_RECTANGLE)) {
            UIRectangle* rect = (UIRectangle*)base;
            free(rect->__widget_type);
        }
        free(base);
    }
    
    if (widget->width) free(widget->width);
    if (widget->height) free(widget->height);
    free(widget);
}