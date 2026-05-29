// =====================================================================
// macOS backend — WKWebView (Cocoa / WebKit.framework).
//
// Compiled only when CMake defines MOCIDA_HAS_WKWEBVIEW (APPLE builds).
// The C side of UIWebView lives in webview.c, whose __APPLE__ +
// MOCIDA_HAS_WKWEBVIEW branch is intentionally EMPTY so every public
// symbol resolves here exactly once.
//
// Architecture (native-subview model, mirrors the Windows HWND backend):
//
//   WKWebView (a real NSView)
//        |
//        +-- added as a subview of the SDL window's NSWindow.contentView
//        +-- paints natively, GPU-composited by the OS ON TOP of the SDL
//            surface (same z-order caveat as the WebView2 child HWND)
//        +-- receives mouse / keyboard / scroll / IME NATIVELY — the
//            UIWebView_DispatchMouse* hooks are therefore no-ops here.
//
//   Per-frame: window.c calls UIWebView_RendererTick_Mac which positions
//   the subview (Cocoa's contentView is bottom-left origin, so we flip Y),
//   tracks visibility, and applies the rounded-corner / border visuals on
//   the view's CALayer (WKWebView is layer-backed, so radius + border are
//   native — no SDL-side compositing needed).
//
// What this MVP delivers:
//   * Create / Destroy / Navigate / Reload / GoBack / GoForward / GetUrl
//   * Native interactivity (mouse, keyboard, scroll, IME) for free
//   * AddInitScript (document-start, every load) / ExecuteScript
//     (queued before ready, flushed after)
//   * ClearCookies
//   * Load-state + URL-change callbacks (KVO on `URL`)
//   * Request blocking via decidePolicyForNavigationAction (navigations
//     only — a documented subset, same surface as the Linux backend)
//   * SetUserAgent / SetDevToolsEnabled / SetContextMenusEnabled
//   * SetRadius / SetBorder (native CALayer)
//
// What is NOT implemented (no-op / stub), matching the Linux backend:
//   * D2D overlays / composition mode (Win32-specific)
//   * SetIsolated / browser arguments (WebView2 multi-process concept)
//   * OnProcessFailed (WKWebView has webViewWebContentProcessDidTerminate;
//     wired to RENDERER so a blank page can be detected)
//
// Memory management: MANUAL retain/release (no ARC). Every ObjC object
// stored in the C struct is retained on store and released in Destroy.
// =====================================================================

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#include <SDL3/SDL.h>

// Both headers carry their own `extern "C"` guards, so they declare the
// public UIWebView API with C linkage when compiled here as Obj-C++ —
// no wrapper needed (and wrapping them would (incorrectly) force C
// linkage onto the C++ stdlib that mimalloc.h transitively pulls in).
#include <uikit/webview.h>
#include <uikit/debug.h>

#include <stdlib.h>
#include <string.h>

// _strdup is aliased to strdup by CMake on non-Windows (PUBLIC), so it is
// available here too; use it for naming parity with the rest of webview.c.

// A queued ExecuteScript fragment, run once the first navigation finishes.
typedef struct WVPendingScript {
    char* js;
    struct WVPendingScript* next;
} WVPendingScript;

@class MocidaWVDelegate;

struct UIWebView {
    const char* __widget_type;        // == UI_WIDGET_WEBVIEW

    // ---- Cocoa / WebKit ----
    WKWebView*        view;           // the native view (retained)
    MocidaWVDelegate* delegate;       // nav + UI delegate (retained)
    NSView*           host;           // contentView we were added to (NOT retained)
    int               added;          // 1 once inserted as a subview
    int               kvo;            // 1 once we registered the URL observer

    // ---- State ----
    int               ready;          // 1 after the first navigation finishes
    int               loading;        // 1 between start and finish/fail

    // ---- Pending (queued before ready) ----
    char*             pendingUrl;     // navigate target queued before view existed (unused: view is eager)
    WVPendingScript*  pendingScripts; // FIFO, drained on ready
    int               pendingClearCookies;

    // ---- Cached URL (stable pointer for UIWebView_GetUrl) ----
    char*             urlBuf;

    // ---- Public callbacks ----
    UIWebViewReadyCallback         onReady;        void* onReadyUserdata;
    UIWebViewUrlChangeCallback     onUrlChange;     void* onUrlChangeUserdata;
    UIWebViewLoadingCallback       onLoading;       void* onLoadingUserdata;
    UIWebViewRequestCallback       onRequest;       void* onRequestUserdata;
    UIWebViewProcessFailedCallback onProcessFailed; void* onProcessFailedUserdata;

    // ---- Visuals (applied to the CALayer when dirty) ----
    float    cornerRadius;
    float    borderWidth;
    UIColor  borderColor;
    int      hasBorder;
    int      visualsDirty;         // re-apply layer props on the next tick

    // ---- Settings ----
    int      contextMenusEnabled;     // 0 = inject a contextmenu suppressor
    int      contextMenuScriptAdded;  // so we only inject the suppressor once
};

// ---------------------------------------------------------------------
// Delegate: navigation lifecycle, request policy, process termination,
// and KVO for URL changes.
// ---------------------------------------------------------------------
@interface MocidaWVDelegate : NSObject <WKNavigationDelegate, WKUIDelegate>
@property (nonatomic, assign) UIWebView* owner;   // back-pointer (not retained)
@end

static void wv_flush_pending(UIWebView* wv);

@implementation MocidaWVDelegate

- (void)webView:(WKWebView*)webView
    didStartProvisionalNavigation:(WKNavigation*)navigation {
    (void)webView; (void)navigation;
    UIWebView* wv = self.owner;
    if (!wv) return;
    if (!wv->loading) {
        wv->loading = 1;
        if (wv->onLoading) wv->onLoading(wv, 1, wv->onLoadingUserdata);
    }
}

- (void)markFinished {
    UIWebView* wv = self.owner;
    if (!wv) return;
    if (wv->loading) {
        wv->loading = 0;
        if (wv->onLoading) wv->onLoading(wv, 0, wv->onLoadingUserdata);
    }
    if (!wv->ready) {
        wv->ready = 1;
        wv_flush_pending(wv);
        if (wv->onReady) wv->onReady(wv, wv->onReadyUserdata);
    }
}

- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation {
    (void)webView; (void)navigation;
    [self markFinished];
}

- (void)webView:(WKWebView*)webView
    didFailNavigation:(WKNavigation*)navigation withError:(NSError*)error {
    (void)webView; (void)navigation; (void)error;
    // A failed load still ends the loading state and (the first time)
    // marks the view ready so queued scripts don't wait forever.
    [self markFinished];
}

- (void)webView:(WKWebView*)webView
    didFailProvisionalNavigation:(WKNavigation*)navigation
                      withError:(NSError*)error {
    (void)webView; (void)navigation; (void)error;
    [self markFinished];
}

// Request policy: WKWebView fires this for navigations (top-level +
// subframe), NOT for every subresource — the same blockable surface the
// WebKitGTK decide-policy signal exposes on Linux.
- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationAction:(WKNavigationAction*)navigationAction
                    decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler {
    (void)webView;
    UIWebView* wv = self.owner;
    NSURL* url = navigationAction.request.URL;
    if (wv && wv->onRequest && url) {
        const char* u = url.absoluteString.UTF8String;
        if (u && wv->onRequest(wv, u, wv->onRequestUserdata) == 1) {
            decisionHandler(WKNavigationActionPolicyCancel);   // BLOCK
            return;
        }
    }
    decisionHandler(WKNavigationActionPolicyAllow);            // ALLOW
}

// Renderer/web-content process died: page goes blank. Map to RENDERER so
// the app can offer a reload, mirroring the Linux/Windows contract.
- (void)webViewWebContentProcessDidTerminate:(WKWebView*)webView {
    (void)webView;
    UIWebView* wv = self.owner;
    if (!wv) return;
    if (wv->loading) {
        wv->loading = 0;
        if (wv->onLoading) wv->onLoading(wv, 0, wv->onLoadingUserdata);
    }
    if (wv->onProcessFailed)
        wv->onProcessFailed(wv, UI_WEBVIEW_PROCESS_RENDERER, wv->onProcessFailedUserdata);
}

// KVO: fires on every URL transition (link click, JS nav, history).
- (void)observeValueForKeyPath:(NSString*)keyPath
                      ofObject:(id)object
                        change:(NSDictionary*)change
                       context:(void*)context {
    (void)object; (void)change; (void)context;
    UIWebView* wv = self.owner;
    if (!wv || !wv->view) return;
    if (![keyPath isEqualToString:@"URL"]) return;
    NSURL* url = wv->view.URL;
    if (url && wv->onUrlChange) {
        const char* u = url.absoluteString.UTF8String;
        if (u) wv->onUrlChange(wv, u, wv->onUrlChangeUserdata);
    }
}
@end

// ---------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------
static void wv_run_js(UIWebView* wv, const char* js) {
    if (!wv || !wv->view || !js) return;
    NSString* s = [NSString stringWithUTF8String:js];
    if (!s) return;
    [wv->view evaluateJavaScript:s completionHandler:^(id result, NSError* err) {
        (void)result;
        if (err) UI_WARN(UI_CAT_RENDER, "UIWebView JS error: %s",
                         err.localizedDescription.UTF8String);
    }];
}

static void wv_add_init_script(UIWebView* wv, const char* js) {
    if (!wv || !wv->view || !js) return;
    NSString* s = [NSString stringWithUTF8String:js];
    if (!s) return;
    WKUserScript* us = [[WKUserScript alloc]
        initWithSource:s
         injectionTime:WKUserScriptInjectionTimeAtDocumentStart
      forMainFrameOnly:NO];
    [wv->view.configuration.userContentController addUserScript:us];
    [us release];
}

static void wv_queue_exec(UIWebView* wv, const char* js) {
    if (!wv || !js) return;
    WVPendingScript* p = (WVPendingScript*)calloc(1, sizeof(*p));
    if (!p) return;
    p->js = _strdup(js);
    if (!wv->pendingScripts) {
        wv->pendingScripts = p;
    } else {
        WVPendingScript* t = wv->pendingScripts;
        while (t->next) t = t->next;
        t->next = p;
    }
}

static void wv_flush_pending(UIWebView* wv) {
    if (!wv) return;
    if (wv->pendingClearCookies) {
        UIWebView_ClearCookies(wv);   // view is ready now; runs immediately
        wv->pendingClearCookies = 0;
    }
    WVPendingScript* s = wv->pendingScripts;
    wv->pendingScripts = NULL;
    while (s) {
        WVPendingScript* next = s->next;
        wv_run_js(wv, s->js);
        free(s->js);
        free(s);
        s = next;
    }
}

// ---------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------
UIWebView* UIWebView_Create(const char* initialUrl) {
    UIWebView* wv = (UIWebView*)calloc(1, sizeof(*wv));
    if (!wv) return NULL;
    wv->__widget_type        = UI_WIDGET_WEBVIEW;
    wv->contextMenusEnabled  = 1;
    wv->borderColor          = (UIColor){ 0, 0, 0, 0.0f };

    WKWebViewConfiguration* cfg = [[WKWebViewConfiguration alloc] init];
    // Developer tools default ON (matches the Windows/Linux default). The
    // key is the documented private toggle WKWebView exposes for this.
    [cfg.preferences setValue:@YES forKey:@"developerExtrasEnabled"];

    wv->view = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 640, 480)
                                  configuration:cfg];
    [cfg release];
    if (!wv->view) { free(wv); return NULL; }

    wv->view.wantsLayer = YES;   // layer-backed: needed for cornerRadius/border

    wv->delegate = [[MocidaWVDelegate alloc] init];
    wv->delegate.owner       = wv;
    wv->view.navigationDelegate = wv->delegate;
    wv->view.UIDelegate         = wv->delegate;

    [wv->view addObserver:wv->delegate
               forKeyPath:@"URL"
                  options:NSKeyValueObservingOptionNew
                  context:NULL];
    wv->kvo = 1;

    if (initialUrl && *initialUrl) {
        NSString* u = [NSString stringWithUTF8String:initialUrl];
        NSURL* url = u ? [NSURL URLWithString:u] : nil;
        if (url) [wv->view loadRequest:[NSURLRequest requestWithURL:url]];
    } else {
        [wv->view loadHTMLString:@"<!doctype html><html><body></body></html>"
                         baseURL:nil];
    }
    return wv;
}

UIWebView* UIWebView_Navigate(UIWebView* wv, const char* url) {
    if (!wv || !wv->view || !url || !*url) return wv;
    NSString* u = [NSString stringWithUTF8String:url];
    NSURL* nu = u ? [NSURL URLWithString:u] : nil;
    if (nu) [wv->view loadRequest:[NSURLRequest requestWithURL:nu]];
    return wv;
}

const char* UIWebView_GetUrl(UIWebView* wv) {
    if (!wv || !wv->view) return NULL;
    NSURL* url = wv->view.URL;
    if (!url) return NULL;
    const char* u = url.absoluteString.UTF8String;
    if (!u) return NULL;
    free(wv->urlBuf);
    wv->urlBuf = _strdup(u);   // stable pointer owned by the widget
    return wv->urlBuf;
}

UIWebView* UIWebView_Reload(UIWebView* wv) {
    if (wv && wv->view) [wv->view reload];
    return wv;
}
UIWebView* UIWebView_GoBack(UIWebView* wv) {
    if (wv && wv->view && wv->view.canGoBack) [wv->view goBack];
    return wv;
}
UIWebView* UIWebView_GoForward(UIWebView* wv) {
    if (wv && wv->view && wv->view.canGoForward) [wv->view goForward];
    return wv;
}

UIWebView* UIWebView_OnReady(UIWebView* wv, UIWebViewReadyCallback cb, void* ud) {
    if (wv) { wv->onReady = cb; wv->onReadyUserdata = ud; }
    return wv;
}
UIWebView* UIWebView_OnUrlChange(UIWebView* wv, UIWebViewUrlChangeCallback cb, void* ud) {
    if (wv) { wv->onUrlChange = cb; wv->onUrlChangeUserdata = ud; }
    return wv;
}
UIWebView* UIWebView_OnLoadingChange(UIWebView* wv, UIWebViewLoadingCallback cb, void* ud) {
    if (wv) { wv->onLoading = cb; wv->onLoadingUserdata = ud; }
    return wv;
}
UIWebView* UIWebView_OnRequest(UIWebView* wv, UIWebViewRequestCallback cb, void* ud) {
    if (wv) { wv->onRequest = cb; wv->onRequestUserdata = ud; }
    return wv;
}
UIWebView* UIWebView_OnProcessFailed(UIWebView* wv, UIWebViewProcessFailedCallback cb, void* ud) {
    if (wv) { wv->onProcessFailed = cb; wv->onProcessFailedUserdata = ud; }
    return wv;
}

UIWebView* UIWebView_AddInitScript(UIWebView* wv, const char* js) {
    // Always applies at document-start on the current + future loads.
    wv_add_init_script(wv, js);
    return wv;
}
UIWebView* UIWebView_ExecuteScript(UIWebView* wv, const char* js) {
    if (!wv || !js) return wv;
    if (wv->ready) wv_run_js(wv, js);
    else           wv_queue_exec(wv, js);   // flushed on ready
    return wv;
}

UIWebView* UIWebView_ClearCookies(UIWebView* wv) {
    if (!wv) return wv;
    if (!wv->view) { wv->pendingClearCookies = 1; return wv; }
    WKWebsiteDataStore* store = wv->view.configuration.websiteDataStore;
    NSSet* types = [NSSet setWithObject:WKWebsiteDataTypeCookies];
    [store removeDataOfTypes:types
              modifiedSince:[NSDate distantPast]
          completionHandler:^{}];
    return wv;
}

UIWebView* UIWebView_SetUserAgent(UIWebView* wv, const char* userAgent) {
    if (!wv || !wv->view) return wv;
    if (userAgent) {
        NSString* s = [NSString stringWithUTF8String:userAgent];
        wv->view.customUserAgent = s;   // nil clears -> default UA
    } else {
        wv->view.customUserAgent = nil;
    }
    return wv;
}
UIWebView* UIWebView_SetDevToolsEnabled(UIWebView* wv, int enabled) {
    if (wv && wv->view)
        [wv->view.configuration.preferences
            setValue:@(enabled ? YES : NO) forKey:@"developerExtrasEnabled"];
    return wv;
}
UIWebView* UIWebView_SetContextMenusEnabled(UIWebView* wv, int enabled) {
    if (!wv) return wv;
    wv->contextMenusEnabled = enabled ? 1 : 0;
    // WKWebView has no public flag to drop the right-click menu, so when
    // disabled we inject a one-time document-start script that swallows
    // the contextmenu event. Re-enabling can't un-inject it, but the
    // common case (disable once at setup) works; matches the Linux MVP's
    // best-effort handling.
    if (!enabled && wv->view && !wv->contextMenuScriptAdded) {
        wv_add_init_script(wv,
            "window.addEventListener('contextmenu',"
            "function(e){e.preventDefault();},true);");
        wv->contextMenuScriptAdded = 1;
    }
    return wv;
}

// Visual setters: stored, applied to the CALayer in RendererTick_Mac.
UIWebView* UIWebView_SetRadius(UIWebView* wv, float r) {
    if (wv) { wv->cornerRadius = (r < 0.0f) ? 0.0f : r; wv->visualsDirty = 1; }
    return wv;
}
UIWebView* UIWebView_SetBorder(UIWebView* wv, UIColor c, float w) {
    if (!wv) return wv;
    wv->borderColor = c;
    wv->borderWidth = (w < 0.0f) ? 0.0f : w;
    wv->hasBorder   = (wv->borderWidth > 0.0f);
    wv->visualsDirty = 1;
    return wv;
}

// ---------------------------------------------------------------------
// Per-frame renderer hook (called from window.c). Positions + sizes the
// native subview and applies layer visuals. Cocoa's contentView is
// bottom-left origin, so flip Y from the top-down widget space.
// ---------------------------------------------------------------------
// C linkage: window.c (compiled as C) calls this via an `extern` decl.
// It isn't in the public header (macOS-internal), so force C linkage here.
extern "C"
void UIWebView_RendererTick_Mac(UIWebView* wv, void* nsWindowPtr,
                                int x, int y, int w, int h, int visible) {
    if (!wv || !wv->view || !nsWindowPtr) return;
    NSWindow* win = (__bridge NSWindow*)nsWindowPtr;
    NSView* content = win.contentView;
    if (!content) return;

    if (!wv->added || wv->view.superview != content) {
        [content addSubview:wv->view];
        wv->host  = content;
        wv->added = 1;
    }

    wv->view.hidden = visible ? NO : YES;
    if (!visible) return;

    const CGFloat ch = content.bounds.size.height;
    NSRect frame = NSMakeRect((CGFloat)x, ch - (CGFloat)(y + h),
                              (CGFloat)w, (CGFloat)h);
    if (!NSEqualRects(wv->view.frame, frame)) wv->view.frame = frame;

    // Layer visuals — WKWebView is layer-backed (wantsLayer set in Create).
    // Apply only when a setter marked them dirty: radius/border rarely
    // change, and re-creating the borderColor every frame would leak one
    // CGColor per frame (the layer retains its own copy; our +1 reference
    // must be released).
    if (wv->visualsDirty) {
        CALayer* layer = wv->view.layer;
        if (layer) {
            layer.cornerRadius  = wv->cornerRadius;
            layer.masksToBounds = (wv->cornerRadius > 0.0f) ? YES : NO;
            if (wv->hasBorder) {
                layer.borderWidth = wv->borderWidth;
                CGColorRef cg = CGColorCreateGenericRGB(
                    wv->borderColor.r / 255.0,
                    wv->borderColor.g / 255.0,
                    wv->borderColor.b / 255.0,
                    wv->borderColor.a);
                layer.borderColor = cg;     // layer retains
                CGColorRelease(cg);         // drop our +1 — no per-frame leak
            } else {
                layer.borderWidth = 0.0;
            }
            wv->visualsDirty = 0;
        }
    }
}

void UIWebView_Destroy(UIWebView* wv) {
    if (!wv) return;
    if (wv->view) {
        if (wv->kvo) {
            @try { [wv->view removeObserver:wv->delegate forKeyPath:@"URL"]; }
            @catch (NSException* e) { (void)e; }
        }
        wv->view.navigationDelegate = nil;
        wv->view.UIDelegate         = nil;
        [wv->view stopLoading];
        [wv->view removeFromSuperview];
        [wv->view release];
        wv->view = nil;
    }
    if (wv->delegate) { [wv->delegate release]; wv->delegate = nil; }
    while (wv->pendingScripts) {
        WVPendingScript* n = wv->pendingScripts->next;
        free(wv->pendingScripts->js);
        free(wv->pendingScripts);
        wv->pendingScripts = n;
    }
    free(wv->pendingUrl);
    free(wv->urlBuf);
    free(wv);
}

// ---------------------------------------------------------------------
// No-ops / stubs (matching the Linux backend's coverage).
//
// Input is delivered NATIVELY by Cocoa to the WKWebView subview, so the
// dispatch hooks do nothing and the hover-cursor query returns default.
// ---------------------------------------------------------------------
void UIWebView_DispatchMouseMotion(UIChildren* c, float x, float y) { (void)c; (void)x; (void)y; }
void UIWebView_DispatchMouseDown  (UIChildren* c, float x, float y, int b) { (void)c; (void)x; (void)y; (void)b; }
void UIWebView_DispatchMouseUp    (UIChildren* c, float x, float y, int b) { (void)c; (void)x; (void)y; (void)b; }
void UIWebView_DispatchMouseWheel (UIChildren* c, float x, float y, float dx, float dy) { (void)c; (void)x; (void)y; (void)dx; (void)dy; }
int  UIWebView_HoverCursorAt      (UIChildren* c, float x, float y) { (void)c; (void)x; (void)y; return 0; }

UIWebView* UIWebView_SetCompositionMode  (UIWebView* wv, int e) { (void)e; return wv; }
UIWebView* UIWebView_SetBrowserArguments (UIWebView* wv, const char* a) { (void)a; return wv; }
UIWebView* UIWebView_AppendBrowserArguments(UIWebView* wv, const char* a) { (void)a; return wv; }
const char* UIWebView_GetDefaultBrowserArguments(void) { return ""; }
UIWebView* UIWebView_SetIsolated(UIWebView* wv, int e) { (void)e; return wv; }

int  UIWebView_AddD2DOverlay(UIWebView* wv, int x, int y, int w, int h, float r, UIColor c) {
    (void)wv; (void)x; (void)y; (void)w; (void)h; (void)r; (void)c; return -1;
}
void UIWebView_SetD2DOverlayText(UIWebView* wv, int h, const char* t, const char* f, float s, UIColor c, float p) {
    (void)wv; (void)h; (void)t; (void)f; (void)s; (void)c; (void)p;
}
void UIWebView_MoveD2DOverlay(UIWebView* wv, int h, int x, int y, int w, int hh) {
    (void)wv; (void)h; (void)x; (void)y; (void)w; (void)hh;
}
void UIWebView_RemoveD2DOverlay(UIWebView* wv, int h) { (void)wv; (void)h; }
