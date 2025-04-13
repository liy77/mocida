#include <uikit/alignment.h>
#include <stdlib.h>
#include <stdio.h>
#include <uikit/widget.h>
#include <string.h>

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

    // Free existing alignment if it exists
    if (widget->alignment != NULL) {
        free(widget->alignment);
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

    float parentWidth = *parent->width;
    float parentHeight = *parent->height;

    float widgetWidth = *widget->width;
    float widgetHeight = *widget->height;
    float parentX = parent->x;
    float parentY = parent->y;

    float marginLeft = 0;
    float marginRight = 0;
    float marginTop = 0;
    float marginBottom = 0;

    UIWidgetBase* parentBase = (UIWidgetBase*)parent->data;
    if (parentBase != NULL) {
        if (
            strcmp(parentBase->__widget_type, UI_WIDGET_RECTANGLE) == 0 ||
            strcmp(parentBase->__widget_type, UI_WIDGET_TEXT) == 0
        ) {
            UIMarginsObject* objectMargins = (UIMarginsObject*)parentBase;
            marginLeft = objectMargins->marginLeft;
            marginRight = objectMargins->marginRight;
            marginTop = objectMargins->marginTop;
            marginBottom = objectMargins->marginBottom;
        }
    }

    switch (alignment.horizontal) {
        case UI_ALIGN_H_LEFT:
            widget->x = parentX + marginLeft;
            break;
        case UI_ALIGN_H_CENTER:
            widget->x = (parentX + (parentWidth - widgetWidth) / 2) + marginLeft;
            break;
        case UI_ALIGN_H_RIGHT:
            widget->x = parentX + parentWidth + marginLeft - widgetWidth - marginRight;
            break;
        default:
            break;
    }

    switch (alignment.vertical) {
        case UI_ALIGN_V_TOP:
            widget->y = parentY + marginTop - marginBottom;
            break;
        case UI_ALIGN_V_CENTER:
            widget->y = parentY + marginTop + (parentHeight - marginTop - marginBottom - widgetHeight) / 2;
            break;
        case UI_ALIGN_V_BOTTOM:
            widget->y = parentY + marginTop + parentHeight - widgetHeight - marginBottom;
            break;
        default:
            break;
    }
}