#include <uikit/alignment.h>
#include <stdlib.h>
#include <stdio.h>
#include <uikit/widget.h>

UIAlignment UIAlignment_Create(UIAlign vertical, UIAlign horizontal) {
    UIAlignment alignment;
    alignment.vertical = vertical;
    alignment.horizontal = horizontal;
    return alignment;
}

void UIWidget_SetAlignment(UIWidget* widget, UIAlignment alignment) {
    if (widget == NULL) {
        return;
    }

    widget->alignment = malloc(sizeof(UIAlignment));
    if (widget->alignment == NULL) {
        fprintf(stderr, "Failed to allocate memory for alignment\n");
        return;
    }
    
    *widget->alignment = alignment;

    UIWidget* parent = UIWidget_GetParent(widget);
    if (parent == NULL) {
        fprintf(stderr, "Parent widget is NULL, alignment will not work\n");
        return;
    }

    if (parent->width == NULL || parent->height == NULL) {
        fprintf(stderr, "Parent widget does not have a defined size.\n");
        return;
    }

    if (widget->width == NULL || widget->height == NULL) {
        fprintf(stderr, "Child widget does not have a defined size.\n");
        return;
    }

    printf("Parent widget size: %f x %f\n", parent->width, parent->height);

    float parentWidth = *parent->width;
    float parentHeight = *parent->height;

    float widgetWidth = *widget->width;
    float widgetHeight = *widget->height;

    switch (alignment.horizontal) {
        case UI_ALIGN_H_LEFT:
            widget->x = 0;
            break;
        case UI_ALIGN_H_CENTER:
            widget->x = (parentWidth - widgetWidth) / 2;
            break;
        case UI_ALIGN_H_RIGHT:
            widget->x = parentWidth - widgetWidth;
            break;
        default:
            break;
    }

    switch (alignment.vertical) {
        case UI_ALIGN_V_TOP:
            widget->y = 0;
            break;
        case UI_ALIGN_V_CENTER:
            widget->y = (parentHeight - widgetHeight) / 2;
            break;
        case UI_ALIGN_V_BOTTOM:
            widget->y = parentHeight - widgetHeight;
            break;
        default:
            break;
    }
}