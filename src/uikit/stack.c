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

int UIStack_AddItem(UIStack* s, UIWidget* item) {
    if (!s || !item) return 0;
    return UIChildren_Add(s->items, item);
}

void UIStack_Destroy(UIStack* s) {
    if (!s) return;
    if (s->items) UIChildren_Destroy(s->items);
    free(s);
}
