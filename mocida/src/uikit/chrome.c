#include <uikit/chrome.h>
#include <uikit/container.h>
#include <uikit/stack.h>
#include <uikit/glass.h>
#include <uikit/rect.h>
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static char* ChromeDup(const char* s) {
    if (!s) return NULL;
    return SDL_strdup(s);
}

static void ChromeItemClear(UIChromeItem* it) {
    if (!it) return;
    free(it->title);
    free(it->icon);
    free(it->symbol);
    free(it->value);
    if (it->iconTex) SDL_DestroyTexture(it->iconTex);
    if (it->titleTex) SDL_DestroyTexture(it->titleTex);
    if (it->symbolTex) SDL_DestroyTexture(it->symbolTex);
    memset(it, 0, sizeof(*it));
}

static int ChromeItemInit(UIChromeItem* it, const char* title,
                          const char* icon, const char* symbol, const char* value) {
    memset(it, 0, sizeof(*it));
    it->title = ChromeDup(title ? title : "");
    it->icon = ChromeDup(icon);
    it->symbol = ChromeDup(symbol);
    it->value = ChromeDup(value);
    it->selectedCached = -1;
    return it->title != NULL || (!title || !*title);
}

static UIChildren* ChromeContainerChildren(UIWidget* w) {
    if (!w || !w->data || !w->visible) return NULL;
    UIWidgetBase* b = (UIWidgetBase*)w->data;
    const char* t = b->__widget_type;
    if (!t) return NULL;
    if (!strcmp(t, UI_WIDGET_STACK)) return ((UIStack*)b)->items;
    if (!strcmp(t, UI_WIDGET_GLASS)) return ((UIGlass*)b)->items;
    if (!strcmp(t, UI_WIDGET_GRID)) return ((UIGrid*)b)->items;
    if (!strcmp(t, UI_WIDGET_RECTANGLE)) return (UIChildren*)((UIRectangle*)b)->children;
    if (!strcmp(t, UI_WIDGET_SCROLL)) {
        UIWidget* content = ((UIScroll*)b)->content;
        return content ? ChromeContainerChildren(content) : NULL;
    }
    return NULL;
}

static int InsideRect(float x, float y, float rx, float ry, float rw, float rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

/* ================================================================== */
/* SegmentedControl                                                    */
/* ================================================================== */

UISegmentedControl* UISegmentedControl_Create(void) {
    UISegmentedControl* sc = (UISegmentedControl*)calloc(1, sizeof(UISegmentedControl));
    if (!sc) return NULL;
    sc->__widget_type = UI_WIDGET_SEGMENTED;
    sc->selected = 0;
    sc->hovered = -1;
    sc->pressed = -1;
    sc->_phase = 0.0f;
    sc->animMs = 160;
    sc->radius = 10.0f;
    sc->padding = 3.0f;
    sc->iconSize = 16.0f;
    sc->gap = 6.0f;
    sc->cursor = UI_CURSOR_POINTER;
    sc->trackBg = (UIColor){ 28, 28, 32, 1.0f };
    sc->selectedBg = (UIColor){ 196, 120, 58, 1.0f };
    sc->textColor = (UIColor){ 168, 162, 152, 1.0f };
    sc->selectedTextColor = (UIColor){ 7, 7, 8, 1.0f };
    sc->borderColor = (UIColor){ 42, 42, 50, 1.0f };
    sc->borderWidth = 1.0f;
    sc->fontSize = 13.0f;
    return sc;
}

int UISegmentedControl_AddSegment(UISegmentedControl* sc,
                                  const char* title,
                                  const char* icon,
                                  const char* symbol) {
    if (!sc || sc->itemCount >= UI_CHROME_MAX_ITEMS) return -1;
    if (!ChromeItemInit(&sc->items[sc->itemCount], title, icon, symbol, NULL))
        return -1;
    return sc->itemCount++;
}

UISegmentedControl* UISegmentedControl_SetSelected(UISegmentedControl* sc, int index) {
    if (!sc) return sc;
    if (index < 0 || index >= sc->itemCount) return sc;
    if (sc->selected == index) return sc;
    sc->selected = index;
    if (sc->animMs <= 0) sc->_phase = (float)index;
    /* Do not fire onChange here — mouse/dispatch owns user callbacks.
     * Programmatic sets (MUI signal subscribe) must not re-enter handlers. */
    return sc;
}

int UISegmentedControl_Selected(const UISegmentedControl* sc) {
    return sc ? sc->selected : 0;
}

UISegmentedControl* UISegmentedControl_SetColors(UISegmentedControl* sc,
                                                 UIColor track, UIColor selected,
                                                 UIColor text, UIColor selectedText) {
    if (!sc) return sc;
    sc->trackBg = track;
    sc->selectedBg = selected;
    sc->textColor = text;
    sc->selectedTextColor = selectedText;
    return sc;
}

UISegmentedControl* UISegmentedControl_SetBorder(UISegmentedControl* sc,
                                                 UIColor color, float width) {
    if (!sc) return sc;
    sc->borderColor = color;
    sc->borderWidth = width < 0.0f ? 0.0f : width;
    return sc;
}

UISegmentedControl* UISegmentedControl_SetRadius(UISegmentedControl* sc, float radius) {
    if (!sc) return sc;
    sc->radius = radius < 0.0f ? 0.0f : radius;
    return sc;
}

UISegmentedControl* UISegmentedControl_SetAnimMs(UISegmentedControl* sc, int ms) {
    if (!sc) return sc;
    sc->animMs = ms < 0 ? 0 : ms;
    return sc;
}

UISegmentedControl* UISegmentedControl_SetFont(UISegmentedControl* sc,
                                               char* family, float size) {
    if (!sc) return sc;
    free(sc->fontFamily);
    sc->fontFamily = family; /* takes ownership */
    sc->fontSize = size > 0.0f ? size : 13.0f;
    return sc;
}

UISegmentedControl* UISegmentedControl_SetIconSize(UISegmentedControl* sc, float size) {
    if (!sc) return sc;
    sc->iconSize = size > 0.0f ? size : 16.0f;
    return sc;
}

UISegmentedControl* UISegmentedControl_SetCursor(UISegmentedControl* sc, UICursor cursor) {
    if (sc) sc->cursor = cursor;
    return sc;
}

UISegmentedControl* UISegmentedControl_OnChange(UISegmentedControl* sc,
                                                UISegmentedCallback cb, void* userdata) {
    if (!sc) return sc;
    sc->onChange = cb;
    sc->userdata = userdata;
    return sc;
}

void UISegmentedControl_Destroy(UISegmentedControl* sc) {
    if (!sc) return;
    for (int i = 0; i < sc->itemCount; i++) ChromeItemClear(&sc->items[i]);
    free(sc->fontFamily);
    free(sc);
}

static int SegmentIndexAt(UISegmentedControl* sc, UIWidget* w, float x, float y) {
    if (!sc || !w || !w->width || !w->height || sc->itemCount <= 0) return -1;
    if (!InsideRect(x, y, w->x, w->y, *w->width, *w->height)) return -1;
    float segW = *w->width / (float)sc->itemCount;
    int idx = (int)((x - w->x) / segW);
    if (idx < 0 || idx >= sc->itemCount) return -1;
    return idx;
}

void UISegmentedControl_DispatchMouseMotion(UIChildren* children, float x, float y) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible || !w->data) continue;
        UIChildren* kids = ChromeContainerChildren(w);
        if (kids) { UISegmentedControl_DispatchMouseMotion(kids, x, y); continue; }
        UIWidgetBase* base = (UIWidgetBase*)w->data;
        if (strcmp(base->__widget_type, UI_WIDGET_SEGMENTED) != 0) continue;
        UISegmentedControl* sc = (UISegmentedControl*)base;
        sc->hovered = SegmentIndexAt(sc, w, x, y);
    }
}

void UISegmentedControl_DispatchMouseDown(UIChildren* children, float x, float y, int button) {
    if (!children || button != SDL_BUTTON_LEFT) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible || !w->data || !w->width || !w->height) continue;
        UIChildren* kids = ChromeContainerChildren(w);
        if (kids) { UISegmentedControl_DispatchMouseDown(kids, x, y, button); continue; }
        UIWidgetBase* base = (UIWidgetBase*)w->data;
        if (strcmp(base->__widget_type, UI_WIDGET_SEGMENTED) != 0) continue;
        if (!UIWidget_EventOcclusionAllows(w)) continue;
        UISegmentedControl* sc = (UISegmentedControl*)base;
        int idx = SegmentIndexAt(sc, w, x, y);
        if (idx >= 0) { sc->pressed = idx; return; }
    }
}

void UISegmentedControl_DispatchMouseUp(UIChildren* children, float x, float y, int button) {
    if (!children || button != SDL_BUTTON_LEFT) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || !w->data) continue;
        UIChildren* kids = ChromeContainerChildren(w);
        if (kids) { UISegmentedControl_DispatchMouseUp(kids, x, y, button); continue; }
        UIWidgetBase* base = (UIWidgetBase*)w->data;
        if (strcmp(base->__widget_type, UI_WIDGET_SEGMENTED) != 0) continue;
        UISegmentedControl* sc = (UISegmentedControl*)base;
        int idx = SegmentIndexAt(sc, w, x, y);
        if (sc->pressed >= 0 && idx == sc->pressed && idx != sc->selected) {
            /* Set without double-firing via SetSelected when same */
            sc->selected = idx;
            if (sc->animMs <= 0) sc->_phase = (float)idx;
            if (sc->onChange) sc->onChange(sc, idx, sc->userdata);
        }
        sc->pressed = -1;
    }
}

/* ================================================================== */
/* TabBar                                                              */
/* ================================================================== */

UITabBar* UITabBar_Create(void) {
    UITabBar* tb = (UITabBar*)calloc(1, sizeof(UITabBar));
    if (!tb) return NULL;
    tb->__widget_type = UI_WIDGET_TABBAR;
    tb->selected = 0;
    tb->hovered = -1;
    tb->pressed = -1;
    tb->iconSize = 22.0f;
    tb->gap = 4.0f;
    tb->cursor = UI_CURSOR_POINTER;
    tb->barBg = (UIColor){ 14, 14, 16, 1.0f };
    tb->selectedTint = (UIColor){ 196, 120, 58, 1.0f };
    tb->unselectedTint = (UIColor){ 110, 110, 118, 1.0f };
    tb->borderColor = (UIColor){ 42, 42, 50, 1.0f };
    tb->borderWidth = 1.0f;
    tb->radius = 0.0f;
    tb->fontSize = 11.0f;
    return tb;
}

int UITabBar_AddItem(UITabBar* tb, const char* title, const char* icon, const char* symbol) {
    if (!tb || tb->itemCount >= UI_CHROME_MAX_ITEMS) return -1;
    if (!ChromeItemInit(&tb->items[tb->itemCount], title, icon, symbol, NULL))
        return -1;
    return tb->itemCount++;
}

UITabBar* UITabBar_SetSelected(UITabBar* tb, int index) {
    if (!tb || index < 0 || index >= tb->itemCount) return tb;
    if (tb->selected == index) return tb;
    tb->selected = index;
    /* Programmatic only — see SegmentedControl_SetSelected. */
    return tb;
}

int UITabBar_Selected(const UITabBar* tb) {
    return tb ? tb->selected : 0;
}

UITabBar* UITabBar_SetColors(UITabBar* tb, UIColor bar, UIColor selected, UIColor unselected) {
    if (!tb) return tb;
    tb->barBg = bar;
    tb->selectedTint = selected;
    tb->unselectedTint = unselected;
    return tb;
}

UITabBar* UITabBar_SetBorder(UITabBar* tb, UIColor color, float width) {
    if (!tb) return tb;
    tb->borderColor = color;
    tb->borderWidth = width < 0.0f ? 0.0f : width;
    return tb;
}

UITabBar* UITabBar_SetRadius(UITabBar* tb, float radius) {
    if (!tb) return tb;
    tb->radius = radius < 0.0f ? 0.0f : radius;
    return tb;
}

UITabBar* UITabBar_SetFont(UITabBar* tb, char* family, float size) {
    if (!tb) return tb;
    free(tb->fontFamily);
    tb->fontFamily = family;
    tb->fontSize = size > 0.0f ? size : 11.0f;
    return tb;
}

UITabBar* UITabBar_SetIconSize(UITabBar* tb, float size) {
    if (!tb) return tb;
    tb->iconSize = size > 0.0f ? size : 22.0f;
    return tb;
}

UITabBar* UITabBar_SetCursor(UITabBar* tb, UICursor cursor) {
    if (tb) tb->cursor = cursor;
    return tb;
}

UITabBar* UITabBar_OnChange(UITabBar* tb, UITabBarCallback cb, void* userdata) {
    if (!tb) return tb;
    tb->onChange = cb;
    tb->userdata = userdata;
    return tb;
}

void UITabBar_Destroy(UITabBar* tb) {
    if (!tb) return;
    for (int i = 0; i < tb->itemCount; i++) ChromeItemClear(&tb->items[i]);
    free(tb->fontFamily);
    free(tb);
}

static int TabBarIndexAt(UITabBar* tb, UIWidget* w, float x, float y) {
    if (!tb || !w || !w->width || !w->height || tb->itemCount <= 0) return -1;
    if (!InsideRect(x, y, w->x, w->y, *w->width, *w->height)) return -1;
    float segW = *w->width / (float)tb->itemCount;
    int idx = (int)((x - w->x) / segW);
    if (idx < 0 || idx >= tb->itemCount) return -1;
    return idx;
}

void UITabBar_DispatchMouseMotion(UIChildren* children, float x, float y) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible || !w->data) continue;
        UIChildren* kids = ChromeContainerChildren(w);
        if (kids) { UITabBar_DispatchMouseMotion(kids, x, y); continue; }
        UIWidgetBase* base = (UIWidgetBase*)w->data;
        if (strcmp(base->__widget_type, UI_WIDGET_TABBAR) != 0) continue;
        ((UITabBar*)base)->hovered = TabBarIndexAt((UITabBar*)base, w, x, y);
    }
}

void UITabBar_DispatchMouseDown(UIChildren* children, float x, float y, int button) {
    if (!children || button != SDL_BUTTON_LEFT) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible || !w->data || !w->width || !w->height) continue;
        UIChildren* kids = ChromeContainerChildren(w);
        if (kids) { UITabBar_DispatchMouseDown(kids, x, y, button); continue; }
        UIWidgetBase* base = (UIWidgetBase*)w->data;
        if (strcmp(base->__widget_type, UI_WIDGET_TABBAR) != 0) continue;
        if (!UIWidget_EventOcclusionAllows(w)) continue;
        UITabBar* tb = (UITabBar*)base;
        int idx = TabBarIndexAt(tb, w, x, y);
        if (idx >= 0) { tb->pressed = idx; return; }
    }
}

void UITabBar_DispatchMouseUp(UIChildren* children, float x, float y, int button) {
    if (!children || button != SDL_BUTTON_LEFT) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || !w->data) continue;
        UIChildren* kids = ChromeContainerChildren(w);
        if (kids) { UITabBar_DispatchMouseUp(kids, x, y, button); continue; }
        UIWidgetBase* base = (UIWidgetBase*)w->data;
        if (strcmp(base->__widget_type, UI_WIDGET_TABBAR) != 0) continue;
        UITabBar* tb = (UITabBar*)base;
        int idx = TabBarIndexAt(tb, w, x, y);
        if (tb->pressed >= 0 && idx == tb->pressed && idx != tb->selected) {
            tb->selected = idx;
            if (tb->onChange) tb->onChange(tb, idx, tb->userdata);
        }
        tb->pressed = -1;
    }
}

/* ================================================================== */
/* NavBar                                                              */
/* ================================================================== */

UINavBar* UINavBar_Create(void) {
    UINavBar* nb = (UINavBar*)calloc(1, sizeof(UINavBar));
    if (!nb) return NULL;
    nb->__widget_type = UI_WIDGET_NAVBAR;
    nb->selected = 0;
    nb->hovered = -1;
    nb->pressed = -1;
    nb->menuHovered = 0;
    nb->menuPressed = 0;
    nb->menuVisible = 0;
    nb->menuOpen = 0;
    nb->iconSize = 26.0f;
    nb->brandGap = 10.0f;
    nb->cursor = UI_CURSOR_POINTER;
    nb->barBg = (UIColor){ 10, 10, 12, 1.0f };
    nb->titleColor = (UIColor){ 244, 239, 230, 1.0f };
    nb->linkColor = (UIColor){ 200, 194, 184, 1.0f };
    nb->linkActiveColor = (UIColor){ 244, 239, 230, 1.0f };
    nb->linkHoverBg = (UIColor){ 255, 255, 255, 0.06f };
    nb->menuBg = (UIColor){ 255, 255, 255, 0.06f };
    nb->menuBgOpen = (UIColor){ 196, 120, 58, 0.2f };
    nb->borderColor = (UIColor){ 28, 28, 31, 1.0f };
    nb->borderWidth = 1.0f;
    nb->fontSize = 14.0f;
    nb->brandFontSize = 12.0f;
    return nb;
}

UINavBar* UINavBar_SetBrand(UINavBar* nb, const char* title, const char* logo) {
    if (!nb) return nb;
    free(nb->brandTitle);
    free(nb->brandLogo);
    if (nb->logoTex) { SDL_DestroyTexture(nb->logoTex); nb->logoTex = NULL; }
    if (nb->brandTex) { SDL_DestroyTexture(nb->brandTex); nb->brandTex = NULL; }
    nb->brandLenCached = -1;
    nb->brandTitle = ChromeDup(title);
    nb->brandLogo = ChromeDup(logo);
    return nb;
}

int UINavBar_AddItem(UINavBar* nb, const char* title, const char* value) {
    if (!nb || nb->itemCount >= UI_CHROME_MAX_ITEMS) return -1;
    if (!ChromeItemInit(&nb->items[nb->itemCount], title, NULL, NULL, value))
        return -1;
    return nb->itemCount++;
}

UINavBar* UINavBar_SetSelected(UINavBar* nb, int index) {
    if (!nb) return nb;
    if (index < -1) return nb;
    if (index >= nb->itemCount) return nb;
    if (nb->selected == index) return nb;
    nb->selected = index;
    /* Programmatic only — see SegmentedControl_SetSelected. */
    return nb;
}

int UINavBar_Selected(const UINavBar* nb) {
    return nb ? nb->selected : 0;
}

UINavBar* UINavBar_SetMenuVisible(UINavBar* nb, int visible) {
    if (nb) nb->menuVisible = visible ? 1 : 0;
    return nb;
}

UINavBar* UINavBar_SetMenuOpen(UINavBar* nb, int open) {
    if (nb) nb->menuOpen = open ? 1 : 0;
    return nb;
}

UINavBar* UINavBar_SetColors(UINavBar* nb, UIColor bar, UIColor title,
                             UIColor link, UIColor linkActive) {
    if (!nb) return nb;
    nb->barBg = bar;
    nb->titleColor = title;
    nb->linkColor = link;
    nb->linkActiveColor = linkActive;
    return nb;
}

UINavBar* UINavBar_SetBorder(UINavBar* nb, UIColor color, float width) {
    if (!nb) return nb;
    nb->borderColor = color;
    nb->borderWidth = width < 0.0f ? 0.0f : width;
    return nb;
}

UINavBar* UINavBar_SetFont(UINavBar* nb, char* family, float size, float brandSize) {
    if (!nb) return nb;
    free(nb->fontFamily);
    nb->fontFamily = family;
    nb->fontSize = size > 0.0f ? size : 14.0f;
    nb->brandFontSize = brandSize > 0.0f ? brandSize : 12.0f;
    return nb;
}

UINavBar* UINavBar_SetCursor(UINavBar* nb, UICursor cursor) {
    if (nb) nb->cursor = cursor;
    return nb;
}

UINavBar* UINavBar_OnChange(UINavBar* nb, UINavBarCallback cb, void* userdata) {
    if (!nb) return nb;
    nb->onChange = cb;
    nb->userdata = userdata;
    return nb;
}

UINavBar* UINavBar_OnBrandClick(UINavBar* nb, UINavBarSimpleCallback cb, void* userdata) {
    if (!nb) return nb;
    nb->onBrandClick = cb;
    nb->brandUserdata = userdata;
    return nb;
}

UINavBar* UINavBar_OnMenuClick(UINavBar* nb, UINavBarSimpleCallback cb, void* userdata) {
    if (!nb) return nb;
    nb->onMenuClick = cb;
    nb->menuUserdata = userdata;
    return nb;
}

void UINavBar_Destroy(UINavBar* nb) {
    if (!nb) return;
    for (int i = 0; i < nb->itemCount; i++) ChromeItemClear(&nb->items[i]);
    free(nb->brandTitle);
    free(nb->brandLogo);
    free(nb->fontFamily);
    if (nb->logoTex) SDL_DestroyTexture(nb->logoTex);
    if (nb->brandTex) SDL_DestroyTexture(nb->brandTex);
    free(nb);
}

/* Layout helpers used by dispatch + renderer (logical coords). */
static float NavBarBrandWidth(const UINavBar* nb) {
    if (!nb) return 140.0f;
    float w = 16.0f;
    if (nb->brandLogo && nb->brandLogo[0]) w += nb->iconSize + nb->brandGap;
    if (nb->brandTitle && nb->brandTitle[0]) w += (float)strlen(nb->brandTitle) * nb->brandFontSize * 0.62f + 8.0f;
    if (w < 120.0f) w = 120.0f;
    return w;
}

static float NavBarMenuWidth(const UINavBar* nb) {
    return (nb && nb->menuVisible) ? 44.0f : 0.0f;
}

/* Compact (= hamburger, hide center links) when the author asked for a menu
 * OR the bar is too narrow for brand + centered links without overlapping. */
static int NavBarCompact(const UINavBar* nb, float W) {
    if (!nb) return 0;
    if (nb->menuVisible) return 1;
    if (nb->itemCount <= 0) return 0;
    float need = NavBarBrandWidth(nb) + 32.0f + (float)nb->itemCount * 72.0f + 44.0f;
    return W < need;
}

static int NavBarHit(UINavBar* nb, UIWidget* w, float x, float y,
                     int* outItem, int* outBrand, int* outMenu) {
    if (outItem) *outItem = -1;
    if (outBrand) *outBrand = 0;
    if (outMenu) *outMenu = 0;
    if (!nb || !w || !w->width || !w->height) return 0;
    if (!InsideRect(x, y, w->x, w->y, *w->width, *w->height)) return 0;

    float brandW = NavBarBrandWidth(nb);
    float pad = 16.0f;
    int compact = NavBarCompact(nb, *w->width);
    float menuW = compact ? 44.0f : 0.0f;

    if (x >= w->x + pad && x < w->x + pad + brandW) {
        if (outBrand) *outBrand = 1;
        return 1;
    }
    if (menuW > 0.0f && x >= w->x + *w->width - pad - menuW && x < w->x + *w->width - pad) {
        if (outMenu) *outMenu = 1;
        return 1;
    }

    /* Center links — hide in compact / hamburger mode. */
    if (compact || nb->itemCount <= 0) return 1;
    float linkAreaW = (float)nb->itemCount * 72.0f;
    float startX = w->x + (*w->width - linkAreaW) * 0.5f;
    if (x >= startX && x < startX + linkAreaW) {
        int idx = (int)((x - startX) / 72.0f);
        if (idx >= 0 && idx < nb->itemCount && outItem) *outItem = idx;
    }
    return 1;
}

void UINavBar_DispatchMouseMotion(UIChildren* children, float x, float y) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible || !w->data) continue;
        UIChildren* kids = ChromeContainerChildren(w);
        if (kids) { UINavBar_DispatchMouseMotion(kids, x, y); continue; }
        UIWidgetBase* base = (UIWidgetBase*)w->data;
        if (strcmp(base->__widget_type, UI_WIDGET_NAVBAR) != 0) continue;
        UINavBar* nb = (UINavBar*)base;
        int item = -1, brand = 0, menu = 0;
        NavBarHit(nb, w, x, y, &item, &brand, &menu);
        nb->hovered = item;
        nb->menuHovered = menu;
        (void)brand;
    }
}

void UINavBar_DispatchMouseDown(UIChildren* children, float x, float y, int button) {
    if (!children || button != SDL_BUTTON_LEFT) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible || !w->data || !w->width || !w->height) continue;
        UIChildren* kids = ChromeContainerChildren(w);
        if (kids) { UINavBar_DispatchMouseDown(kids, x, y, button); continue; }
        UIWidgetBase* base = (UIWidgetBase*)w->data;
        if (strcmp(base->__widget_type, UI_WIDGET_NAVBAR) != 0) continue;
        if (!UIWidget_EventOcclusionAllows(w)) continue;
        UINavBar* nb = (UINavBar*)base;
        int item = -1, brand = 0, menu = 0;
        if (!NavBarHit(nb, w, x, y, &item, &brand, &menu)) continue;
        /* Fire menu on press (not release) so a tiny finger/pan slop — or a
         * ContentPan cancel that skips MouseUp — can't swallow the hamburger. */
        if (menu) {
            nb->menuPressed = 1;
            if (nb->onMenuClick) nb->onMenuClick(nb, nb->menuUserdata);
            return;
        }
        if (brand) { nb->pressed = -2; return; } /* -2 = brand */
        if (item >= 0) { nb->pressed = item; return; }
    }
}

void UINavBar_DispatchMouseUp(UIChildren* children, float x, float y, int button) {
    if (!children || button != SDL_BUTTON_LEFT) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || !w->data) continue;
        UIChildren* kids = ChromeContainerChildren(w);
        if (kids) { UINavBar_DispatchMouseUp(kids, x, y, button); continue; }
        UIWidgetBase* base = (UIWidgetBase*)w->data;
        if (strcmp(base->__widget_type, UI_WIDGET_NAVBAR) != 0) continue;
        UINavBar* nb = (UINavBar*)base;
        int item = -1, brand = 0, menu = 0;
        NavBarHit(nb, w, x, y, &item, &brand, &menu);
        /* Menu already fired on MouseDown. */
        if (nb->menuPressed) {
            nb->menuPressed = 0;
            nb->pressed = -1;
            continue;
        }
        if (nb->pressed == -2 && brand) {
            if (nb->onBrandClick) nb->onBrandClick(nb, nb->brandUserdata);
        } else if (nb->pressed >= 0 && item == nb->pressed) {
            if (item != nb->selected) {
                nb->selected = item;
                if (nb->onChange) nb->onChange(nb, item, nb->userdata);
            } else if (nb->onChange) {
                nb->onChange(nb, item, nb->userdata);
            }
        }
        nb->pressed = -1;
        nb->menuPressed = 0;
    }
}
