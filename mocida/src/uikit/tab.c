#include <uikit/tab.h>
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

UITabView* UITabView_Create(float tabHeight) {
    UITabView* tv = (UITabView*)calloc(1, sizeof(UITabView));
    if (!tv) return NULL;
    tv->__widget_type = UI_WIDGET_TABVIEW;
    tv->tabHeight   = tabHeight > 0.0f ? tabHeight : 36.0f;
    tv->fontSize    = 14.0f;
    tv->radius      = 8.0f;
    tv->tabBg       = (UIColor){ 226, 232, 240, 1.0f };
    tv->tabBgActive = (UIColor){ 255, 255, 255, 1.0f };
    tv->tabText     = (UIColor){ 71, 85, 105, 1.0f };
    tv->tabTextActive = (UIColor){ 15, 23, 42, 1.0f };
    tv->panelBg     = (UIColor){ 255, 255, 255, 1.0f };
    tv->panels      = UIChildren_Create(8);
    if (!tv->panels) { free(tv); return NULL; }
    return tv;
}

UITabView* UITabView_SetFont(UITabView* tv, char* family, float size) {
    if (!tv) return tv;
    free(tv->fontFamily);
    tv->fontFamily = family ? _strdup(family) : NULL;
    tv->fontSize = size > 0.0f ? size : 14.0f;
    return tv;
}

int UITabView_AddTab(UITabView* tv, const char* title, UIWidget* content) {
    if (!tv || !title) return -1;
    if (tv->tabCount + 1 > tv->tabCapacity) {
        int newCap = tv->tabCapacity ? tv->tabCapacity * 2 : 4;
        char** p = (char**)realloc(tv->titles, (size_t)newCap * sizeof(char*));
        if (!p) return -1;
        tv->titles      = p;
        tv->tabCapacity = newCap;
    }
    tv->titles[tv->tabCount] = _strdup(title);
    if (!tv->titles[tv->tabCount]) return -1;
    if (content) UIChildren_Add(tv->panels, content);
    return tv->tabCount++;
}

UITabView* UITabView_SetActive(UITabView* tv, int index) {
    if (!tv || index < 0 || index >= tv->tabCount) return tv;
    if (tv->activeIndex == index) return tv;
    tv->activeIndex = index;
    if (tv->onTabChanged) tv->onTabChanged(tv, index, tv->userdata);
    return tv;
}

UITabView* UITabView_OnChange(UITabView* tv, UITabChangedCallback cb, void* userdata) {
    if (!tv) return tv;
    tv->onTabChanged = cb;
    tv->userdata = userdata;
    return tv;
}

void UITabView_Destroy(UITabView* tv) {
    if (!tv) return;
    for (int i = 0; i < tv->tabCount; i++) free(tv->titles[i]);
    free(tv->titles);
    // Per-tab cached label textures (built lazily by the renderer).
    for (int i = 0; i < tv->__titleCacheN; i++) {
        if (tv->__titleTex && tv->__titleTex[i]) SDL_DestroyTexture(tv->__titleTex[i]);
    }
    free(tv->__titleTex);
    free(tv->__titleActiveCached);
    free(tv->__titleStrCached);
    if (tv->panels) UIChildren_Destroy(tv->panels);
    free(tv->fontFamily);
    free(tv);
}

void UITabView_DispatchMouseDown(UIChildren* children, float x, float y, int button) {
    if (!children || button != SDL_BUTTON_LEFT) return;
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible || !w->data || !w->width || !w->height) continue;
        UIWidgetBase* b = (UIWidgetBase*)w->data;
        if (strcmp(b->__widget_type, UI_WIDGET_TABVIEW) != 0) continue;

        UITabView* tv = (UITabView*)b;
        if (tv->tabCount <= 0) return;

        // Header strip occupies the top tabHeight pixels.
        if (y < w->y || y > w->y + tv->tabHeight) return;
        if (x < w->x || x > w->x + *w->width)     return;

        const float tabW = *w->width / (float)tv->tabCount;
        const int idx = (int)((x - w->x) / tabW);
        if (idx >= 0 && idx < tv->tabCount) {
            UITabView_SetActive(tv, idx);
        }
        return;
    }
}
