#include <uikit/text.h>
#include <uikit/textfield.h>
#include <uikit/textarea.h>
#include <uikit/window.h>
#include <uikit/debug.h>
#include <uikit/widget.h>
#include <string.h>

// Defined in textfield.c. Drops focus on every text widget except the
// one we're keeping focused.
void UIKitFocus_BlurOthers(UITextField* keepField, UITextArea* keepArea,
                           UIText* keepText);

UIText* UIText_Create(char* text, float fontSize) {
    UIText* uiText = (UIText*)calloc(1, sizeof(UIText));
    if (uiText == NULL) {
        UI_ERROR(UI_CAT_TEXT, "out of memory allocating UIText");
        return NULL; // Memory allocation failed
    }

    if (text == NULL) {
        text = _strdup(""); // Default text if NULL
    }

    uiText->fontSize = fontSize;
    uiText->fontFamily = _strdup(DEFAULT_FONT_PATH); // Default font family
    uiText->fontStyle = Normal; // Default style
    uiText->color = UI_COLOR_BLACK; // Default color (black)
    UIRectangle* UIRecN = UIRectangle_Create(); // Default rectangle
    UIRectangle_SetColor(UIRecN, UI_COLOR_TRANSPARENT); // Set default color (white)

    uiText->background = UIRecN; // Default background color (white
    uiText->text = _strdup(text);
    uiText->textLength = strlen(text);
    uiText->__widget_type = UI_WIDGET_TEXT; // Set the widget type
    uiText->__SDL_textTexture = NULL;

    uiText->selAnchor       = -1;
    uiText->selCaret        = 0;
    uiText->lastClickPos    = -1;
    uiText->selectionColor  = (UIColor){ 191, 219, 254, 1.0f };
    uiText->cursor          = UI_CURSOR_TEXT;
    uiText->__cachedTextLen  = -1;
    uiText->__cachedWrapMode = -1;
    uiText->__cachedWrapW    = -1;

    return uiText;
}

UIText* UIText_SetFontFamily(UIText* text, char* fontFamily) {
    if (text == NULL || fontFamily == NULL) {
        return NULL;
    }

    free(text->fontFamily); // Free previous font family
    text->fontFamily = _strdup(fontFamily);
    return text;
}

UIText* UIText_SetFontStyle(UIText* text, int fontStyle) {
    if (text == NULL) {
        return NULL;
    }

    if (text->fontStyle == fontStyle) return text;
    text->fontStyle = fontStyle;
    // The cached glyph texture was baked at the previous style — drop
    // it so the next render reruns TTF_SetFontStyle and rebuilds.
    UIText_DestroyTexture(text);
    return text;
}

UIText* UIText_SetColor(UIText* text, UIColor color) {
    if (text == NULL) {
        return NULL;
    }

    // Glyphs are rasterised with the colour baked into the cached
    // texture (TTF_RenderText_Blended is called with the foreground
    // SDL_Color, not a tint mod), so a colour change has to drop the
    // cached texture — otherwise the renderer keeps blitting the old
    // colour until something else (text edit, font swap, etc.) forces
    // a rebuild.
    const int sameColor = text->color.r == color.r &&
                          text->color.g == color.g &&
                          text->color.b == color.b &&
                          text->color.a == color.a;
    text->color = color;
    if (!sameColor) {
        UIText_DestroyTexture(text);
    }
    return text;
}

UIText* UIText_SetBackground(UIText* text, UIRectangle* backgroundRect) {
    if (text == NULL) {
        return NULL;
    }

    // Use the proper destructor for the old rect, not raw free().
    if (text->background) {
        UIRectangle_Destroy(text->background);
        text->background = NULL;
    }

    if (backgroundRect == NULL) {
        // Allocate a transparent default to preserve the invariant of
        // text->background never being NULL after UIText_Create.
        backgroundRect = UIRectangle_Create();
        if (backgroundRect) UIRectangle_SetColor(backgroundRect, UI_COLOR_TRANSPARENT);
    }

    text->background = backgroundRect;
    return text;
}

UIText* UIText_SetText(UIText* text, char* newText) {
    if (text == NULL || newText == NULL) {
        return NULL; // Invalid arguments
    }

    if (text->text != NULL) {
        free(text->text); // Free previous text if not NULL
    }
    
    text->text = _strdup(newText);
    text->textLength = strlen(newText);

    // Invalidate caches so the next render rebuilds against the new
    // text. Drops both the single-texture path and the per-line cache
    // (selectable mode).
    UIText_DestroyTexture(text);
    if (text->selAnchor > text->textLength) text->selAnchor = text->textLength;
    if (text->selCaret  > text->textLength) text->selCaret  = text->textLength;
    return text;
}

UIText* UIText_SetMargins(UIText* text, float left, float top, float right, float bottom) {
    if (text == NULL) {
        return NULL; // Invalid argument
    }

    text->marginLeft = left;
    text->marginTop = top;
    text->marginRight = right;
    text->marginBottom = bottom;
    return text;
}

UIText* UIText_SetPadding(UIText* text, float left, float top, float right, float bottom) {
    if (text == NULL) {
        return NULL; // Invalid argument
    }

    text->paddingLeft = left;
    text->paddingTop = top;
    text->paddingRight = right;
    text->paddingBottom = bottom;
    return text;
}

UIText* UIText_SetHAlign(UIText* text, UITextHAlign hAlign) {
    if (!text) return NULL;
    text->hAlign = hAlign;
    return text;
}

UIText* UIText_SetVAlign(UIText* text, UITextVAlign vAlign) {
    if (!text) return NULL;
    text->vAlign = vAlign;
    return text;
}

UIText* UIText_SetAlignment(UIText* text, UITextHAlign h, UITextVAlign v) {
    if (!text) return NULL;
    text->hAlign = h;
    text->vAlign = v;
    return text;
}

UIText* UIText_SetWrapWidth(UIText* text, int wrapWidth) {
    if (!text) return NULL;
    if (wrapWidth < 0) wrapWidth = 0;
    if (text->wrapWidth != wrapWidth) {
        text->wrapWidth = wrapWidth;
        // Invalidate cached glyph texture so the next render rebuilds
        // it at the new wrap width.
        UIText_DestroyTexture(text);
    }
    return text;
}

UIText* UIText_SetWrapMode(UIText* text, UIWrapMode mode) {
    if (!text) return NULL;
    if (text->wrapMode != mode) {
        text->wrapMode = mode;
        UIText_DestroyTexture(text);
    }
    return text;
}

UIText* UIText_SetWrapToBounds(UIText* text, int enabled) {
    if (!text) return NULL;
    enabled = enabled ? 1 : 0;
    if (text->wrapToBounds != enabled) {
        text->wrapToBounds = enabled;
        UIText_DestroyTexture(text);
    }
    return text;
}

// Frees the per-line cache built by the selectable render path.
static void UIText_InvalidateLineCache(UIText* text) {
    if (!text) return;
    if (text->__lineTextures) {
        for (int i = 0; i < text->__linesLen; i++) {
            if (text->__lineTextures[i]) SDL_DestroyTexture(text->__lineTextures[i]);
        }
    }
    if (text->__lineCharOffsets) {
        for (int i = 0; i < text->__linesLen; i++) {
            free(text->__lineCharOffsets[i]);
        }
    }
    free(text->__lineTextures);       text->__lineTextures = NULL;
    free(text->__lineStarts);         text->__lineStarts = NULL;
    free(text->__lineLengths);        text->__lineLengths = NULL;
    free(text->__lineCharOffsets);    text->__lineCharOffsets = NULL;
    free(text->__lineCharOffsetsLen); text->__lineCharOffsetsLen = NULL;
    free(text->__lineIsSoft);         text->__lineIsSoft = NULL;
    text->__linesLen        = 0;
    text->__linesCap        = 0;
    text->__cachedTextLen   = -1;
    text->__cachedWrapMode  = -1;
    text->__cachedWrapW     = -1;
}

UIText* UIText_SetSelectable(UIText* text, int enabled) {
    if (!text) return NULL;
    enabled = enabled ? 1 : 0;
    if (text->selectable == enabled) return text;
    text->selectable = enabled;
    text->selAnchor       = -1;
    text->selCaret        = 0;
    text->mouseSelecting  = 0;
    // Switching modes invalidates both caches: single-texture vs per-line.
    UIText_DestroyTexture(text);
    UIText_InvalidateLineCache(text);
    return text;
}

UIText* UIText_SetSelectionColor(UIText* text, UIColor color) {
    if (!text) return NULL;
    text->selectionColor = color;
    return text;
}

UIText* UIText_SetCursor(UIText* text, UICursor cursor) {
    if (!text) return NULL;
    text->cursor = cursor;
    return text;
}

UIText* UIText_ClearSelection(UIText* text) {
    if (!text) return NULL;
    text->selAnchor      = -1;
    text->selCaret       = 0;
    text->mouseSelecting = 0;
    return text;
}

UIText* UIText_SetFocus(UIText* text, int focused) {
    if (!text) return NULL;
    if (!text->selectable) return text;
    focused = focused ? 1 : 0;

    UIWidget* w = UIWidget_FindByData(text);
    if (w) {
        UIWidget_SetFocus(w, focused);
        return text;
    }
    // Fallback: not in the children tree. Apply locally.
    if (focused) UIKitFocus_BlurOthers(NULL, NULL, text);
    else         text->mouseSelecting = 0;
    text->focused = focused;
    return text;
}

int UIText_IsFocused(const UIText* text) {
    return text ? text->focused : 0;
}

void UIText_DestroyTexture(UIText* text) {
    if (!text) return;
    if (text->__SDL_textTexture) {
        SDL_DestroyTexture(text->__SDL_textTexture);
        text->__SDL_textTexture = NULL;
    }
    UIText_InvalidateLineCache(text);
}

void UIText_Destroy(UIText* text) {
    if (text) {
        free(text->fontFamily);
        free(text->text);
        UIRectangle_Destroy(text->background);
        UIText_DestroyTexture(text);
        free(text);
    }
}

// ---------------------------------------------------------------------
// Dispatchers
// ---------------------------------------------------------------------

static UIText* AsSelectableText(UIWidget* w) {
    if (!w || !w->data || !w->visible || !w->width || !w->height) return NULL;
    UIWidgetBase* b = (UIWidgetBase*)w->data;
    if (strcmp(b->__widget_type, UI_WIDGET_TEXT) != 0) return NULL;
    UIText* t = (UIText*)b;
    return t->selectable ? t : NULL;
}

static int InsideWidget(const UIWidget* w, float x, float y) {
    if (!w || !w->visible || !w->width || !w->height) return 0;
    return (x >= w->x && x < w->x + *w->width &&
            y >= w->y && y < w->y + *w->height);
}

static int IsWordChar(unsigned char c) {
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    if (c == '_') return 1;
    return 0;
}

static void WordAroundUIText(const UIText* t, int pos, int* outStart, int* outEnd) {
    if (!t || t->textLength == 0) {
        if (outStart) *outStart = 0;
        if (outEnd)   *outEnd   = 0;
        return;
    }
    if (pos < 0) pos = 0;
    if (pos > t->textLength) pos = t->textLength;
    int probe = pos;
    if (probe >= t->textLength) probe = t->textLength - 1;
    if (t->text[probe] == '\n') {
        if (outStart) *outStart = probe;
        if (outEnd)   *outEnd   = probe;
        return;
    }
    const int target = IsWordChar((unsigned char)t->text[probe]);
    int s = probe;
    while (s > 0 && t->text[s - 1] != '\n' &&
           IsWordChar((unsigned char)t->text[s - 1]) == target) s--;
    int e = probe;
    while (e < t->textLength && t->text[e] != '\n' &&
           IsWordChar((unsigned char)t->text[e]) == target) e++;
    if (outStart) *outStart = s;
    if (outEnd)   *outEnd   = e;
}

// Maps a local pixel (relative to the inside of padding) to a byte
// offset, using the per-line cache populated by the renderer.
static int UIText_CaretFromLocalXY(const UIText* t, float localX, float localY) {
    if (!t || t->__linesLen <= 0 || !t->__lineCharOffsets) return 0;
    const float lh = t->fontSize * 1.25f;
    int line = (lh > 0.0f) ? (int)(localY / lh) : 0;
    if (line < 0) line = 0;
    if (line >= t->__linesLen) line = t->__linesLen - 1;

    const int* off = t->__lineCharOffsets[line];
    const int  n   = t->__lineCharOffsetsLen[line] - 1;
    if (!off || n < 0) return t->__lineStarts[line];
    if (localX <= 0.0f) return t->__lineStarts[line];
    int col = n;
    for (int i = 0; i < n; i++) {
        const float a = (float)off[i];
        const float b = (float)off[i + 1];
        if (localX < (a + b) * 0.5f) { col = i; break; }
    }
    return t->__lineStarts[line] + col;
}

void UIText_DispatchMouseDown(UIChildren* children, SDL_Window* win,
                              float x, float y, int button) {
    (void)win;
    if (!children || button != SDL_BUTTON_LEFT) return;

    UIText*   hit  = NULL;
    UIWidget* hitW = NULL;
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w  = children->children[i];
        UIText*   t  = AsSelectableText(w);
        if (!t) continue;
        if (InsideWidget(w, x, y)) { hit = t; hitW = w; break; }
    }

    // Drop focus + selection on every other selectable UIText.
    for (int i = 0; i < children->count; i++) {
        UIText* t = AsSelectableText(children->children[i]);
        if (!t || t == hit) continue;
        t->focused        = 0;
        t->mouseSelecting = 0;
        t->lastClickMs    = 0;
        t->lastClickPos   = -1;
        t->clickCount     = 0;
        t->selAnchor      = -1;
        t->selCaret       = 0;
    }

    if (!hit) return;
    UIText* t = hit;
    t->focused = 1;
    UIWidget_SetFocus(hitW, 1);

    const float lx = x - (hitW->x + t->paddingLeft);
    const float ly = y - (hitW->y + t->paddingTop);
    int pos = UIText_CaretFromLocalXY(t, lx, ly);
    if (pos < 0) pos = 0;
    if (pos > t->textLength) pos = t->textLength;

    const Uint64 now = SDL_GetTicks();
    const int nearLast = (t->lastClickPos >= 0 &&
                          ((pos > t->lastClickPos ? pos - t->lastClickPos
                                                  : t->lastClickPos - pos) <= 1));
    if (t->lastClickMs != 0 && (now - t->lastClickMs) <= 500 && nearLast) {
        t->clickCount = t->clickCount + 1;
        if (t->clickCount > 3) t->clickCount = 3;
    } else {
        t->clickCount = 1;
    }
    t->lastClickMs  = now;
    t->lastClickPos = pos;

    if (t->clickCount == 1) {
        t->selCaret       = pos;
        t->selAnchor      = pos;
        t->mouseSelecting = 1;
    } else if (t->clickCount == 2) {
        int ws, we; WordAroundUIText(t, pos, &ws, &we);
        t->selAnchor      = ws;
        t->selCaret       = we;
        t->mouseSelecting = 0;
    } else {
        // Triple-click: select the entire visual line.
        if (t->__linesLen > 0) {
            int lo = 0, hi = t->__linesLen - 1;
            while (lo < hi) {
                int mid = (lo + hi + 1) / 2;
                if (t->__lineStarts[mid] <= pos) lo = mid; else hi = mid - 1;
            }
            t->selAnchor = t->__lineStarts[lo];
            t->selCaret  = t->__lineStarts[lo] + t->__lineLengths[lo];
        }
        t->mouseSelecting = 0;
    }
}

void UIText_DispatchMouseMotion(UIChildren* children, float x, float y) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        UIText*   t = AsSelectableText(w);
        if (!t || !t->mouseSelecting) continue;
        const float lx = x - (w->x + t->paddingLeft);
        const float ly = y - (w->y + t->paddingTop);
        int pos = UIText_CaretFromLocalXY(t, lx, ly);
        if (pos < 0) pos = 0;
        if (pos > t->textLength) pos = t->textLength;
        t->selCaret = pos;
    }
}

void UIText_DispatchMouseUp(UIChildren* children, float x, float y, int button) {
    (void)x; (void)y;
    if (!children || button != SDL_BUTTON_LEFT) return;
    for (int i = 0; i < children->count; i++) {
        UIText* t = AsSelectableText(children->children[i]);
        if (!t) continue;
        t->mouseSelecting = 0;
    }
}

void UIText_DispatchKeyDown(UIChildren* children, SDL_Window* win,
                            SDL_Scancode key, Uint16 mod) {
    (void)win;
    if (!children) return;
    const int ctrl = (mod & SDL_KMOD_CTRL) != 0;
    for (int i = 0; i < children->count; i++) {
        UIText* t = AsSelectableText(children->children[i]);
        if (!t || !t->focused) continue;

        if (ctrl && key == SDL_SCANCODE_C) {
            if (t->selAnchor >= 0 && t->selAnchor != t->selCaret) {
                int s = t->selAnchor, e = t->selCaret;
                if (s > e) { int tmp = s; s = e; e = tmp; }
                if (s < 0) s = 0;
                if (e > t->textLength) e = t->textLength;
                const int n = e - s;
                char* buf = (char*)malloc((size_t)n + 1);
                if (buf) {
                    memcpy(buf, t->text + s, (size_t)n);
                    buf[n] = '\0';
                    SDL_SetClipboardText(buf);
                    free(buf);
                }
            }
        } else if (ctrl && key == SDL_SCANCODE_A) {
            t->selAnchor = 0;
            t->selCaret  = t->textLength;
        } else if (key == SDL_SCANCODE_ESCAPE) {
            t->selAnchor      = -1;
            t->selCaret       = 0;
            t->mouseSelecting = 0;
            t->focused        = 0;
        }
        return; // first focused widget wins
    }
}