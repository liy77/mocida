#ifndef UIKIT_APP_H
#define UIKIT_APP_H

#include <uikit/color.h>
#include <uikit/window.h>
#include <uikit/children.h>
#include <uikit/widget.h>
#include <uikit/text.h>
#include <uikit/rect.h>
#include <uikit/alignment.h>
#include <uikit/font.h>
#include <uikit/button.h>
#include <uikit/mouse_area.h>
#include <uikit/container.h>
#include <uikit/controls.h>
#include <uikit/textfield.h>
#include <uikit/textarea.h>
#include <uikit/webview.h>
#include <uikit/stack.h>
#include <uikit/dialog.h>
#include <uikit/tab.h>
#include <uikit/theme.h>
#include <uikit/popup.h>
#include <uikit/sound.h>
#include <uikit/video.h>
#include <uikit/file_drop.h>
#include <uikit/file_dialog.h>
#include <uikit/anim.h>
#include <uikit/clipboard.h>
#include <uikit/cursor.h>
#include <uikit/debug.h>
#include <uikit/profile.h>
#include <uikit/screen.h>
#include <uikit/bundle.h>
#include <uikit/overlay.h>
#include <uikit/crash.h>
#include <uikit/walker.h>

// On iOS the app is launched by SDL's UIKit app delegate
// (UIApplicationMain), which calls SDL_main. Including SDL_main.h renames
// the app's main() to SDL_main and pulls in that bootstrap. We do it here
// automatically so app authors never have to write the boilerplate.
// Guarded so the library's OWN translation units (which define
// MOCIDA_BUILDING_LIBRARY) never pull in SDL_main.h / the `main` macro.
#if defined(MOCIDA_IOS) && !defined(MOCIDA_BUILDING_LIBRARY)
#include <SDL3/SDL_main.h>
#endif

typedef int UIRenderDriver;

#define UI_RENDER_SOFTWARE (UIRenderDriver)0
#define UI_RENDER_OPENGL (UIRenderDriver)1
#define UI_RENDER_VULKAN (UIRenderDriver)2

#ifdef _WIN32
#define UI_RENDER_3D12 (UIRenderDriver)3
#define UI_RENDER_3D11 (UIRenderDriver)4
#define UI_RENDER_3D9 (UIRenderDriver)5
#endif

#ifdef __APPLE__
#define UI_RENDER_METAL (UIRenderDriver)6
#endif

#define UI_RENDER_GPU (UIRenderDriver)7

/**
 * Screen-orientation bit flags for the iOS rotation lock. Combine with
 * bitwise-OR and pass to UIApp_SetOrientation to restrict which
 * orientations the device may rotate into. Default is UI_ORIENTATION_ALL
 * (every orientation allowed - the current behavior).
 *
 * No-op on desktop (SDL ignores the orientation hint off-device).
 */
typedef enum {
    UI_ORIENTATION_PORTRAIT             = (1 << 0), /**< Portrait, upright. */
    UI_ORIENTATION_PORTRAIT_UPSIDE_DOWN = (1 << 1), /**< Portrait, upside-down. */
    UI_ORIENTATION_LANDSCAPE_LEFT       = (1 << 2), /**< Landscape, home left. */
    UI_ORIENTATION_LANDSCAPE_RIGHT      = (1 << 3)  /**< Landscape, home right. */
} UIOrientation;

/** Both landscape orientations. */
#define UI_ORIENTATION_LANDSCAPE \
    (UI_ORIENTATION_LANDSCAPE_LEFT | UI_ORIENTATION_LANDSCAPE_RIGHT)

/** Every orientation (the default). */
#define UI_ORIENTATION_ALL \
    (UI_ORIENTATION_PORTRAIT | UI_ORIENTATION_PORTRAIT_UPSIDE_DOWN | \
     UI_ORIENTATION_LANDSCAPE_LEFT | UI_ORIENTATION_LANDSCAPE_RIGHT)

// Pass this value (or any <= 0) to UIApp_SetTargetFPS to unlock the
// frame rate (renders as fast as possible).
#define UI_FPS_UNLIMITED 0

/**
 * Anti-aliasing quality presets. The value represents "samples per side"
 * used by the analytic-coverage rasterizer for circles and rounded
 * corners (e.g. HIGH = 4 -> 4*4 = 16 samples per pixel, equivalent in
 * quality to 16x MSAA for those shapes).
 *
 * Also enables hardware MSAA on the OpenGL backend with the same sample
 * count (no effect on D3D / Vulkan / Metal, where the analytic-coverage
 * path already produces a superior, backend-consistent result).
 */
typedef enum {
    UI_QUALITY_LOW    = 1, // no AA - aliased edges, maximum performance
    UI_QUALITY_MEDIUM = 2, // 4 SPP  - good for small/fast UI
    UI_QUALITY_HIGH   = 4, // 16 SPP - default, strong MSAA-equivalent
    UI_QUALITY_ULTRA  = 8  // 64 SPP - for close-ups and screenshots
} UIRenderQuality;

/**
 * Anti-aliasing pipeline. UI_AA_COVERAGE is the default and uses the
 * analytic-coverage rasterizer at the quality configured via
 * UIApp_SetRenderQuality. The other modes add a full-frame stage on
 * top of it:
 *
 *   - SSAA_2X / SSAA_4X : renders the whole frame to a 2x / 4x texture
 *                         and bilinear-downscales to the window. Best
 *                         pure quality, GPU-bound.
 *   - FXAA              : after the frame is composed, a CPU edge-blur
 *                         pass detects high-contrast luma edges and
 *                         softens them. Cheap visual smoothing for
 *                         everything (text, image borders, geometry).
 *   - TAA               : blends the current frame with the previous
 *                         frame (final = lerp(prev, current, alpha)).
 *                         Excellent for static UI; can ghost on motion.
 */
typedef enum {
    UI_AA_NONE      = 0, /**< Coverage AA disabled (samples-per-side = 1). */
    UI_AA_COVERAGE  = 1, /**< Default: analytic-coverage AA only. */
    UI_AA_SSAA_2X   = 2, /**< Render at 2x, downscale.   ~4x fill cost. */
    UI_AA_SSAA_4X   = 3, /**< Render at 4x, downscale.   ~16x fill cost. */
    UI_AA_FXAA      = 4, /**< Coverage + post-process FXAA-style edge blur. */
    UI_AA_TAA       = 5  /**< Coverage + temporal accumulation. */
} UIAAMode;

/**
 * UIApp structure representing the main application.
 * It contains properties for the main window, background color,
 * and other application settings.
 */
/**
 * Resize callback: fires every time the window changes size, including
 * mid-drag during the OS modal sizing loop. Use it to adjust widget
 * dimensions that aren't covered by the alignment system (e.g. to keep
 * a UIWebView filling the available area).
 */
typedef void (*UIAppResizeCallback)(int width, int height, void* userdata);

/**
 * Top-level application object. Owns the main window, the root widget,
 * and global render / animation tuning knobs. One UIApp is enough for
 * the vast majority of programs; multi-window apps share these
 * settings across windows.
 */
typedef struct {
    UIWindow* window;          /**< Backing window the app is rendering into. */
    UIWidget* mainWidget;      /**< Root widget mounted under `window->children`. */
    UIColor backgroundColor;   /**< Window clear color used between frames. */
    int targetFps;             /**< Target frame rate; 60 by default. <= 0 unlocks. */
    int msaaSamples;           /**< Samples-per-side for analytic-coverage AA. Default 4. */
    int aaMode;                /**< UIAAMode; default UI_AA_COVERAGE. */
    float taaBlend;            /**< [0..1] history weight when aaMode == UI_AA_TAA. 0.5 default. */

    UIAppResizeCallback onResize; /**< Fires on every window resize (incl. live-drag). */
    void*               onResizeUserdata; /**< Opaque pointer forwarded to onResize. */

    int   runInBackground;     /**< 1 = keep the run loop alive when the window is hidden (e.g. minimized to tray). */
    void* tray;                /**< SDL_Tray* for the desktop tray icon, or NULL. */
    unsigned orientations;     /**< iOS: allowed UIOrientation bitmask. Default UI_ORIENTATION_ALL. */
    int   statusBarHidden;     /**< iOS: 0 = status bar visible (default), 1 = hidden. */
} UIApp;

/**
 * Creates a UIApp object with the specified title, width, and height.
 * @param title Title of the application window.
 * @param width Width of the application window.
 * @param height Height of the application window.
 * @return A pointer to the created UIApp object.
 */
UIApp* UIApp_Create(const char* title, int width, int height);

/**
 * Gets the main window of the UIApp object.
 * @param app Pointer to the UIApp object.
 * @return Pointer to the UIWindow object.
 */
UIWidget* UIApp_GetWindow(UIApp* app);

/**
 * Gets a property of the UIApp object.
 * @param app Pointer to the UIApp object.
 * @param property Property name to be retrieved.
 * @return Pointer to the property value.
 */
void* UIApp_GetProperty(UIApp* app, const char* property);

/**
 * Sets a property of the UIApp object.
 * @param app Pointer to the UIApp object.
 * @param property Property name to be set.
 * @param value Value to be set for the property.
 * @return None.
 */
void UIApp_SetProperty(UIApp* app, const char* property, void* value);

/**
 * Emits an event to the UIApp object.
 * @param app Pointer to the UIApp object.
 * @param event Event type to be emitted.
 * @param data Data associated with the event.
 * @return None.
 */
void UIApp_EmitEvent(UIApp* app, UI_EVENT event, UIEventData data);

/**
 * Sets the child widgets of the UIApp object.
 * @param app Pointer to the UIApp object.
 * @param children Pointer to the UIChildren object containing child widgets.
 * @return None.
 */
void UIApp_SetChildren(UIApp* app, UIChildren* children);

/**
 * Sets the background color of the UIApp object.
 * @param app Pointer to the UIApp object.
 * @param color Background color to be set.
 * @return None.
 */
void UIApp_SetBackgroundColor(UIApp* app, UIColor color);

/**
 * Sets the title of the application window.
 * @param app Pointer to the UIApp object.
 * @param title Title to be set.
 * @return None.
 */
void UIApp_SetWindowTitle(UIApp* app, const char* title);

/**
 * Sets the app's display name: updates the bundle name (the name shown on
 * iOS / in the manifest, see UIApp_SetBundleName) AND the desktop window
 * title in one call. On iOS the home-screen name itself is fixed at build
 * time (from app.bundle); this still updates the in-app title state.
 */
void UIApp_SetName(UIApp* app, const char* name);

/**
 * Keep the run loop alive while the window is hidden (e.g. after minimizing
 * to the tray) instead of exiting when it stops being visible. Events still
 * pump (so the tray menu works); rendering is skipped while hidden. Desktop
 * feature — iOS controls background execution itself.
 */
void UIApp_SetRunInBackground(UIApp* app, int enabled);

/** Tray menu item click callback. */
typedef void (*UITrayCallback)(void* userdata);

/**
 * Installs a desktop system-tray (menu-bar / notification-area) icon from
 * an image path (a mocida:// URI works). `tooltip` may be NULL. Returns 1
 * on success. No-op (returns 0) on iOS. Add menu items with
 * UIApp_AddTrayMenuItem; without any items the icon is still shown.
 */
int UIApp_SetTrayIcon(UIApp* app, const char* iconPath, const char* tooltip);

/**
 * Appends a clickable item to the tray icon's menu. `cb(userdata)` fires
 * when the user selects it. Requires UIApp_SetTrayIcon first. No-op on iOS.
 */
void UIApp_AddTrayMenuItem(UIApp* app, const char* label,
                           UITrayCallback cb, void* userdata);

/**
 * Sets the window icon from an image file (PNG, JPG, BMP, ICO - any
 * format supported by SDL_image). For sharp results in taskbars, prefer
 * PNG/ICO with multiple sizes (16, 32, 48, 256).
 *
 * @param app  Pointer to the UIApp object.
 * @param path Icon file path (absolute or relative; resolved via
 *             UIAsset_LoadSurface, which tries CWD then exe dir).
 * @return 1 on success, 0 on failure (see stderr for details).
 */
int UIApp_SetWindowIcon(UIApp* app, const char* path);

/**
 * Variant taking an already-loaded SDL_Surface. Useful when the icon
 * comes from memory (e.g. embedded resources, procedural generation).
 * The surface is NOT consumed; the caller remains its owner.
 */
int UIApp_SetWindowIconFromSurface(UIApp* app, SDL_Surface* surface);

/**
 * Sets the size of the application window.
 * @param app Pointer to the UIApp object.
 * @param width Width to be set.
 * @param height Height to be set.
 * @return None.
 */
void UIApp_SetWindowSize(UIApp* app, int width, int height);

/**
 * Sets the position of the application window.
 * @param app Pointer to the UIApp object.
 * @param x X-coordinate to be set.
 * @param y Y-coordinate to be set.
 * @return None.
 */
void UIApp_SetWindowPosition(UIApp* app, int x, int y);

/**
 * Toggles whether the user can resize the application window. The
 * window is resizable by default. Pass 0 to lock it to the size given
 * at UIApp_Create (or the last UIApp_SetWindowSize call); pass 1 to
 * re-enable the resize grip and the OS maximize button.
 *
 * Forwards to SDL_SetWindowResizable. No-op if the window has not
 * been created yet.
 */
void UIApp_SetResizable(UIApp* app, int resizable);

/**
 * Constrains how small the user can resize the window. Useful for apps
 * whose layout assumes a minimum viewport (e.g. fixed-size widgets
 * arranged in a 3-row anchor matrix that overlap when squeezed).
 * Pass any dimension < 1 and it is clamped back to 1.
 *
 * Forwards to SDL_SetWindowMinimumSize.
 */
void UIApp_SetMinSize(UIApp* app, int width, int height);

/** Mirror of UIApp_SetMinSize for the upper bound. */
void UIApp_SetMaxSize(UIApp* app, int width, int height);

/**
 * Sets the scale mode of the application window.
 * @param app Pointer to the UIApp object.
 * @param displayMode Display mode to be set.
 * @return None.
 */
void UIApp_SetWindowDisplayMode(UIApp* app, UIWindowDisplayMode displayMode);

/**
 * Sets the render driver of the application window.
 * @param app Pointer to the UIApp object.
 * @param renderDriver Render driver to be set.
 * @return None.
 */
void UIApp_ShowWindow(UIApp* app);

/**
 * Hides the application window.
 * @param app Pointer to the UIApp object.
 * @return None.
 */
void UIApp_HideWindow(UIApp* app);

/**
 * Sets the render driver of the application window.
 * @param app Pointer to the UIApp object.
 * @param renderDriver Render driver to be set.
 * @return None.
 */
void UIApp_SetRenderDriver(UIApp* app, UIRenderDriver renderDriver);

/**
 * Sets an event callback to the UIApp object.
 * @param app Pointer to the UIApp object.
 * @param event Event type to listen for.
 * @param callback Callback function to be called when the event occurs.
 * @return None.
 */
void UIApp_SetEventCallback(UIApp* app, UI_EVENT event, UIEventCallback callback);

/**
 * Registers a callback fired every time the window is resized. The
 * callback receives the new width/height and the userdata pointer passed
 * here. Useful for updating widget sizes (the alignment system
 * repositions widgets but doesn't resize them).
 */
void UIApp_OnResize(UIApp* app, UIAppResizeCallback cb, void* userdata);

/**
 * Console window control (Windows only). Mocida apps link as the
 * GUI subsystem, so no console window appears by default. In Debug
 * builds (MOCIDA_DEBUG defined) UIApp_Create automatically allocates
 * a console at startup so logs are visible — you can suppress that by
 * setting the env var MOCIDA_NO_CONSOLE=1 before launching, or by
 * calling UIApp_HideConsole() right after UIApp_Create.
 *
 * In Release builds no console is allocated and these calls are
 * no-ops; the logs simply have nowhere to go.
 */
void UIApp_EnableConsole(void);
void UIApp_HideConsole  (void);
int  UIApp_IsConsoleVisible(void);

/**
 * Declares this process's AppUserModelID (AUMID). Windows uses it as
 * the app identity for taskbar pinning, jump lists, and crucially
 * Task Manager grouping. Without it, WebView2's child processes show
 * up as "Gerenciador WebView2" in Task Manager; with it, they nest
 * under your app's name (similar to how WhatsApp Desktop groups).
 *
 * Call this VERY early - ideally right after UIApp_Create, before any
 * UIWebView is constructed. WebView2 child processes inherit the
 * AUMID from their parent at spawn time, so anything started before
 * this call keeps the default group.
 *
 * Format: "Vendor.Product" or "Vendor.Product.Component" (max 128
 * ASCII chars, no whitespace). Example: "Mocida.Demo".
 *
 * No-op on non-Windows. Note: 100%-clean Task Manager grouping
 * (matching WhatsApp's MSIX behaviour) requires distributing the app
 * as an MSIX/AppX package; AUMID is the 90% solution for plain .exe.
 */
void UIApp_SetAppId(UIApp* app, const char* aumid);

/**
 * Restricts the device orientations the app may rotate into. Pass a
 * bitmask of UIOrientation flags, e.g.:
 *   UIApp_SetOrientation(app, UI_ORIENTATION_PORTRAIT);  // portrait only
 *   UIApp_SetOrientation(app, UI_ORIENTATION_LANDSCAPE); // landscape only
 *   UIApp_SetOrientation(app, UI_ORIENTATION_ALL);       // all (default)
 *
 * On iOS this is honored live: rotation into a disallowed orientation is
 * blocked. Call it any time (typically right after UIApp_Create). The
 * Info.plist lists all four orientations so this runtime mask is the real
 * gate. No-op on desktop (SDL ignores the orientation hint there).
 */
void UIApp_SetOrientation(UIApp* app, unsigned orientations);

/**
 * Returns the currently configured orientation bitmask, or
 * UI_ORIENTATION_ALL if never set.
 */
unsigned UIApp_GetOrientation(UIApp* app);

/**
 * Destroys the UIApp object and frees its resources.
 * @param app Pointer to the UIApp object to be destroyed.
 * @return None.
 */
void UIApp_Destroy(UIApp* app);

/**
 * Sets the main loop's target FPS.
 * @param app Pointer to the UIApp object.
 * @param fps Target FPS (>= 1). Pass UI_FPS_UNLIMITED (0) or any value
 *            <= 0 to unlock the frame rate.
 */
void UIApp_SetTargetFPS(UIApp* app, int fps);

/**
 * Returns the currently configured target FPS (0 = unlimited).
 * @param app Pointer to the UIApp object.
 */
int UIApp_GetTargetFPS(UIApp* app);

/** Current window size in logical points (real device size on iOS). */
int UIApp_GetWidth(UIApp* app);
int UIApp_GetHeight(UIApp* app);

/**
 * Sets the number of samples-per-side used by the analytic-coverage AA
 * (rasterization of circles and rounded corners). Recommended values
 * come from the UIRenderQuality enum (1, 2, 4, 8). Invalidates the
 * texture cache so it can be regenerated at the new quality.
 *
 * Note: for the OpenGL hardware MSAA hint to take effect, this must be
 * called BEFORE UIApp_Create. After the window is created only the
 * CPU coverage AA is affected.
 */
void UIApp_SetMSAASamples(UIApp* app, int samples);

/**
 * Returns the currently configured samples-per-side.
 */
int UIApp_GetMSAASamples(UIApp* app);

/**
 * Sets the render quality for the app (shortcut that forwards to
 * UIApp_SetMSAASamples).
 */
void UIApp_SetRenderQuality(UIApp* app, UIRenderQuality quality);

/**
 * Selects the anti-aliasing pipeline. See UIAAMode for the trade-offs.
 * Can be changed at runtime - the per-frame pipeline picks up the new
 * value on the next UIWindow_Render call.
 */
void UIApp_SetAAMode(UIApp* app, UIAAMode mode);

/**
 * Returns the current AA mode.
 */
UIAAMode UIApp_GetAAMode(UIApp* app);

/**
 * Sets the blend weight used by UI_AA_TAA. 0.0 = ignore history (no
 * smoothing), 1.0 = ignore current frame (frozen). Sensible range
 * is 0.3 .. 0.7. Default 0.5.
 */
void UIApp_SetTAABlend(UIApp* app, float alpha);

/**
 * Releases every cached renderer resource (circle/shadow textures,
 * AA target, TAA history). Subsequent renders rebuild what's needed
 * lazily. Call this when transitioning between heavy and light UI
 * states (eg leaving a gallery screen) to free GPU/RAM.
 */
void UIApp_TrimCaches(UIApp* app);

/**
 * Snapshot of allocator stats reported by mimalloc when MOCIDA_USE_MIMALLOC
 * is on. All sizes are in bytes. When the build was made without
 * mimalloc, every field is 0.
 */
typedef struct {
    size_t current;       /**< Live allocated bytes right now.        */
    size_t peak;          /**< Highest live bytes during this run.    */
    size_t reserved;      /**< Virtual address space reserved.        */
    size_t committed;     /**< Physically committed bytes.            */
    size_t mallocRequests;/**< Total bytes requested by callers.      */
} UIMemoryStats;

void UIApp_GetMemoryStats(UIApp* app, UIMemoryStats* out);

/**
 * Controls iOS system status bar (clock / battery / signal) visibility.
 * The status bar is VISIBLE by default (hidden = 0); pass 1 to hide it.
 * Honored live - call before or after the window exists. No-op on desktop.
 *
 * @param app    Pointer to the UIApp object.
 * @param hidden 0 = status bar visible (default), 1 = status bar hidden.
 */
void UIApp_SetStatusBarHidden(UIApp* app, int hidden);

/**
 * Runs the main loop of the application.
 * @param app Pointer to the UIApp object.
 * @return None.
 */
void UIApp_Run(UIApp* app);

#endif