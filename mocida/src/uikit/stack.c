#include <uikit/stack.h>
#include <stdlib.h>

UIStack* UIStack_Create(UIStackOrientation orientation) {
    UIStack* s = (UIStack*)calloc(1, sizeof(UIStack));
    if (!s) return NULL;
    s->__widget_type = UI_WIDGET_STACK;
    s->orientation   = (int)orientation;
    s->spacing       = 8.0f;
    s->items         = UIChildren_Create(16);
    if (!s->items) { free(s); return NULL; }
    return s;
}

UIStack* UIStack_SetSpacing(UIStack* s, float spacing) {
    if (s) s->spacing = spacing;
    return s;
}

UIStack* UIStack_SetPadding(UIStack* s, float l, float t, float r, float b) {
    if (!s) return s;
    s->paddingLeft   = l;
    s->paddingTop    = t;
    s->paddingRight  = r;
    s->paddingBottom = b;
    return s;
}

UIStack* UIStack_SetAlign(UIStack* s, UIStackAlign align) {
    if (s) s->align = (int)align;
    return s;
}

UIStack* UIStack_SetJustify(UIStack* s, UIStackJustify justify) {
    if (s) s->justify = (int)justify;
    return s;
}

UIStack* UIStack_SetBackground(UIStack* s, UIColor color) {
    if (s) s->bgColor = color;
    return s;
}

UIStack* UIStack_SetRadius(UIStack* s, float radius) {
    if (s) s->radius = radius;
    return s;
}

UIStack* UIStack_SetBorder(UIStack* s, UIColor color, float width) {
    if (!s) return s;
    s->borderColor = color;
    s->borderWidth = width;
    return s;
}

UIStack* UIStack_SetFreeLayout(UIStack* s, int enabled) {
    if (s) s->freeLayout = enabled ? 1 : 0;
    return s;
}

int UIStack_AddItem(UIStack* s, UIWidget* item) {
    if (!s || !item) return 0;
    return UIChildren_Add(s->items, item);
}

void UIStack_Destroy(UIStack* s) {
    if (!s) return;
    if (s->items) UIChildren_Destroy(s->items);
    free(s);
}
