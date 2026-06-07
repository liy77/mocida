#include <uikit/textarea.h>
#include <uikit/textfield.h>
#include <uikit/window.h>
#include <uikit/font.h>
#include <uikit/stack.h>
#include <uikit/glass.h>
#include <uikit/container.h>
#include <uikit/rect.h>
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

// Defined further down; used by the recursive unfocus helper below.
static void SetFocused(UITextArea* ta, SDL_Window* win, int focused);

// If `w` is a layout container (Stack / Grid / Rectangle / Scroll content),
// return its child collection so dispatch can recurse into a nested TextArea.
// Without this, a TextArea inside any container — the common case (e.g. an editor
// pane deep in a Stack tree) — never receives focus, text, keys or wheel.
static UIChildren* TA_ContainerChildren(UIWidget* w) {
    if (!w || !w->data) return NULL;
    UIWidgetBase* base = (UIWidgetBase*)w->data;
    const char* t = base->__widget_type;
    if (strcmp(t, UI_WIDGET_STACK) == 0)     return ((UIStack*)base)->items;
    if (strcmp(t, UI_WIDGET_GLASS) == 0)     return ((UIGlass*)base)->items;
    if (strcmp(t, UI_WIDGET_GRID) == 0)      return ((UIGrid*)base)->items;
    if (strcmp(t, UI_WIDGET_RECTANGLE) == 0) return (UIChildren*)((UIRectangle*)base)->children;
    if (strcmp(t, UI_WIDGET_SCROLL) == 0) {
        UIWidget* content = ((UIScroll*)base)->content;
        return content ? TA_ContainerChildren(content) : NULL;
    }
    return NULL;
}

// Recursively find the topmost TextArea under (x, y). Returns 1 and fills the
// out-params on hit.
static int TA_FindHit(UIChildren* children, float x, float y,
                      UITextArea** outTa, UIWidget** outW) {
    if (!children) return 0;
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w = children->children[i];
        UITextArea* ta = AsTextArea(w);
        if (ta) {
            if (InsideWidget(w, x, y) && UIWidget_EventOcclusionAllows(w)) {
                *outTa = ta; *outW = w; return 1;
            }
        } else {
            UIChildren* kids = TA_ContainerChildren(w);
            if (kids && TA_FindHit(kids, x, y, outTa, outW)) return 1;
        }
    }
    return 0;
}

// Recursively drop focus on every TextArea except `keep` (window-scoped text
// input is toggled per area). Forward-declared via its use below.
static void TA_UnfocusExcept(UIChildren* children, SDL_Window* win, UITextArea* keep) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        UITextArea* ta = AsTextArea(w);
        if (ta) {
            if (ta == keep) continue;
            SetFocused(ta, win, 0);
            ta->mouseSelecting = 0;
            ta->lastClickMs    = 0;
            ta->lastClickPos   = -1;
            ta->clickCount     = 0;
        } else {
            UIChildren* kids = TA_ContainerChildren(w);
            if (kids) TA_UnfocusExcept(kids, win, keep);
        }
    }
}

// The focused TextArea anywhere in the tree (recursive), or NULL.
static UITextArea* TA_Focused(UIChildren* children) {
    if (!children) return NULL;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        UITextArea* ta = AsTextArea(w);
        if (ta) {
            if (ta->focused) return ta;
        } else {
            UIChildren* kids = TA_ContainerChildren(w);
            if (kids) { UITextArea* f = TA_Focused(kids); if (f) return f; }
        }
    }
    return NULL;
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
    free(ta->__hlSpans);          ta->__hlSpans = NULL;
    ta->__hlSpanCount = 0;
    ta->__builtLo = 0;
    ta->__builtHi = 0;
    ta->linesLen = 0;
    ta->linesCap = 0;
    ta->__cachedTextLen  = -1;
    ta->__cachedWrapMode = -1;
    ta->__cachedWrapW    = -1;
}

// Defined further down with the rest of the undo machinery; declared here so
// UITextArea_SetText (above the definition) can drop history on a fresh load.
static void HistoryReset(UITextArea* ta);

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
    ta->caretPos  = 0;            // open at the top, not the end of the content
    ta->maxLength = -1;

    ta->fontSize          = fontSize > 0.0f ? fontSize : 16.0f;
    {
        // Default to the app font (like UIText_Create) so text renders.
        const char* defFont = UIGetDefaultFontPath();
        ta->fontFamily    = defFont ? _strdup(defFont) : NULL;
    }
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
    ta->onSave            = NULL;
    ta->saveUd            = NULL;
    ta->highlighter       = NULL;
    ta->highlighterUd     = NULL;
    ta->__hlVersion       = 0;
    ta->__cachedHlVersion = -1;
    ta->__lastCaretPos    = -1;  // first render follows the caret (shows top)
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
    ta->__gapAt = NULL; ta->__gapW = NULL; ta->__gapCount = 0; ta->__gapCap = 0;
    ta->__gapVersion = 0; ta->__cachedGapVersion = -1;
    ta->__swatchColor = NULL; ta->__swatchCount = 0; ta->__swatchCap = 0;
    ta->__swatchRX = NULL; ta->__swatchRY = NULL; ta->__swatchRW = NULL; ta->__swatchRH = NULL;
    ta->__swatchByte = NULL; ta->__swatchRectCount = 0; ta->__swatchRectCap = 0;
    ta->onSwatchClick = NULL; ta->onSwatchClickUd = NULL;
    ta->__hlSpans = NULL; ta->__hlSpanCount = 0;
    ta->__builtLo = 0; ta->__builtHi = 0;
    return ta;
}

// Swatch geometry derived from the font size (so it scales with zoom).
static float TA_SwatchBox(const UITextArea* ta)    { return ta->fontSize * 0.86f; }
static float TA_SwatchMargin(const UITextArea* ta) { return ta->fontSize * 0.15f; }
static float TA_SwatchGap(const UITextArea* ta)    { return TA_SwatchBox(ta) + 2.0f * TA_SwatchMargin(ta); }

void UITextArea_SetColorSwatches(UITextArea* ta, const int* offsets, const UIColor* colors, int count) {
    if (!ta) return;
    if (count < 0) count = 0;
    // Reserve a gap of TA_SwatchGap before each swatch (so the box has room).
    const float gw = TA_SwatchGap(ta);
    // Does the GAP layout (count / offsets / width) change? Colors alone don't —
    // the swatch draw reads __swatchColor live each render — so a color-only update
    // must NOT force a line-cache rebuild (that flashes the whole editor while you
    // drag the picker). Only re-fold + re-split the line cache when gaps move.
    int gaps_changed = (count != ta->__gapCount);
    if (!gaps_changed) {
        for (int i = 0; i < count; i++) {
            if (ta->__gapAt[i] != (offsets ? offsets[i] : 0) || ta->__gapW[i] != gw) {
                gaps_changed = 1;
                break;
            }
        }
    }
    if (count > ta->__gapCap) {
        int*   na = (int*)realloc(ta->__gapAt, (size_t)count * sizeof(int));
        float* nw = (float*)realloc(ta->__gapW, (size_t)count * sizeof(float));
        if (na) ta->__gapAt = na;
        if (nw) ta->__gapW = nw;
        if (!na || !nw) return;
        ta->__gapCap = count;
    }
    if (count > ta->__swatchCap) {
        UIColor* nc = (UIColor*)realloc(ta->__swatchColor, (size_t)count * sizeof(UIColor));
        if (!nc) return;
        ta->__swatchColor = nc;
        ta->__swatchCap = count;
    }
    for (int i = 0; i < count; i++) {
        ta->__gapAt[i]      = offsets ? offsets[i] : 0;
        ta->__gapW[i]       = gw;
        ta->__swatchColor[i]= colors ? colors[i] : (UIColor){0,0,0,1.0f};
    }
    ta->__gapCount = count;
    ta->__swatchCount = count;
    if (gaps_changed) {
        ta->__gapVersion++;
        ta->__cachedTextLen = -1; // force the offset fold + texture split to rebuild
    }
}

UITextArea* UITextArea_SetOnSwatchClick(UITextArea* ta, UISwatchClickFn cb, void* ud) {
    if (ta) { ta->onSwatchClick = cb; ta->onSwatchClickUd = ud; }
    return ta;
}

void UITextArea_SetInlineGaps(UITextArea* ta, const int* offsets, const float* widths, int count) {
    if (!ta) return;
    if (count < 0) count = 0;
    if (count > ta->__gapCap) {
        int nc = count;
        int*   na = (int*)realloc(ta->__gapAt, (size_t)nc * sizeof(int));
        float* nw = (float*)realloc(ta->__gapW, (size_t)nc * sizeof(float));
        if (na) ta->__gapAt = na;
        if (nw) ta->__gapW = nw;
        if (!na || !nw) return;
        ta->__gapCap = nc;
    }
    for (int i = 0; i < count; i++) {
        ta->__gapAt[i] = offsets ? offsets[i] : 0;
        ta->__gapW[i]  = widths  ? widths[i]  : 0.0f;
    }
    ta->__gapCount = count;
    ta->__gapVersion++;     // force the line cache (offsets + texture split) to rebuild
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
    ta->caretPos  = 0;      // a fresh load shows the top of the file
    ta->scrollY   = 0.0f;
    ta->__lastCaretPos = 0; // caret didn't "move" — keep us pinned at the top
    ta->selAnchor = -1;
    HistoryReset(ta);       // a fresh document — discard the old undo history
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
UITextArea* UITextArea_SetHighlighter(UITextArea* ta, UITextAreaHighlighter fn, void* ud) {
    if (!ta) return ta;
    ta->highlighter   = fn;
    ta->highlighterUd = ud;
    ta->__hlVersion++;       // force the line cache (and its colors) to rebuild
    InvalidateLineCache(ta);
    return ta;
}
UITextArea* UITextArea_OnSave(UITextArea* ta, UITextAreaSaveCallback fn, void* ud) {
    if (!ta) return ta;
    ta->onSave = fn;
    ta->saveUd = ud;
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

UITextArea* UITextArea_SetShowLineNumbers(UITextArea* ta, int show) {
    if (!ta) return ta;
    show = show ? 1 : 0;
    if (ta->showLineNumbers != show) {
        ta->showLineNumbers = show;
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
    HistoryReset(ta);          // free the undo/redo snapshots
    free(ta->text);
    free(ta->placeholder);
    free(ta->fontFamily);
    InvalidateLineCache(ta);
    free(ta->__gapAt); free(ta->__gapW);
    free(ta->__swatchColor);
    free(ta->__swatchRX); free(ta->__swatchRY); free(ta->__swatchRW); free(ta->__swatchRH);
    free(ta->__swatchByte);
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

// ── Completion-popup nav-lock ────────────────────────────────────────
// While an autocomplete popup is open the host turns this on so the editor
// TextArea swallows Up/Down/Enter/Tab/Esc (instead of moving the caret /
// inserting) and records which nav key was pressed; the host polls it via
// UITextArea_TakeCompletionNavKey to drive the popup selection.
static int g_taNavLock = 0;
static int g_taNavKey  = 0;  // 0 none, 1 Up, 2 Down, 3 Enter, 4 Tab, 5 Esc
void UITextArea_SetCompletionNavLock(int on) { g_taNavLock = on; if (!on) g_taNavKey = 0; }
int  UITextArea_TakeCompletionNavKey(void)   { int k = g_taNavKey; g_taNavKey = 0; return k; }

// ── Bracket auto-close (VSCode-style) ────────────────────────────────
static void InsertChars(UITextArea* ta, const char* chars, int n);  // defined below
static char CloserFor(char c) { return c == '(' ? ')' : c == '{' ? '}' : c == '[' ? ']' : 0; }
static int  IsOpener (char c) { return c == '(' || c == '{' || c == '['; }
// Insert an open/close pair and leave the caret between them.
static void InsertBracketPair(UITextArea* ta, char open, char close) {
    char buf[2] = { open, close };
    InsertChars(ta, buf, 2);   // caretPos now sits AFTER close
    ta->caretPos--;            // step back so the caret is between ( | )
    CollapseSelection(ta);
}

// ---------------------------------------------------------------------
// Undo / redo
//
// History is a stack of full-text snapshots (heap copies) + the caret
// position at each point. Edits record a PRE-edit checkpoint; Ctrl+Z pops it
// (pushing the current state onto the redo stack) and Ctrl+Y / Ctrl+Shift+Z
// reverses that. A run of same-kind, caret-contiguous edits (e.g. typing a
// word, or holding Backspace) coalesces into one step.
// ---------------------------------------------------------------------

#define UI_TA_UNDO_MAX 256

static void HistoryFree(char*** stack, int** carets, int* len, int* cap) {
    if (*stack) { for (int i = 0; i < *len; i++) free((*stack)[i]); free(*stack); }
    free(*carets);
    *stack = NULL; *carets = NULL; *len = 0; *cap = 0;
}

static void HistoryPush(char*** stack, int** carets, int* len, int* cap,
                        const char* text, int caret) {
    if (*len >= *cap) {
        int nc = *cap ? *cap * 2 : 32;
        *stack  = (char**)realloc(*stack,  (size_t)nc * sizeof(char*));
        *carets = (int*)  realloc(*carets, (size_t)nc * sizeof(int));
        *cap = nc;
    }
    (*stack)[*len]  = _strdup(text ? text : "");
    (*carets)[*len] = caret;
    (*len)++;
    if (*len > UI_TA_UNDO_MAX) {        // cap depth: drop the oldest snapshot
        free((*stack)[0]);
        memmove(&(*stack)[0],  &(*stack)[1],  (size_t)(*len - 1) * sizeof(char*));
        memmove(&(*carets)[0], &(*carets)[1], (size_t)(*len - 1) * sizeof(int));
        (*len)--;
    }
}

// Drop the whole undo + redo history (a fresh document was loaded).
static void HistoryReset(UITextArea* ta) {
    HistoryFree(&ta->undoText, &ta->undoCaret, &ta->undoLen, &ta->undoCap);
    HistoryFree(&ta->redoText, &ta->redoCaret, &ta->redoLen, &ta->redoCap);
    ta->__lastEditKind  = 0;
    ta->__lastEditCaret = -1;
}

// Record a PRE-edit checkpoint, coalescing contiguous same-kind runs.
// `kind`: 1 = insert, 2 = delete. `boundary` forces a new step (word break,
// newline, multi-char paste, selection replace). Clears the redo stack on a
// genuine new edit.
static void RecordUndo(UITextArea* ta, int kind, int boundary) {
    if (!ta || ta->__suppressHistory) return;
    const int contiguous = !boundary && kind == ta->__lastEditKind &&
                           ta->caretPos == ta->__lastEditCaret;
    if (contiguous) return;
    HistoryPush(&ta->undoText, &ta->undoCaret, &ta->undoLen, &ta->undoCap,
                ta->text, ta->caretPos);
    HistoryFree(&ta->redoText, &ta->redoCaret, &ta->redoLen, &ta->redoCap);
}

// Replace the buffer with `text` and place the caret, without recording
// history (used by undo/redo themselves). Fires onChange so the host binding
// stays in sync.
static void ApplyHistoryText(UITextArea* ta, const char* text, int caret) {
    const int len = text ? (int)strlen(text) : 0;
    if (!EnsureTextCapacity(ta, len + 1)) return;
    if (text) memcpy(ta->text, text, (size_t)len);
    ta->text[len] = '\0';
    ta->textLen   = len;
    ta->caretPos  = caret < 0 ? 0 : (caret > len ? len : caret);
    ta->selAnchor = -1;
    InvalidateLineCache(ta);
    if (ta->onChange) ta->onChange(ta, ta->text, ta->userdata);
}

static void DoUndo(UITextArea* ta) {
    if (!ta || ta->undoLen <= 0) return;
    HistoryPush(&ta->redoText, &ta->redoCaret, &ta->redoLen, &ta->redoCap,
                ta->text, ta->caretPos);
    ta->undoLen--;
    char* t = ta->undoText[ta->undoLen]; ta->undoText[ta->undoLen] = NULL;
    const int c = ta->undoCaret[ta->undoLen];
    ta->__suppressHistory = 1;
    ApplyHistoryText(ta, t, c);
    ta->__suppressHistory = 0;
    free(t);
    ta->__lastEditKind = 0;   // a later edit starts a fresh run
}

static void DoRedo(UITextArea* ta) {
    if (!ta || ta->redoLen <= 0) return;
    HistoryPush(&ta->undoText, &ta->undoCaret, &ta->undoLen, &ta->undoCap,
                ta->text, ta->caretPos);
    ta->redoLen--;
    char* t = ta->redoText[ta->redoLen]; ta->redoText[ta->redoLen] = NULL;
    const int c = ta->redoCaret[ta->redoLen];
    ta->__suppressHistory = 1;
    ApplyHistoryText(ta, t, c);
    ta->__suppressHistory = 0;
    free(t);
    ta->__lastEditKind = 0;
}

static void DeleteSelection(UITextArea* ta) {
    if (!HasSelection(ta)) return;
    int s, e;
    SelectionRange(ta, &s, &e);
    if (s < 0) s = 0;
    if (e > ta->textLen) e = ta->textLen;
    if (e <= s) { ta->selAnchor = ta->caretPos; return; }
    RecordUndo(ta, 2, 1);   // replacing/deleting a selection is its own step
    const int n = e - s;
    memmove(ta->text + s, ta->text + e, (size_t)(ta->textLen - e + 1));
    ta->textLen  -= n;
    ta->caretPos  = s;
    ta->selAnchor = s;
    InvalidateLineCache(ta);
    ta->__lastEditKind = 2; ta->__lastEditCaret = ta->caretPos;
    if (ta->onChange) ta->onChange(ta, ta->text, ta->userdata);
}

// ---------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------

static void InsertChars(UITextArea* ta, const char* chars, int n) {
    if (!ta || !chars || n <= 0) return;
    ClampCaretAndSelection(ta);
    if (HasSelection(ta)) {
        DeleteSelection(ta);   // records the pre-edit checkpoint for the replace
    } else {
        // Break the undo run on whitespace / newline / multi-char paste so a
        // word, not the whole paragraph, is one undo step.
        const int boundary = (n != 1) || chars[0] == '\n' ||
                             chars[0] == ' ' || chars[0] == '\t';
        RecordUndo(ta, 1, boundary);
    }
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
    ta->__lastEditKind = 1; ta->__lastEditCaret = ta->caretPos;
    if (ta->onChange) ta->onChange(ta, ta->text, ta->userdata);
}

static void DeleteBefore(UITextArea* ta) {
    if (!ta) return;
    ClampCaretAndSelection(ta);
    if (HasSelection(ta)) { DeleteSelection(ta); return; }
    if (ta->caretPos <= 0) return;
    RecordUndo(ta, 2, 0);
    memmove(ta->text + ta->caretPos - 1,
            ta->text + ta->caretPos,
            (size_t)(ta->textLen - ta->caretPos + 1));
    ta->caretPos--;
    ta->textLen--;
    CollapseSelection(ta);
    InvalidateLineCache(ta);
    ta->__lastEditKind = 2; ta->__lastEditCaret = ta->caretPos;
    if (ta->onChange) ta->onChange(ta, ta->text, ta->userdata);
}

static void DeleteAfter(UITextArea* ta) {
    if (!ta) return;
    ClampCaretAndSelection(ta);
    if (HasSelection(ta)) { DeleteSelection(ta); return; }
    if (ta->caretPos >= ta->textLen) return;
    RecordUndo(ta, 2, 0);
    memmove(ta->text + ta->caretPos,
            ta->text + ta->caretPos + 1,
            (size_t)(ta->textLen - ta->caretPos));
    ta->textLen--;
    CollapseSelection(ta);
    InvalidateLineCache(ta);
    ta->__lastEditKind = 2; ta->__lastEditCaret = ta->caretPos;
    if (ta->onChange) ta->onChange(ta, ta->text, ta->userdata);
}

// ---------------------------------------------------------------------
// Autocomplete / programmatic-edit support (host-driven completion).
// ---------------------------------------------------------------------

int UITextArea_GetCaretByte(const UITextArea* ta) {
    return ta ? ta->caretPos : 0;
}

void UITextArea_SetCaretByte(UITextArea* ta, int pos) {
    if (!ta) return;
    if (pos < 0) pos = 0;
    if (pos > ta->textLen) pos = ta->textLen;
    ta->caretPos = pos;
    ta->selAnchor = -1;     // collapse any selection to the caret
    ClampCaretAndSelection(ta);
}

int UITextArea_GetSelAnchor(const UITextArea* ta) {
    return ta ? ta->selAnchor : -1;
}

void UITextArea_SetSelAnchor(UITextArea* ta, int anchor) {
    if (!ta) return;
    // Call AFTER SetCaretByte (which collapses the selection to the caret), so the
    // anchor sticks. -1 = no selection; otherwise clamp into the buffer.
    if (anchor >= 0) {
        if (anchor > ta->textLen) anchor = ta->textLen;
    } else {
        anchor = -1;
    }
    ta->selAnchor = anchor;
    ClampCaretAndSelection(ta);
}

float UITextArea_GetScrollY(const UITextArea* ta) {
    return ta ? ta->scrollY : 0.0f;
}

void UITextArea_SetScrollY(UITextArea* ta, float y) {
    if (!ta) return;
    ta->scrollY = y < 0.0f ? 0.0f : y;
}

void UITextArea_InsertText(UITextArea* ta, const char* s) {
    if (!ta || !s) return;
    InsertChars(ta, s, (int)strlen(s));
}

void UITextArea_ReplaceBeforeCaret(UITextArea* ta, int n, const char* s) {
    if (!ta) return;
    ClampCaretAndSelection(ta);
    for (int i = 0; i < n && ta->caretPos > 0; i++) DeleteBefore(ta);
    if (s && *s) InsertChars(ta, s, (int)strlen(s));
}

void UITextArea_GetCaretScreenPos(UITextArea* ta, float* ox, float* oy) {
    if (ox) *ox = 0.0f;
    if (oy) *oy = 0.0f;
    if (!ta) return;
    const float lineH = ta->fontSize * ta->lineSpacing;
    int pos = ta->caretPos;
    if (pos < 0) pos = 0;
    if (pos > ta->textLen) pos = ta->textLen;
    int caretLine = 0;
    if (ta->lineStarts && ta->linesLen > 0) {
        int lo = 0, hi = ta->linesLen - 1;
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            if (ta->lineStarts[mid] <= pos) lo = mid; else hi = mid - 1;
        }
        caretLine = lo;
    }
    float cx = 0.0f;
    if (ta->lineCharOffsets && ta->lineStarts && ta->lineCharOffsetsLen &&
        caretLine < ta->linesLen && ta->lineCharOffsets[caretLine]) {
        int col = pos - ta->lineStarts[caretLine];
        if (col >= 0 && col < ta->lineCharOffsetsLen[caretLine]) {
            cx = (float)ta->lineCharOffsets[caretLine][col];
        }
    }
    if (ox) *ox = ta->__lastX + ta->paddingLeft + ta->__gutterW + cx;
    if (oy) *oy = ta->__lastY + ta->paddingTop
                  + (float)caretLine * lineH - ta->scrollY + lineH;
}

void UITextArea_GetByteScreenPos(const UITextArea* ta, int pos, float* ox, float* oy) {
    if (ox) *ox = 0.0f;
    if (oy) *oy = 0.0f;
    if (!ta) return;
    const float lineH = ta->fontSize * ta->lineSpacing;
    if (pos < 0) pos = 0;
    if (pos > ta->textLen) pos = ta->textLen;
    int line = 0;
    if (ta->lineStarts && ta->linesLen > 0) {
        int lo = 0, hi = ta->linesLen - 1;
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            if (ta->lineStarts[mid] <= pos) lo = mid; else hi = mid - 1;
        }
        line = lo;
    }
    float cx = 0.0f;
    if (ta->lineCharOffsets && ta->lineStarts && ta->lineCharOffsetsLen &&
        line < ta->linesLen && ta->lineCharOffsets[line]) {
        int col = pos - ta->lineStarts[line];
        if (col >= 0 && col < ta->lineCharOffsetsLen[line]) {
            cx = (float)ta->lineCharOffsets[line][col];
        }
    }
    if (ox) *ox = ta->__lastX + ta->paddingLeft + ta->__gutterW + cx;
    // Line TOP (caret version adds +lineH for the baseline-ish bottom).
    if (oy) *oy = ta->__lastY + ta->paddingTop + (float)line * lineH - ta->scrollY;
}

void UITextArea_GetContentOrigin(const UITextArea* ta, float* x, float* y) {
    if (x) *x = ta ? ta->__lastX : 0.0f;
    if (y) *y = ta ? ta->__lastY : 0.0f;
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

    // Recurse the whole tree so a TextArea nested in layout containers is found.
    UITextArea* hit  = NULL;
    UIWidget*   hitW = NULL;
    TA_FindHit(children, x, y, &hit, &hitW);

    // Unfocus every other TextArea BEFORE focusing the hit (SDL_StartTextInput
    // is window-scoped; the other order leaves text input off after a change).
    TA_UnfocusExcept(children, win, hit);

    if (!hit) return;

    UITextArea* ta = hit;

    // Color-swatch click: if the press landed on a swatch box, fire its callback
    // and consume the click (don't focus / move the caret) so the host can open a
    // picker. Rects were recorded in window space by the last render.
    if (ta->onSwatchClick && ta->__swatchRectCount > 0) {
        for (int i = 0; i < ta->__swatchRectCount; i++) {
            if (x >= ta->__swatchRX[i] && x < ta->__swatchRX[i] + ta->__swatchRW[i] &&
                y >= ta->__swatchRY[i] && y < ta->__swatchRY[i] + ta->__swatchRH[i]) {
                ta->onSwatchClick(ta, ta->__swatchByte[i], ta->onSwatchClickUd);
                return;
            }
        }
    }

    SetFocused(ta, win, 1);
    UIWidget_SetFocus(hitW, 1);

    const float lx = x - (hitW->x + ta->paddingLeft + ta->__gutterW);
    const float ly = y - (hitW->y + ta->paddingTop) + ta->scrollY;
    int pos = CaretFromLocalXY(ta, lx, ly);
    if (pos < 0) pos = 0;
    if (pos > ta->textLen) pos = ta->textLen;
    ta->__lastEditKind = 0;   // clicking ends the current undo run

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
        if (ta) {
            if (!ta->mouseSelecting) continue;
            const float lx = x - (w->x + ta->paddingLeft + ta->__gutterW);
            const float ly = y - (w->y + ta->paddingTop) + ta->scrollY;
            int pos = CaretFromLocalXY(ta, lx, ly);
            if (pos < 0) pos = 0;
            if (pos > ta->textLen) pos = ta->textLen;
            if (pos != ta->caretPos) ta->caretPos = pos;
        } else {
            UIChildren* kids = TA_ContainerChildren(w);
            if (kids) UITextArea_DispatchMouseMotion(kids, x, y);
        }
    }
}

void UITextArea_DispatchMouseUp(UIChildren* children, float x, float y, int button) {
    if (!children || button != SDL_BUTTON_LEFT) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        UITextArea* ta = AsTextArea(w);
        if (ta) {
            ta->mouseSelecting = 0;
        } else {
            UIChildren* kids = TA_ContainerChildren(w);
            if (kids) UITextArea_DispatchMouseUp(kids, x, y, button);
        }
    }
}

// First TextArea under (x, y) captures the wheel; recurses into containers.
static int TA_WheelRec(UIChildren* children, float x, float y, float dy) {
    if (!children) return 0;
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w = children->children[i];
        UITextArea* ta = AsTextArea(w);
        if (ta) {
            if (!InsideWidget(w, x, y)) continue;
            if (!UIWidget_EventOcclusionAllows(w)) continue; // hidden behind an overlay
            ta->scrollY -= dy * (ta->fontSize * ta->lineSpacing) * 2.0f;
            if (ta->scrollY < 0.0f) ta->scrollY = 0.0f;
            return 1;
        } else {
            UIChildren* kids = TA_ContainerChildren(w);
            if (kids && TA_WheelRec(kids, x, y, dy)) return 1;
        }
    }
    return 0;
}

void UITextArea_DispatchMouseWheel(UIChildren* children, float x, float y, float dy) {
    TA_WheelRec(children, x, y, dy);
}

void UITextArea_DispatchTextInput(UIChildren* children, const char* text) {
    if (!children || !text || !*text) return;
    // Ignore text generated while Ctrl is held without Alt. SDL can emit a
    // TEXTINPUT of " " for Ctrl+Space on Windows; without this guard a Ctrl+Space
    // shortcut (e.g. "open autocomplete") also inserts a stray space. Ctrl+Alt
    // (AltGr) is left alone so it still produces real characters.
    SDL_Keymod km = SDL_GetModState();
    if ((km & SDL_KMOD_CTRL) && !(km & SDL_KMOD_ALT)) return;
    UITextArea* ta = TA_Focused(children);
    if (!ta) return;
    // Bracket auto-close, typed-input only (never in InsertChars — paste / Tab /
    // Enter / completion-insert route through it and must stay literal). Gated to
    // a lone ASCII bracket with no active selection.
    if (text[0] && text[1] == '\0' && !HasSelection(ta)) {
        char c = text[0];
        if (IsOpener(c)) { InsertBracketPair(ta, c, CloserFor(c)); return; }
        // Step over an existing closer instead of inserting a duplicate.
        if ((c == ')' || c == '}' || c == ']') &&
            ta->caretPos < ta->textLen && ta->text[ta->caretPos] == c) {
            ta->caretPos++;
            CollapseSelection(ta);
            return;
        }
    }
    InsertChars(ta, text, (int)strlen(text));
}

void UITextArea_DispatchKeyDown(UIChildren* children, SDL_Window* win,
                                SDL_Scancode key, Uint16 mod) {
    if (!children) return;
    // The focused TextArea may be nested in containers — search recursively.
    UITextArea* ta = TA_Focused(children);
    if (!ta) return;
    {
        const int ctrl  = (mod & SDL_KMOD_CTRL)  != 0;
        const int shift = (mod & SDL_KMOD_SHIFT) != 0;

        // Completion-popup nav-lock: swallow the nav keys so the caret never
        // moves (the host polls the swallowed key to drive popup selection).
        // Other keys (typing) fall through and refine the popup as usual.
        if (g_taNavLock) {
            switch (key) {
                case SDL_SCANCODE_UP:        g_taNavKey = 1; return;
                case SDL_SCANCODE_DOWN:      g_taNavKey = 2; return;
                case SDL_SCANCODE_RETURN:
                case SDL_SCANCODE_KP_ENTER:  g_taNavKey = 3; return;
                case SDL_SCANCODE_TAB:       g_taNavKey = 4; return;
                case SDL_SCANCODE_ESCAPE:    g_taNavKey = 5; return;
                default: break;
            }
        }

        const int isMove = (key == SDL_SCANCODE_LEFT  || key == SDL_SCANCODE_RIGHT ||
                            key == SDL_SCANCODE_UP    || key == SDL_SCANCODE_DOWN  ||
                            key == SDL_SCANCODE_HOME  || key == SDL_SCANCODE_END);
        if (isMove && shift) {
            if (ta->selAnchor < 0 || ta->selAnchor == ta->caretPos) {
                ta->selAnchor = ta->caretPos;
            }
        }

        switch (key) {
            case SDL_SCANCODE_BACKSPACE:
                // Auto-pair: backspace inside an empty bracket pair "(|)" removes
                // both sides.
                if (!HasSelection(ta) && ta->caretPos > 0 && ta->caretPos < ta->textLen) {
                    char l = ta->text[ta->caretPos - 1];
                    char r = ta->text[ta->caretPos];
                    if (IsOpener(l) && CloserFor(l) == r) {
                        DeleteAfter(ta);   // closer to the right
                        DeleteBefore(ta);  // opener to the left
                        break;
                    }
                }
                DeleteBefore(ta);
                break;
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
            case SDL_SCANCODE_S:
                if (ctrl && ta->onSave) ta->onSave(ta, ta->saveUd);
                break;
            case SDL_SCANCODE_Z:
                if (ctrl) { if (shift) DoRedo(ta); else DoUndo(ta); }
                break;
            case SDL_SCANCODE_Y:
                if (ctrl) DoRedo(ta);
                break;
            default:
                break;
        }
        // Moving the caret ends the current typing/deleting run, so the next
        // edit starts a fresh undo step.
        if (isMove) ta->__lastEditKind = 0;
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

// ---------------------------------------------------------------------
// Edit commands (programmatic equivalents of the Ctrl+Z/Y/X/C/V/A key
// handlers) — used by the host's Edit menu, acting on the focused TextArea.
// ---------------------------------------------------------------------
UITextArea* UITextArea_Undo(UITextArea* ta) { if (ta) DoUndo(ta); return ta; }
UITextArea* UITextArea_Redo(UITextArea* ta) { if (ta) DoRedo(ta); return ta; }

UITextArea* UITextArea_Copy(UITextArea* ta) {
    if (!ta) return ta;
    char* sel = CopySelectedText(ta);
    if (sel) { SDL_SetClipboardText(sel); free(sel); }
    else if (ta->textLen > 0) SDL_SetClipboardText(ta->text);
    return ta;
}
UITextArea* UITextArea_Cut(UITextArea* ta) {
    if (!ta) return ta;
    char* sel = CopySelectedText(ta);
    if (sel) { SDL_SetClipboardText(sel); free(sel); DeleteSelection(ta); }
    return ta;
}
UITextArea* UITextArea_Paste(UITextArea* ta) {
    if (!ta) return ta;
    char* clip = SDL_GetClipboardText();
    if (clip && *clip) InsertChars(ta, clip, (int)strlen(clip));
    if (clip) SDL_free(clip);
    return ta;
}
UITextArea* UITextArea_SelectAll(UITextArea* ta) {
    if (!ta) return ta;
    ta->selAnchor = 0;
    ta->caretPos  = ta->textLen;
    return ta;
}
