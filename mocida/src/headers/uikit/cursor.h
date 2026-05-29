#ifndef UIKIT_CURSOR_H
#define UIKIT_CURSOR_H

/**
 * Logical mouse cursor used by interactive widgets. Maps onto SDL's
 * system cursors but kept enum-stable so callers don't need to pull in
 * SDL headers.
 */
typedef enum {
    UI_CURSOR_DEFAULT = 0,   /**< Plain arrow.                       */
    UI_CURSOR_POINTER,       /**< Pointing hand (clickables).        */
    UI_CURSOR_TEXT,          /**< I-beam (text fields).              */
    UI_CURSOR_CROSSHAIR,     /**< Crosshair (precision picks).       */
    UI_CURSOR_MOVE,          /**< Four-arrow move.                   */
    UI_CURSOR_NOT_ALLOWED,   /**< Forbidden / disabled.              */
    UI_CURSOR_WAIT,          /**< Busy / hourglass.                  */
    UI_CURSOR_PROGRESS,      /**< Working in background.             */
    UI_CURSOR_EW_RESIZE,     /**< Left-right resize.                 */
    UI_CURSOR_NS_RESIZE,     /**< Up-down resize.                    */
    UI_CURSOR_NWSE_RESIZE,   /**< Diagonal resize.                   */
    UI_CURSOR_NESW_RESIZE    /**< Other diagonal resize.             */
} UICursor;

/**
 * Switches the active mouse cursor to the given logical kind. System
 * cursors are allocated lazily on first use and cached for the rest of
 * the process lifetime. Safe to call multiple times with the same kind
 * (SDL no-ops when the cursor is already active).
 */
void UICursor_Apply(UICursor kind);

/**
 * Frees the cached system cursors. Called from UIApp_Destroy; safe to
 * call manually too.
 */
void UICursor_Shutdown(void);

#endif // UIKIT_CURSOR_H
