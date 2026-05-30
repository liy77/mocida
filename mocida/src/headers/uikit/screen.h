#ifndef UIKIT_SCREEN_H
#define UIKIT_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Screen / display metrics, for computing layout sizes relative to the
 * device screen (essential on mobile, where one fixed pixel size can't
 * fit every device). Values are in the same logical points the widget
 * coordinate system uses.
 *
 * Reports the PRIMARY display's bounds. On mobile that is the device
 * screen; on desktop it is the monitor the app opened on. For sizing
 * against the actual window (which can be smaller than the screen on
 * desktop) use the width/height delivered to UIApp_OnResize instead.
 */
typedef struct {
    int width;   /**< Screen width in logical points. */
    int height;  /**< Screen height in logical points. */
} UIScreenSize;

/** Returns the primary screen size. Both fields are 0 if SDL video is
 *  not initialised yet. Lets you write Screen.width / Screen.height:
 *      UIScreenSize Screen = UIScreen_GetSize();
 *      float w = Screen.width * 0.5f;            // half the screen wide */
UIScreenSize UIScreen_GetSize(void);

/** Convenience scalar accessors (same data as UIScreen_GetSize). */
int UIScreen_GetWidth(void);
int UIScreen_GetHeight(void);

/**
 * Safe-area insets (in logical points) — the margins to keep clear of the
 * notch / Dynamic Island / status bar / home indicator / rounded corners.
 * On iOS `top` is typically the status-bar or notch height; on a desktop
 * window everything is 0. Lay your top-level content out inside these so it
 * isn't hidden behind device cutouts:
 *
 *     UIScreenInsets safe = UIScreen_GetSafeArea();
 *     float topY = safe.top + 16.0f;   // 16pt below the notch
 */
typedef struct {
    int top;     /**< Inset from the top edge (notch / status bar). */
    int left;    /**< Inset from the left edge. */
    int bottom;  /**< Inset from the bottom edge (home indicator). */
    int right;   /**< Inset from the right edge. */
} UIScreenInsets;

/** Safe-area insets of the active window (all 0 on desktop / if unknown). */
UIScreenInsets UIScreen_GetSafeArea(void);

#ifdef __cplusplus
}
#endif

#endif // UIKIT_SCREEN_H
