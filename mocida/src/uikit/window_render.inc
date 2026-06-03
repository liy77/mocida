// window_render.inc — #included by window.c (SAME translation unit).
//
// The widget render pass: EnsureWidgetSize, RenderGrid / RenderScroll, the
// cull test, and RenderSingleWidget(_Inner) — the per-widget-type draw dispatch
// (Stack/Rectangle/Text/Button/Image/... ). Calls the Draw* primitives from
// window_draw.inc. Split out of window.c; relies on its file-level state.

// Helper: makes sure a UIWidget has float* width/height initialised to
// the given default values. Used by container layouts so children get
// usable bounds without forcing the caller to size every item.
static void EnsureWidgetSize(UIWidget* w, float defaultW, float defaultH) {
    if (!w) return;
    if (!w->width) {
        w->width = (float*)malloc(sizeof(float));
        if (w->width) *w->width = defaultW;
    }
    if (!w->height) {
        w->height = (float*)malloc(sizeof(float));
        if (w->height) *w->height = defaultH;
    }
}

// Forward declaration so containers can recurse into themselves.
static void RenderSingleWidget(UIWindow* window, UIWidget* el);
static void RenderSingleWidget_Inner(UIWindow* window, UIWidget* el);

// Lays a UIGrid's items out relative to the grid widget's (x, y) and
// recursively renders them via RenderSingleWidget. Children are sized
// to (grid->cellW, grid->cellH) when they have no explicit size.
static void RenderGrid(UIWindow* window, UIWidget* el, UIGrid* g) {
    if (!g->items) return;
    UIChildren_SortByZ(g->items);

    const int cols = g->columns > 0 ? g->columns : 1;
    const float stepX = g->cellW + g->gapX;
    const float stepY = g->cellH + g->gapY;
    const float originX = el->x + g->paddingLeft;
    const float originY = el->y + g->paddingTop;

    for (int i = 0; i < g->items->count; i++) {
        UIWidget* item = g->items->children[i];
        if (!item) continue;
        const int col = i % cols;
        const int row = i / cols;
        EnsureWidgetSize(item, g->cellW, g->cellH);
        item->x = originX + col * stepX;
        item->y = originY + row * stepY;
        RenderSingleWidget(window, item);
    }
}

// Renders a UIScroll: optional background, clip rect over the viewport,
// then renders content offset by (-scrollX, -scrollY). The content's
// position is mutated so nested grids see their absolute on-screen
// coords.
static void RenderScroll(UIWindow* window, UIWidget* el, UIScroll* s) {
    if (!el->width || !el->height) return;
    const SDL_FRect viewport = { el->x, el->y, *el->width, *el->height };
    if (viewport.w <= 0 || viewport.h <= 0) return;

    // Measure content the first time (or after invalidation).
    if (s->contentW == 0.0f || s->contentH == 0.0f) {
        if (s->content && s->content->data) {
            UIWidgetBase* cb = (UIWidgetBase*)s->content->data;
            if (!strcmp(cb->__widget_type, UI_WIDGET_GRID)) {
                UIGrid* g = (UIGrid*)cb;
                // ListView-style: cellW <= 0 stretches to viewport width.
                if (g->cellW <= 0.0f) {
                    g->cellW = viewport.w - g->paddingLeft - g->paddingRight;
                }
                UIGrid_GetContentSize(g, &s->contentW, &s->contentH);
            } else if (s->content->width && s->content->height) {
                s->contentW = *s->content->width;
                s->contentH = *s->content->height;
            }
        }
    }

    // Clamp scroll offsets.
    float maxX = s->contentW - viewport.w; if (maxX < 0.0f) maxX = 0.0f;
    float maxY = s->contentH - viewport.h; if (maxY < 0.0f) maxY = 0.0f;
    if (s->scrollX < 0.0f) s->scrollX = 0.0f;
    if (s->scrollY < 0.0f) s->scrollY = 0.0f;
    if (s->scrollX > maxX) s->scrollX = maxX;
    if (s->scrollY > maxY) s->scrollY = maxY;
    if (!s->allowHorizontal) s->scrollX = 0.0f;
    if (!s->allowVertical)   s->scrollY = 0.0f;

    // Optional background. Honour the rectangle's drop shadow (drawn
    // first, behind the fill) so a scroll/panel can cast a shadow like a
    // standalone UIRectangle does.
    if (s->background) {
        UIRectangle* bg = (UIRectangle*)s->background;
        const float op = el->opacity;
        if (bg->hasShadow) {
            UIShadow sh = bg->shadow; sh.color.a *= op;
            DrawDropShadow(window->sdlRenderer, viewport, bg->radius, sh);
        }
        UIColor bgC     = bg->color;       bgC.a *= op;
        UIColor borderC = bg->borderColor; borderC.a *= op;
        DrawRoundedRectWithBorder(window->sdlRenderer, viewport,
                                  bgC, bg->radius,
                                  (int)bg->borderWidth, borderC);
    }

    // Flush pending batches before changing the clip rect - otherwise
    // they may be rendered against the wrong scissor.
    FlushRenderBatch(window->sdlRenderer);

    SDL_Rect clip = {
        (int)viewport.x, (int)viewport.y,
        (int)ceilf(viewport.w), (int)ceilf(viewport.h)
    };
    SDL_SetRenderClipRect(window->sdlRenderer, &clip);

    if (s->content) {
        s->content->x = viewport.x - s->scrollX;
        s->content->y = viewport.y - s->scrollY;
        EnsureWidgetSize(s->content, s->contentW, s->contentH);
        RenderSingleWidget(window, s->content);
    }

    FlushRenderBatch(window->sdlRenderer);
    SDL_SetRenderClipRect(window->sdlRenderer, NULL);
}

// Bounds-check that culls widgets entirely outside the current render
// target. The numbers we render with always have width/height set
// (containers ensure that via EnsureWidgetSize), so this is cheap.
static int WidgetCulled(UIWindow* window, UIWidget* el) {
    if (!el->width || !el->height) return 0;
    const float w = *el->width;
    const float h = *el->height;
    if (w <= 0 || h <= 0) return 1;
    int rw = 0, rh = 0;
    SDL_GetWindowSize(window->sdlWindow, &rw, &rh);
    if (el->x + w < 0 || el->y + h < 0 ||
        el->x > (float)rw || el->y > (float)rh) {
        return 1;
    }
    return 0;
}

// Wrapper that honours UIWidget_SetClipChildren: when the widget being
// rendered is anchored (directly or transitively) to an ancestor that
// has clipChildren = 1, we set the SDL render clip rect to that ancestor's
// bounds for the duration of the inner render. The previous clip is
// saved on a small local stack and restored after, so nested clips
// compose with stack discipline.
static void RenderSingleWidget(UIWindow* window, UIWidget* el) {
    if (!el || !window || !window->sdlRenderer) {
        RenderSingleWidget_Inner(window, el);
        return;
    }

    // Walk the alignment chain looking for the nearest ancestor with
    // clipChildren enabled. The chain is "widget -> H-target -> its
    // H-target -> ..."; we use H over V because in practice both
    // resolve to the same parent in this codebase, and inconsistencies
    // would just degrade to no-clip rather than mis-clip.
    UIWidget* clipAnc = NULL;
    if (el->alignment) {
        UIWidget* cur = UIAlignment_GetHTarget((UIAlignment*)el->alignment);
        // Bound the walk so a pathological cycle can't lock the renderer.
        for (int hops = 0; cur != NULL && hops < 32; hops++) {
            if (cur->clipChildren && cur->width && cur->height) {
                clipAnc = cur;
                break;
            }
            cur = cur->alignment
                ? UIAlignment_GetHTarget((UIAlignment*)cur->alignment)
                : NULL;
        }
    }

    if (!clipAnc) {
        RenderSingleWidget_Inner(window, el);
        return;
    }

    // Save the current clip so we can restore it after — supports
    // nesting (a clipping child of a clipping parent).
    const bool hadClip = SDL_RenderClipEnabled(window->sdlRenderer);
    SDL_Rect prevClip = {0, 0, 0, 0};
    if (hadClip) SDL_GetRenderClipRect(window->sdlRenderer, &prevClip);

    const SDL_Rect cr = {
        (int)clipAnc->x,
        (int)clipAnc->y,
        (int)*clipAnc->width,
        (int)*clipAnc->height
    };
    SDL_SetRenderClipRect(window->sdlRenderer, &cr);

    RenderSingleWidget_Inner(window, el);

    if (hadClip) SDL_SetRenderClipRect(window->sdlRenderer, &prevClip);
    else         SDL_SetRenderClipRect(window->sdlRenderer, NULL);
}

#include "window_widgets.inc"

static void RenderSingleWidget_Inner(UIWindow* window, UIWidget* el) {
    if (UI_UNLIKELY(!el || !el->visible || !el->data)) return;
    // Skip widgets fully outside the window. Big win for scrolls /
    // lists with off-screen content; cheap when the widget has bounds.
    if (UI_UNLIKELY(WidgetCulled(window, el))) return;

    if (el->alignment) UIAlignment_Align(el);
    UIWidgetBase* base = (UIWidgetBase*)el->data;

    // Opacity: skip fully transparent widgets to save GPU work, otherwise
    // propagate the value to per-shape alpha. Stored on UIWidget;
    // defaults to 1.0 from UIWidget_Create.
    float op = el->opacity;
    if (op <= 0.0f) return;
    if (op > 1.0f) op = 1.0f;

    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_GRID)) {
        RenderGrid(window, el, (UIGrid*)base);
        return;
    }
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_SCROLL)) {
        RenderScroll(window, el, (UIScroll*)base);
        return;
    }
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_STACK)) {
        RenderWidget_Stack(window, el, base, op);
        return;
    }
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_DIALOG)) {
        RenderWidget_Dialog(window, el, base, op);
        return;
    }
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_TABVIEW)) {
        RenderWidget_TabView(window, el, base, op);
        return;
    }
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_RECTANGLE)) {
        RenderWidget_Rectangle(window, el, base, op);
        return;
    }
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_TEXT)) {
        RenderWidget_Text(window, el, base, op);
        return;
    }
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_BUTTON)) {
        RenderWidget_Button(window, el, base, op);
        return;
    }
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_IMAGE)) {
        RenderWidget_Image(window, el, base, op);
        return;
    }
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_VIDEO)) {
        RenderWidget_Video(window, el, base, op);
        return;
    }
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_WEBVIEW)) {
        RenderWidget_Webview(window, el, base, op);
        return;
    }
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_FILE_DROP)) {
        RenderWidget_FileDrop(window, el, base, op);
        return;
    }
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_CHECKBOX)) {
        RenderWidget_Checkbox(window, el, base, op);
        return;
    }
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_SLIDER)) {
        RenderWidget_Slider(window, el, base, op);
        return;
    }
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_PROGRESS_BAR)) {
        RenderWidget_ProgressBar(window, el, base, op);
        return;
    }
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_TEXTFIELD)) {
        RenderWidget_TextField(window, el, base, op);
        return;
    }
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_TEXTAREA)) {
        RenderWidget_TextArea(window, el, base, op);
        return;
    }
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_SPINNER)) {
        RenderWidget_Spinner(window, el, base, op);
        return;
    }
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_RADIO)) {
        RenderWidget_Radio(window, el, base, op);
        return;
    }
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_SWITCH)) {
        RenderWidget_Switch(window, el, base, op);
        return;
    }
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_TOOLTIP)) {
        RenderWidget_Tooltip(window, el, base, op);
        return;
    }
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_MENU)) {
        RenderWidget_Menu(window, el, base, op);
        return;
    }
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_DROPDOWN)) {
        RenderWidget_Dropdown(window, el, base, op);
        return;
    }

}
