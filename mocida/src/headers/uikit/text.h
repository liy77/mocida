#ifndef UIKIT_TEXT_H
#define UIKIT_TEXT_H

#include <uikit/color.h>
#include <uikit/rect.h>
#include <uikit/font.h>
#include <uikit/cursor.h>
#include <uikit/children.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3/SDL.h>
#include <stdio.h>

/**
 * Font styles for text rendering. Bitmask — combine flags with `|`.
 *
 *     UIText_SetFontStyle(label, Bold | Italic);
 *
 * The bit values intentionally mirror SDL_ttf's TTF_STYLE_* constants
 * so the renderer can pass them through directly:
 *
 *     Bold          == TTF_STYLE_BOLD
 *     Italic        == TTF_STYLE_ITALIC
 *     Underscore    == TTF_STYLE_UNDERLINE   (alias: Underline)
 *     Strikethrough == TTF_STYLE_STRIKETHROUGH
 *
 * Applies to: UIText, UIButton (via the label), UITextField, UITextArea.
 */
typedef enum FontStyle {
    Normal        = 0,
    Bold          = 1 << 0,
    Italic        = 1 << 1,
    Underscore    = 1 << 2,
    Underline     = 1 << 2, /**< Alias for Underscore — more conventional name. */
    Strikethrough = 1 << 3
} FontStyle;

/**
 * Horizontal alignment of the rendered text glyph block inside the
 * widget's defined bounds. Only has an observable effect when the
 * widget's explicit width is larger than the natural text width.
 */
typedef enum UITextHAlign {
    UI_TEXT_HALIGN_LEFT   = 0,
    UI_TEXT_HALIGN_CENTER = 1,
    UI_TEXT_HALIGN_RIGHT  = 2
} UITextHAlign;

/**
 * Vertical alignment of the glyph block inside the widget's bounds.
 * Only has an observable effect when the widget's explicit height is
 * larger than the natural text height (single line) or wrapped block.
 */
typedef enum UITextVAlign {
    UI_TEXT_VALIGN_TOP    = 0,
    UI_TEXT_VALIGN_CENTER = 1,
    UI_TEXT_VALIGN_BOTTOM = 2
} UITextVAlign;

/**
 * Text wrap mode shared by UIText / UITextArea.
 *  - UI_WRAP_NONE : no automatic wrap. Only explicit "\n" breaks lines.
 *  - UI_WRAP_WORD : wrap at word boundaries (spaces). Words longer than
 *                   the available width fall back to character wrap so
 *                   nothing overflows the bounds.
 *  - UI_WRAP_CHAR : wrap at any character once the available width is
 *                   exceeded (no word awareness).
 */
typedef enum UIWrapMode {
    UI_WRAP_NONE = 0,
    UI_WRAP_WORD = 1,
    UI_WRAP_CHAR = 2,
    /**
     * Shrink-to-fit. The text is laid out on a single line (no wrap)
     * and scaled down uniformly when its natural width exceeds the
     * available width. If it already fits, it is rendered at the
     * natural size. Supported by UIText.
     */
    UI_WRAP_FIT  = 3
} UIWrapMode;

/**
 * UIText structure representing a text widget.
 * It contains properties for font size, font family, font style,
 * color, background color, and the text itself.
 */
typedef struct UIText {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_TEXT). */

    float marginLeft;          /**< Left outer margin (pixels). */
    float marginTop;           /**< Top outer margin (pixels). */
    float marginRight;         /**< Right outer margin (pixels). */
    float marginBottom;        /**< Bottom outer margin (pixels). */

    float paddingLeft;         /**< Inner left padding around the glyph block. */
    float paddingTop;          /**< Inner top padding. */
    float paddingRight;        /**< Inner right padding. */
    float paddingBottom;       /**< Inner bottom padding. */

    float fontSize;            /**< Font size in points. */
    char* fontFamily;          /**< Heap-owned font path (.ttf / .otf). */

    int fontStyle;             /**< Bitmask of FontStyle flags (Bold | Italic | ...). */
    UIColor color;             /**< Glyph color. */
    UIRectangle* background;   /**< Owned background rect drawn behind the text. */
    char* text;                /**< Heap-owned UTF-8 text. */
    int textLength;            /**< Byte length of `text`, excluding the terminator. */

    /**
     * When > 0, the text is wrapped to this pixel width via
     * TTF_RenderText_Blended_Wrapped. 0 = single line (default).
     * Ignored when wrapMode is UI_WRAP_CHAR.
     */
    int wrapWidth;

    /**
     * Selects how text is broken into multiple visual lines.
     * - UI_WRAP_NONE  : no automatic wrap. Defers to wrapWidth (>0 -> word wrap)
     *                   for backwards compatibility.
     * - UI_WRAP_WORD  : word-boundary wrap. Uses wrapWidth (or the widget
     *                   width when wrapToBounds is set).
     * - UI_WRAP_CHAR  : break at any character once the width is exceeded.
     *                   Uses the widget width when wrapToBounds is set,
     *                   otherwise falls back to wrapWidth.
     */
    UIWrapMode wrapMode;

    /**
     * When non-zero, wrap follows the widget's explicit width instead
     * of the fixed pixel value in wrapWidth.
     */
    int wrapToBounds;

    /**
     * Horizontal alignment of the text glyph block within the widget
     * bounds. Defaults to UI_TEXT_HALIGN_LEFT.
     */
    UITextHAlign hAlign;

    /**
     * Vertical alignment of the text glyph block within the widget
     * bounds. Defaults to UI_TEXT_VALIGN_TOP.
     */
    UITextVAlign vAlign;

    /**
     * When non-zero, the text becomes interactive: the user can click
     * and drag to select a range, double-click to select a word, Ctrl+A
     * to select all and Ctrl+C to copy. Layout switches to a per-line
     * cache so wrap, selection highlights and click-to-position all
     * work together.
     */
    int selectable;

    /** Highlight color drawn behind selected glyphs. */
    UIColor selectionColor;

    /**
     * Selection state (byte offsets into `text`). selAnchor is the
     * sticky anchor of the current drag; selCaret is the moving end.
     * When selAnchor < 0 or selAnchor == selCaret there is no
     * selection.
     */
    int selAnchor;             /**< Byte offset of the selection anchor; -1 = no selection. */
    int selCaret;              /**< Byte offset of the moving end of the selection. */
    int mouseSelecting;        /**< Internal: 1 while the left button is held during a drag. */
    int focused;               /**< 1 when this widget owns keyboard focus (Ctrl+C / Ctrl+A). */
    Uint64 lastClickMs;        /**< Internal: SDL_GetTicks() of the previous click. */
    int    lastClickPos;       /**< Internal: caret position at the previous click. */
    int    clickCount;         /**< Internal: 1 = single, 2 = word, 3 = line (multi-click). */

    /** Mouse cursor advertised while hovering when selectable. */
    UICursor cursor;

    SDL_Texture* __SDL_textTexture; /**< Internal: non-selectable single-texture cache. */

    SDL_Texture** __lineTextures;   /**< Internal: per visual-line glyph texture. */
    int*          __lineStarts;     /**< Internal: byte offset where each line starts. */
    int*          __lineLengths;    /**< Internal: byte length of each line (excludes \\n). */
    int**         __lineCharOffsets;/**< Internal: per-char X offsets for each line. */
    int*          __lineCharOffsetsLen; /**< Internal: length of each line's char-offset array. */
    int*          __lineIsSoft;     /**< Internal: 1 = soft (wrapped) line break, 0 = hard \\n. */
    int           __linesLen;       /**< Internal: number of visual lines. */
    int           __linesCap;       /**< Internal: allocated capacity of the line arrays. */
    int           __cachedTextLen;  /**< Internal: textLength the cache was built for. */
    int           __cachedWrapMode; /**< Internal: wrapMode the cache was built for. */
    int           __cachedWrapW;    /**< Internal: wrapW the cache was built for. */
} UIText;

/**
 * Creates a UIText object with the specified text and font size.
 * @param text Text to be displayed.
 * @param fontSize Font size of the text.
 * @return A pointer to the UIText object.
 */
UIText* UIText_Create(char* text, float fontSize);

/**
 * Sets the font family of the UIText object.
 * @param text Pointer to the UIText object.
 * @param fontFamily Font family to be set.
 * @return Pointer to the updated UIText object.
 */
UIText* UIText_SetFontFamily(UIText* text, char* fontFamily);

/**
 * Sets the font style of the UIText object.
 * @param text Pointer to the UIText object.
 * @param fontStyle Font style to be set.
 * @return Pointer to the updated UIText object.
 */
UIText* UIText_SetFontStyle(UIText* text, int fontStyle);

/**
 * Sets the font size (points) and invalidates the cached glyph texture so
 * the next render rebuilds at the new size. Enables responsive text sizing
 * (e.g. scaling against UIScreen_GetSize()).
 */
UIText* UIText_SetFontSize(UIText* text, float fontSize);

/**
 * Sets the color of the UIText object.
 * @param text Pointer to the UIText object.
 * @param color Color to be set.
 * @return Pointer to the updated UIText object.
 */
UIText* UIText_SetColor(UIText* text, UIColor color);

/**
 * Sets the background of the UIText object.
 * @param text Pointer to the UIText object.
 * @param background Background to be set.
 * @return Pointer to the updated UIText object.
 */
UIText* UIText_SetBackground(UIText* text, UIRectangle* backgroundRect);

/**
 * Sets the text of the UIText object.
 * @param text Pointer to the UIText object.
 * @param newText New text to be set.
 * @return Pointer to the updated UIText object.
 */
UIText* UIText_SetText(UIText* text, char* newText);

/**
 * Sets the margins of the UIText object.
 * @param text Pointer to the UIText object.
 * @param left Left margin.
 * @param top Top margin.
 * @param right Right margin.
 * @param bottom Bottom margin.
 * @return Pointer to the updated UIText object.
 */
UIText* UIText_SetMargins(UIText* text, float left, float top, float right, float bottom);

/**
 * Sets the padding of the UIText object.
 * @param text Pointer to the UIText object.
 * @param left Left padding.
 * @param top Top padding.
 * @param right Right padding.
 * @param bottom Bottom padding.
 * @return Pointer to the updated UIText object.
 */
UIText* UIText_SetPadding(UIText* text, float left, float top, float right, float bottom);

/**
 * Enables word-wrap rendering at a fixed pixel width. Pass 0 to disable
 * (single-line). Equivalent to UI_WRAP_WORD with an explicit width.
 */
UIText* UIText_SetWrapWidth(UIText* text, int wrapWidth);

/**
 * Sets the wrap mode (none / word / char). Combine with
 * UIText_SetWrapToBounds(1) to auto-wrap to the widget's width, or with
 * UIText_SetWrapWidth(px) for a fixed wrap width.
 */
UIText* UIText_SetWrapMode(UIText* text, UIWrapMode mode);

/**
 * When enabled, wrap uses the widget's explicit width instead of
 * wrapWidth. Useful for responsive text inside a sized container.
 */
UIText* UIText_SetWrapToBounds(UIText* text, int enabled);

/**
 * Sets the horizontal alignment of the text inside the widget bounds.
 * Only observable when the widget has an explicit width wider than the
 * rendered glyph block.
 */
UIText* UIText_SetHAlign(UIText* text, UITextHAlign hAlign);

/**
 * Sets the vertical alignment of the text inside the widget bounds.
 * Only observable when the widget has an explicit height taller than
 * the rendered glyph block.
 */
UIText* UIText_SetVAlign(UIText* text, UITextVAlign vAlign);

/**
 * Convenience: sets both horizontal and vertical alignment at once.
 */
UIText* UIText_SetAlignment(UIText* text, UITextHAlign h, UITextVAlign v);

/**
 * Enables / disables interactive text selection. Default off.
 */
UIText* UIText_SetSelectable(UIText* text, int enabled);

/** Color used to fill the highlight behind selected glyphs. */
UIText* UIText_SetSelectionColor(UIText* text, UIColor color);

/** Mouse cursor advertised while hovering when selectable. */
UIText* UIText_SetCursor(UIText* text, UICursor cursor);

/** Clears the current selection (no-op when not selectable). */
UIText* UIText_ClearSelection(UIText* text);

/**
 * Programmatic focus control for selectable text. Pass 1 to focus,
 * 0 to blur. Focusing unfocuses every other selectable UIText in the
 * active window's children. No-op when the widget isn't selectable.
 */
UIText* UIText_SetFocus(UIText* text, int focused);

/** Whether this text currently has keyboard focus (Ctrl+C / Ctrl+A). */
int     UIText_IsFocused(const UIText* text);

// ---------------------------------------------------------------------
// Dispatchers (called by app.c). They walk the children tree, find any
// selectable UIText under the cursor and update its selection state.
// ---------------------------------------------------------------------
void UIText_DispatchMouseDown  (UIChildren* children, SDL_Window* win,
                                float x, float y, int button);
void UIText_DispatchMouseMotion(UIChildren* children, float x, float y);
void UIText_DispatchMouseUp    (UIChildren* children, float x, float y, int button);
void UIText_DispatchKeyDown    (UIChildren* children, SDL_Window* win,
                                SDL_Scancode key, Uint16 mod);

/**
 * Destroys the UIText object and frees its resources.
 * @param text Pointer to the UIText object to be destroyed.
 * @return None.
 */
void UIText_DestroyTexture(UIText* text);

/**
 * Destroys the UIText object and frees its memory.
 * @param text Pointer to the UIText object to be destroyed.
 * @return None.
 */
void UIText_Destroy(UIText* text);

#endif // UIKIT_TEXT_H