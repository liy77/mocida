#include <uikit/textarea.h>
#include <uikit/textfield.h>
#include <uikit/window.h>
#include <stdlib.h>
#include <string.h>

// Defined in textfield.c. Drops focus on every text widget except the
// one we're keeping focused.
void UIKitFocus_BlurOthers(UITextField* keepField, UITextArea* keepArea,
                           UIText* keepText);

// ---------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------

static int InsideWidget(const UIWidget* w, float x, float y) {
    if (!w || !w->visible || !w->width || !w->height) return 0;
    return (x >= w->x && x < w->x + *w->width &&
            y >= w->y && y < w->y + *w->height);
}

static UITextArea* AsTextArea(UIWidget* w) {
    if (!w || !w->data) return NULL;
    UIWidgetBase* b = (UIWidgetBase*)w->data;
    if (strcmp(b->__widget_type, UI_WIDGET_TEXTAREA) != 0) return NULL;
    return (UITextArea*)b;
}

static int EnsureTextCapacity(UITextArea* ta, int needCap) {
    if (!ta) return 0;
    if (ta->textCapacity >= needCap) return 1;
    int newCap = ta->textCapacity ? ta->textCapacity : 64;
    while (newCap < needCap) newCap *= 2;
    char* p = (char*)realloc(ta->text, (size_t)newCap);
    if (!p) return 0;
    ta->text = p;
    ta->textCapacity = newCap;
    return 1;
}

// Frees the per-line caches. The next render rebuilds them.
static void InvalidateLineCache(UITextArea* ta) {
    if (!ta) return;
    if (ta->lineTextures) {
        for (int i = 0; i < ta->linesLen; i++) {
            if (ta->lineTextures[i]) SDL_DestroyTexture(ta->lineTextures[i]);
        }
    }
    if (ta->lineCharOffsets) {
        for (int i = 0; i < ta->linesLen; i++) {
            free(ta->lineCharOffsets[i]);
        }
    }
    free(ta->lineTextures);       ta->lineTextures = NULL;
    free(ta->lineStarts);         ta->lineStarts = NULL;
    free(ta->lineLengths);        ta->lineLengths = NULL;
    free(ta->lineCharOffsets);    ta->lineCharOffsets = NULL;
    free(ta->lineCharOffsetsLen); ta->lineCharOffsetsLen = NULL;
    free(ta->lineIsSoft);         ta->lineIsSoft = NULL;
    ta->linesLen = 0;
    ta->linesCap = 0;
    ta->__cachedTextLen  = -1;
    ta->__cachedWrapMode = -1;
    ta->__cachedWrapW    = -1;
}

static void ClampCaretAndSelection(UITextArea* ta) {
    if (!ta) return;
    if (ta->caretPos < 0)            ta->caretPos = 0;
    if (ta->caretPos > ta->textLen)  ta->caretPos = ta->textLen;
    if (ta->selAnchor >= 0 && ta->selAnchor > ta->textLen) {
        ta->selAnchor = ta->textLen;
    }
}

// ---------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------

UITextArea* UITextArea_Create(const char* initialText, float fontSize) {
    UITextArea* ta = (UITextArea*)calloc(1, sizeof(UITextArea));
    if (!ta) return NULL;
    ta->__widget_type = UI_WIDGET_TEXTAREA;

    const int initLen = initialText ? (int)strlen(initialText) : 0;
    if (!EnsureTextCapacity(ta, initLen + 32)) { free(ta); return NULL; }
    if (initialText) memcpy(ta->text, initialText, (size_t)initLen);
    ta->text[initLen] = '\0';
    ta->textLen   = initLen;
    ta->caretPos  = initLen;
    ta->maxLength = -1;

    ta->fontSize          = fontSize > 0.0f ? fontSize : 16.0f;
    ta->textColor         = (UIColor){ 15, 23, 42, 1.0f };
    ta->placeholderColor  = (UIColor){ 148, 163, 184, 1.0f };
    ta->caretColor        = (UIColor){ 59, 130, 246, 1.0f };
    ta->bgColor           = (UIColor){ 255, 255, 255, 1.0f };
    ta->borderColor       = (UIColor){ 203, 213, 225, 1.0f };
    ta->borderColorFocused = (UIColor){ 59, 130, 246, 1.0f };
    ta->borderWidth       = 1.0f;
    ta->radius            = 6.0f;
    ta->paddingLeft       = 10.0f;
    ta->paddingRight      = 10.0f;
    ta->paddingTop        = 8.0f;
    ta->paddingBottom     = 8.0f;
    ta->lineSpacing       = 1.25f;
    ta->selAnchor         = -1;
    ta->selectionColor    = (UIColor){ 191, 219, 254, 1.0f };
    ta->lastClickPos      = -1;
    ta->__cachedTextLen   = -1;
    ta->__cachedWrapMode  = -1;
    ta->__cachedWrapW     = -1;
    ta->wrapMode          = UI_WRAP_NONE;
    ta->cursor            = UI_CURSOR_TEXT;
    ta->placeholderAnimated = 0;
    ta->caretBlinkMs        = 530;
    return ta;
}

UITextArea* UITextArea_SetWrapMode(UITextArea* ta, UIWrapMode mode) {
    if (!ta) return ta;
    if (ta->wrapMode == mode) return ta;
    ta->wrapMode = mode;
    InvalidateLineCache(ta);
    return ta;
}

UITextArea* UITextArea_SetCursor(UITextArea* ta, UICursor cursor) {
    if (ta) ta->cursor = cursor;
    return ta;
}

UITextArea* UITextArea_SetText(UITextArea* ta, const char* text) {
    if (!ta) return ta;
    const int len = text ? (int)strlen(text) : 0;
    if (!EnsureTextCapacity(ta, len + 1)) return ta;
    if (text) memcpy(ta->text, text, (size_t)len);
    ta->text[len] = '\0';
    ta->textLen   = len;
    ta->caretPos  = len;
    ta->selAnchor = -1;
    InvalidateLineCache(ta);
    if (ta->onChange) ta->onChange(ta, ta->text, ta->userdata);
    return ta;
}

const char* UITextArea_GetText(UITextArea* ta) {
    return ta ? ta->text : NULL;
}

UITextArea* UITextArea_SetPlaceholder(UITextArea* ta, const char* placeholder) {
    if (!ta) return ta;
    free(ta->placeholder);
    ta->placeholder = placeholder ? _strdup(placeholder) : NULL;
    return ta;
}

UITextArea* UITextArea_SetPlaceholderAnimated(UITextArea* ta, int yes) {
    if (!ta) return ta;
    ta->placeholderAnimated = yes ? 1 : 0;
    return ta;
}

UITextArea* UITextArea_SetCaretBlinkRate(UITextArea* ta, int halfPeriodMs) {
    if (!ta) return ta;
    if (halfPeriodMs < 0) halfPeriodMs = 0;
    ta->caretBlinkMs = halfPeriodMs;
    return ta;
}

UITextArea* UITextArea_SetFontFamily(UITextArea* ta, char* family) {
    if (!ta) return ta;
    free(ta->fontFamily);
    ta->fontFamily = family ? _strdup(family) : NULL;
    InvalidateLineCache(ta);
    return ta;
}

UITextArea* UITextArea_SetMaxLength(UITextArea* ta, int maxLen) {
    if (!ta) return ta;
    ta->maxLength = maxLen;
    if (maxLen > 0 && ta->textLen > maxLen) {
        ta->text[maxLen] = '\0';
        ta->textLen = maxLen;
        if (ta->caretPos > maxLen) ta->caretPos = maxLen;
        InvalidateLineCache(ta);
    }
    return ta;
}

UITextArea* UITextArea_SetLineSpacing(UITextArea* ta, float spacing) {
    if (!ta) return ta;
    if (spacing < 0.5f) spacing = 0.5f;
    ta->lineSpacing = spacing;
    return ta;
}

UITextArea* UITextArea_SetBgColor    (UITextArea* ta, UIColor color) { if (ta) ta->bgColor = color;   return ta; }
UITextArea* UITextArea_SetTextColor  (UITextArea* ta, UIColor color) {
    if (!ta) return ta;
    ta->textColor = color;
    InvalidateLineCache(ta);
    return ta;
}
UITextArea* UITextArea_SetBorder(UITextArea* ta, UIColor normal, UIColor focused, float width) {
    if (!ta) return ta;
    ta->borderColor = normal;
    ta->borderColorFocused = focused;
    ta->borderWidth = width < 0.0f ? 0.0f : width;
    return ta;
}
UITextArea* UITextArea_SetRadius     (UITextArea* ta, float radius) { if (ta) ta->radius = radius < 0.0f ? 0.0f : radius; return ta; }

UITextArea* UITextArea_SetPadding(UITextArea* ta, float x, float y) {
    if (!ta) return ta;
    if (x < 0.0f) x = 0.0f;
    if (y < 0.0f) y = 0.0f;
    ta->paddingLeft = ta->paddingRight = x;
    ta->paddingTop  = ta->paddingBottom = y;
    return ta;
}
UITextArea* UITextArea_SetPaddingLeft  (UITextArea* ta, float v) { if (ta) ta->paddingLeft   = v < 0.0f ? 0.0f : v; return ta; }
UITextArea* UITextArea_SetPaddingRight (UITextArea* ta, float v) { if (ta) ta->paddingRight  = v < 0.0f ? 0.0f : v; return ta; }
UITextArea* UITextArea_SetPaddingTop   (UITextArea* ta, float v) { if (ta) ta->paddingTop    = v < 0.0f ? 0.0f : v; return ta; }
UITextArea* UITextArea_SetPaddingBottom(UITextArea* ta, float v) { if (ta) ta->paddingBottom = v < 0.0f ? 0.0f : v; return ta; }

UITextArea* UITextArea_SetSelectionColor(UITextArea* ta, UIColor color) {
    if (ta) ta->selectionColor = color; return ta;
}

UITextArea* UITextArea_SetPlaceholderColor(UITextArea* ta, UIColor color) {
    if (ta) ta->placeholderColor = color;
    return ta;
}

UITextArea* UITextArea_SetCaretColor(UITextArea* ta, UIColor color) {
    if (ta) ta->caretColor = color;
    return ta;
}

UITextArea* UITextArea_SetBorderColor(UITextArea* ta, UIColor color) {
    if (ta) ta->borderColor = color;
    return ta;
}

UITextArea* UITextArea_SetBorderColorFocused(UITextArea* ta, UIColor color) {
    if (ta) ta->borderColorFocused = color;
    return ta;
}

UITextArea* UITextArea_SetBorderWidth(UITextArea* ta, float width) {
    if (ta) ta->borderWidth = width < 0.0f ? 0.0f : width;
    return ta;
}

UITextArea* UITextArea_SetFontSize(UITextArea* ta, float size) {
    if (!ta || size <= 0.0f) return ta;
    if (ta->fontSize != size) {
        ta->fontSize = size;
        InvalidateLineCache(ta);
    }
    return ta;
}

UITextArea* UITextArea_SetFontStyle(UITextArea* ta, int fontStyle) {
    if (!ta) return ta;
    if (ta->fontStyle != fontStyle) {
        ta->fontStyle = fontStyle;
        InvalidateLineCache(ta);
    }
    return ta;
}

UITextArea* UITextArea_OnChange(UITextArea* ta, UITextAreaChangedCallback cb, void* userdata) {
    if (!ta) return ta;
    ta->onChange = cb;
    ta->userdata = userdata;
    return ta;
}

void UITextArea_Destroy(UITextArea* ta) {
    if (!ta) return;
    free(ta->text);
    free(ta->placeholder);
    free(ta->fontFamily);
    InvalidateLineCache(ta);
    free(ta);
}

// ---------------------------------------------------------------------
// Selection helpers
// ---------------------------------------------------------------------

static int HasSelection(const UITextArea* ta) {
    return ta && ta->selAnchor >= 0 && ta->selAnchor != ta->caretPos;
}

static void SelectionRange(const UITextArea* ta, int* outStart, int* outEnd) {
    int a = ta->selAnchor;
    int b = ta->caretPos;
    if (a > b) { int t = a; a = b; b = t; }
    if (outStart) *outStart = a;
    if (outEnd)   *outEnd   = b;
}

static void CollapseSelection(UITextArea* ta) {
    if (ta) ta->selAnchor = ta->caretPos;
}

static void DeleteSelection(UITextArea* ta) {
    if (!HasSelection(ta)) return;
    int s, e;
    SelectionRange(ta, &s, &e);
    if (s < 0) s = 0;
    if (e > ta->textLen) e = ta->textLen;
    if (e <= s) { ta->selAnchor = ta->caretPos; return; }
    const int n = e - s;
    memmove(ta->text + s, ta->text + e, (size_t)(ta->textLen - e + 1));
    ta->textLen  -= n;
    ta->caretPos  = s;
    ta->selAnchor = s;
    InvalidateLineCache(ta);
    if (ta->onChange) ta->onChange(ta, ta->text, ta->userdata);
}

// ---------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------

static void InsertChars(UITextArea* ta, const char* chars, int n) {
    if (!ta || !chars || n <= 0) return;
    ClampCaretAndSelection(ta);
    if (HasSelection(ta)) DeleteSelection(ta);
    if (ta->maxLength >= 0 && ta->textLen + n > ta->maxLength) {
        n = ta->maxLength - ta->textLen;
        if (n <= 0) return;
    }
    if (!EnsureTextCapacity(ta, ta->textLen + n + 1)) return;
    memmove(ta->text + ta->caretPos + n,
            ta->text + ta->caretPos,
            (size_t)(ta->textLen - ta->caretPos + 1));
    memcpy(ta->text + ta->caretPos, chars, (size_t)n);
    ta->caretPos += n;
    ta->textLen  += n;
    CollapseSelection(ta);
    InvalidateLineCache(ta);
    if (ta->onChange) ta->onChange(ta, ta->text, ta->userdata);
}

static void DeleteBefore(UITextArea* ta) {
    if (!ta) return;
    ClampCaretAndSelection(ta);
    if (HasSelection(ta)) { DeleteSelection(ta); return; }
    if (ta->caretPos <= 0) return;
    memmove(ta->text + ta->caretPos - 1,
            ta->text + ta->caretPos,
            (size_t)(ta->textLen - ta->caretPos + 1));
    ta->caretPos--;
    ta->textLen--;
    CollapseSelection(ta);
    InvalidateLineCache(ta);
    if (ta->onChange) ta->onChange(ta, ta->text, ta->userdata);
}

static void DeleteAfter(UITextArea* ta) {
    if (!ta) return;
    ClampCaretAndSelection(ta);
    if (HasSelection(ta)) { DeleteSelection(ta); return; }
    if (ta->caretPos >= ta->textLen) return;
    memmove(ta->text + ta->caretPos,
            ta->text + ta->caretPos + 1,
            (size_t)(ta->textLen - ta->caretPos));
    ta->textLen--;
    CollapseSelection(ta);
    InvalidateLineCache(ta);
    if (ta->onChange) ta->onChange(ta, ta->text, ta->userdata);
}

// ---------------------------------------------------------------------
// Line layout queries (read-only, use the cache the renderer fills).
// ---------------------------------------------------------------------

// Maps a byte offset to (lineIndex, columnByte).
static void LineFromPos(const UITextArea* ta, int pos, int* outLine, int* outCol) {
    if (outLine) *outLine = 0;
    if (outCol)  *outCol  = 0;
    if (!ta || ta->linesLen <= 0) return;
    if (pos < 0) pos = 0;
    if (pos > ta->textLen) pos = ta->textLen;

    int lo = 0, hi = ta->linesLen - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (ta->lineStarts[mid] <= pos) lo = mid; else hi = mid - 1;
    }
    if (outLine) *outLine = lo;
    if (outCol)  *outCol  = pos - ta->lineStarts[lo];
}

// Inverse: given (line, col), produce a byte offset.
static int PosFromLineCol(const UITextArea* ta, int line, int col) {
    if (!ta || ta->linesLen <= 0) return 0;
    if (line < 0) line = 0;
    if (line >= ta->linesLen) line = ta->linesLen - 1;
    if (col < 0) col = 0;
    if (col > ta->lineLengths[line]) col = ta->lineLengths[line];
    return ta->lineStarts[line] + col;
}

// Maps a local pixel (x, y) (relative to the inside of the padding) to
// a caret byte position.
static int CaretFromLocalXY(const UITextArea* ta, float localX, float localY) {
    if (!ta || ta->linesLen <= 0 || !ta->lineCharOffsets) return 0;
    float lh = ta->fontSize * ta->lineSpacing;
    if (lh <= 0.0f) lh = ta->fontSize;
    int line = (int)(localY / lh);
    if (line < 0) line = 0;
    if (line >= ta->linesLen) line = ta->linesLen - 1;

    const int* off = ta->lineCharOffsets[line];
    const int  n   = ta->lineCharOffsetsLen[line] - 1; // last valid byte offset
    if (!off || n < 0) return ta->lineStarts[line];

    if (localX <= 0.0f) return ta->lineStarts[line];
    int col = n;
    for (int i = 0; i < n; i++) {
        const float a = (float)off[i];
        const float b = (float)off[i + 1];
        if (localX < (a + b) * 0.5f) { col = i; break; }
    }
    return ta->lineStarts[line] + col;
}

// Returns the pixel x of a position WITHIN its line (column-local). Uses
// the cached offsets; assumes the renderer has built them.
static float ColumnPixelX(const UITextArea* ta, int line, int col) {
    if (!ta || ta->linesLen <= 0 || !ta->lineCharOffsets) return 0.0f;
    if (line < 0 || line >= ta->linesLen) return 0.0f;
    const int* off = ta->lineCharOffsets[line];
    if (!off) return 0.0f;
    if (col < 0) col = 0;
    if (col >= ta->lineCharOffsetsLen[line]) col = ta->lineCharOffsetsLen[line] - 1;
    return (float)off[col];
}

// ---------------------------------------------------------------------
// Focus
// ---------------------------------------------------------------------

static void SetFocused(UITextArea* ta, SDL_Window* win, int focused) {
    if (!ta) return;
    if (ta->focused == focused) return;
    ta->focused = focused;
    if (!focused) {
        ta->selAnchor      = -1;
        ta->mouseSelecting = 0;
        ta->clickCount     = 0;
    }
    if (win) {
        if (focused) SDL_StartTextInput(win);
        else         SDL_StopTextInput(win);
    }
}

// Word boundary for double-click.
static int IsWordChar(unsigned char c) {
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    if (c == '_') return 1;
    return 0;
}

static void WordAround(const UITextArea* ta, int pos, int* outStart, int* outEnd) {
    if (!ta || ta->textLen == 0) {
        if (outStart) *outStart = 0;
        if (outEnd)   *outEnd   = 0;
        return;
    }
    if (pos < 0) pos = 0;
    if (pos > ta->textLen) pos = ta->textLen;
    int probe = pos;
    if (probe >= ta->textLen) probe = ta->textLen - 1;

    // Don't cross a newline when picking a word.
    if (ta->text[probe] == '\n') {
        if (outStart) *outStart = probe;
        if (outEnd)   *outEnd   = probe;
        return;
    }

    const int target = IsWordChar((unsigned char)ta->text[probe]);
    int s = probe;
    while (s > 0 && ta->text[s - 1] != '\n' &&
           IsWordChar((unsigned char)ta->text[s - 1]) == target) s--;
    int e = probe;
    while (e < ta->textLen && ta->text[e] != '\n' &&
           IsWordChar((unsigned char)ta->text[e]) == target) e++;
    if (outStart) *outStart = s;
    if (outEnd)   *outEnd   = e;
}

// Selected substring as a malloc'd copy. Caller frees.
static char* CopySelectedText(const UITextArea* ta) {
    if (!HasSelection(ta)) return NULL;
    int s, e; SelectionRange(ta, &s, &e);
    const int n = e - s;
    char* out = (char*)malloc((size_t)n + 1);
    if (!out) return NULL;
    memcpy(out, ta->text + s, (size_t)n);
    out[n] = '\0';
    return out;
}

// ---------------------------------------------------------------------
// Dispatchers
// ---------------------------------------------------------------------

void UITextArea_DispatchMouseDown(UIChildren* children, SDL_Window* win,
                                  float x, float y, int button) {
    if (!children || button != SDL_BUTTON_LEFT) return;

    UITextArea* hit  = NULL;
    UIWidget*   hitW = NULL;
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w = children->children[i];
        UITextArea* ta = AsTextArea(w);
        if (!ta) continue;
        if (InsideWidget(w, x, y)) { hit = ta; hitW = w; break; }
    }

    // Unfocus everyone else BEFORE focusing the hit (SDL_StartTextInput
    // is window-scoped; doing it the other way around leaves text input
    // off after a focus change).
    for (int i = 0; i < children->count; i++) {
        UITextArea* ta = AsTextArea(children->children[i]);
        if (!ta || ta == hit) continue;
        SetFocused(ta, win, 0);
        ta->mouseSelecting = 0;
        ta->lastClickMs    = 0;
        ta->lastClickPos   = -1;
        ta->clickCount     = 0;
    }

    if (!hit) return;

    UITextArea* ta = hit;
    SetFocused(ta, win, 1);
    UIWidget_SetFocus(hitW, 1);

    const float lx = x - (hitW->x + ta->paddingLeft);
    const float ly = y - (hitW->y + ta->paddingTop) + ta->scrollY;
    int pos = CaretFromLocalXY(ta, lx, ly);
    if (pos < 0) pos = 0;
    if (pos > ta->textLen) pos = ta->textLen;

    const Uint64 now = SDL_GetTicks();
    const int near = (ta->lastClickPos >= 0 &&
                      ((pos > ta->lastClickPos ? pos - ta->lastClickPos
                                               : ta->lastClickPos - pos) <= 1));
    if (ta->lastClickMs != 0 && (now - ta->lastClickMs) <= 500 && near) {
        ta->clickCount = ta->clickCount + 1;
        if (ta->clickCount > 3) ta->clickCount = 3;
    } else {
        ta->clickCount = 1;
    }
    ta->lastClickMs  = now;
    ta->lastClickPos = pos;

    if (ta->clickCount == 1) {
        ta->caretPos       = pos;
        ta->selAnchor      = pos;
        ta->mouseSelecting = 1;
    } else if (ta->clickCount == 2) {
        int ws, we;
        WordAround(ta, pos, &ws, &we);
        ta->selAnchor      = ws;
        ta->caretPos       = we;
        ta->mouseSelecting = 0;
    } else {
        // Triple-click: select the entire current line.
        int line, col; (void)col;
        LineFromPos(ta, pos, &line, &col);
        if (ta->linesLen > 0) {
            ta->selAnchor = ta->lineStarts[line];
            ta->caretPos  = ta->lineStarts[line] + ta->lineLengths[line];
        }
        ta->mouseSelecting = 0;
    }
}

void UITextArea_DispatchMouseMotion(UIChildren* children, float x, float y) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        UITextArea* ta = AsTextArea(w);
        if (!ta || !ta->mouseSelecting) continue;
        const float lx = x - (w->x + ta->paddingLeft);
        const float ly = y - (w->y + ta->paddingTop) + ta->scrollY;
        int pos = CaretFromLocalXY(ta, lx, ly);
        if (pos < 0) pos = 0;
        if (pos > ta->textLen) pos = ta->textLen;
        if (pos != ta->caretPos) ta->caretPos = pos;
    }
}

void UITextArea_DispatchMouseUp(UIChildren* children, float x, float y, int button) {
    (void)x; (void)y;
    if (!children || button != SDL_BUTTON_LEFT) return;
    for (int i = 0; i < children->count; i++) {
        UITextArea* ta = AsTextArea(children->children[i]);
        if (!ta) continue;
        ta->mouseSelecting = 0;
    }
}

void UITextArea_DispatchMouseWheel(UIChildren* children, float x, float y, float dy) {
    if (!children) return;
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w = children->children[i];
        UITextArea* ta = AsTextArea(w);
        if (!ta) continue;
        if (!InsideWidget(w, x, y)) continue;
        ta->scrollY -= dy * (ta->fontSize * ta->lineSpacing) * 2.0f;
        if (ta->scrollY < 0.0f) ta->scrollY = 0.0f;
        return; // first hit captures
    }
}

void UITextArea_DispatchTextInput(UIChildren* children, const char* text) {
    if (!children || !text || !*text) return;
    for (int i = 0; i < children->count; i++) {
        UITextArea* ta = AsTextArea(children->children[i]);
        if (!ta || !ta->focused) continue;
        InsertChars(ta, text, (int)strlen(text));
        return;
    }
}

void UITextArea_DispatchKeyDown(UIChildren* children, SDL_Window* win,
                                SDL_Scancode key, Uint16 mod) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UITextArea* ta = AsTextArea(children->children[i]);
        if (!ta || !ta->focused) continue;

        const int ctrl  = (mod & SDL_KMOD_CTRL)  != 0;
        const int shift = (mod & SDL_KMOD_SHIFT) != 0;

        const int isMove = (key == SDL_SCANCODE_LEFT  || key == SDL_SCANCODE_RIGHT ||
                            key == SDL_SCANCODE_UP    || key == SDL_SCANCODE_DOWN  ||
                            key == SDL_SCANCODE_HOME  || key == SDL_SCANCODE_END);
        if (isMove && shift) {
            if (ta->selAnchor < 0 || ta->selAnchor == ta->caretPos) {
                ta->selAnchor = ta->caretPos;
            }
        }

        switch (key) {
            case SDL_SCANCODE_BACKSPACE: DeleteBefore(ta); break;
            case SDL_SCANCODE_DELETE:    DeleteAfter(ta);  break;

            case SDL_SCANCODE_LEFT:
                if (!shift && HasSelection(ta)) {
                    int s, e; SelectionRange(ta, &s, &e); (void)e;
                    ta->caretPos = s;
                } else if (ta->caretPos > 0) {
                    ta->caretPos--;
                }
                if (!shift) CollapseSelection(ta);
                break;
            case SDL_SCANCODE_RIGHT:
                if (!shift && HasSelection(ta)) {
                    int s, e; SelectionRange(ta, &s, &e); (void)s;
                    ta->caretPos = e;
                } else if (ta->caretPos < ta->textLen) {
                    ta->caretPos++;
                }
                if (!shift) CollapseSelection(ta);
                break;
            case SDL_SCANCODE_UP: {
                int line, col;
                LineFromPos(ta, ta->caretPos, &line, &col);
                if (line > 0) {
                    // Preserve approximate x by using ColumnPixelX and
                    // then mapping back via CaretFromLocalXY of the
                    // previous line. Simpler: same byte column, clamped.
                    const int newCol = col;
                    ta->caretPos = PosFromLineCol(ta, line - 1, newCol);
                }
                if (!shift) CollapseSelection(ta);
                break;
            }
            case SDL_SCANCODE_DOWN: {
                int line, col;
                LineFromPos(ta, ta->caretPos, &line, &col);
                if (line < ta->linesLen - 1) {
                    const int newCol = col;
                    ta->caretPos = PosFromLineCol(ta, line + 1, newCol);
                }
                if (!shift) CollapseSelection(ta);
                break;
            }
            case SDL_SCANCODE_HOME: {
                int line, col; (void)col;
                LineFromPos(ta, ta->caretPos, &line, &col);
                if (ta->linesLen > 0) ta->caretPos = ta->lineStarts[line];
                if (!shift) CollapseSelection(ta);
                break;
            }
            case SDL_SCANCODE_END: {
                int line, col; (void)col;
                LineFromPos(ta, ta->caretPos, &line, &col);
                if (ta->linesLen > 0) {
                    ta->caretPos = ta->lineStarts[line] + ta->lineLengths[line];
                }
                if (!shift) CollapseSelection(ta);
                break;
            }

            case SDL_SCANCODE_RETURN:
            case SDL_SCANCODE_KP_ENTER:
                InsertChars(ta, "\n", 1);
                break;

            case SDL_SCANCODE_TAB:
                InsertChars(ta, "    ", 4); // soft tab
                break;

            case SDL_SCANCODE_ESCAPE:
                SetFocused(ta, win, 0);
                break;

            case SDL_SCANCODE_V:
                if (ctrl) {
                    char* clip = SDL_GetClipboardText();
                    if (clip && *clip) InsertChars(ta, clip, (int)strlen(clip));
                    if (clip) SDL_free(clip);
                }
                break;
            case SDL_SCANCODE_C:
                if (ctrl) {
                    char* sel = CopySelectedText(ta);
                    if (sel) { SDL_SetClipboardText(sel); free(sel); }
                    else if (ta->textLen > 0) SDL_SetClipboardText(ta->text);
                }
                break;
            case SDL_SCANCODE_X:
                if (ctrl) {
                    char* sel = CopySelectedText(ta);
                    if (sel) {
                        SDL_SetClipboardText(sel); free(sel);
                        DeleteSelection(ta);
                    }
                }
                break;
            case SDL_SCANCODE_A:
                if (ctrl) {
                    ta->selAnchor = 0;
                    ta->caretPos  = ta->textLen;
                }
                break;
            default:
                break;
        }
        return;
    }
}

// ---------------------------------------------------------------------
// Programmatic focus
// ---------------------------------------------------------------------

UITextArea* UITextArea_SetFocus(UITextArea* ta, int focused) {
    if (!ta) return NULL;
    focused = focused ? 1 : 0;

    UIWidget* w = UIWidget_FindByData(ta);
    if (w) {
        UIWidget_SetFocus(w, focused);
        return ta;
    }

    UIWindow*   win  = UIWindow_GetActive();
    SDL_Window* sdlw = win ? win->sdlWindow : NULL;
    if (focused) UIKitFocus_BlurOthers(NULL, ta, NULL);
    SetFocused(ta, sdlw, focused);
    return ta;
}

int UITextArea_IsFocused(const UITextArea* ta) {
    return ta ? ta->focused : 0;
}
