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
        targetH->width == NULL || targetH->height == NULL ||
        targetV->width == NULL || targetV->height == NULL) {
        UI_WARN(UI_CAT_LAYOUT, "alignment target is NULL or has no defined size");
        return;
    }

    const float targetV_height = *targetV->height;
    const float targetV_Y      = targetV->y;
    const float targetH_width  = *targetH->width;
    const float targetH_X      = targetH->x;
    const float widgetWidth    = *widget->width;
    const float widgetHeight   = *widget->height;

    // Margins are read from the WIDGET BEING ALIGNED (CSS-margin semantics):
    // they offset the widget away from its anchor point. The previous
    // implementation read them from the target, which conflated the
    // target's intrinsic inset with the child's outer margin and made
    // anchoring with a per-widget offset impossible.
    //
    // Only UIRectangle / UIText expose the margin fields at a known offset
    // (same prefix as UIMarginsObject). For every other widget type the
    // margins are taken as zero — those widgets shouldn't carry their
    // own margin metadata since the field layout isn't compatible with
    // the UIMarginsObject cast.
    float marginLeft = 0, marginRight = 0, marginTop = 0, marginBottom = 0;
    UIWidgetBase* base = (UIWidgetBase*)widget->data;
    if (base != NULL &&
        (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_RECTANGLE) ||
         UIWidget_TypeIs(base->__widget_type, UI_WIDGET_TEXT))) {
        UIMarginsObject* m = (UIMarginsObject*)base;
        marginLeft   = m->marginLeft;
        marginRight  = m->marginRight;
        marginTop    = m->marginTop;
        marginBottom = m->marginBottom;
    }

    // Horizontal: compute the anchor-only base, then offset by the
    // widget's own margins. Margins compose as (left - right) so a
    // negative right margin lets the widget hang off the right edge
    // of its target.
    switch (alignment.horizontal.value) {
        case UI_ALIGN_H_LEFT:
            widget->x = targetH_X;
            break;
        case UI_ALIGN_H_CENTER:
            widget->x = targetH_X + (targetH_width - widgetWidth) / 2.0f;
            break;
        case UI_ALIGN_H_RIGHT:
            widget->x = targetH_X + targetH_width - widgetWidth;
            break;
        default:
            break;
    }
    widget->x += marginLeft - marginRight;

    // Vertical: same shape as horizontal.
    switch (alignment.vertical.value) {
        case UI_ALIGN_V_TOP:
            widget->y = targetV_Y;
            break;
        case UI_ALIGN_V_CENTER:
            widget->y = targetV_Y + (targetV_height - widgetHeight) / 2.0f;
            break;
        case UI_ALIGN_V_BOTTOM:
            widget->y = targetV_Y + targetV_height - widgetHeight;
            break;
        default:
            break;
    }
    widget->y += marginTop - marginBottom;
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