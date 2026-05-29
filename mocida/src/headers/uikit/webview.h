#ifndef UIKIT_WEBVIEW_H
#define UIKIT_WEBVIEW_H

#include <uikit/widget.h>
#include <uikit/color.h>
#include <uikit/children.h>

#define UI_WIDGET_WEBVIEW "@uikit/webview"

/**
 * Embeds a real browser (Microsoft Edge / WebView2 on Windows) inside
 * the widget's bounds. The native control is created lazily on the
 * first render so that creating a UIWebView before UIApp_ShowWindow
 * works.
 *
 * Notes:
 *   - Requires the Microsoft Edge runtime (installed by default on
 *     Windows 10 22H2 / 11).
 *   - The widget MUST have an explicit size (use widgcs or
 *     UIWidget_SetSize), otherwise the renderer has nothing to map
 *     the browser surface to.
 *   - WebView2 draws straight to the window via a child HWND - it
 *     does NOT compose through SDL. As a consequence the WebView
 *     always paints on top of widgets that share its bounds. Reserve
 *     a dedicated region for it (or use UIWidget_SetZIndex with a
 *     lower z than the overlays you want to keep visible).
 */
typedef struct UIWebView UIWebView;

/** Lifecycle callback invoked once the browser is ready to use. */
typedef void (*UIWebViewReadyCallback)(UIWebView* wv, void* userdata);

/**
 * Fires when the webview's URL changes (link click, JS navigation,
 * history nav, etc). The url string is UTF-8 and only valid for the
 * duration of the call - copy it if you need to keep it.
 */
typedef void (*UIWebViewUrlChangeCallback)(UIWebView* wv, const char* url, void* userdata);

/**
 * Fires when the loading state changes. `loading` is 1 when a
 * navigation starts, 0 when it completes (success or failure).
 */
typedef void (*UIWebViewLoadingCallback)(UIWebView* wv, int loading, void* userdata);

/**
 * Request interception callback. Fires for every network request the
 * webview makes (top-level navigations, sub-resources, fetch, XHR,
 * etc).
 *
 * Return value:
 *   - 0 : allow the request (default behavior).
 *   - 1 : block the request - the browser receives an "aborted"
 *         response and the resource fails to load.
 *
 * The URL is in UTF-8 and is only valid for the duration of the call;
 * copy it if you need to keep it.
 */
typedef int (*UIWebViewRequestCallback)(UIWebView* wv, const char* url, void* userdata);

/**
 * Creates a UIWebView pointed at the given URL. Pass NULL to start
 * blank (you can navigate later with UIWebView_Navigate).
 */
UIWebView* UIWebView_Create(const char* initialUrl);

/** Navigates to a new URL. Safe to call before the browser is ready;
 *  the navigation is queued until creation completes. */
UIWebView* UIWebView_Navigate(UIWebView* wv, const char* url);

/** Returns the current URL or NULL if none. The returned string is
 *  owned by the widget; copy it if you need to keep it. */
const char* UIWebView_GetUrl(UIWebView* wv);

/** Reloads the current page. */
UIWebView* UIWebView_Reload(UIWebView* wv);

/** History navigation. */
UIWebView* UIWebView_GoBack   (UIWebView* wv);
UIWebView* UIWebView_GoForward(UIWebView* wv);

/** Registers a callback fired once the browser becomes ready. Useful
 *  for executing initial scripts or wiring up additional events. */
UIWebView* UIWebView_OnReady(UIWebView* wv, UIWebViewReadyCallback cb, void* userdata);

/**
 * Subscribes to URL-change notifications (WebView2 SourceChanged event).
 * The callback fires every time the displayed URL changes, including
 * link clicks, script navigation, and back/forward.
 *
 * Passing cb = NULL clears the handler.
 */
UIWebView* UIWebView_OnUrlChange(UIWebView* wv,
                                 UIWebViewUrlChangeCallback cb,
                                 void* userdata);

/**
 * Subscribes to loading-state notifications. The callback fires with
 * `loading = 1` when NavigationStarting fires, and `loading = 0` when
 * NavigationCompleted fires. Useful for driving a progress indicator.
 *
 * Passing cb = NULL clears the handler.
 */
UIWebView* UIWebView_OnLoadingChange(UIWebView* wv,
                                     UIWebViewLoadingCallback cb,
                                     void* userdata);

/* ===================================================================
 * D2D-rendered overlays (composition mode only)
 *
 * When the webview is in composition mode (UIWebView_SetCompositionMode),
 * these overlays are rendered by Direct2D into the same DComp tree
 * the webview uses. They have per-pixel alpha so rounded shapes
 * compose cleanly on top of the page - no SetWindowRgn punch
 * artifacts.
 *
 * In HWND mode these calls are no-ops (return -1 / do nothing).
 * =================================================================== */

/** Adds a rounded-rect overlay at (x, y, w, h) filled with `color`.
 *  Returns a non-negative handle or -1 on failure. */
int UIWebView_AddD2DOverlay(UIWebView* wv,
                            int x, int y, int w, int h,
                            float radius,
                            UIColor fill);

/** Updates the text inside an existing overlay. */
void UIWebView_SetD2DOverlayText(UIWebView* wv, int handle,
                                 const char* utf8Text,
                                 const char* fontFamily,
                                 float fontSize,
                                 UIColor textColor,
                                 float padding);

/** Re-positions / resizes an existing overlay. */
void UIWebView_MoveD2DOverlay(UIWebView* wv, int handle,
                              int x, int y, int w, int h);

/** Removes an overlay. */
void UIWebView_RemoveD2DOverlay(UIWebView* wv, int handle);

/* ===================================================================
 * Input dispatchers (composition mode)
 *
 * In HWND mode the WebView2 child window receives input natively, so
 * these are no-ops. In composition mode the visual has no message
 * loop and we have to forward mouse events to the controller via
 * SendMouseInput. app.c calls these from its event handler.
 * =================================================================== */

void UIWebView_DispatchMouseMotion(UIChildren* children, float x, float y);
void UIWebView_DispatchMouseDown  (UIChildren* children, float x, float y, int sdlButton);
void UIWebView_DispatchMouseUp    (UIChildren* children, float x, float y, int sdlButton);
void UIWebView_DispatchMouseWheel (UIChildren* children, float x, float y, float dx, float dy);

/** Returns the UICursor value WebView2 most recently reported for the
 *  composition-mode webview under (x, y), or UI_CURSOR_DEFAULT (0) if
 *  none. Used by app.c's hover-cursor picker. */
int UIWebView_HoverCursorAt(UIChildren* children, float x, float y);

/**
 * Forces this webview to spin up a dedicated ICoreWebView2Environment
 * (and therefore its own browser/GPU/network/utility processes).
 *
 * Default: 0 (shared singleton). All non-isolated webviews reuse one
 * environment, saving ~100-150 MB per additional webview. Crash
 * semantics match a Chrome window with many tabs:
 *
 *   - Renderer crash: only that webview goes blank.
 *   - GPU crash:      brief glitch, Chromium recovers automatically.
 *   - Browser crash:  rare; all shared webviews die together.
 *
 * Pass enabled=1 for fully isolated mode (use it for untrusted
 * content or when you want a separate cookie/cache profile). Must
 * be called before the first render.
 */
UIWebView* UIWebView_SetIsolated(UIWebView* wv, int enabled);

/**
 * Process-crash notifications. The callback fires when WebView2
 * reports that one of its child processes died. `kind` tells you
 * which one - in practice you mostly care about RENDERER, since the
 * page goes blank and you may want to show a "reload" UI.
 */
typedef enum {
    UI_WEBVIEW_PROCESS_BROWSER = 0,           /* fatal: full env dies */
    UI_WEBVIEW_PROCESS_RENDERER,              /* page blank; can reload */
    UI_WEBVIEW_PROCESS_RENDERER_UNRESPONSIVE, /* hung, not crashed */
    UI_WEBVIEW_PROCESS_FRAME_RENDERER,
    UI_WEBVIEW_PROCESS_UTILITY,
    UI_WEBVIEW_PROCESS_SANDBOX,
    UI_WEBVIEW_PROCESS_GPU,                   /* Chromium will retry */
    UI_WEBVIEW_PROCESS_OTHER
} UIWebViewProcessKind;

typedef void (*UIWebViewProcessFailedCallback)(UIWebView* wv,
                                               UIWebViewProcessKind kind,
                                               void* userdata);

UIWebView* UIWebView_OnProcessFailed(UIWebView* wv,
                                     UIWebViewProcessFailedCallback cb,
                                     void* userdata);

/**
 * Returns the default `additionalBrowserArguments` string that every
 * new UIWebView starts with. These are the "safe and useful" flags
 * (disable extensions, default apps, telemetry, etc.) chosen to cut
 * background work without breaking modern web pages. Read-only - do
 * NOT free.
 */
const char* UIWebView_GetDefaultBrowserArguments(void);

/**
 * Replaces the `additionalBrowserArguments` entirely. Defaults are
 * dropped. Pass NULL or "" to start the WebView2 environment with no
 * extra flags at all (just whatever Edge defaults to).
 *
 * MUST be called before the first render. The string is copied.
 *
 * Note: WebView2 caches the environment after first creation, so this
 * setter only takes effect when this webview is the first one to
 * trigger env creation (or when isolated via UIWebView_SetIsolated).
 */
UIWebView* UIWebView_SetBrowserArguments(UIWebView* wv, const char* args);

/**
 * Appends `extra` to the existing arguments (defaults + anything
 * previously appended). Useful to ADD a flag without losing the safe
 * defaults. Same restrictions as SetBrowserArguments (must be called
 * before first render, copies the string).
 *
 * Example:
 *   UIWebView_AppendBrowserArguments(wv, "--js-flags=--max-old-space-size=512");
 */
UIWebView* UIWebView_AppendBrowserArguments(UIWebView* wv, const char* extra);

/**
 * Switches the webview to DirectComposition rendering instead of a
 * child HWND. MUST be called before the widget is first rendered
 * (typically right after UIWebView_Create).
 *
 * Why use it:
 *   - The default HWND backend uses SetWindowRgn to punch holes for
 *     higher-z overlays. SetWindowRgn is pixel-quantised, so SDL's
 *     sub-pixel AA at the overlay's rounded corners doesn't align
 *     with the punch and the webview content bleeds around the card.
 *   - Composition mode renders WebView2 into an IDCompositionVisual
 *     composed by DWM on top of the HWND. This gives sub-pixel
 *     edges and (longer term) makes rounded clipping possible via
 *     IDCompositionDevice2 geometry effects.
 *
 * Current limitations:
 *   - Mouse input forwarding is partial: motion + click are forwarded
 *     via SendMouseInput when the cursor is over the webview bounds.
 *     Wheel, keyboard, IME and pointer events still need wiring.
 *   - The visual is clipped to the webview's bounds via a rectangle
 *     clip - overlays inside those bounds are HIDDEN by the webview
 *     visual until per-overlay geometry clipping is added. For now
 *     keep overlays outside the webview bounds.
 *   - Requires Windows 8+ (DirectComposition).
 */
UIWebView* UIWebView_SetCompositionMode(UIWebView* wv, int enabled);

/* ===================================================================
 * Visual customisation
 *
 * These calls are safe at any time (including before UIApp_ShowWindow).
 * The values are applied to the host HWND on the next renderer tick
 * and persist across resizes.
 * =================================================================== */

/**
 * Rounds the corners of the webview surface. Pass 0 to restore a
 * sharp rectangle. The host HWND is clipped via SetWindowRgn, so
 * outside the rounded area the SDL surface (and whatever widgets you
 * paint there) shows through.
 */
UIWebView* UIWebView_SetRadius(UIWebView* wv, float radius);

/**
 * Frames the webview with a coloured border of the given width. The
 * webview content is inset by `width` on all sides; the border itself
 * is painted on the SDL surface by the renderer, so it composes
 * correctly with the rounded corners configured by SetRadius.
 *
 * Pass width = 0 to remove the border.
 */
UIWebView* UIWebView_SetBorder(UIWebView* wv, UIColor color, float width);

/* ===================================================================
 * Browser settings
 *
 * Setters are safe pre-init - values are queued and applied to
 * ICoreWebView2Settings once the controller becomes ready.
 * =================================================================== */

/**
 * Overrides the User-Agent string sent with every request. Pass NULL
 * to restore the default Edge UA.
 */
UIWebView* UIWebView_SetUserAgent(UIWebView* wv, const char* userAgent);

/**
 * Enables / disables the F12 developer tools (default: enabled).
 */
UIWebView* UIWebView_SetDevToolsEnabled(UIWebView* wv, int enabled);

/**
 * Enables / disables the right-click context menu (default: enabled).
 */
UIWebView* UIWebView_SetContextMenusEnabled(UIWebView* wv, int enabled);

/* ===================================================================
 * Scripting
 * =================================================================== */

/**
 * Registers a JavaScript fragment to execute on EVERY page load, before
 * the page's own scripts run. Persists across navigations. Multiple
 * calls accumulate. Pre-init calls are queued.
 *
 * Typical use: inject globals, monkey-patch APIs, install message
 * channels with the host.
 */
UIWebView* UIWebView_AddInitScript(UIWebView* wv, const char* js);

/**
 * Runs a JavaScript snippet once, in the currently loaded page. Result
 * (return value of the snippet) is discarded by this minimal binding.
 * Calling before the browser is ready is a no-op.
 */
UIWebView* UIWebView_ExecuteScript(UIWebView* wv, const char* js);

/* ===================================================================
 * Cookies
 * =================================================================== */

/**
 * Deletes every cookie stored by the webview's user-data profile.
 * Calling before the browser is ready queues the operation.
 */
UIWebView* UIWebView_ClearCookies(UIWebView* wv);

/* ===================================================================
 * Request interception
 * =================================================================== */

/**
 * Registers a per-request callback. Receives the URL of each outgoing
 * request and can return 1 to block (the resource fails to load) or
 * 0 to allow.
 *
 * Currently restricted to allow/block decisions. Header rewriting and
 * response synthesis are planned future extensions and would go
 * through this same registration.
 *
 * Passing cb = NULL clears the handler.
 */
UIWebView* UIWebView_OnRequest(UIWebView* wv,
                               UIWebViewRequestCallback cb,
                               void* userdata);

/* ===================================================================
 * Cleanup
 * =================================================================== */

/** Destroys the widget. Called automatically by UIWidget_Destroy when
 *  the wrapping UIWidget goes away. */
void UIWebView_Destroy(UIWebView* wv);

#endif // UIKIT_WEBVIEW_H
