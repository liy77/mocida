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
 *
 * A kind that has a custom image installed via `UICursor_SetImage`
 * shows that image instead of the system shape.
 */
void UICursor_Apply(UICursor kind);

/**
 * Replaces the image used for one logical cursor kind.
 *
 * `path` is an image SDL_image can decode (PNG, CUR, SVG, …), resolved
 * the same way as other assets (CWD, then next to the executable).
 * `hot_x` / `hot_y` are the click point inside the image, in pixels.
 * Pass NULL (or an empty path) to drop the override and go back to the
 * system cursor for that kind.
 *
 * The next `UICursor_Apply` of this kind picks the new image up. Widgets
 * that already advertise the kind (pointer, text, resize, …) start
 * showing it without any other change.
 *
 * @return 1 on success, 0 if the image could not be loaded.
 */
int UICursor_SetImage(UICursor kind, const char* path, int hot_x, int hot_y);

/**
 * Drops every custom cursor image. System cursors stay cached.
 */
void UICursor_ClearImages(void);

/**
 * Frees the cached system cursors and any custom images. Called from
 * UIApp_Destroy; safe to call manually too.
 */
void UICursor_Shutdown(void);

#endif // UIKIT_CURSOR_H
