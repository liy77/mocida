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

#ifdef __cplusplus
}
#endif

#endif // UIKIT_SCREEN_H
