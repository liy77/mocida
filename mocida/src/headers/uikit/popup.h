#ifndef UIKIT_POPUP_H
#define UIKIT_POPUP_H

#include <SDL3/SDL.h>
#include <uikit/widget.h>
#include <uikit/children.h>
#include <uikit/color.h>

#define UI_WIDGET_TOOLTIP  "@uikit/tooltip"
#define UI_WIDGET_MENU     "@uikit/menu"
#define UI_WIDGET_DROPDOWN "@uikit/dropdown"

// ---------------------------------------------------------------------
// UITooltip - hover-triggered floating label.
// ---------------------------------------------------------------------
// The tooltip widget sits at a high z-index. It tracks a `target`
// widget; when the mouse is inside `target`'s bounds for at least
// `delayMs`, the tooltip appears next to the cursor. Move out -> hides
// immediately. Tooltip text doesn't render unless `_visible` is on.

/**
 * Hover-triggered floating label. Watches its `target` widget; when
 * the cursor sits inside the target's bounds for at least `delayMs`,
 * the tooltip appears next to the cursor. Moving out hides it
 * immediately.
 */
typedef struct {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_TOOLTIP). */
    UIWidget* target;          /**< Borrowed pointer to the hover area. */
    char*     text;            /**< Heap-owned tooltip text. */
    char*     fontFamily;      /**< Heap-owned font path. */
    float     fontSize;        /**< Font size in points. */
    UIColor   bgColor;         /**< Background fill. */
    UIColor   textColor;       /**< Text color. */
    float     radius;          /**< Corner radius (pixels). */
    float     paddingX;        /**< Horizontal text padding (pixels). */
    float     paddingY;        /**< Vertical text padding (pixels). */
    Uint32    delayMs;         /**< Hover time required before showing (ms). */

    int     _visible;          /**< Internal: 1 once the tooltip is on screen. */
    int     _insideTarget;     /**< Internal: 1 while the cursor is over `target`. */
    Uint64  _enterMs;          /**< Internal: SDL_GetTicks() when the cursor entered. */
    float   _cursorX;          /**< Internal: last known cursor X (anchor). */
    float   _cursorY;          /**< Internal: last known cursor Y (anchor). */

    SDL_Texture* __SDL_textTexture; /**< Internal: lazy text texture rebuilt on change. */
    int          __cachedTextLen;   /**< Internal: length the texture was rendered for. */
} UITooltip;

UITooltip* UITooltip_Create  (UIWidget* target, const char* text, float fontSize);
UITooltip* UITooltip_SetText (UITooltip* tt, const char* text);
UITooltip* UITooltip_SetFontFamily(UITooltip* tt, char* family);
UITooltip* UITooltip_SetDelay(UITooltip* tt, Uint32 delayMs);
UITooltip* UITooltip_SetColors(UITooltip* tt, UIColor bg, UIColor text);
void       UITooltip_Destroy (UITooltip* tt);

// ---------------------------------------------------------------------
// UIMenu - simple popup with a list of clickable items. Drop the menu
// widget into the window's children, give it a high z-index, and call
// UIMenu_ShowAt() to make it visible at a point. Clicking an item runs
// its callback and hides the menu. Clicking outside also hides.
// ---------------------------------------------------------------------

typedef struct UIMenu UIMenu;
typedef void (*UIMenuItemCallback)(UIMenu* menu, int index, const char* label, void* userdata);

/**
 * Floating popup with a vertical list of clickable items. Call
 * UIMenu_ShowAt() to display the menu at a screen point; selecting
 * an item runs `onItem` and hides the menu. Clicks outside the menu
 * dismiss it.
 */
struct UIMenu {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_MENU). */

    int       visible;         /**< 0 hides the menu entirely. */
    float     anchorX;         /**< Top-left X of the menu when visible. */
    float     anchorY;         /**< Top-left Y of the menu when visible. */
    float     itemHeight;      /**< Height of each item row (pixels). */
    float     itemWidth;       /**< Item row width; 0 means auto-size from longest label. */
    float     radius;          /**< Corner radius (pixels). */
    UIColor   bgColor;         /**< Menu background fill. */
    UIColor   itemHoverColor;  /**< Background fill of the hovered item. */
    UIColor   textColor;       /**< Item text color. */
    UIColor   borderColor;     /**< Border color around the menu. */
    float     borderWidth;     /**< Border thickness (pixels). */
    float     paddingX;        /**< Inner horizontal padding (pixels). */
    float     paddingY;        /**< Inner vertical padding (pixels). */
    char*     fontFamily;      /**< Heap-owned font path. */
    float     fontSize;        /**< Item font size in points. */

    char**    labels;          /**< Heap-owned array of item labels, length itemCount. */
    int       itemCount;       /**< Number of items currently in the menu. */
    int       itemCapacity;    /**< Allocated capacity of `labels`. */
    int       hoverIndex;      /**< Index of the hovered item, or -1 for none. */

    UIMenuItemCallback onItem; /**< Fires when an item is clicked. */
    void* userdata;            /**< Opaque pointer forwarded to onItem. */
};

UIMenu*  UIMenu_Create  (float itemHeight, float itemWidth);
int      UIMenu_AddItem (UIMenu* m, const char* label);
UIMenu*  UIMenu_SetFont (UIMenu* m, char* family, float size);
UIMenu*  UIMenu_OnItem  (UIMenu* m, UIMenuItemCallback cb, void* userdata);
UIMenu*  UIMenu_ShowAt  (UIMenu* m, float x, float y);
UIMenu*  UIMenu_Hide    (UIMenu* m);
void     UIMenu_Destroy (UIMenu* m);

// ---------------------------------------------------------------------
// UIDropdown - single-select with a label and a popup list. The button
// area shows the currently selected value. Clicking opens a UIMenu-
// flavoured popup; selecting an item closes it and fires the callback.
// ---------------------------------------------------------------------

typedef struct UIDropdown UIDropdown;
typedef void (*UIDropdownChangedCallback)(UIDropdown* d, int index, const char* label, void* userdata);

/**
 * Single-select control with a labelled button and a popup list. The
 * button area shows the currently selected option; clicking opens a
 * UIMenu-flavoured popup, and choosing an item closes it and fires
 * `onChange` with the new index.
 */
struct UIDropdown {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_DROPDOWN). */

    int       selectedIndex;   /**< Index of the currently selected option (-1 = none). */
    int       open;            /**< 1 while the popup list is showing. */
    int       hovered;         /**< Internal: mouse currently over the field. */
    int       pressed;         /**< Internal: mouse-down without release. */
    int       hoverIndex;      /**< Index of the popup row currently highlighted (-1 = none). */

    char**    labels;          /**< Heap-owned array of option labels, length itemCount. */
    int       itemCount;       /**< Number of options. */
    int       itemCapacity;    /**< Allocated capacity of `labels`. */

    char*     fontFamily;      /**< Heap-owned font path. */
    float     fontSize;        /**< Font size in points. */
    UIColor   bgColor;         /**< Field background fill. */
    UIColor   textColor;       /**< Text color (field and popup). */
    UIColor   borderColor;     /**< Border color around the field. */
    UIColor   itemHoverColor;  /**< Background fill of the hovered popup row. */
    float     borderWidth;     /**< Border thickness (pixels). */
    float     radius;          /**< Corner radius (pixels). */
    float     paddingX;        /**< Inner horizontal padding (pixels). */
    float     paddingY;        /**< Inner vertical padding (pixels). */
    float     popupItemHeight; /**< Height of each row in the popup list. */

    UIDropdownChangedCallback onChange; /**< Fires when selectedIndex changes. */
    void* userdata;                     /**< Opaque pointer forwarded to onChange. */
};

UIDropdown* UIDropdown_Create(void);
int         UIDropdown_AddOption (UIDropdown* d, const char* label);
UIDropdown* UIDropdown_SetSelected(UIDropdown* d, int index);
int         UIDropdown_GetSelected(UIDropdown* d);
UIDropdown* UIDropdown_SetFont   (UIDropdown* d, char* family, float size);
UIDropdown* UIDropdown_OnChange  (UIDropdown* d, UIDropdownChangedCallback cb, void* userdata);
void        UIDropdown_Destroy   (UIDropdown* d);

// ---------------------------------------------------------------------
// Dispatchers (called by app.c) for the whole popup family.
// ---------------------------------------------------------------------
void UIPopup_DispatchMouseMotion(UIChildren* children, float x, float y);
void UIPopup_DispatchMouseDown  (UIChildren* children, float x, float y, int button);
void UIPopup_DispatchMouseUp    (UIChildren* children, float x, float y, int button);

#endif // UIKIT_POPUP_H
