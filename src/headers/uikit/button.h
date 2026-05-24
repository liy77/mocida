#ifndef UIKIT_BUTTON_H
#define UIKIT_BUTTON_H

#include <uikit/color.h>
#include <uikit/rect.h>
#include <uikit/text.h>
#include <uikit/widget.h>
#include <uikit/children.h>
#include <uikit/cursor.h>

#define UI_WIDGET_BUTTON "@uikit/button"

/**
 * Visual states of a button. The renderer paints with the UIButtonStyle
 * corresponding to the current state.
 */
typedef enum {
    UI_BUTTON_STATE_NORMAL   = 0,
    UI_BUTTON_STATE_HOVER    = 1,
    UI_BUTTON_STATE_PRESSED  = 2,
    UI_BUTTON_STATE_DISABLED = 3
} UIButtonState;

/**
 * Color set applied per state.
 * - background: button fill
 * - border:     border color (only used when borderWidth > 0)
 * - text:       label color
 */
typedef struct {
    UIColor background; /**< Button fill color for this state. */
    UIColor border;     /**< Border color (only painted when borderWidth > 0). */
    UIColor text;       /**< Label color for this state. */
} UIButtonStyle;

// Forward declaration so UIButtonCallback can take a UIButton*.
typedef struct UIButton UIButton;

/**
 * Callback fired when the button is clicked. Receives the button itself
 * and the associated userdata pointer.
 */
typedef void (*UIButtonCallback)(UIButton* button, void* userdata);

/**
 * Clickable button widget. Owns a background UIRectangle and a UIText
 * label, plus a per-state UIButtonStyle table (normal / hover /
 * pressed / disabled). Fires `onClick` on a completed press + release
 * inside the bounds.
 */
struct UIButton {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_BUTTON). */

    float marginLeft;          /**< Left outer margin. */
    float marginTop;           /**< Top outer margin. */
    float marginRight;         /**< Right outer margin. */
    float marginBottom;        /**< Bottom outer margin. */

    UIRectangle* background;   /**< Owned background rect (controls radius / borderWidth). */
    UIText*      label;        /**< Owned label widget (font family / size). */

    UIButtonStyle styles[4];   /**< One color set per UIButtonState. */

    UIButtonState state;       /**< Currently rendered visual state. */
    int enabled;               /**< 0 forces UI_BUTTON_STATE_DISABLED and ignores input. */
    int isMouseInside;         /**< Internal flag updated by mouse-motion dispatcher. */
    int isPressed;             /**< Tracks the mouse-down → mouse-up cycle. */
    UICursor cursor;           /**< Cursor shown on hover (default UI_CURSOR_POINTER). */

    UIButtonCallback onClick;  /**< Fired on a completed click inside the button. */
    void* userdata;            /**< Opaque pointer passed to onClick. */
};

/**
 * Creates a button with initial text and font size. The default styles
 * form a primary-blue theme (use UIButton_SetColors or
 * UIButton_SetStateStyle to customize).
 */
UIButton* UIButton_Create(const char* text, float fontSize);

/**
 * Updates the label text. Invalidates the cached texture so the next
 * render rebuilds it with the new text.
 */
UIButton* UIButton_SetText(UIButton* btn, const char* text);

/**
 * Sets the font family (path to a .ttf file, typically obtained via
 * UIGetFont("FontName")).
 */
UIButton* UIButton_SetFontFamily(UIButton* btn, char* family);

/**
 * Sets the label's font size in points. Invalidates the cached texture
 * to force regeneration.
 */
UIButton* UIButton_SetFontSize(UIButton* btn, float size);

/**
 * Sets the label's font style as a bitmask of FontStyle flags (Bold,
 * Italic, Underline, Strikethrough). Delegates to the internal UIText
 * label; invalidates the cached glyph texture so the change takes
 * effect on the next render.
 */
UIButton* UIButton_SetFontStyle(UIButton* btn, int fontStyle);

/**
 * Sets the border radius of the button's background. Rounded corners
 * use the AA circle texture (analytic coverage).
 */
UIButton* UIButton_SetRadius(UIButton* btn, float radius);

/**
 * Sets the border width of the background. Use 0 for borderless buttons.
 */
UIButton* UIButton_SetBorderWidth(UIButton* btn, float width);

/**
 * Sets the button's outer margins (affect hit testing and its position
 * inside the parent).
 */
UIButton* UIButton_SetMargins(UIButton* btn, float l, float t, float r, float b);

/**
 * Replaces the style for a specific state.
 */
UIButton* UIButton_SetStateStyle(UIButton* btn, UIButtonState state, UIButtonStyle style);

/**
 * Shortcut: sets background + text color for the NORMAL state and
 * automatically derives HOVER (lightens), PRESSED (darkens), and
 * DISABLED (desaturates). Convenient for most cases.
 */
UIButton* UIButton_SetColors(UIButton* btn, UIColor background, UIColor text);

/**
 * Registers the click callback together with an optional userdata pointer.
 */
UIButton* UIButton_OnClick(UIButton* btn, UIButtonCallback cb, void* userdata);

/**
 * Enables (1) or disables (0) the button. When disabled, state becomes
 * UI_BUTTON_STATE_DISABLED and all mouse events are ignored.
 */
UIButton* UIButton_SetEnabled(UIButton* btn, int enabled);

/**
 * Sets the cursor shown while the mouse is hovering an enabled button.
 * Disabled buttons fall back to UI_CURSOR_NOT_ALLOWED automatically.
 */
UIButton* UIButton_SetCursor(UIButton* btn, UICursor cursor);

/**
 * Applies a drop shadow to the button's background (delegates to
 * UIRectangle_SetShadow). The shadow stays consistent across all
 * states (NORMAL/HOVER/PRESSED/DISABLED).
 */
UIButton* UIButton_SetShadow(UIButton* btn, UIShadow shadow);

/**
 * Clears any previously configured shadow.
 */
UIButton* UIButton_ClearShadow(UIButton* btn);

/**
 * Frees the button and its owned resources (background, label).
 * Passing NULL is safe.
 */
void UIButton_Destroy(UIButton* btn);

// ---------------------------------------------------------------------
// Internal dispatchers called by app.c when mouse events arrive. They
// walk the children list and update state / fire callbacks for buttons
// whose bounds contain the cursor.
// ---------------------------------------------------------------------
void UIButton_DispatchMouseMotion(UIChildren* children, float x, float y);
void UIButton_DispatchMouseDown  (UIChildren* children, float x, float y);
void UIButton_DispatchMouseUp    (UIChildren* children, float x, float y);

#endif // UIKIT_BUTTON_H
