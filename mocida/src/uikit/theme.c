#include <uikit/theme.h>

static UITheme g_theme;
static int     g_themeInited = 0;

static void EnsureInited(void) {
    if (g_themeInited) return;
    UITheme_FillLight(&g_theme);
    g_themeInited = 1;
}

void UITheme_FillLight(UITheme* out) {
    if (!out) return;
    out->primary    = (UIColor){ 59, 130, 246, 1.0f };
    out->onPrimary  = (UIColor){ 255, 255, 255, 1.0f };
    out->surface    = (UIColor){ 255, 255, 255, 1.0f };
    out->onSurface  = (UIColor){ 15, 23, 42, 1.0f };
    out->background = (UIColor){ 248, 250, 252, 1.0f };
    out->border     = (UIColor){ 203, 213, 225, 1.0f };
    out->success    = (UIColor){ 34, 197, 94, 1.0f };
    out->warning    = (UIColor){ 250, 204, 21, 1.0f };
    out->danger     = (UIColor){ 239, 68, 68, 1.0f };
    out->disabled   = (UIColor){ 148, 163, 184, 1.0f };
    out->radius     = 8.0f;
    out->spacing    = 8.0f;
    out->fontSizeSmall  = 12.0f;
    out->fontSizeMedium = 16.0f;
    out->fontSizeLarge  = 24.0f;
}

void UITheme_FillDark(UITheme* out) {
    if (!out) return;
    out->primary    = (UIColor){ 96, 165, 250, 1.0f };
    out->onPrimary  = (UIColor){ 15, 23, 42, 1.0f };
    out->surface    = (UIColor){ 30, 41, 59, 1.0f };
    out->onSurface  = (UIColor){ 226, 232, 240, 1.0f };
    out->background = (UIColor){ 15, 23, 42, 1.0f };
    out->border     = (UIColor){ 51, 65, 85, 1.0f };
    out->success    = (UIColor){ 74, 222, 128, 1.0f };
    out->warning    = (UIColor){ 253, 224, 71, 1.0f };
    out->danger     = (UIColor){ 248, 113, 113, 1.0f };
    out->disabled   = (UIColor){ 100, 116, 139, 1.0f };
    out->radius     = 8.0f;
    out->spacing    = 8.0f;
    out->fontSizeSmall  = 12.0f;
    out->fontSizeMedium = 16.0f;
    out->fontSizeLarge  = 24.0f;
}

const UITheme* UITheme_GetGlobal(void) {
    EnsureInited();
    return &g_theme;
}

void UITheme_SetGlobal(const UITheme* t) {
    if (!t) return;
    g_theme = *t;
    g_themeInited = 1;
}
