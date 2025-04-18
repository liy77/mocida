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

UIWidget* UIAlignment_GetHTarget(UIAlignment* alignment) {
    if (alignment == NULL) {
        return NULL;
    }

    return (UIWidget*)alignment->horizontal.target_widget;
}


UIWidget* UIAlignment_GetVTarget(UIAlignment* alignment) {
    if (alignment == NULL) {
        return NULL;
    }

    return (UIWidget*)alignment->vertical.target_widget;
}

void UIAlignment_Align(UIWidget* widget) {
    if (widget == NULL || widget->alignment == NULL) {
        return;
    }

    UIAlignment alignment = *(UIAlignment*)widget->alignment;
    UIWidget* targetH = UIAlignment_GetHTarget(&alignment);
    UIWidget* targetV = UIAlignment_GetVTarget(&alignment);

    if (targetH == NULL || targetV == NULL || 
        targetH->width == NULL || targetH->height == NULL) {
        fprintf(stderr, "Target widget is NULL or has no defined size.\n");
        return;
    }

    float targetV_width = *targetV->width;
    float targetV_height = *targetV->height;
    float targetV_X = targetV->x, targetV_Y = targetV->y;
    float targetH_width = *targetH->width;
    float targetH_height = *targetH->height;
    float targetH_X = targetH->x, targetH_Y = targetH->y;
    float widgetWidth = *widget->width, widgetHeight = *widget->height;
    
    float V_marginLeft = 0, V_marginRight = 0, V_marginTop = 0, V_marginBottom = 0;
    float H_marginLeft = 0, H_marginRight = 0, H_marginTop = 0, H_marginBottom = 0;

    UIWidgetBase* base;
    if ((base = (UIWidgetBase*)targetV->data) != NULL &&
        (strcmp(base->__widget_type, UI_WIDGET_RECTANGLE) == 0 || strcmp(base->__widget_type, UI_WIDGET_TEXT) == 0)) {
        UIMarginsObject* margins = (UIMarginsObject*)base;
        V_marginLeft = margins->marginLeft;
        V_marginRight = margins->marginRight;
        V_marginTop = margins->marginTop;
        V_marginBottom = margins->marginBottom;
    }

    if ((base = (UIWidgetBase*)targetH->data) != NULL && 
        (strcmp(base->__widget_type, UI_WIDGET_RECTANGLE) == 0 || strcmp(base->__widget_type, UI_WIDGET_TEXT) == 0)) {
        UIMarginsObject* margins = (UIMarginsObject*)base;
        H_marginLeft = margins->marginLeft;
        H_marginRight = margins->marginRight;
        H_marginTop = margins->marginTop;
        H_marginBottom = margins->marginBottom;
    }

    switch (alignment.horizontal.value) {
        case UI_ALIGN_H_LEFT:
            widget->x = targetH_X + H_marginLeft;
            break;
        case UI_ALIGN_H_CENTER:
            widget->x = (targetH_X + (targetH_width - widgetWidth) / 2) + H_marginLeft;
            break;
        case UI_ALIGN_H_RIGHT:
            widget->x = targetH_X + targetH_width + H_marginLeft - widgetWidth - H_marginRight;
            printf("widget->x: %f\n", widget->x);
            break;
        default:
            break;
    }

    switch (alignment.vertical.value) {
        case UI_ALIGN_V_TOP:
            widget->y = targetV_Y + V_marginTop - V_marginBottom;
            break;
        case UI_ALIGN_V_CENTER:
            widget->y = targetV_Y + V_marginTop + (targetV_height - V_marginTop - V_marginBottom - widgetHeight) / 2;
            break;
        case UI_ALIGN_V_BOTTOM:
            widget->y = targetV_Y + V_marginTop + targetV_height - widgetHeight - V_marginBottom;
            break;
        default:
            break;
    }
} 

void UIWidget_SetAlignmentByParent(UIWidget* widget, uint8_t valign, uint8_t halign) {
    if (widget == NULL) {
        return;
    }

    UIWidget* parent = UIWidget_GetParent(widget);
    if (parent == NULL) {
        fprintf(stderr, "Parent widget is NULL, alignment will not work\n");
        return;
    }

    if (parent->width == NULL || parent->height == NULL) {
        fprintf(stderr, "Parent widget does not have a defined size.\n");
        return;
    }

    UIAlignment alignment = UIAlignment_Create(
        (UIAlign){.value = valign, .target_widget = parent},
        (UIAlign){.value = halign, .target_widget = parent}
    );

    UIWidget_SetAlignment(widget, alignment);
}

void UIWidget_SetAlignment(UIWidget* widget, UIAlignment alignment) {
    if (widget == NULL) {
        return;
    }

    // Free existing alignment if it exists
    if (widget->alignment != NULL) {
        free(widget->alignment);
    }

    // Allocate memory for the new alignment
    widget->alignment = malloc(sizeof(UIAlignment));
    if (widget->alignment == NULL) {
        fprintf(stderr, "Failed to allocate memory for alignment\n");
        return;
    }
    
    *widget->alignment = alignment;

    if (widget->width == NULL || widget->height == NULL) {
        fprintf(stderr, "Child widget does not have a defined size.\n");
        return;
    }

    UIAlignment_Align(widget);
}