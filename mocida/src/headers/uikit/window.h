#ifndef UIKIT_WINDOW_H
#define UIKIT_WINDOW_H

// UIKit includes
#include <uikit/widget.h>
#include <uikit/text.h>
#include <uikit/color.h>
#include <uikit/children.h>
#include <uikit/event.h>
#include <uikit/image.h>
#include <uikit/extra.h>
#include <uikit/glass.h>

// SDL includes
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

typedef enum UIWindowDisplayMode {
    WINDOW_WINDOWED = 0,
    WINDOW_FULLSCREEN,
    WINDOW_BORDERLESS
} UIWindowDisplayMode;

// Properties
#define UI_PROP_MAX_EVENTS (char*)"mocida.events.max"

/**
 * UIProp structure representing a property of the UIWindow object.
 * It contains a key-value pair for the property.
 */
typedef struct {
    const char* key;   /**< Property name (e.g. UI_PROP_MAX_EVENTS). */
    void* value;       /**< Caller-owned value pointer (type depends on key). */
} UIProp;

/**
 * UIProps structure representing a collection of properties.
 * It contains an array of UIProp pointers, count, and capacity.
 */
typedef struct {
    UIProp** props;        /**< Heap array of property pointers, length == capacity. */
    unsigned int count;    /**< Number of valid entries in `props`. */
    unsigned int capacity; /**< Allocated slot count in `props`. */
} UIProps;

/**
 * UIWindow structure representing a window in the UI framework.
 * It contains properties for size, position, visibility,
 * title, background color, and child widgets.
 */
typedef struct {
    int width;                       /**< Logical width in pixels. */
    int height;                      /**< Logical height in pixels. */
    float x;                         /**< Window X on the screen. */
    float y;                         /**< Window Y on the screen. */
    int z;                           /**< Z-order hint for multi-window setups. */
    char* title;                     /**< Heap-owned window title string. */
    int visible;                     /**< 0 = hidden, 1 = shown. */
    float framerate;                 /**< Last measured FPS (driven by UIApp_Run). */
    UIChildren* children;            /**< Root children tree rendered into this window. */
    UIColor backgroundColor;         /**< Clear color used at the start of each frame. */
    UIWindowDisplayMode displayMode; /**< Windowed / borderless / fullscreen. */

    SDL_Renderer* sdlRenderer;       /**< Backing SDL renderer; NULL after destroy. */
    SDL_Window*   sdlWindow;         /**< Backing SDL window; NULL after destroy. */
    UIEventCallbackData** events;    /**< Sparse array of registered callbacks, indexed by UI_EVENT. */
    UIProps __ui_props;              /**< Internal property bag (see UIWindow_GetProperty). */

    UIBackdropMaterial backdrop;     /**< Active window-wide OS backdrop (Mica/Acrylic/KDE), or NONE. */
    UIColor backdropTint;            /**< Optional tint forwarded to the OS backdrop. */
    float   backdropTintOpacity;     /**< 0..1 strength of backdropTint. */
    int     backdropNative;          /**< 1 when a native compositor effect is active (window made transparent). */
} UIWindow;

/**
 * Enables (or updates / disables) the OS-level window backdrop. `material`
 * should be a window-wide family (Mica / Mica Alt / KDE blur-window);
 * UI_BACKDROP_AUTO resolves to the platform default and UI_BACKDROP_NONE
 * disables. When a native effect is applied the window clear color is made
 * transparent so the compositor's blur shows through.
 *
 * Region materials (Acrylic / Liquid Glass / Vibrancy) passed here degrade to
 * the closest window-wide equivalent.
 */
void UIWindow_SetBackdrop(UIWindow* window, UIBackdropMaterial material,
                          UIColor tint, float tintOpacity);

/**
 * Sets how many samples-per-side the analytic-coverage AA pipeline uses
 * for circles and rounded corners. When called before any UIWindow_Create,
 * also enables hardware MSAA on the OpenGL backend. When called later,
 * it merely invalidates the cache and regenerates the textures at the
 * new quality.
 *
 * Typical values: 1 (no AA), 2, 4 (default), 8 (ultra).
 */
void UIWindow_SetMSAASamples(int samples);

/**
 * Returns the currently configured samples-per-side.
 */
int UIWindow_GetMSAASamples(void);

/**
 * Sets the AA pipeline (1 = COVERAGE only, 2 = SSAA 2x, 3 = SSAA 4x,
 * 4 = FXAA post, 5 = TAA). Forwarded by UIApp_SetAAMode. Mirrors the
 * integer values of UIAAMode in app.h to avoid pulling that header
 * here.
 */
void UIWindow_SetAAMode(int mode);
int  UIWindow_GetAAMode(void);

/**
 * History weight for the TAA pass (0..1). 0.5 by default.
 */
void  UIWindow_SetTAABlend(float alpha);
float UIWindow_GetTAABlend(void);

/**
 * Sets the motion threshold (0..255) used by TAA's per-pixel rejection.
 * Lower = more sensitive (less ghosting but also less smoothing on slow
 * motion); higher = more permissive blending (smoother but a bit of
 * ghosting can appear on fast motion). Default 24 (~9% per channel).
 */
void UIWindow_SetTAAMotionThreshold(int threshold);
int  UIWindow_GetTAAMotionThreshold(void);

/**
 * Drops every renderer-owned cache: circle textures, shadow textures,
 * the AA offscreen target and the TAA history. Useful in low-memory
 * situations or when the user finishes an interaction-heavy screen.
 * The next render rebuilds whatever it needs lazily.
 */
void UIWindow_TrimCaches(void);

/**
 * Renders the UIWindow and its child widgets.
 * @param window Pointer to the UIWindow object.
 * @return 0 on success, -1 on failure.
 */
int UIWindow_Render(UIWindow* window);

/**
 * Creates a UIWindow object with the specified title, width, and height.
 * @param title Title of the window.
 * @param width Width of the window.
 * @param height Height of the window.
 * @return A pointer to the created UIWindow object.
 */
UIWindow* UIWindow_Create(const char* title, int width, int height);

/**
 * Requests client-side window decorations (a custom title bar). Must be
 * called BEFORE UIWindow_Create / UIApp_Create — it controls the
 * SDL_WINDOW_BORDERLESS flag at creation time. When on, the window is created
 * without the native title bar/frame (still resizable); the app paints its own
 * bar and marks the drag region + resize borders via the hit-test wired in
 * app.c (see UIApp_SetDragRegion). Pass 0 for the default native chrome.
 */
void UIWindow_RequestCustomTitlebar(int on);

/** Returns 1 if a custom (client-side) title bar was requested. */
int  UIWindow_WantsCustomTitlebar(void);

/**
 * Requests a transparent (per-pixel alpha) window. Must be called BEFORE
 * UIWindow_Create / UIApp_Create — it adds SDL_WINDOW_TRANSPARENT at creation
 * time so SDL builds a composition swapchain (DirectComposition on the D3D11
 * renderer) that DWM can blend its system backdrop (Mica/Acrylic) behind. The
 * app still clears opaque by default, so the window looks identical until a
 * backdrop zeroes the clear alpha (UIWindow_SetBackdrop). Only the D3D11
 * renderer honors this on Windows; Vulkan/GL create an opaque swapchain. Pass 0
 * for the default opaque window.
 */
void UIWindow_RequestTransparent(int on);

/** Returns 1 if a transparent (per-pixel alpha) window was requested. */
int  UIWindow_WantsTransparent(void);

/**
 * Re-applies the native OS decorations (rounded corners + drop shadow + snap)
 * to a custom-titlebar window. Going SDL_WINDOW_BORDERLESS strips the DWM frame
 * on Win11, which kills the rounded corners and shadow; this asks the OS to put
 * them back without bringing back the caption:
 *
 *   - Windows 11: DWMWA_WINDOW_CORNER_PREFERENCE = ROUND so DWM rounds the
 *     borderless window, plus a 1px DwmExtendFrameIntoClientArea so the drop
 *     shadow is drawn. Aero-snap / maximize-to-workarea already work through
 *     SDL's borderless hit-test. No-op on Win10 (no corner-preference API).
 *   - macOS: the NSWindow is configured for a full-size content view with a
 *     transparent, hidden titlebar (NSWindowStyleMaskFullSizeContentView +
 *     titlebarAppearsTransparent + cleared title string), which keeps native
 *     rounding, shadow, resize and the traffic-light buttons. Implemented in
 *     titlebar_cocoa.mm. (The window must NOT be created borderless on macOS;
 *     UIWindow_Create honours that — see the g_customTitlebar branch.)
 *   - Linux / other: no-op (the borderless + SSD path is left as-is).
 *
 * Safe to call repeatedly; called automatically by UIWindow_Create right after
 * the window exists when a custom titlebar was requested.
 */
#ifdef __cplusplus
extern "C" {
#endif
void UIWindow_ApplyNativeDecorations(SDL_Window* window);
#ifdef __cplusplus
}
#endif

/**
 * 1 when running on macOS, 0 elsewhere. The MUI/host uses this to keep its own
 * min/max/close window-control buttons Windows/Linux-only (macOS keeps its
 * native traffic-lights) and to offset the top bar for the traffic-lights.
 * A compile-time constant — no window needed.
 */
int  UIWindow_IsMacOS(void);

/**
 * Returns the active window - the last one created via UIWindow_Create
 * or assigned via UIWindow_SetActive. Used by widget-level focus
 * helpers (UITextField_SetFocus, etc.) so callers don't have to thread
 * a window pointer through. Returns NULL when no window exists.
 */
UIWindow* UIWindow_GetActive(void);

/** Override the active window. Pass NULL to clear. */
void UIWindow_SetActive(UIWindow* window);

/**
 * Sets the event callback for the UIWindow object.
 * @param window Pointer to the UIWindow object.
 * @param event Event type to be set.
 * @param callback Callback function to be called on the event.
 * @return None.
 */
void UIWindow_SetEventCallback(UIWindow* window, UI_EVENT event, UIEventCallback callback);

/**
 * Gets a property of the UIWindow object.
 * @param window Pointer to the UIWindow object.
 * @param property Property name to be retrieved.
 * @return Pointer to the property value.
 */
void* UIWindow_GetProperty(UIWindow* window, const char* property);

/**
 * Sets a property of the UIWindow object.
 * @param window Pointer to the UIWindow object.
 * @param property Property name to be set.
 * @param value Value to be set for the property.
 * @return None.
 */
void UIWindow_SetProperty(UIWindow* window, const char* property, void* value);

/**
 * Destroys the UIWindow object and frees its resources.
 * @param window Pointer to the UIWindow object to be destroyed.
 * @return None.
 */
void UIWindow_Destroy(UIWindow* window);

/**
 * Emits an event to the UIWindow object.
 * @param window Pointer to the UIWindow object.
 * @param event Event type to be emitted.
 * @param data Data associated with the event.
 * @return None.
 */
void UIWindow_EmitEvent(UIWindow* app, UI_EVENT event, UIEventData data);

#endif // UIKIT_WINDOW_H