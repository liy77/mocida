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
    // NULL target = "align inside parent" (see UIAlign.target_widget). MUI
    // calls SetAlignmentByParent before the container Widget exists, so the
    // rule is stored with NULL targets and resolved once SetParent runs —
    // and again every frame from the render path.
    if (targetH == NULL) targetH = UIWidget_GetParent(widget);
    if (targetV == NULL) targetV = UIWidget_GetParent(widget);

    if (targetH == NULL || targetV == NULL ||
        targetH->width == NULL || targetH->height == NULL ||
        targetV->width == NULL || targetV->height == NULL) {
        // Deferred: parent not linked or sized yet. Not an error — AdoptChildren
        // / SetParent / the next render pass will retry.
        return;
    }

    const float targetV_height = *targetV->height;
    const float targetV_Y      = targetV->y;
    const float targetH_width  = *targetH->width;
    const float targetH_X      = targetH->x;
    const float widgetWidth    = *widget->width;
    const float widgetHeight   = *widget->height;

    // Margins offset the widget away from its anchor (CSS-margin semantics).
    // Prefer UIWidget_SetMargin (widget->margin*) so EVERY widget type —
    // Image, Button, MouseArea, Stack, … — can inset from `anchor:` + `margin:`.
    // Rectangle/Text also expose the same fields on their payload; use those
    // when set so authors who called UIRectangle_SetMargins still win.
    float marginLeft   = widget->marginLeft;
    float marginRight  = widget->marginRight;
    float marginTop    = widget->marginTop;
    float marginBottom = widget->marginBottom;
    UIWidgetBase* base = (UIWidgetBase*)widget->data;
    if (base != NULL &&
        (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_RECTANGLE) ||
         UIWidget_TypeIs(base->__widget_type, UI_WIDGET_TEXT))) {
        UIMarginsObject* m = (UIMarginsObject*)base;
        if (m->marginLeft != 0 || m->marginRight != 0 ||
            m->marginTop != 0 || m->marginBottom != 0) {
            marginLeft   = m->marginLeft;
            marginRight  = m->marginRight;
            marginTop    = m->marginTop;
            marginBottom = m->marginBottom;
        }
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

void UIWidget_SetSelfAlign(UIWidget* widget, int align) {
    if (widget) widget->selfAlign = align;
}

void UIWidget_SetMargin(UIWidget* widget, float left, float top, float right, float bottom) {
    if (!widget) return;
    widget->marginLeft   = left;
    widget->marginTop    = top;
    widget->marginRight  = right;
    widget->marginBottom = bottom;
}

void UIWidget_SetAlignmentByParent(UIWidget* widget, uint8_t valign, uint8_t halign) {
    if (widget == NULL) {
        return;
    }

    // Always store the rule. Parent may be NULL here (MUI builds the child,
    // applies `anchor:`, then adds it to a container that only becomes a
    // Widget later via widgcs). NULL targets mean "use parent when linked";
    // UIWidget_SetParent / UIWidget_AdoptChildren / render-time Align resolve it.
    UIWidget* parent = UIWidget_GetParent(widget);

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

    // Size / parent may still be pending (MUI builds apply `anchor:` before
    // widgcs / AdoptChildren). Align is a no-op until both are ready.
    UIAlignment_Align(widget);
}