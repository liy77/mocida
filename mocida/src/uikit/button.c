#include <uikit/button.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------

static int ClampI(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Linear blend of two colors. t=0 -> a, t=1 -> b.
static UIColor BlendColors(UIColor a, UIColor b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    UIColor out;
    out.r = ClampI((int)(a.r * (1.0f - t) + b.r * t + 0.5f), 0, 255);
    out.g = ClampI((int)(a.g * (1.0f - t) + b.g * t + 0.5f), 0, 255);
    out.b = ClampI((int)(a.b * (1.0f - t) + b.b * t + 0.5f), 0, 255);
    out.a = a.a * (1.0f - t) + b.a * t;
    return out;
}

// Hit test against the widget's bounding box. Requires width/height to
// be set (buttons don't use dynamic sizing - always created via widgcs).
static int InsideWidget(const UIWidget* w, float x, float y) {
    if (!w || !w->visible || !w->width || !w->height) return 0;
    const float ww = *w->width;
    const float hh = *w->height;
    return (x >= w->x && x < w->x + ww &&
            y >= w->y && y < w->y + hh);
}

// ---------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------

UIButton* UIButton_Create(const char* text, float fontSize) {
    UIButton* btn = (UIButton*)calloc(1, sizeof(UIButton));
    if (!btn) return NULL;

    btn->__widget_type = UI_WIDGET_BUTTON;
    btn->state         = UI_BUTTON_STATE_NORMAL;
    btn->enabled       = 1;
    btn->cursor        = UI_CURSOR_POINTER;

    btn->background = UIRectangle_Create();
    if (!btn->background) { free(btn); return NULL; }
    UIRectangle_SetRadius(btn->background, 6.0f);
    UIRectangle_SetBorderWidth(btn->background, 0.0f);

    btn->label = UIText_Create((char*)(text ? text : ""), fontSize);
    if (!btn->label) {
        UIRectangle_Destroy(btn->background);
        free(btn);
        return NULL;
    }
    // The label is rendered in white and tinted via SetTextureColorMod
    // at draw time, so the color can change per state without rebuilding
    // the texture.
    UIText_SetColor(btn->label, UI_COLOR_WHITE);

    // Default theme: primary-blue with white text.
    const UIColor primary = { 59, 130, 246, 1.0f }; // tailwind blue-500-ish
    UIButton_SetColors(btn, primary, UI_COLOR_WHITE);

    return btn;
}

// ---------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------

UIButton* UIButton_SetText(UIButton* btn, const char* text) {
    if (!btn || !text) return btn;
    UIText_SetText(btn->label, (char*)text);
    return btn;
}

UIButton* UIButton_SetFontFamily(UIButton* btn, char* family) {
    if (!btn || !family) return btn;
    UIText_SetFontFamily(btn->label, family);
    UIText_DestroyTexture(btn->label); // invalidate the glyph cache
    return btn;
}

UIButton* UIButton_SetFontSize(UIButton* btn, float size) {
    if (!btn || size <= 0.0f) return btn;
    btn->label->fontSize = size;
    UIText_DestroyTexture(btn->label);
    return btn;
}

UIButton* UIButton_SetFontStyle(UIButton* btn, int fontStyle) {
    if (!btn || !btn->label) return btn;
    UIText_SetFontStyle(btn->label, fontStyle);
    return btn;
}

UIButton* UIButton_SetRadius(UIButton* btn, float radius) {
    if (!btn) return btn;
    UIRectangle_SetRadius(btn->background, radius);
    return btn;
}

UIButton* UIButton_SetBorderWidth(UIButton* btn, float width) {
    if (!btn) return btn;
    UIRectangle_SetBorderWidth(btn->background, width);
    return btn;
}

UIButton* UIButton_SetMargins(UIButton* btn, float l, float t, float r, float b) {
    if (!btn) return btn;
    btn->marginLeft   = l;
    btn->marginTop    = t;
    btn->marginRight  = r;
    btn->marginBottom = b;
    return btn;
}

UIButton* UIButton_SetStateStyle(UIButton* btn, UIButtonState state, UIButtonStyle style) {
    if (!btn) return btn;
    if (state < UI_BUTTON_STATE_NORMAL || state > UI_BUTTON_STATE_DISABLED) return btn;
    btn->styles[state] = style;
    return btn;
}

UIButton* UIButton_SetColors(UIButton* btn, UIColor background, UIColor text) {
    if (!btn) return btn;

    const UIColor transparent = UI_COLOR_TRANSPARENT;
    const UIColor white       = UI_COLOR_WHITE;
    const UIColor black       = UI_COLOR_BLACK;
    const UIColor gray        = UI_COLOR_GRAY;

    UIButtonStyle normal   = { background,                           transparent, text };
    UIButtonStyle hover    = { BlendColors(background, white, 0.15f), transparent, text };
    UIButtonStyle pressed  = { BlendColors(background, black, 0.20f), transparent, text };

    UIColor disabledBg     = BlendColors(background, gray, 0.55f);
    UIColor disabledText   = BlendColors(text,       gray, 0.55f);
    UIButtonStyle disabled = { disabledBg, transparent, disabledText };

    btn->styles[UI_BUTTON_STATE_NORMAL]   = normal;
    btn->styles[UI_BUTTON_STATE_HOVER]    = hover;
    btn->styles[UI_BUTTON_STATE_PRESSED]  = pressed;
    btn->styles[UI_BUTTON_STATE_DISABLED] = disabled;
    return btn;
}

UIButton* UIButton_OnClick(UIButton* btn, UIButtonCallback cb, void* userdata) {
    if (!btn) return btn;
    btn->onClick  = cb;
    btn->userdata = userdata;
    return btn;
}

UIButton* UIButton_SetShadow(UIButton* btn, UIShadow shadow) {
    if (!btn) return btn;
    UIRectangle_SetShadow(btn->background, shadow);
    return btn;
}

UIButton* UIButton_ClearShadow(UIButton* btn) {
    if (!btn) return btn;
    UIRectangle_ClearShadow(btn->background);
    return btn;
}

UIButton* UIButton_SetEnabled(UIButton* btn, int enabled) {
    if (!btn) return btn;
    btn->enabled = enabled ? 1 : 0;
    if (!btn->enabled) {
        btn->state = UI_BUTTON_STATE_DISABLED;
    } else if (btn->state == UI_BUTTON_STATE_DISABLED) {
        btn->state = UI_BUTTON_STATE_NORMAL;
    }
    return btn;
}

UIButton* UIButton_SetCursor(UIButton* btn, UICursor cursor) {
    if (btn) btn->cursor = cursor;
    return btn;
}

void UIButton_Destroy(UIButton* btn) {
    if (!btn) return;
    if (btn->background) UIRectangle_Destroy(btn->background);
    if (btn->label)      UIText_Destroy(btn->label);
    free(btn);
}

// ---------------------------------------------------------------------
// Mouse dispatchers
// ---------------------------------------------------------------------
// The loop in app.c calls these on every event. For each button in
// children:
//   - motion: updates isMouseInside and toggles state between
//             NORMAL/HOVER (or keeps PRESSED while the user is still
//             holding the mouse button inside).
//   - down:   if hit, sets isPressed + state = PRESSED.
//   - up:     if isPressed AND still inside -> fires onClick;
//             otherwise just resets isPressed.

static UIButton* AsButton(UIWidget* w) {
    if (!w || !w->data) return NULL;
    UIWidgetBase* base = (UIWidgetBase*)w->data;
    if (strcmp(base->__widget_type, UI_WIDGET_BUTTON) != 0) return NULL;
    return (UIButton*)base;
}

void UIButton_DispatchMouseMotion(UIChildren* children, float x, float y) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w   = children->children[i];
        UIButton* btn = AsButton(w);
        if (!btn || !btn->enabled) continue;

        const int inside = InsideWidget(w, x, y);
        btn->isMouseInside = inside;

        if (btn->isPressed) {
            btn->state = inside ? UI_BUTTON_STATE_PRESSED : UI_BUTTON_STATE_NORMAL;
        } else {
            btn->state = inside ? UI_BUTTON_STATE_HOVER : UI_BUTTON_STATE_NORMAL;
        }
    }
}

void UIButton_DispatchMouseDown(UIChildren* children, float x, float y) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w   = children->children[i];
        UIButton* btn = AsButton(w);
        if (!btn || !btn->enabled) continue;

        if (InsideWidget(w, x, y)) {
            btn->isPressed     = 1;
            btn->isMouseInside = 1;
            btn->state         = UI_BUTTON_STATE_PRESSED;
        }
    }
}

void UIButton_DispatchMouseUp(UIChildren* children, float x, float y) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w   = children->children[i];
        UIButton* btn = AsButton(w);
        if (!btn || !btn->enabled) continue;

        const int inside = InsideWidget(w, x, y);

        if (btn->isPressed && inside) {
            if (btn->onClick) btn->onClick(btn, btn->userdata);
            btn->state = UI_BUTTON_STATE_HOVER;
        } else {
            btn->state = inside ? UI_BUTTON_STATE_HOVER : UI_BUTTON_STATE_NORMAL;
        }

        btn->isPressed     = 0;
        btn->isMouseInside = inside;
    }
}
