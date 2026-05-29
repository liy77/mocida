#include <uikit/container.h>
#include <uikit/rect.h>
#include <stdlib.h>
#include <string.h>

// =====================================================================
// UIGrid
// =====================================================================

UIGrid* UIGrid_Create(int columns) {
    if (columns < 1) columns = 1;
    UIGrid* g = (UIGrid*)calloc(1, sizeof(UIGrid));
    if (!g) return NULL;
    g->__widget_type = UI_WIDGET_GRID;
    g->columns = columns;
    g->gapX = g->gapY = 8.0f;
    g->cellW = g->cellH = 100.0f;
    g->items = UIChildren_Create(16);
    if (!g->items) { free(g); return NULL; }
    return g;
}

UIGrid* UIGrid_SetColumns(UIGrid* g, int columns) {
    if (!g) return g;
    if (columns < 1) columns = 1;
    g->columns = columns;
    return g;
}

UIGrid* UIGrid_SetGap(UIGrid* g, float gapX, float gapY) {
    if (!g) return g;
    g->gapX = gapX;
    g->gapY = gapY;
    return g;
}

UIGrid* UIGrid_SetCellSize(UIGrid* g, float w, float h) {
    if (!g) return g;
    g->cellW = w;
    g->cellH = h;
    return g;
}

UIGrid* UIGrid_SetPadding(UIGrid* g, float l, float t, float r, float b) {
    if (!g) return g;
    g->paddingLeft = l;
    g->paddingTop = t;
    g->paddingRight = r;
    g->paddingBottom = b;
    return g;
}

int UIGrid_AddItem(UIGrid* g, UIWidget* item) {
    if (!g || !item) return 0;
    return UIChildren_Add(g->items, item);
}

void UIGrid_GetContentSize(UIGrid* g, float* outW, float* outH) {
    if (!g) { if (outW) *outW = 0; if (outH) *outH = 0; return; }
    const int count = g->items ? g->items->count : 0;
    if (count == 0) {
        if (outW) *outW = g->paddingLeft + g->paddingRight;
        if (outH) *outH = g->paddingTop + g->paddingBottom;
        return;
    }
    const int cols = g->columns;
    const int rows = (count + cols - 1) / cols;
    const float w = g->paddingLeft + g->paddingRight
                  + cols * g->cellW + (cols - 1) * g->gapX;
    const float h = g->paddingTop + g->paddingBottom
                  + rows * g->cellH + (rows - 1) * g->gapY;
    if (outW) *outW = w;
    if (outH) *outH = h;
}

void UIGrid_Destroy(UIGrid* g) {
    if (!g) return;
    if (g->items) UIChildren_Destroy(g->items);
    free(g);
}

// =====================================================================
// UIScroll
// =====================================================================

UIScroll* UIScroll_Create(void) {
    UIScroll* s = (UIScroll*)calloc(1, sizeof(UIScroll));
    if (!s) return NULL;
    s->__widget_type = UI_WIDGET_SCROLL;
    s->allowVertical = 1;
    s->allowHorizontal = 1;
    s->allowDragScroll = 0;
    s->wheelSpeed = 60.0f;
    return s;
}

UIScroll* UIScroll_SetContent(UIScroll* s, UIWidget* content) {
    if (!s) return s;
    if (s->content) {
        UIWidget_Destroy(s->content);
    }
    s->content = content;
    // Invalidate cached size.
    s->contentW = 0.0f;
    s->contentH = 0.0f;
    return s;
}

UIScroll* UIScroll_SetScroll(UIScroll* s, float x, float y) {
    if (!s) return s;
    s->scrollX = x;
    s->scrollY = y;
    return s;
}

UIScroll* UIScroll_SetAxes(UIScroll* s, int allowVertical, int allowHorizontal) {
    if (!s) return s;
    s->allowVertical = allowVertical ? 1 : 0;
    s->allowHorizontal = allowHorizontal ? 1 : 0;
    return s;
}

UIScroll* UIScroll_SetDragScroll(UIScroll* s, int enabled) {
    if (!s) return s;
    s->allowDragScroll = enabled ? 1 : 0;
    return s;
}

UIScroll* UIScroll_SetWheelSpeed(UIScroll* s, float pxPerNotch) {
    if (!s) return s;
    s->wheelSpeed = pxPerNotch;
    return s;
}

void UIScroll_InvalidateContentSize(UIScroll* s) {
    if (!s) return;
    s->contentW = 0.0f;
    s->contentH = 0.0f;
}

void UIScroll_Destroy(UIScroll* s) {
    if (!s) return;
    if (s->content) UIWidget_Destroy(s->content);
    if (s->background) UIRectangle_Destroy((UIRectangle*)s->background);
    free(s);
}

// =====================================================================
// UIListView (1-column grid wrapped in a scroll)
// =====================================================================

UIScroll* UIListView_Create(float itemHeight) {
    UIScroll* s = UIScroll_Create();
    if (!s) return NULL;
    UIScroll_SetAxes(s, /*vert=*/1, /*horiz=*/0);

    UIGrid* g = UIGrid_Create(1);
    UIGrid_SetCellSize(g, 0.0f, itemHeight > 0.0f ? itemHeight : 60.0f);
    UIGrid_SetGap(g, 0.0f, 6.0f);
    UIGrid_SetPadding(g, 8.0f, 8.0f, 8.0f, 8.0f);

    // The cellW will be replaced with the scroll's bounds at first
    // render (see RenderWidget in window.c). Stored 0 means "stretch
    // to viewport width".
    UIWidget* gridW = UIWidget_Create(g);
    UIScroll_SetContent(s, gridW);
    return s;
}

UIGrid* UIListView_GetGrid(UIScroll* listView) {
    if (!listView || !listView->content) return NULL;
    UIWidgetBase* base = (UIWidgetBase*)listView->content->data;
    if (!base || strcmp(base->__widget_type, UI_WIDGET_GRID) != 0) return NULL;
    return (UIGrid*)base;
}

int UIListView_AddItem(UIScroll* listView, UIWidget* item) {
    UIGrid* g = UIListView_GetGrid(listView);
    if (!g) return 0;
    return UIGrid_AddItem(g, item);
}

// =====================================================================
// UIGridView (N-column grid wrapped in a scroll)
// =====================================================================

UIScroll* UIGridView_Create(int columns, float cellW, float cellH) {
    UIScroll* s = UIScroll_Create();
    if (!s) return NULL;
    UIScroll_SetAxes(s, /*vert=*/1, /*horiz=*/0);

    UIGrid* g = UIGrid_Create(columns);
    UIGrid_SetCellSize(g, cellW, cellH);
    UIGrid_SetGap(g, 10.0f, 10.0f);
    UIGrid_SetPadding(g, 12.0f, 12.0f, 12.0f, 12.0f);

    UIWidget* gridW = UIWidget_Create(g);
    UIScroll_SetContent(s, gridW);
    return s;
}

UIGrid* UIGridView_GetGrid(UIScroll* gridView) {
    return UIListView_GetGrid(gridView); // same shape
}

int UIGridView_AddItem(UIScroll* gridView, UIWidget* item) {
    return UIListView_AddItem(gridView, item);
}
