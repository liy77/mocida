#include <uikit/alignment.h>
#include <stdlib.h>
#include <stdio.h>
#include <uikit/widget.h>
#include <uikit/debug.h>
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
        UI_WARN(UI_CAT_LAYOUT, "alignment target is NULL or has no defined size");
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
        UI_WARN(UI_CAT_LAYOUT, "parent widget is NULL, alignment will not work");
        return;
    }

    if (parent->width == NULL || parent->height == NULL) {
        UI_WARN(UI_CAT_LAYOUT, "parent widget does not have a defined size");
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

    // Lazy-allocate the alignment slot once. Repeated calls now just
    // overwrite the value in place rather than free()/malloc()-cycling
    // a tiny struct on every update.
    if (widget->alignment == NULL) {
        widget->alignment = malloc(sizeof(UIAlignment));
        if (widget->alignment == NULL) {
            UI_ERROR(UI_CAT_LAYOUT, "out of memory allocating UIAlignment");
            return;
        }
    }

    *widget->alignment = alignment;

    if (widget->width == NULL || widget->height == NULL) {
        UI_WARN(UI_CAT_LAYOUT, "child widget does not have a defined size");
        return;
    }

    UIAlignment_Align(widget);
}