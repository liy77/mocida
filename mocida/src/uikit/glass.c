#include <uikit/glass.h>
#include <string.h>
#include <stdlib.h>

// =====================================================================
// Material helpers (pure logic — same on every platform)
// =====================================================================

UIBackdropMaterial UIBackdrop_FromString(const char* effect) {
    if (!effect || !effect[0]) return UI_BACKDROP_AUTO;

    // Case-insensitive compare against a known set. Hyphens and underscores
    // are treated the same so "mica-alt" and "mica_alt" both work.
    char buf[32];
    size_t n = 0;
    for (const char* p = effect; *p && n < sizeof(buf) - 1; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c == '_') c = '-';
        buf[n++] = c;
    }
    buf[n] = '\0';

    if (!strcmp(buf, "auto"))             return UI_BACKDROP_AUTO;
    if (!strcmp(buf, "none") ||
        !strcmp(buf, "off"))              return UI_BACKDROP_NONE;
    if (!strcmp(buf, "mica"))             return UI_BACKDROP_MICA;
    if (!strcmp(buf, "mica-alt"))         return UI_BACKDROP_MICA_ALT;
    if (!strcmp(buf, "acrylic"))          return UI_BACKDROP_ACRYLIC;
    if (!strcmp(buf, "acrylic-legacy"))   return UI_BACKDROP_ACRYLIC_LEGACY;
    if (!strcmp(buf, "liquid") ||
        !strcmp(buf, "liquid-glass"))     return UI_BACKDROP_LIQUID_GLASS;
    if (!strcmp(buf, "vibrancy-sidebar")) return UI_BACKDROP_VIBRANCY_SIDEBAR;
    if (!strcmp(buf, "vibrancy-header"))  return UI_BACKDROP_VIBRANCY_HEADER;
    if (!strcmp(buf, "vibrancy-menu"))    return UI_BACKDROP_VIBRANCY_MENU;
    if (!strcmp(buf, "vibrancy-popover")) return UI_BACKDROP_VIBRANCY_POPOVER;
    if (!strcmp(buf, "vibrancy-hud"))     return UI_BACKDROP_VIBRANCY_HUD;
    if (!strcmp(buf, "kde-window"))       return UI_BACKDROP_KDE_BLUR_WINDOW;
    if (!strcmp(buf, "kde-region") ||
        !strcmp(buf, "kde-blur"))         return UI_BACKDROP_KDE_BLUR_REGION;

    return UI_BACKDROP_AUTO;
}

uint32_t UIBackdropMaterial_SupportedProps(UIBackdropMaterial m) {
    switch (m) {
        case UI_BACKDROP_MICA:
        case UI_BACKDROP_MICA_ALT:
            return UI_GLASS_PROP_TINT;

        case UI_BACKDROP_ACRYLIC:
        case UI_BACKDROP_ACRYLIC_LEGACY:
            return UI_GLASS_PROP_TINT | UI_GLASS_PROP_NOISE;

        case UI_BACKDROP_LIQUID_GLASS:
            return UI_GLASS_PROP_TINT | UI_GLASS_PROP_REFRACTION;

        case UI_BACKDROP_VIBRANCY_SIDEBAR:
        case UI_BACKDROP_VIBRANCY_HEADER:
        case UI_BACKDROP_VIBRANCY_MENU:
        case UI_BACKDROP_VIBRANCY_POPOVER:
            return UI_GLASS_PROP_STATE;

        case UI_BACKDROP_VIBRANCY_HUD:
            return UI_GLASS_PROP_STATE | UI_GLASS_PROP_NOISE;

        case UI_BACKDROP_KDE_BLUR_WINDOW:
        case UI_BACKDROP_KDE_BLUR_REGION:
            return UI_GLASS_PROP_TINT | UI_GLASS_PROP_BLUR;

        case UI_BACKDROP_NONE:
        case UI_BACKDROP_AUTO:
        default:
            // The in-app fallback understands everything it can paint.
            return UI_GLASS_PROP_TINT | UI_GLASS_PROP_BLUR | UI_GLASS_PROP_NOISE;
    }
}

int UIBackdropMaterial_IsWindowWide(UIBackdropMaterial m) {
    return m == UI_BACKDROP_MICA
        || m == UI_BACKDROP_MICA_ALT
        || m == UI_BACKDROP_KDE_BLUR_WINDOW;
}

// =====================================================================
// UIGlass widget (mirrors UIStack, plus the glass styling fields)
// =====================================================================

UIGlass* UIGlass_Create(UIBackdropMaterial material) {
    UIGlass* g = (UIGlass*)calloc(1, sizeof(UIGlass));
    if (!g) return NULL;
    g->__widget_type = UI_WIDGET_GLASS;
    g->material      = material;
    g->radius        = 0.0f;
    g->tint          = (UIColor){ 255, 255, 255, 1.0f };
    g->tintOpacity   = 0.5f;
    g->thickness     = UI_GLASS_REGULAR;
    g->refraction    = 0.0f;
    g->blurPx        = 16.0f;
    g->noise         = 0.0f;
    g->vibrancyState = UI_VIBRANCY_ACTIVE;
    g->orientation   = 0; // vertical
    g->spacing       = 8.0f;
    g->items         = UIChildren_Create(16);
    if (!g->items) { free(g); return NULL; }
    return g;
}

UIGlass* UIGlass_SetRadius(UIGlass* g, float radius) {
    if (g) g->radius = radius;
    return g;
}

UIGlass* UIGlass_SetTint(UIGlass* g, UIColor tint) {
    if (g) g->tint = tint;
    return g;
}

UIGlass* UIGlass_SetTintOpacity(UIGlass* g, float opacity) {
    if (g) g->tintOpacity = opacity;
    return g;
}

UIGlass* UIGlass_SetThickness(UIGlass* g, UIGlassThickness thickness) {
    if (g) g->thickness = thickness;
    return g;
}

UIGlass* UIGlass_SetBlur(UIGlass* g, float px) {
    if (g) g->blurPx = px;
    return g;
}

UIGlass* UIGlass_SetRefraction(UIGlass* g, float refraction) {
    if (g) g->refraction = refraction;
    return g;
}

UIGlass* UIGlass_SetNoise(UIGlass* g, float noise) {
    if (g) g->noise = noise;
    return g;
}

UIGlass* UIGlass_SetVibrancyState(UIGlass* g, UIVibrancyState state) {
    if (g) g->vibrancyState = state;
    return g;
}

UIGlass* UIGlass_SetOrientation(UIGlass* g, int horizontal) {
    if (g) g->orientation = horizontal ? 1 : 0;
    return g;
}

UIGlass* UIGlass_SetSpacing(UIGlass* g, float spacing) {
    if (g) g->spacing = spacing;
    return g;
}

UIGlass* UIGlass_SetAlign(UIGlass* g, int align) {
    if (g) g->align = align;
    return g;
}

UIGlass* UIGlass_SetJustify(UIGlass* g, int justify) {
    if (g) g->justify = justify;
    return g;
}

UIGlass* UIGlass_SetPadding(UIGlass* g, float l, float t, float r, float b) {
    if (!g) return g;
    g->paddingLeft   = l;
    g->paddingTop    = t;
    g->paddingRight  = r;
    g->paddingBottom = b;
    return g;
}

UIGlass* UIGlass_SetFreeLayout(UIGlass* g, int enabled) {
    if (g) g->freeLayout = enabled ? 1 : 0;
    return g;
}

int UIGlass_AddItem(UIGlass* g, UIWidget* item) {
    if (!g || !item) return 0;
    return UIChildren_Add(g->items, item);
}

void UIGlass_GetContentSize(UIGlass* g, float* outW, float* outH) {
    float w = 0.0f, h = 0.0f;
    if (g && g->items) {
        for (int i = 0; i < g->items->count; i++) {
            UIWidget* it = g->items->children[i];
            if (!it || !it->visible) continue;
            const float iw = it->width  ? *it->width  : 0.0f;
            const float ih = it->height ? *it->height : 0.0f;
            if (g->orientation == 1) {        // horizontal
                w += iw + (w > 0 ? g->spacing : 0.0f);
                if (ih > h) h = ih;
            } else {                          // vertical
                h += ih + (h > 0 ? g->spacing : 0.0f);
                if (iw > w) w = iw;
            }
        }
        w += g->paddingLeft + g->paddingRight;
        h += g->paddingTop  + g->paddingBottom;
    }
    if (outW) *outW = w;
    if (outH) *outH = h;
}

void UIGlass_Destroy(UIGlass* g) {
    if (!g) return;
    if (g->items) UIChildren_Destroy(g->items);
    free(g);
}
