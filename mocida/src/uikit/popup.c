#include <uikit/popup.h>
#include <stdlib.h>
#include <string.h>

static int InsideRect(float px, float py, float x, float y, float w, float h) {
    return (px >= x && px < x + w && py >= y && py < y + h);
}

// =====================================================================
// UITooltip
// =====================================================================

UITooltip* UITooltip_Create(UIWidget* target, const char* text, float fontSize) {
    UITooltip* tt = (UITooltip*)calloc(1, sizeof(UITooltip));
    if (!tt) return NULL;
    tt->__widget_type = UI_WIDGET_TOOLTIP;
    tt->target     = target;
    tt->text       = text ? _strdup(text) : _strdup("");
    tt->fontSize   = fontSize > 0.0f ? fontSize : 12.0f;
    tt->bgColor    = (UIColor){ 15, 23, 42, 0.92f };
    tt->textColor  = (UIColor){ 248, 250, 252, 1.0f };
    tt->radius     = 6.0f;
    tt->paddingX   = 10.0f;
    tt->paddingY   = 6.0f;
    tt->delayMs    = 400;
    tt->__cachedTextLen = -1;
    return tt;
}

UITooltip* UITooltip_SetText(UITooltip* tt, const char* text) {
    if (!tt) return tt;
    free(tt->text);
    tt->text = text ? _strdup(text) : _strdup("");
    if (tt->__SDL_textTexture) {
        SDL_DestroyTexture(tt->__SDL_textTexture);
        tt->__SDL_textTexture = NULL;
    }
    tt->__cachedTextLen = -1;
    return tt;
}

UITooltip* UITooltip_SetFontFamily(UITooltip* tt, char* family) {
    if (!tt) return tt;
    free(tt->fontFamily);
    tt->fontFamily = family ? _strdup(family) : NULL;
    if (tt->__SDL_textTexture) {
        SDL_DestroyTexture(tt->__SDL_textTexture);
        tt->__SDL_textTexture = NULL;
    }
    return tt;
}

UITooltip* UITooltip_SetDelay(UITooltip* tt, Uint32 delayMs) {
    if (!tt) return tt;
    tt->delayMs = delayMs;
    return tt;
}

UITooltip* UITooltip_SetColors(UITooltip* tt, UIColor bg, UIColor text) {
    if (!tt) return tt;
    tt->bgColor = bg;
    tt->textColor = text;
    return tt;
}

void UITooltip_Destroy(UITooltip* tt) {
    if (!tt) return;
    free(tt->text);
    free(tt->fontFamily);
    if (tt->__SDL_textTexture) SDL_DestroyTexture(tt->__SDL_textTexture);
    free(tt);
}

// =====================================================================
// UIMenu
// =====================================================================

UIMenu* UIMenu_Create(float itemHeight, float itemWidth) {
    UIMenu* m = (UIMenu*)calloc(1, sizeof(UIMenu));
    if (!m) return NULL;
    m->__widget_type = UI_WIDGET_MENU;
    m->itemHeight  = itemHeight > 0.0f ? itemHeight : 32.0f;
    m->itemWidth   = itemWidth;
    m->radius      = 8.0f;
    m->bgColor     = (UIColor){ 255, 255, 255, 1.0f };
    m->itemHoverColor = (UIColor){ 241, 245, 249, 1.0f };
    m->textColor   = (UIColor){ 15, 23, 42, 1.0f };
    m->borderColor = (UIColor){ 203, 213, 225, 1.0f };
    m->borderWidth = 1.0f;
    m->paddingX    = 12.0f;
    m->paddingY    = 6.0f;
    m->fontSize    = 14.0f;
    m->hoverIndex  = -1;
    return m;
}

int UIMenu_AddItem(UIMenu* m, const char* label) {
    if (!m || !label) return -1;
    if (m->itemCount + 1 > m->itemCapacity) {
        const int cap = m->itemCapacity ? m->itemCapacity * 2 : 4;
        char** p = (char**)realloc(m->labels, (size_t)cap * sizeof(char*));
        if (!p) return -1;
        m->labels = p;
        m->itemCapacity = cap;
    }
    m->labels[m->itemCount] = _strdup(label);
    if (!m->labels[m->itemCount]) return -1;
    return m->itemCount++;
}

UIMenu* UIMenu_SetFont(UIMenu* m, char* family, float size) {
    if (!m) return m;
    free(m->fontFamily);
    m->fontFamily = family ? _strdup(family) : NULL;
    m->fontSize = size > 0.0f ? size : 14.0f;
    return m;
}

UIMenu* UIMenu_OnItem(UIMenu* m, UIMenuItemCallback cb, void* userdata) {
    if (!m) return m;
    m->onItem = cb;
    m->userdata = userdata;
    return m;
}

UIMenu* UIMenu_ShowAt(UIMenu* m, float x, float y) {
    if (!m) return m;
    m->anchorX = x;
    m->anchorY = y;
    m->visible = 1;
    return m;
}

UIMenu* UIMenu_Hide(UIMenu* m) {
    if (!m) return m;
    m->visible = 0;
    m->hoverIndex = -1;
    return m;
}

void UIMenu_Destroy(UIMenu* m) {
    if (!m) return;
    for (int i = 0; i < m->itemCount; i++) free(m->labels[i]);
    free(m->labels);
    free(m->fontFamily);
    free(m);
}

// =====================================================================
// UIDropdown
// =====================================================================

UIDropdown* UIDropdown_Create(void) {
    UIDropdown* d = (UIDropdown*)calloc(1, sizeof(UIDropdown));
    if (!d) return NULL;
    d->__widget_type   = UI_WIDGET_DROPDOWN;
    d->selectedIndex   = 0;
    d->fontSize        = 14.0f;
    d->bgColor         = (UIColor){ 255, 255, 255, 1.0f };
    d->textColor       = (UIColor){ 15, 23, 42, 1.0f };
    d->borderColor     = (UIColor){ 203, 213, 225, 1.0f };
    d->itemHoverColor  = (UIColor){ 226, 232, 240, 1.0f };
    d->borderWidth     = 1.0f;
    d->radius          = 6.0f;
    d->paddingX        = 10.0f;
    d->paddingY        = 8.0f;
    d->popupItemHeight = 32.0f;
    d->hoverIndex      = -1;
    return d;
}

int UIDropdown_AddOption(UIDropdown* d, const char* label) {
    if (!d || !label) return -1;
    if (d->itemCount + 1 > d->itemCapacity) {
        const int cap = d->itemCapacity ? d->itemCapacity * 2 : 4;
        char** p = (char**)realloc(d->labels, (size_t)cap * sizeof(char*));
        if (!p) return -1;
        d->labels = p;
        d->itemCapacity = cap;
    }
    d->labels[d->itemCount] = _strdup(label);
    if (!d->labels[d->itemCount]) return -1;
    return d->itemCount++;
}

UIDropdown* UIDropdown_SetSelected(UIDropdown* d, int index) {
    if (!d || index < 0 || index >= d->itemCount) return d;
    d->selectedIndex = index;
    return d;
}

int UIDropdown_GetSelected(UIDropdown* d) {
    return d ? d->selectedIndex : -1;
}

UIDropdown* UIDropdown_SetFont(UIDropdown* d, char* family, float size) {
    if (!d) return d;
    free(d->fontFamily);
    d->fontFamily = family ? _strdup(family) : NULL;
    d->fontSize = size > 0.0f ? size : 14.0f;
    return d;
}

UIDropdown* UIDropdown_OnChange(UIDropdown* d, UIDropdownChangedCallback cb, void* userdata) {
    if (!d) return d;
    d->onChange = cb;
    d->userdata = userdata;
    return d;
}

void UIDropdown_Destroy(UIDropdown* d) {
    if (!d) return;
    for (int i = 0; i < d->itemCount; i++) free(d->labels[i]);
    free(d->labels);
    free(d->fontFamily);
    free(d);
}

// =====================================================================
// Dispatchers
// =====================================================================

static UITooltip*  AsTooltip (UIWidget* w) {
    if (!w || !w->data) return NULL;
    UIWidgetBase* b = (UIWidgetBase*)w->data;
    return strcmp(b->__widget_type, UI_WIDGET_TOOLTIP)  == 0 ? (UITooltip*)b  : NULL;
}
static UIMenu*     AsMenu    (UIWidget* w) {
    if (!w || !w->data) return NULL;
    UIWidgetBase* b = (UIWidgetBase*)w->data;
    return strcmp(b->__widget_type, UI_WIDGET_MENU)     == 0 ? (UIMenu*)b     : NULL;
}
static UIDropdown* AsDropdown(UIWidget* w) {
    if (!w || !w->data) return NULL;
    UIWidgetBase* b = (UIWidgetBase*)w->data;
    return strcmp(b->__widget_type, UI_WIDGET_DROPDOWN) == 0 ? (UIDropdown*)b : NULL;
}

// Returns the menu's on-screen rect, accounting for `itemWidth` which
// may have been auto-computed at render time.
static void GetMenuRect(UIMenu* m, float* outX, float* outY, float* outW, float* outH) {
    const float w = (m->itemWidth > 0.0f) ? m->itemWidth : 180.0f;
    const float h = m->paddingY * 2.0f + m->itemCount * m->itemHeight;
    if (outX) *outX = m->anchorX;
    if (outY) *outY = m->anchorY;
    if (outW) *outW = w;
    if (outH) *outH = h;
}

void UIPopup_DispatchMouseMotion(UIChildren* children, float x, float y) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible) continue;
        UITooltip* tt = AsTooltip(w);
        if (tt) {
            const UIWidget* tgt = tt->target;
            const int inside = (tgt && tgt->width && tgt->height &&
                                InsideRect(x, y, tgt->x, tgt->y, *tgt->width, *tgt->height));
            if (inside) {
                if (!tt->_insideTarget) {
                    tt->_insideTarget = 1;
                    tt->_enterMs = SDL_GetTicks();
                }
                tt->_cursorX = x;
                tt->_cursorY = y;
                if (!tt->_visible &&
                    (SDL_GetTicks() - tt->_enterMs) >= (Uint64)tt->delayMs) {
                    tt->_visible = 1;
                }
            } else {
                tt->_insideTarget = 0;
                tt->_visible      = 0;
            }
            continue;
        }
        UIMenu* m = AsMenu(w);
        if (m && m->visible) {
            float mx, my, mw, mh;
            GetMenuRect(m, &mx, &my, &mw, &mh);
            if (InsideRect(x, y, mx, my, mw, mh)) {
                const float rel = y - (my + m->paddingY);
                int idx = (int)(rel / m->itemHeight);
                if (idx < 0 || idx >= m->itemCount) idx = -1;
                m->hoverIndex = idx;
            } else {
                m->hoverIndex = -1;
            }
            continue;
        }
        UIDropdown* d = AsDropdown(w);
        if (d) {
            const int insideBtn = (w->width && w->height &&
                InsideRect(x, y, w->x, w->y, *w->width, *w->height));
            d->hovered = insideBtn;
            if (d->open) {
                const float popY = w->y + (*w->height);
                const float popH = d->itemCount * d->popupItemHeight;
                if (w->width && InsideRect(x, y, w->x, popY, *w->width, popH)) {
                    int idx = (int)((y - popY) / d->popupItemHeight);
                    if (idx < 0 || idx >= d->itemCount) idx = -1;
                    d->hoverIndex = idx;
                } else {
                    d->hoverIndex = -1;
                }
            }
        }
    }
}

void UIPopup_DispatchMouseDown(UIChildren* children, float x, float y, int button) {
    if (!children || button != SDL_BUTTON_LEFT) return;

    // Pass 1: handle visible menus (they capture and steal the click).
    for (int i = children->count - 1; i >= 0; i--) {
        UIMenu* m = AsMenu(children->children[i]);
        if (!m || !m->visible) continue;
        float mx, my, mw, mh;
        GetMenuRect(m, &mx, &my, &mw, &mh);
        if (InsideRect(x, y, mx, my, mw, mh)) {
            const float rel = y - (my + m->paddingY);
            int idx = (int)(rel / m->itemHeight);
            if (idx >= 0 && idx < m->itemCount) {
                if (m->onItem) m->onItem(m, idx, m->labels[idx], m->userdata);
                UIMenu_Hide(m);
            }
            return;
        }
        // Click outside the visible menu hides it.
        UIMenu_Hide(m);
        return;
    }

    // Pass 2: dropdowns - open / close / select.
    for (int i = children->count - 1; i >= 0; i--) {
        UIDropdown* d = AsDropdown(children->children[i]);
        UIWidget* w = children->children[i];
        if (!d || !w || !w->width || !w->height) continue;

        const int insideBtn = InsideRect(x, y, w->x, w->y, *w->width, *w->height);
        if (insideBtn) {
            d->pressed = 1;
            d->open = !d->open;
            return;
        }

        if (d->open) {
            const float popY = w->y + (*w->height);
            const float popH = d->itemCount * d->popupItemHeight;
            if (InsideRect(x, y, w->x, popY, *w->width, popH)) {
                const int idx = (int)((y - popY) / d->popupItemHeight);
                if (idx >= 0 && idx < d->itemCount) {
                    d->selectedIndex = idx;
                    d->open = 0;
                    if (d->onChange) d->onChange(d, idx, d->labels[idx], d->userdata);
                }
                return;
            }
            // Click outside an open dropdown closes it.
            d->open = 0;
        }
    }
}

void UIPopup_DispatchMouseUp(UIChildren* children, float x, float y, int button) {
    (void)x; (void)y;
    if (!children || button != SDL_BUTTON_LEFT) return;
    for (int i = 0; i < children->count; i++) {
        UIDropdown* d = AsDropdown(children->children[i]);
        if (!d) continue;
        d->pressed = 0;
    }
}
