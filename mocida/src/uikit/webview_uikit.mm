// =====================================================================
// iOS backend — WKWebView (UIKit / WebKit.framework).
//
// Compiled only when CMake defines MOCIDA_HAS_WKWEBVIEW on an iOS build.
// webview.c's __APPLE__ + MOCIDA_HAS_WKWEBVIEW branch is empty so every
// public symbol resolves here exactly once. UIKit twin of webview_cocoa.mm:
// the surface is a UIView subview of the SDL UIWindow (top-left origin, no
// Y flip), with native touch/keyboard/scroll and a layer-backed border.
//
// IMPORTANT — naming collision: UIKit owns the `UI` prefix, so its UIColor,
// UIImage, UIWindow, UIEvent, UIButton, ... ObjC classes collide with
// Mocida's C structs of the same name. So this TU must NOT include any of
// Mocida's uikit/*.h headers. Instead it redeclares — with C linkage and
// matching ABI — exactly the handful of symbols it implements, using
// non-colliding local type names (MUIColor for UIColor, etc). The struct
// layouts/parameter ABIs match the public headers, so the C side links and
// calls these unchanged.
//
// MRC (no ARC): stored ObjC objects retained on store, released in Destroy.
// =====================================================================

// WebKit/UIKit on iOS still declare the deprecated `UIWebView` ObjC class,
// so the internal struct uses the non-colliding name MocidaWV. The public
// C symbols stay `UIWebView_*` (distinct identifiers from the class) and
// take MocidaWV* — pointer ABI is identical to the header's MocidaWV*.
#import <UIKit/UIKit.h>
#import <WebKit/WebKit.h>
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

// _strdup is aliased to strdup by CMake on non-Windows (PUBLIC).

// ---- ABI-compatible redeclarations of the public UIWebView surface ----
// MUIColor mirrors UIColor { int r,g,b; float a; } exactly (same 16-byte
// layout), under a name that doesn't clash with UIKit's UIColor class.
typedef struct { int r; int g; int b; float a; } MUIColor;

typedef struct MocidaWV MocidaWV;
typedef struct UIChildren UIChildren;   // opaque; only used by input stubs

typedef void (*UIWebViewReadyCallback)(MocidaWV*, void*);
typedef void (*UIWebViewUrlChangeCallback)(MocidaWV*, const char*, void*);
typedef void (*UIWebViewLoadingCallback)(MocidaWV*, int, void*);
typedef int  (*UIWebViewRequestCallback)(MocidaWV*, const char*, void*);
// The public typedef uses UIWebViewProcessKind (an int-sized enum); the
// pointer ABI is identical with an int parameter.
typedef void (*UIWebViewProcessFailedCallback)(MocidaWV*, int, void*);

#define UI_WIDGET_WEBVIEW "@uikit/webview"
#define MUI_PROCESS_RENDERER 1   // == UI_WEBVIEW_PROCESS_RENDERER

typedef struct WVPendingScript {
    char* js;
    struct WVPendingScript* next;
} WVPendingScript;

@class MocidaWVDelegate;

struct MocidaWV {
    const char* __widget_type;        // == UI_WIDGET_WEBVIEW

    WKWebView*        view;
    MocidaWVDelegate* delegate;
    UIView*           host;           // not retained
    int               added;
    int               kvo;

    int               ready;
    int               loading;

    char*             pendingUrl;
    WVPendingScript*  pendingScripts;
    int               pendingClearCookies;

    char*             urlBuf;

    UIWebViewReadyCallback         onReady;        void* onReadyUserdata;
    UIWebViewUrlChangeCallback     onUrlChange;     void* onUrlChangeUserdata;
    UIWebViewLoadingCallback       onLoading;       void* onLoadingUserdata;
    UIWebViewRequestCallback       onRequest;       void* onRequestUserdata;
    UIWebViewProcessFailedCallback onProcessFailed; void* onProcessFailedUserdata;

    float    cornerRadius;
    float    borderWidth;
    MUIColor borderColor;
    int      hasBorder;
    int      visualsDirty;

    int      contextMenusEnabled;
    int      contextMenuScriptAdded;
};

@interface MocidaWVDelegate : NSObject <WKNavigationDelegate, WKUIDelegate>
@property (nonatomic, assign) MocidaWV* owner;
@end

static void wv_flush_pending(MocidaWV* wv);

// Forward decls for the C-linkage entry points the delegate / helpers call.
extern "C" MocidaWV* UIWebView_ClearCookies(MocidaWV* wv);

@implementation MocidaWVDelegate

- (void)webView:(WKWebView*)webView
    didStartProvisionalNavigation:(WKNavigation*)navigation {
    (void)webView; (void)navigation;
    MocidaWV* wv = self.owner;
    if (!wv) return;
    if (!wv->loading) {
        wv->loading = 1;
        if (wv->onLoading) wv->onLoading(wv, 1, wv->onLoadingUserdata);
    }
}

- (void)markFinished {
    MocidaWV* wv = self.owner;
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
    (void)webView; (void)navigation; [self markFinished];
}
- (void)webView:(WKWebView*)webView
    didFailNavigation:(WKNavigation*)navigation withError:(NSError*)error {
    (void)webView; (void)navigation; (void)error; [self markFinished];
}
- (void)webView:(WKWebView*)webView
    didFailProvisionalNavigation:(WKNavigation*)navigation withError:(NSError*)error {
    (void)webView; (void)navigation; (void)error; [self markFinished];
}

- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationAction:(WKNavigationAction*)navigationAction
                    decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler {
    (void)webView;
    MocidaWV* wv = self.owner;
    NSURL* url = navigationAction.request.URL;
    if (wv && wv->onRequest && url) {
        const char* u = url.absoluteString.UTF8String;
        if (u && wv->onRequest(wv, u, wv->onRequestUserdata) == 1) {
            decisionHandler(WKNavigationActionPolicyCancel);
            return;
        }
    }
    decisionHandler(WKNavigationActionPolicyAllow);
}

- (void)webViewWebContentProcessDidTerminate:(WKWebView*)webView {
    (void)webView;
    MocidaWV* wv = self.owner;
    if (!wv) return;
    if (wv->loading) {
        wv->loading = 0;
        if (wv->onLoading) wv->onLoading(wv, 0, wv->onLoadingUserdata);
    }
    if (wv->onProcessFailed)
        wv->onProcessFailed(wv, MUI_PROCESS_RENDERER, wv->onProcessFailedUserdata);
}

- (void)observeValueForKeyPath:(NSString*)keyPath ofObject:(id)object
                        change:(NSDictionary*)change context:(void*)context {
    (void)object; (void)change; (void)context;
    MocidaWV* wv = self.owner;
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
static void wv_run_js(MocidaWV* wv, const char* js) {
    if (!wv || !wv->view || !js) return;
    NSString* s = [NSString stringWithUTF8String:js];
    if (!s) return;
    [wv->view evaluateJavaScript:s completionHandler:^(id result, NSError* err) {
        (void)result;
        if (err) SDL_Log("UIWebView JS error: %s", err.localizedDescription.UTF8String);
    }];
}

static void wv_add_init_script(MocidaWV* wv, const char* js) {
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

static void wv_queue_exec(MocidaWV* wv, const char* js) {
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

static void wv_flush_pending(MocidaWV* wv) {
    if (!wv) return;
    if (wv->pendingClearCookies) {
        UIWebView_ClearCookies(wv);
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
// Public API — C linkage to match the extern "C" declarations in webview.h.
// ---------------------------------------------------------------------
extern "C" {

MocidaWV* UIWebView_Create(const char* initialUrl) {
    MocidaWV* wv = (MocidaWV*)calloc(1, sizeof(*wv));
    if (!wv) return NULL;
    wv->__widget_type        = UI_WIDGET_WEBVIEW;
    wv->contextMenusEnabled  = 1;
    wv->borderColor          = (MUIColor){ 0, 0, 0, 0.0f };

    WKWebViewConfiguration* cfg = [[WKWebViewConfiguration alloc] init];
    wv->view = [[WKWebView alloc] initWithFrame:CGRectMake(0, 0, 640, 480)
                                  configuration:cfg];
    [cfg release];
    if (!wv->view) { free(wv); return NULL; }

    wv->delegate = [[MocidaWVDelegate alloc] init];
    wv->delegate.owner          = wv;
    wv->view.navigationDelegate = wv->delegate;
    wv->view.UIDelegate         = wv->delegate;

    [wv->view addObserver:wv->delegate forKeyPath:@"URL"
                  options:NSKeyValueObservingOptionNew context:NULL];
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

MocidaWV* UIWebView_Navigate(MocidaWV* wv, const char* url) {
    if (!wv || !wv->view || !url || !*url) return wv;
    NSString* u = [NSString stringWithUTF8String:url];
    NSURL* nu = u ? [NSURL URLWithString:u] : nil;
    if (nu) [wv->view loadRequest:[NSURLRequest requestWithURL:nu]];
    return wv;
}

const char* UIWebView_GetUrl(MocidaWV* wv) {
    if (!wv || !wv->view) return NULL;
    NSURL* url = wv->view.URL;
    if (!url) return NULL;
    const char* u = url.absoluteString.UTF8String;
    if (!u) return NULL;
    free(wv->urlBuf);
    wv->urlBuf = _strdup(u);
    return wv->urlBuf;
}

MocidaWV* UIWebView_Reload(MocidaWV* wv) {
    if (wv && wv->view) [wv->view reload];
    return wv;
}
MocidaWV* UIWebView_GoBack(MocidaWV* wv) {
    if (wv && wv->view && wv->view.canGoBack) [wv->view goBack];
    return wv;
}
MocidaWV* UIWebView_GoForward(MocidaWV* wv) {
    if (wv && wv->view && wv->view.canGoForward) [wv->view goForward];
    return wv;
}

MocidaWV* UIWebView_OnReady(MocidaWV* wv, UIWebViewReadyCallback cb, void* ud) {
    if (wv) { wv->onReady = cb; wv->onReadyUserdata = ud; } return wv;
}
MocidaWV* UIWebView_OnUrlChange(MocidaWV* wv, UIWebViewUrlChangeCallback cb, void* ud) {
    if (wv) { wv->onUrlChange = cb; wv->onUrlChangeUserdata = ud; } return wv;
}
MocidaWV* UIWebView_OnLoadingChange(MocidaWV* wv, UIWebViewLoadingCallback cb, void* ud) {
    if (wv) { wv->onLoading = cb; wv->onLoadingUserdata = ud; } return wv;
}
MocidaWV* UIWebView_OnRequest(MocidaWV* wv, UIWebViewRequestCallback cb, void* ud) {
    if (wv) { wv->onRequest = cb; wv->onRequestUserdata = ud; } return wv;
}
MocidaWV* UIWebView_OnProcessFailed(MocidaWV* wv, UIWebViewProcessFailedCallback cb, void* ud) {
    if (wv) { wv->onProcessFailed = cb; wv->onProcessFailedUserdata = ud; } return wv;
}

MocidaWV* UIWebView_AddInitScript(MocidaWV* wv, const char* js) {
    wv_add_init_script(wv, js); return wv;
}
MocidaWV* UIWebView_ExecuteScript(MocidaWV* wv, const char* js) {
    if (!wv || !js) return wv;
    if (wv->ready) wv_run_js(wv, js); else wv_queue_exec(wv, js);
    return wv;
}

MocidaWV* UIWebView_ClearCookies(MocidaWV* wv) {
    if (!wv) return wv;
    if (!wv->view) { wv->pendingClearCookies = 1; return wv; }
    WKWebsiteDataStore* store = wv->view.configuration.websiteDataStore;
    NSSet* types = [NSSet setWithObject:WKWebsiteDataTypeCookies];
    [store removeDataOfTypes:types modifiedSince:[NSDate distantPast]
          completionHandler:^{}];
    return wv;
}

MocidaWV* UIWebView_SetUserAgent(MocidaWV* wv, const char* userAgent) {
    if (!wv || !wv->view) return wv;
    wv->view.customUserAgent = userAgent ? [NSString stringWithUTF8String:userAgent] : nil;
    return wv;
}
MocidaWV* UIWebView_SetDevToolsEnabled(MocidaWV* wv, int enabled) {
    // iOS Web Inspector is gated by WKWebView.inspectable (iOS 16.4+); there
    // is no developerExtrasEnabled preference (KVC would throw).
    if (wv && wv->view && [wv->view respondsToSelector:@selector(setInspectable:)])
        [wv->view setValue:@(enabled ? YES : NO) forKey:@"inspectable"];
    return wv;
}
MocidaWV* UIWebView_SetContextMenusEnabled(MocidaWV* wv, int enabled) {
    if (!wv) return wv;
    wv->contextMenusEnabled = enabled ? 1 : 0;
    if (!enabled && wv->view && !wv->contextMenuScriptAdded) {
        wv_add_init_script(wv,
            "window.addEventListener('contextmenu',function(e){e.preventDefault();},true);");
        wv->contextMenuScriptAdded = 1;
    }
    return wv;
}

MocidaWV* UIWebView_SetRadius(MocidaWV* wv, float r) {
    if (wv) { wv->cornerRadius = (r < 0.0f) ? 0.0f : r; wv->visualsDirty = 1; }
    return wv;
}
MocidaWV* UIWebView_SetBorder(MocidaWV* wv, MUIColor c, float w) {
    if (!wv) return wv;
    wv->borderColor = c;
    wv->borderWidth = (w < 0.0f) ? 0.0f : w;
    wv->hasBorder   = (wv->borderWidth > 0.0f);
    wv->visualsDirty = 1;
    return wv;
}

// Per-frame renderer hook. UIKit is top-left origin so the widget rect maps
// 1:1 (no Y flip). Shares window.c's Apple webview arm with macOS (only one
// backend is ever compiled).
void UIWebView_RendererTick_Mac(MocidaWV* wv, void* uiWindowPtr,
                                int x, int y, int w, int h, int visible) {
    if (!wv || !wv->view || !uiWindowPtr) return;
    UIWindow* win = (__bridge UIWindow*)uiWindowPtr;
    UIView* host = win.rootViewController.view ? win.rootViewController.view
                                               : (UIView*)win;
    if (!host) return;

    if (!wv->added || wv->view.superview != host) {
        [host addSubview:wv->view];
        wv->host  = host;
        wv->added = 1;
    }

    wv->view.hidden = visible ? NO : YES;
    if (!visible) return;

    CGRect frame = CGRectMake((CGFloat)x, (CGFloat)y, (CGFloat)w, (CGFloat)h);
    if (!CGRectEqualToRect(wv->view.frame, frame)) wv->view.frame = frame;

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
                layer.borderColor = cg;
                CGColorRelease(cg);
            } else {
                layer.borderWidth = 0.0;
            }
            wv->visualsDirty = 0;
        }
    }
}

void UIWebView_Destroy(MocidaWV* wv) {
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

// Touch is delivered natively by UIKit to the subview, so the dispatch
// hooks are no-ops (matching the macOS backend).
void UIWebView_DispatchMouseMotion(UIChildren* c, float x, float y) { (void)c; (void)x; (void)y; }
void UIWebView_DispatchMouseDown  (UIChildren* c, float x, float y, int b) { (void)c; (void)x; (void)y; (void)b; }
void UIWebView_DispatchMouseUp    (UIChildren* c, float x, float y, int b) { (void)c; (void)x; (void)y; (void)b; }
void UIWebView_DispatchMouseWheel (UIChildren* c, float x, float y, float dx, float dy) { (void)c; (void)x; (void)y; (void)dx; (void)dy; }
int  UIWebView_HoverCursorAt      (UIChildren* c, float x, float y) { (void)c; (void)x; (void)y; return 0; }

MocidaWV* UIWebView_SetCompositionMode  (MocidaWV* wv, int e) { (void)e; return wv; }
MocidaWV* UIWebView_SetBrowserArguments (MocidaWV* wv, const char* a) { (void)a; return wv; }
MocidaWV* UIWebView_AppendBrowserArguments(MocidaWV* wv, const char* a) { (void)a; return wv; }
const char* UIWebView_GetDefaultBrowserArguments(void) { return ""; }
MocidaWV* UIWebView_SetIsolated(MocidaWV* wv, int e) { (void)e; return wv; }

int  UIWebView_AddD2DOverlay(MocidaWV* wv, int x, int y, int w, int h, float r, MUIColor c) {
    (void)wv; (void)x; (void)y; (void)w; (void)h; (void)r; (void)c; return -1;
}
void UIWebView_SetD2DOverlayText(MocidaWV* wv, int h, const char* t, const char* f, float s, MUIColor c, float p) {
    (void)wv; (void)h; (void)t; (void)f; (void)s; (void)c; (void)p;
}
void UIWebView_MoveD2DOverlay(MocidaWV* wv, int h, int x, int y, int w, int hh) {
    (void)wv; (void)h; (void)x; (void)y; (void)w; (void)hh;
}
void UIWebView_RemoveD2DOverlay(MocidaWV* wv, int h) { (void)wv; (void)h; }

} // extern "C"
