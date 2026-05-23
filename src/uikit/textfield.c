#include <uikit/textfield.h>
#include <uikit/textarea.h>
#include <uikit/text.h>
#include <uikit/window.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------

static int InsideWidget(const UIWidget* w, float x, float y) {
    if (!w || !w->visible || !w->width || !w->height) return 0;
    const float ww = *w->width;
    const float hh = *w->height;
    return (x >= w->x && x < w->x + ww && y >= w->y && y < w->y + hh);
}

static UITextField* AsTextField(UIWidget* w) {
    if (!w || !w->data) return NULL;
    UIWidgetBase* b = (UIWidgetBase*)w->data;
    if (strcmp(b->__widget_type, UI_WIDGET_TEXTFIELD) != 0) return NULL;
    return (UITextField*)b;
}

// Ensures the text buffer can hold at least `needCap` bytes (including
// the terminator). Grows by doubling for amortised O(1) inserts.
static int EnsureTextCapacity(UITextField* tf, int needCap) {
    if (!tf) return 0;
    if (tf->textCapacity >= needCap) return 1;
    int newCap = tf->textCapacity ? tf->textCapacity : 32;
    while (newCap < needCap) newCap *= 2;
    char* p = (char*)realloc(tf->text, (size_t)newCap);
    if (!p) return 0;
    tf->text = p;
    tf->textCapacity = newCap;
    return 1;
}

static void DropTextTexture(UITextField* tf) {
    if (tf->__SDL_textTexture) {
        SDL_DestroyTexture(tf->__SDL_textTexture);
        tf->__SDL_textTexture = NULL;
    }
    tf->__cachedTextLen = -1;
}

// ---------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------

UITextField* UITextField_Create(const char* initialText, float fontSize) {
    UITextField* tf = (UITextField*)calloc(1, sizeof(UITextField));
    if (!tf) return NULL;
    tf->__widget_type = UI_WIDGET_TEXTFIELD;

    const int initLen = initialText ? (int)strlen(initialText) : 0;
    if (!EnsureTextCapacity(tf, initLen + 16)) { free(tf); return NULL; }
    if (initialText) memcpy(tf->text, initialText, (size_t)initLen);
    tf->text[initLen] = '\0';
    tf->textLen   = initLen;
    tf->caretPos  = initLen;
    tf->maxLength = -1;

    tf->fontSize         = fontSize > 0.0f ? fontSize : 16.0f;
    tf->fontFamily       = NULL;
    tf->textColor        = (UIColor){ 15, 23, 42, 1.0f };
    tf->placeholderColor = (UIColor){ 148, 163, 184, 1.0f };
    tf->caretColor       = (UIColor){ 59, 130, 246, 1.0f };
    tf->bgColor          = (UIColor){ 255, 255, 255, 1.0f };
    tf->borderColor      = (UIColor){ 203, 213, 225, 1.0f };
    tf->borderColorFocused = (UIColor){ 59, 130, 246, 1.0f };
    tf->borderWidth      = 1.0f;
    tf->radius           = 6.0f;
    tf->paddingLeft      = 10.0f;
    tf->paddingRight     = 10.0f;
    tf->paddingTop       = 8.0f;
    tf->paddingBottom    = 8.0f;
    tf->__cachedTextLen  = -1;
    tf->selAnchor        = -1;
    tf->mouseSelecting   = 0;
    tf->selectionColor   = (UIColor){ 191, 219, 254, 1.0f }; // soft blue
    tf->__charOffsets    = NULL;
    tf->__charOffsetsLen = 0;
    tf->lastClickMs      = 0;
    tf->lastClickPos     = -1;
    tf->clickCount       = 0;
    tf->cursor           = UI_CURSOR_TEXT;
    tf->placeholderAnimated = 0;     // static placeholder by default
    tf->caretBlinkMs        = 530;   // classic Win32 blink cadence
    return tf;
}

UITextField* UITextField_SetCursor(UITextField* tf, UICursor cursor) {
    if (tf) tf->cursor = cursor;
    return tf;
}

UITextField* UITextField_SetText(UITextField* tf, const char* text) {
    if (!tf) return tf;
    const int len = text ? (int)strlen(text) : 0;
    if (!EnsureTextCapacity(tf, len + 1)) return tf;
    if (text) memcpy(tf->text, text, (size_t)len);
    tf->text[len] = '\0';
    tf->textLen   = len;
    tf->caretPos  = len;
    // The cached offsets and the selection are tied to the previous
    // text. Invalidate both so a stale entry can't push caretPos past
    // the new (possibly shorter) textLen.
    tf->selAnchor = -1;
    tf->__charOffsetsLen = 0;
    DropTextTexture(tf);
    if (tf->onChange) tf->onChange(tf, tf->text, tf->userdata);
    return tf;
}

const char* UITextField_GetText(UITextField* tf) {
    return tf ? tf->text : NULL;
}

UITextField* UITextField_SetPlaceholder(UITextField* tf, const char* placeholder) {
    if (!tf) return tf;
    free(tf->placeholder);
    tf->placeholder = placeholder ? _strdup(placeholder) : NULL;
    return tf;
}

UITextField* UITextField_SetPlaceholderAnimated(UITextField* tf, int yes) {
    if (!tf) return tf;
    tf->placeholderAnimated = yes ? 1 : 0;
    return tf;
}

UITextField* UITextField_SetCaretBlinkRate(UITextField* tf, int halfPeriodMs) {
    if (!tf) return tf;
    if (halfPeriodMs < 0) halfPeriodMs = 0;
    tf->caretBlinkMs = halfPeriodMs;
    return tf;
}

UITextField* UITextField_SetFontFamily(UITextField* tf, char* family) {
    if (!tf) return tf;
    free(tf->fontFamily);
    tf->fontFamily = family ? _strdup(family) : NULL;
    DropTextTexture(tf);
    return tf;
}

UITextField* UITextField_SetPassword(UITextField* tf, int yes) {
    if (!tf) return tf;
    tf->passwordMask = yes ? 1 : 0;
    DropTextTexture(tf);
    return tf;
}

UITextField* UITextField_SetSelectionColor(UITextField* tf, UIColor color) {
    if (!tf) return tf;
    tf->selectionColor = color;
    return tf;
}

UITextField* UITextField_SetFontSize(UITextField* tf, float size) {
    if (!tf || size <= 0.0f) return tf;
    if (tf->fontSize != size) {
        tf->fontSize = size;
        DropTextTexture(tf);
    }
    return tf;
}

UITextField* UITextField_SetBgColor(UITextField* tf, UIColor color) {
    if (tf) tf->bgColor = color;
    return tf;
}

UITextField* UITextField_SetTextColor(UITextField* tf, UIColor color) {
    if (!tf) return tf;
    tf->textColor = color;
    DropTextTexture(tf);
    return tf;
}

UITextField* UITextField_SetPlaceholderColor(UITextField* tf, UIColor color) {
    if (!tf) return tf;
    tf->placeholderColor = color;
    DropTextTexture(tf);
    return tf;
}

UITextField* UITextField_SetCaretColor(UITextField* tf, UIColor color) {
    if (tf) tf->caretColor = color;
    return tf;
}

UITextField* UITextField_SetBorder(UITextField* tf, UIColor normal, UIColor focused, float width) {
    if (!tf) return tf;
    tf->borderColor        = normal;
    tf->borderColorFocused = focused;
    tf->borderWidth        = (width < 0.0f) ? 0.0f : width;
    return tf;
}

UITextField* UITextField_SetBorderColor(UITextField* tf, UIColor color) {
    if (tf) tf->borderColor = color;
    return tf;
}

UITextField* UITextField_SetBorderColorFocused(UITextField* tf, UIColor color) {
    if (tf) tf->borderColorFocused = color;
    return tf;
}

UITextField* UITextField_SetBorderWidth(UITextField* tf, float width) {
    if (tf) tf->borderWidth = (width < 0.0f) ? 0.0f : width;
    return tf;
}

UITextField* UITextField_SetRadius(UITextField* tf, float radius) {
    if (tf) tf->radius = (radius < 0.0f) ? 0.0f : radius;
    return tf;
}

UITextField* UITextField_SetPadding(UITextField* tf, float x, float y) {
    if (!tf) return tf;
    if (x < 0.0f) x = 0.0f;
    if (y < 0.0f) y = 0.0f;
    tf->paddingLeft = tf->paddingRight = x;
    tf->paddingTop  = tf->paddingBottom = y;
    return tf;
}

UITextField* UITextField_SetPaddingLeft  (UITextField* tf, float v) { if (tf) tf->paddingLeft   = v < 0.0f ? 0.0f : v; return tf; }
UITextField* UITextField_SetPaddingRight (UITextField* tf, float v) { if (tf) tf->paddingRight  = v < 0.0f ? 0.0f : v; return tf; }
UITextField* UITextField_SetPaddingTop   (UITextField* tf, float v) { if (tf) tf->paddingTop    = v < 0.0f ? 0.0f : v; return tf; }
UITextField* UITextField_SetPaddingBottom(UITextField* tf, float v) { if (tf) tf->paddingBottom = v < 0.0f ? 0.0f : v; return tf; }

UITextField* UITextField_SetMaxLength(UITextField* tf, int maxLen) {
    if (!tf) return tf;
    tf->maxLength = maxLen;
    if (maxLen > 0 && tf->textLen > maxLen) {
        tf->text[maxLen] = '\0';
        tf->textLen      = maxLen;
        if (tf->caretPos > maxLen) tf->caretPos = maxLen;
        DropTextTexture(tf);
    }
    return tf;
}

UITextField* UITextField_OnChange(UITextField* tf, UITextFieldChangedCallback cb, void* userdata) {
    if (!tf) return tf;
    tf->onChange = cb;
    tf->userdata = userdata;
    return tf;
}

UITextField* UITextField_OnSubmit(UITextField* tf, UITextFieldSubmitCallback cb, void* userdata) {
    if (!tf) return tf;
    tf->onSubmit = cb;
    tf->userdata = userdata;
    return tf;
}

void UITextField_Destroy(UITextField* tf) {
    if (!tf) return;
    free(tf->text);
    free(tf->placeholder);
    free(tf->fontFamily);
    free(tf->__charOffsets);
    if (tf->__SDL_textTexture) SDL_DestroyTexture(tf->__SDL_textTexture);
    free(tf);
}

// Keeps caretPos and selAnchor inside [0, textLen]. Cheap; catches any
// stale value before it reaches memmove inside the edit ops.
static void ClampCaretAndSelection(UITextField* tf) {
    if (!tf) return;
    if (tf->caretPos < 0)             tf->caretPos = 0;
    if (tf->caretPos > tf->textLen)   tf->caretPos = tf->textLen;
    if (tf->selAnchor >= 0 && tf->selAnchor > tf->textLen) {
        tf->selAnchor = tf->textLen;
    }
}

// ---------------------------------------------------------------------
// Selection helpers
// ---------------------------------------------------------------------

static int HasSelection(const UITextField* tf) {
    return tf && tf->selAnchor >= 0 && tf->selAnchor != tf->caretPos;
}

static void SelectionRange(const UITextField* tf, int* outStart, int* outEnd) {
    int a = tf->selAnchor;
    int b = tf->caretPos;
    if (a > b) { int t = a; a = b; b = t; }
    if (outStart) *outStart = a;
    if (outEnd)   *outEnd   = b;
}

static void CollapseSelection(UITextField* tf) {
    if (!tf) return;
    tf->selAnchor = tf->caretPos;
}

static void DeleteSelection(UITextField* tf) {
    if (!HasSelection(tf)) return;
    int s, e;
    SelectionRange(tf, &s, &e);
    if (s < 0) s = 0;
    if (e > tf->textLen) e = tf->textLen;
    if (e <= s) { tf->selAnchor = tf->caretPos; return; }
    const int n = e - s;
    memmove(tf->text + s,
            tf->text + e,
            (size_t)(tf->textLen - e + 1)); // include terminator
    tf->textLen  -= n;
    tf->caretPos  = s;
    tf->selAnchor = s;
    DropTextTexture(tf);
    if (tf->onChange) tf->onChange(tf, tf->text, tf->userdata);
}

// ---------------------------------------------------------------------
// Editing operations
// ---------------------------------------------------------------------

static void InsertChars(UITextField* tf, const char* chars, int n) {
    if (!tf || !chars || n <= 0) return;
    ClampCaretAndSelection(tf);
    // Typing with an active selection replaces the selected range.
    if (HasSelection(tf)) DeleteSelection(tf);
    if (tf->maxLength >= 0 && tf->textLen + n > tf->maxLength) {
        n = tf->maxLength - tf->textLen;
        if (n <= 0) return;
    }
    if (!EnsureTextCapacity(tf, tf->textLen + n + 1)) return;

    // Shift right to make room at caret.
    memmove(tf->text + tf->caretPos + n,
            tf->text + tf->caretPos,
            (size_t)(tf->textLen - tf->caretPos + 1)); // include terminator
    memcpy(tf->text + tf->caretPos, chars, (size_t)n);
    tf->caretPos += n;
    tf->textLen  += n;
    CollapseSelection(tf);
    DropTextTexture(tf);
    if (tf->onChange) tf->onChange(tf, tf->text, tf->userdata);
}

static void DeleteBefore(UITextField* tf) {
    if (!tf) return;
    ClampCaretAndSelection(tf);
    if (HasSelection(tf)) { DeleteSelection(tf); return; }
    if (tf->caretPos <= 0) return;
    memmove(tf->text + tf->caretPos - 1,
            tf->text + tf->caretPos,
            (size_t)(tf->textLen - tf->caretPos + 1));
    tf->caretPos--;
    tf->textLen--;
    CollapseSelection(tf);
    DropTextTexture(tf);
    if (tf->onChange) tf->onChange(tf, tf->text, tf->userdata);
}

static void DeleteAfter(UITextField* tf) {
    if (!tf) return;
    ClampCaretAndSelection(tf);
    if (HasSelection(tf)) { DeleteSelection(tf); return; }
    if (tf->caretPos >= tf->textLen) return;
    memmove(tf->text + tf->caretPos,
            tf->text + tf->caretPos + 1,
            (size_t)(tf->textLen - tf->caretPos));
    tf->textLen--;
    CollapseSelection(tf);
    DropTextTexture(tf);
    if (tf->onChange) tf->onChange(tf, tf->text, tf->userdata);
}

// Maps a local pixel x (relative to the start of the visible text area)
// to a caret byte offset using the cached per-character offsets.
// Caps the result at tf->textLen so a stale offsets array (e.g. after
// SetText("") shrank the text but the next render hasn't rebuilt it
// yet) can never produce a caret position past the actual end of text.
static int CaretFromLocalX(const UITextField* tf, float localX) {
    if (!tf || tf->textLen <= 0) return 0;
    if (!tf->__charOffsets || tf->__charOffsetsLen <= 1) return 0;
    if (localX <= 0.0f) return 0;
    int n = tf->__charOffsetsLen - 1;
    if (n > tf->textLen) n = tf->textLen;
    for (int i = 0; i < n; i++) {
        const float a = (float)tf->__charOffsets[i];
        const float b = (float)tf->__charOffsets[i + 1];
        if (localX < (a + b) * 0.5f) return i;
    }
    return n;
}

static void SetFocused(UITextField* tf, SDL_Window* win, int focused) {
    if (!tf) return;
    if (tf->focused == focused) return;
    tf->focused = focused;
    if (!focused) {
        // Matches the platform convention: clicking elsewhere drops the
        // selection, the caret stays where it was.
        tf->selAnchor      = -1;
        tf->mouseSelecting = 0;
        tf->clickCount     = 0;
    }
    if (win) {
        if (focused) {
            SDL_StartTextInput(win);
        } else {
            SDL_StopTextInput(win);
        }
    }
}

// Treat ASCII alphanumerics + underscore as part of a word. Anything
// else (whitespace, punctuation, control chars) is a boundary.
static int IsWordChar(unsigned char c) {
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    if (c == '_') return 1;
    return 0;
}

// Expands a position into the word it belongs to. Stores the word's
// half-open [start, end) range. When the position sits on a boundary
// (e.g. caret between two non-word characters) we widen to the run of
// non-word characters instead, matching the Windows behaviour.
static void WordAround(const UITextField* tf, int pos, int* outStart, int* outEnd) {
    if (!tf || tf->textLen == 0) {
        if (outStart) *outStart = 0;
        if (outEnd)   *outEnd   = 0;
        return;
    }
    if (pos < 0) pos = 0;
    if (pos > tf->textLen) pos = tf->textLen;

    int probe = pos;
    if (probe >= tf->textLen) probe = tf->textLen - 1;

    const int target = IsWordChar((unsigned char)tf->text[probe]);
    int s = probe;
    while (s > 0 && IsWordChar((unsigned char)tf->text[s - 1]) == target) s--;
    int e = probe;
    while (e < tf->textLen && IsWordChar((unsigned char)tf->text[e]) == target) e++;

    if (outStart) *outStart = s;
    if (outEnd)   *outEnd   = e;
}

// ---------------------------------------------------------------------
// Dispatchers
// ---------------------------------------------------------------------

void UITextField_DispatchMouseDown(UIChildren* children, SDL_Window* win,
                                   float x, float y, int button) {
    if (!children || button != SDL_BUTTON_LEFT) return;

    // First pass: find the topmost text field under the cursor and the
    // widget that owns it (so we can convert x into local coords).
    UITextField* hit    = NULL;
    UIWidget*    hitW   = NULL;
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w = children->children[i];
        UITextField* tf = AsTextField(w);
        if (!tf) continue;
        if (InsideWidget(w, x, y)) { hit = tf; hitW = w; break; }
    }

    // Unfocus everyone else BEFORE focusing the hit. Order matters:
    // SDL only tracks one active text-input session per window, so a
    // StartTextInput on the new field followed by StopTextInput on the
    // old one would leave SDL with text input OFF even though our
    // internal `focused` flag says otherwise. By stopping first and
    // starting last we keep the SDL state consistent with our flags.
    for (int i = 0; i < children->count; i++) {
        UITextField* tf = AsTextField(children->children[i]);
        if (!tf || tf == hit) continue;
        SetFocused(tf, win, 0);
        tf->mouseSelecting = 0;
        tf->lastClickMs    = 0;
        tf->lastClickPos   = -1;
        tf->clickCount     = 0;
    }

    if (hit) {
        UITextField* tf = hit;
        SetFocused(tf, win, 1);
        // Bridge into the generic UIWidget focus system so any other
        // focused widget (button, slider, etc.) is blurred and
        // hitW->focused reflects the click.
        UIWidget_SetFocus(hitW, 1);
        // Position the caret based on the click x. The renderer fills
        // __charOffsets relative to the start of the text (just inside
        // paddingLeft).
        {
            const float localX = x - (hitW->x + tf->paddingLeft);
            int pos = CaretFromLocalX(tf, localX);
            if (pos > tf->textLen) pos = tf->textLen;
            if (pos < 0)           pos = 0;

            // Multi-click detection. Same byte position within the
            // double-click window and the previous click was on this
            // field counts as the next click in the sequence.
            const Uint64 now = SDL_GetTicks();
            const int near = (tf->lastClickPos >= 0 &&
                              ((pos > tf->lastClickPos ? pos - tf->lastClickPos
                                                       : tf->lastClickPos - pos) <= 1));
            if (tf->lastClickMs != 0 &&
                (now - tf->lastClickMs) <= 500 && near) {
                tf->clickCount = tf->clickCount + 1;
                if (tf->clickCount > 3) tf->clickCount = 3;
            } else {
                tf->clickCount = 1;
            }
            tf->lastClickMs  = now;
            tf->lastClickPos = pos;

            if (tf->clickCount == 1) {
                tf->caretPos       = pos;
                tf->selAnchor      = pos; // collapse
                tf->mouseSelecting = 1;
            } else if (tf->clickCount == 2) {
                // Word selection.
                int ws, we;
                WordAround(tf, pos, &ws, &we);
                tf->selAnchor      = ws;
                tf->caretPos       = we;
                tf->mouseSelecting = 0; // double-click doesn't extend by drag
            } else {
                // Triple+ click: select all.
                tf->selAnchor      = 0;
                tf->caretPos       = tf->textLen;
                tf->mouseSelecting = 0;
            }
        }
    }
}

void UITextField_DispatchMouseMotion(UIChildren* children, float x, float y) {
    (void)y;
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        UITextField* tf = AsTextField(w);
        if (!tf || !tf->mouseSelecting) continue;
        const float localX = x - (w->x + tf->paddingLeft);
        int pos = CaretFromLocalX(tf, localX);
        if (pos > tf->textLen) pos = tf->textLen;
        if (pos < 0)           pos = 0;
        if (pos != tf->caretPos) {
            tf->caretPos = pos;
            // Leave selAnchor where it is - we are extending the
            // selection from the original click position.
        }
    }
}

void UITextField_DispatchMouseUp(UIChildren* children, float x, float y, int button) {
    (void)x; (void)y;
    if (!children || button != SDL_BUTTON_LEFT) return;
    for (int i = 0; i < children->count; i++) {
        UITextField* tf = AsTextField(children->children[i]);
        if (!tf) continue;
        tf->mouseSelecting = 0;
    }
}

void UITextField_DispatchTextInput(UIChildren* children, const char* text) {
    if (!children || !text || !*text) return;
    for (int i = 0; i < children->count; i++) {
        UITextField* tf = AsTextField(children->children[i]);
        if (!tf || !tf->focused) continue;
        InsertChars(tf, text, (int)strlen(text));
        return;
    }
}

// Picks out the selected substring as a heap-allocated copy. Caller
// owns the result and must free() it. Returns NULL when there is no
// selection.
static char* CopySelectedText(const UITextField* tf) {
    if (!HasSelection(tf)) return NULL;
    int s, e;
    SelectionRange(tf, &s, &e);
    const int n = e - s;
    char* out = (char*)malloc((size_t)n + 1);
    if (!out) return NULL;
    memcpy(out, tf->text + s, (size_t)n);
    out[n] = '\0';
    return out;
}

void UITextField_DispatchKeyDown(UIChildren* children, SDL_Window* win,
                                 SDL_Scancode key, Uint16 mod) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UITextField* tf = AsTextField(children->children[i]);
        if (!tf || !tf->focused) continue;

        const int ctrl  = (mod & SDL_KMOD_CTRL)  != 0;
        const int shift = (mod & SDL_KMOD_SHIFT) != 0;

        // Movement keys: with shift we extend the existing selection
        // (seeding selAnchor from the current caret if needed); without
        // shift we collapse the selection to the new caret position.
        // The actual movement happens in the per-key cases below.
        const int isMove = (key == SDL_SCANCODE_LEFT  || key == SDL_SCANCODE_RIGHT ||
                            key == SDL_SCANCODE_HOME  || key == SDL_SCANCODE_END);
        if (isMove) {
            if (shift) {
                if (tf->selAnchor < 0 || tf->selAnchor == tf->caretPos) {
                    tf->selAnchor = tf->caretPos;
                }
            }
        }

        switch (key) {
            case SDL_SCANCODE_BACKSPACE:
                DeleteBefore(tf);
                break;
            case SDL_SCANCODE_DELETE:
                DeleteAfter(tf);
                break;
            case SDL_SCANCODE_LEFT:
                if (!shift && HasSelection(tf)) {
                    // Plain Left collapses to the selection's left edge.
                    int s, e; SelectionRange(tf, &s, &e); (void)e;
                    tf->caretPos = s;
                } else if (tf->caretPos > 0) {
                    tf->caretPos--;
                }
                if (!shift) CollapseSelection(tf);
                break;
            case SDL_SCANCODE_RIGHT:
                if (!shift && HasSelection(tf)) {
                    int s, e; SelectionRange(tf, &s, &e); (void)s;
                    tf->caretPos = e;
                } else if (tf->caretPos < tf->textLen) {
                    tf->caretPos++;
                }
                if (!shift) CollapseSelection(tf);
                break;
            case SDL_SCANCODE_HOME:
                tf->caretPos = 0;
                if (!shift) CollapseSelection(tf);
                break;
            case SDL_SCANCODE_END:
                tf->caretPos = tf->textLen;
                if (!shift) CollapseSelection(tf);
                break;
            case SDL_SCANCODE_RETURN:
            case SDL_SCANCODE_KP_ENTER:
                if (tf->onSubmit) tf->onSubmit(tf, tf->text, tf->userdata);
                break;
            case SDL_SCANCODE_ESCAPE:
                SetFocused(tf, win, 0);
                break;
            case SDL_SCANCODE_V:
                if (ctrl) {
                    char* clip = SDL_GetClipboardText();
                    if (clip && *clip) InsertChars(tf, clip, (int)strlen(clip));
                    if (clip) SDL_free(clip);
                }
                break;
            case SDL_SCANCODE_C:
                if (ctrl) {
                    char* sel = CopySelectedText(tf);
                    if (sel) {
                        SDL_SetClipboardText(sel);
                        free(sel);
                    } else if (tf->textLen > 0) {
                        // Backwards compatible: copy the whole field
                        // when nothing is selected.
                        SDL_SetClipboardText(tf->text);
                    }
                }
                break;
            case SDL_SCANCODE_X:
                if (ctrl) {
                    char* sel = CopySelectedText(tf);
                    if (sel) {
                        SDL_SetClipboardText(sel);
                        free(sel);
                        DeleteSelection(tf);
                    }
                }
                break;
            case SDL_SCANCODE_A:
                if (ctrl) {
                    tf->selAnchor = 0;
                    tf->caretPos  = tf->textLen;
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

// Walks the active window's children and drops the focus + selection
// state on every text widget except `keepField` / `keepArea` / `keepText`.
// One of the three should normally be non-NULL; pass NULL for all to
// blur everything. Centralised so each *_SetFocus can reuse it.
void UIKitFocus_BlurOthers(UITextField* keepField, UITextArea* keepArea,
                           UIText* keepText) {
    UIWindow* win = UIWindow_GetActive();
    if (!win || !win->children) return;
    UIChildren* ch = win->children;
    for (int i = 0; i < ch->count; i++) {
        UIWidget* w = ch->children[i];
        if (!w || !w->data) continue;
        UIWidgetBase* b = (UIWidgetBase*)w->data;
        if (!strcmp(b->__widget_type, UI_WIDGET_TEXTFIELD)) {
            UITextField* tf2 = (UITextField*)b;
            if (tf2 == keepField) continue;
            tf2->focused        = 0;
            tf2->selAnchor      = -1;
            tf2->mouseSelecting = 0;
            tf2->clickCount     = 0;
        } else if (!strcmp(b->__widget_type, UI_WIDGET_TEXTAREA)) {
            UITextArea* ta2 = (UITextArea*)b;
            if (ta2 == keepArea) continue;
            ta2->focused        = 0;
            ta2->selAnchor      = -1;
            ta2->mouseSelecting = 0;
            ta2->clickCount     = 0;
        } else if (!strcmp(b->__widget_type, UI_WIDGET_TEXT)) {
            UIText* t2 = (UIText*)b;
            if (t2 == keepText) continue;
            t2->focused        = 0;
        }
    }
}

UITextField* UITextField_SetFocus(UITextField* tf, int focused) {
    if (!tf) return NULL;
    focused = focused ? 1 : 0;

    // Prefer the generic focus path so UIWidget.focused stays in sync
    // and other focusable widgets get blurred too.
    UIWidget* w = UIWidget_FindByData(tf);
    if (w) {
        UIWidget_SetFocus(w, focused);
        return tf;
    }

    // Fallback: not attached to the active window's children tree.
    // Toggle just the local state.
    UIWindow*   win  = UIWindow_GetActive();
    SDL_Window* sdlw = win ? win->sdlWindow : NULL;
    if (focused) UIKitFocus_BlurOthers(tf, NULL, NULL);
    SetFocused(tf, sdlw, focused);
    return tf;
}

int UITextField_IsFocused(const UITextField* tf) {
    return tf ? tf->focused : 0;
}
