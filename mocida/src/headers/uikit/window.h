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
} UIWindow;

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