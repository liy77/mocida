#ifndef UIKIT_TEXTAREA_H
#define UIKIT_TEXTAREA_H

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <uikit/widget.h>
#include <uikit/color.h>
#include <uikit/children.h>
#include <uikit/cursor.h>
#include <uikit/text.h>

#define UI_WIDGET_TEXTAREA "@uikit/textarea"

/**
 * Multi-line editable text input. Unlike UITextField:
 *   - Enter inserts a newline (no onSubmit).
 *   - Up / Down move the caret between lines.
 *   - Selection can span multiple lines.
 *   - The content scrolls vertically when it overflows the bounds.
 *
 * Byte positions are used throughout (ASCII-friendly). UTF-8 caret
 * navigation is a follow-up; for now non-ASCII glyphs are still
 * rendered correctly, they just count as multiple positions.
 */
typedef struct UITextArea UITextArea;
typedef void (*UITextAreaChangedCallback)(UITextArea* ta, const char* text, void* userdata);

/**
 * Multi-line editable text input. Unlike UITextField, Enter inserts a
 * newline (no onSubmit), the caret can move between lines, and
 * selection can span them. Long content scrolls vertically inside the
 * widget bounds.
 */
struct UITextArea {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_TEXTAREA). */

    char* text;                /**< Heap-owned NUL-terminated buffer; '\\n' separates logical lines. */
    int   textLen;             /**< Byte length of `text`, excluding the terminator. */
    int   textCapacity;        /**< Allocated size of the `text` buffer. */
    int   caretPos;            /**< Caret byte offset into `text`. */
    int   maxLength;           /**< Maximum length in bytes; -1 = unlimited. */

    char*    placeholder;      /**< Heap-owned placeholder text, shown when empty. */
    int      placeholderAnimated; /**< 1 makes the placeholder gently pulse opacity while the area is unfocused. */
    char*    fontFamily;       /**< Heap-owned font path (.ttf / .otf). */
    float    fontSize;         /**< Font size in points. */
    int      fontStyle;        /**< Bitmask of FontStyle flags (Bold | Italic | Underline | Strikethrough). */
    UIColor  textColor;        /**< Glyph color. */
    UIColor  placeholderColor; /**< Placeholder glyph color. */
    UIColor  caretColor;       /**< Caret bar color. */
    int      caretBlinkMs;     /**< Half-period of the caret blink. Default 530. <= 0 disables blinking. */

    UIColor  bgColor;          /**< Background fill. */
    UIColor  borderColor;      /**< Border color when not focused. */
    UIColor  borderColorFocused;/**< Border color while focused. */
    float    borderWidth;      /**< Border thickness (pixels). */
    float    radius;           /**< Corner radius (pixels). */
    float    paddingLeft;      /**< Inner left padding (pixels). */
    float    paddingRight;     /**< Inner right padding (pixels). */
    float    paddingTop;       /**< Inner top padding (pixels). */
    float    paddingBottom;    /**< Inner bottom padding (pixels). */
    float    lineSpacing;      /**< Line-height multiplier on fontSize (default 1.2). */

    int      focused;          /**< 1 when this area owns keyboard input. */
    UICursor cursor;           /**< Cursor advertised while hovering (default I-beam). */

    int     selAnchor;         /**< Byte offset of the selection anchor; -1 = no selection. */
    int     mouseSelecting;    /**< Internal: 1 while the user is dragging a selection. */
    UIColor selectionColor;    /**< Highlight color drawn behind selected glyphs. */

    Uint64  lastClickMs;       /**< Internal: SDL_GetTicks() of the previous click. */
    int     lastClickPos;      /**< Internal: caret position at the previous click. */
    int     clickCount;        /**< Internal: 1 = single, 2 = word, 3 = line. */

    /**
     * Vertical scroll offset, in pixels. The renderer keeps the caret
     * visible by adjusting this whenever the caret moves outside the
     * viewport.
     */
    float   scrollY;

    /**
     * Wrap behaviour. UI_WRAP_NONE (default) lets long lines overflow
     * horizontally; UI_WRAP_WORD / UI_WRAP_CHAR rebuild the per-line
     * cache so every visual line fits inside the widget's content box.
     */
    UIWrapMode wrapMode;

    SDL_Texture** lineTextures;     /**< Internal: per visual-line glyph texture. */
    int*          lineStarts;       /**< Internal: byte offset where each visual line starts. */
    int*          lineLengths;      /**< Internal: byte length of each line (excludes \\n). */
    int**         lineCharOffsets;  /**< Internal: per-char X offsets, length lineLengths[i]+1. */
    int*          lineCharOffsetsLen;/**< Internal: length of each line's char-offset array. */
    int*          lineIsSoft;       /**< Internal: 1 = soft (wrapped) break, 0 = hard \\n. */
    int           linesLen;         /**< Internal: number of visual lines in the cache. */
    int           linesCap;         /**< Internal: allocated capacity of the line arrays. */
    int           __cachedTextLen;  /**< Internal: textLen the cache was built for. */
    int           __cachedWrapMode; /**< Internal: wrapMode the cache was built for. */
    int           __cachedWrapW;    /**< Internal: wrap width the cache was built for. */

    UITextAreaChangedCallback onChange; /**< Fires on every textual change. */
    void* userdata;                     /**< Opaque pointer forwarded to onChange. */
};

UITextArea* UITextArea_Create        (const char* initialText, float fontSize);
UITextArea* UITextArea_SetText       (UITextArea* ta, const char* text);
const char* UITextArea_GetText       (UITextArea* ta);
UITextArea* UITextArea_SetPlaceholder(UITextArea* ta, const char* placeholder);

/** See UITextField_SetPlaceholderAnimated. */
UITextArea* UITextArea_SetPlaceholderAnimated(UITextArea* ta, int yes);

/** See UITextField_SetCaretBlinkRate. */
UITextArea* UITextArea_SetCaretBlinkRate(UITextArea* ta, int halfPeriodMs);
UITextArea* UITextArea_SetFontFamily (UITextArea* ta, char* family);
UITextArea* UITextArea_SetMaxLength  (UITextArea* ta, int maxLen);
UITextArea* UITextArea_SetLineSpacing(UITextArea* ta, float spacing);
UITextArea* UITextArea_SetBgColor    (UITextArea* ta, UIColor color);
UITextArea* UITextArea_SetTextColor  (UITextArea* ta, UIColor color);
UITextArea* UITextArea_SetPlaceholderColor(UITextArea* ta, UIColor color);
UITextArea* UITextArea_SetCaretColor (UITextArea* ta, UIColor color);
UITextArea* UITextArea_SetBorder     (UITextArea* ta, UIColor normal, UIColor focused, float width);
UITextArea* UITextArea_SetBorderColor(UITextArea* ta, UIColor color);
UITextArea* UITextArea_SetBorderColorFocused(UITextArea* ta, UIColor color);
UITextArea* UITextArea_SetBorderWidth(UITextArea* ta, float width);
UITextArea* UITextArea_SetRadius     (UITextArea* ta, float radius);
UITextArea* UITextArea_SetPadding    (UITextArea* ta, float x, float y);
UITextArea* UITextArea_SetPaddingLeft  (UITextArea* ta, float v);
UITextArea* UITextArea_SetPaddingRight (UITextArea* ta, float v);
UITextArea* UITextArea_SetPaddingTop   (UITextArea* ta, float v);
UITextArea* UITextArea_SetPaddingBottom(UITextArea* ta, float v);
UITextArea* UITextArea_SetSelectionColor(UITextArea* ta, UIColor color);
UITextArea* UITextArea_SetFontSize   (UITextArea* ta, float size);

/**
 * Sets the font style as a bitmask of FontStyle flags. Invalidates the
 * per-line glyph cache so the change is picked up on the next render.
 */
UITextArea* UITextArea_SetFontStyle  (UITextArea* ta, int fontStyle);

UITextArea* UITextArea_SetCursor     (UITextArea* ta, UICursor cursor);
UITextArea* UITextArea_SetWrapMode   (UITextArea* ta, UIWrapMode mode);
UITextArea* UITextArea_OnChange      (UITextArea* ta, UITextAreaChangedCallback cb, void* userdata);
void        UITextArea_Destroy       (UITextArea* ta);

/**
 * Programmatic focus control. Pass 1 to focus, 0 to blur. When focusing,
 * any other UITextArea in the active window's children is unfocused
 * first so only one area claims keyboard input. Returns the area.
 */
UITextArea* UITextArea_SetFocus      (UITextArea* ta, int focused);

/** Whether this area currently has keyboard focus. */
int         UITextArea_IsFocused     (const UITextArea* ta);

// Dispatchers (wired from app.c).
void UITextArea_DispatchMouseDown  (UIChildren* children, SDL_Window* win,
                                    float x, float y, int button);
void UITextArea_DispatchMouseMotion(UIChildren* children, float x, float y);
void UITextArea_DispatchMouseUp    (UIChildren* children, float x, float y, int button);
void UITextArea_DispatchMouseWheel (UIChildren* children, float x, float y,
                                    float dy);
void UITextArea_DispatchTextInput  (UIChildren* children, const char* text);
void UITextArea_DispatchKeyDown    (UIChildren* children, SDL_Window* win,
                                    SDL_Scancode key, Uint16 mod);

#endif // UIKIT_TEXTAREA_H
