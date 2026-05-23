#ifndef UIKIT_TEXTFIELD_H
#define UIKIT_TEXTFIELD_H

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <uikit/widget.h>
#include <uikit/color.h>
#include <uikit/children.h>
#include <uikit/cursor.h>

#define UI_WIDGET_TEXTFIELD "@uikit/textfield"

typedef struct UITextField UITextField;
typedef void (*UITextFieldChangedCallback)(UITextField* tf, const char* text, void* userdata);
typedef void (*UITextFieldSubmitCallback) (UITextField* tf, const char* text, void* userdata);

/**
 * Single-line editable text input. Enter fires `onSubmit`; Tab leaves
 * focus alone. Supports selection (click+drag, double/triple-click),
 * Ctrl+A/C/X/V, and an optional password-mask mode.
 */
struct UITextField {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_TEXTFIELD). */

    char* text;                /**< Heap-owned mutable text buffer (NUL-terminated). */
    int   textLen;             /**< Bytes used (excluding the NUL terminator). */
    int   textCapacity;        /**< Allocated size of the `text` buffer. */
    int   caretPos;            /**< Caret byte offset into `text`. */
    int   maxLength;           /**< Maximum length in bytes; -1 = unlimited. */

    char*    placeholder;      /**< Heap-owned placeholder string shown when empty. */
    int      placeholderAnimated; /**< 1 makes the placeholder gently pulse opacity while the field is unfocused. */
    char*    fontFamily;       /**< Heap-owned font path (.ttf / .otf). */
    float    fontSize;         /**< Font size in points. */
    UIColor  textColor;        /**< Glyph color. */
    UIColor  placeholderColor; /**< Placeholder glyph color. */
    UIColor  caretColor;       /**< Caret bar color. */
    int      caretBlinkMs;     /**< Half-period of the caret blink. Default 530. <= 0 disables blinking. */

    UIColor  bgColor;          /**< Field background fill. */
    UIColor  borderColor;      /**< Border color when not focused. */
    UIColor  borderColorFocused;/**< Border color while focused. */
    float    borderWidth;      /**< Border thickness (pixels). */
    float    radius;           /**< Corner radius (pixels). */
    float    paddingLeft;      /**< Inner left padding (pixels). */
    float    paddingRight;     /**< Inner right padding (pixels). */
    float    paddingTop;       /**< Inner top padding (pixels). */
    float    paddingBottom;    /**< Inner bottom padding (pixels). */

    int focused;               /**< 1 when this field owns keyboard input. */
    int passwordMask;          /**< 1 renders dots instead of the underlying glyphs. */
    UICursor cursor;           /**< Cursor advertised while hovering (default I-beam). */

    int     selAnchor;         /**< Byte offset where the current selection started; -1 = none. */
    int     mouseSelecting;    /**< Internal: 1 while the user is dragging a selection. */
    UIColor selectionColor;    /**< Highlight color drawn behind selected glyphs. */

    Uint64  lastClickMs;       /**< Internal: SDL_GetTicks() of the previous click. */
    int     lastClickPos;      /**< Internal: caret position at the previous click. */
    int     clickCount;        /**< Internal: 1 = single, 2 = word, 3+ = select-all. */

    SDL_Texture* __SDL_textTexture; /**< Internal: cached glyph texture for the current text. */
    int          __cachedTextLen;   /**< Internal: textLen the texture was rendered for. */

    /**
     * Pixel X offset of each character position, 0..textLen inclusive.
     * Filled by the renderer when the text texture is (re)built. Used
     * by the dispatcher to convert a mouse X to a caret position.
     */
    int*  __charOffsets;
    int   __charOffsetsLen;    /**< Internal: number of valid entries in __charOffsets. */

    UITextFieldChangedCallback onChange; /**< Fires on every textual change. */
    UITextFieldSubmitCallback  onSubmit; /**< Fires on Enter (textfield is single-line). */
    void* userdata;                      /**< Opaque pointer forwarded to onChange / onSubmit. */
};

UITextField* UITextField_Create     (const char* initialText, float fontSize);
UITextField* UITextField_SetText    (UITextField* tf, const char* text);
const char*  UITextField_GetText    (UITextField* tf);
UITextField* UITextField_SetPlaceholder(UITextField* tf, const char* placeholder);

/**
 * Toggles a subtle "breathing" animation on the placeholder text while
 * the field is empty AND unfocused. Modulates the alpha between ~55%
 * and 100% on a 2.5 s sine. Click the field (or focus it
 * programmatically) and the placeholder disappears, the caret takes
 * over. Zero cost when off — default is off.
 */
UITextField* UITextField_SetPlaceholderAnimated(UITextField* tf, int yes);

/**
 * Sets how often the caret toggles visibility while the field is
 * focused. Pass milliseconds for the half-period (530 = classic blink).
 * Pass 0 to disable blinking entirely (caret stays solid).
 */
UITextField* UITextField_SetCaretBlinkRate(UITextField* tf, int halfPeriodMs);
UITextField* UITextField_SetFontFamily(UITextField* tf, char* family);
UITextField* UITextField_SetFontSize (UITextField* tf, float size);
UITextField* UITextField_SetPassword (UITextField* tf, int yes);
UITextField* UITextField_SetMaxLength(UITextField* tf, int maxLen);
UITextField* UITextField_SetSelectionColor(UITextField* tf, UIColor color);
UITextField* UITextField_SetBgColor  (UITextField* tf, UIColor color);
UITextField* UITextField_SetTextColor(UITextField* tf, UIColor color);
UITextField* UITextField_SetPlaceholderColor(UITextField* tf, UIColor color);
UITextField* UITextField_SetCaretColor(UITextField* tf, UIColor color);
UITextField* UITextField_SetBorder   (UITextField* tf, UIColor normal, UIColor focused, float width);
UITextField* UITextField_SetBorderColor(UITextField* tf, UIColor color);
UITextField* UITextField_SetBorderColorFocused(UITextField* tf, UIColor color);
UITextField* UITextField_SetBorderWidth(UITextField* tf, float width);
UITextField* UITextField_SetRadius   (UITextField* tf, float radius);
UITextField* UITextField_SetPadding  (UITextField* tf, float x, float y);
UITextField* UITextField_SetPaddingLeft  (UITextField* tf, float v);
UITextField* UITextField_SetPaddingRight (UITextField* tf, float v);
UITextField* UITextField_SetPaddingTop   (UITextField* tf, float v);
UITextField* UITextField_SetPaddingBottom(UITextField* tf, float v);
UITextField* UITextField_SetCursor  (UITextField* tf, UICursor cursor);
UITextField* UITextField_OnChange   (UITextField* tf, UITextFieldChangedCallback cb, void* userdata);
UITextField* UITextField_OnSubmit   (UITextField* tf, UITextFieldSubmitCallback  cb, void* userdata);
void         UITextField_Destroy    (UITextField* tf);

/**
 * Programmatic focus control. Pass 1 to focus, 0 to blur. When focusing,
 * any other UITextField in the active window's children is unfocused
 * first so only one field claims keyboard input. Returns the field.
 */
UITextField* UITextField_SetFocus   (UITextField* tf, int focused);

/** Whether this field currently has keyboard focus. */
int          UITextField_IsFocused  (const UITextField* tf);

// ---------------------------------------------------------------------
// Dispatchers (called by app.c). They walk the children tree, find the
// focused text field (if any) and apply the appropriate edit.
// ---------------------------------------------------------------------
void UITextField_DispatchMouseDown  (UIChildren* children, SDL_Window* win,
                                     float x, float y, int button);
void UITextField_DispatchMouseMotion(UIChildren* children, float x, float y);
void UITextField_DispatchMouseUp    (UIChildren* children, float x, float y, int button);
void UITextField_DispatchTextInput  (UIChildren* children, const char* text);
void UITextField_DispatchKeyDown    (UIChildren* children, SDL_Window* win,
                                     SDL_Scancode key, Uint16 mod);

#endif // UIKIT_TEXTFIELD_H
