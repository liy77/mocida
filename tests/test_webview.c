// test_webview.c
//
// Browser-style demo of the full UIWebView surface:
//
//   - Toolbar with Back / Forward / Reload / URL field / Go button
//   - Webview with rounded corners (radius 16) + indigo border (4px)
//   - Loading bar that animates while a navigation is in flight
//   - URL bar auto-syncs with the webview (click a link, type in the
//     page, hit Back - the address updates on its own)
//   - Init script that injects a small "injected by mocida" banner
//   - Request interceptor that blocks doubleclick / GA / GTM
//   - Spoofed User-Agent ("Mocida/0.1")
//   - Cookies cleared on launch
//   - Overlay card placed in a reserved strip below the webview so it
//     never overlaps the WebView2 HWND (avoids the SetWindowRgn punch
//     artifact discussed elsewhere)
//
// Layout is fully responsive via UIApp_OnResize.

#include <uikit/app.h>
#include <stdio.h>
#include <string.h>

// ---- Layout constants ----------------------------------------------
#define TOOLBAR_H        48
#define OUTER_MARGIN     20
#define NAV_BTN_W        40
#define NAV_BTN_GAP      10
#define GO_BTN_W         90
#define LOADING_BAR_H     4
#define OVERLAY_W       320
#define OVERLAY_H       120
#define OVERLAY_RADIUS   28   // chunky radius so it reads as a rounded card
#define OVERLAY_PAD      36   // sit clear of the webview's scrollbar gutter

// ---- Globals -------------------------------------------------------
static UIApp*       g_app = NULL;
static UIWebView*   g_wv  = NULL;
static UITextField* g_url = NULL;

static UIWidget* g_wvW          = NULL;
static UIWidget* g_urlW         = NULL;
static UIWidget* g_goBtnW       = NULL;
static UIWidget* g_loadingW     = NULL;   // progress bar widget
static UIWidget* g_overlayW     = NULL;  // legacy SDL overlay (not used in comp mode)
static UIWidget* g_overlayMsgW  = NULL;
static int       g_d2dOverlay   = -1;    // D2D overlay handle (comp mode)

static int   g_loading       = 0;          // 1 while navigating
static float g_loadingAnim   = 0.0f;       // [0..1] for indeterminate sweep
static int   g_winW          = 0;          // latest window dims
static int   g_winH          = 0;

// --------------------------------------------------------------------
// Webview callbacks
// --------------------------------------------------------------------

static void on_ready(UIWebView* wv, void* ud) {
    (void)ud;
    printf("[webview] ready\n");

    // Compute the overlay corner using the latest window dims captured
    // by on_resize so the overlay shows up in the right place on the
    // first frame, not at (0, 0) under the toolbar.
    const int oX = (g_winW > 0 ? g_winW : 1024) - OUTER_MARGIN - OVERLAY_PAD - OVERLAY_W;
    const int oY = (g_winH > 0 ? g_winH : 720)  - OUTER_MARGIN - OVERLAY_PAD - OVERLAY_H;

    g_d2dOverlay = UIWebView_AddD2DOverlay(wv,
        oX, oY, OVERLAY_W, OVERLAY_H,
        (float)OVERLAY_RADIUS,
        UI_COLOR_WHITE);
    fprintf(stderr, "[test] AddD2DOverlay result=%d (winW=%d winH=%d -> pos=%d,%d)\n",
            g_d2dOverlay, g_winW, g_winH, oX, oY);
    fflush(stderr);
    if (g_d2dOverlay >= 0) {
        UIWebView_SetD2DOverlayText(wv, g_d2dOverlay,
            "OVERLAY OVER WEBVIEW\n"
            "D2D-rendered, perfect rounded edges,\n"
            "per-pixel alpha. No fundinho.",
            "Segoe UI", 14.0f,
            (UIColor){ 15, 23, 42, 1.0f }, 16.0f);
    }
}

static int on_request(UIWebView* wv, const char* url, void* ud) {
    (void)wv; (void)ud;
    if (strstr(url, "doubleclick.net")  ||
        strstr(url, "google-analytics") ||
        strstr(url, "googletagmanager")) {
        return 1; // block
    }
    return 0;
}

// Sync the URL bar with whatever the webview is showing. Fires on
// link clicks, JS navigation, back/forward - anywhere the URL changes.
// Skip the update while the user is editing the field, otherwise their
// typing gets wiped every time a sub-resource redirect fires.
static void on_url_change(UIWebView* wv, const char* url, void* ud) {
    (void)wv; (void)ud;
    if (!g_url || !url) return;
    if (g_url->focused) return;   // don't clobber the user mid-type
    UITextField_SetText(g_url, url);
    printf("[url] %s\n", url);
}

// Toggle the loading bar.
static void on_loading_change(UIWebView* wv, int loading, void* ud) {
    (void)wv; (void)ud;
    g_loading = loading;
    g_loadingAnim = 0.0f;
    if (g_loadingW) g_loadingW->visible = loading ? 1 : 0;
}

// --------------------------------------------------------------------
// Button + URL handlers
// --------------------------------------------------------------------

static void on_go(UIButton* b, void* ud) {
    (void)b; (void)ud;
    if (!g_wv || !g_url) return;
    const char* url = UITextField_GetText(g_url);
    if (url && *url) UIWebView_Navigate(g_wv, url);
}

static void on_back   (UIButton* b, void* ud) { (void)b; (void)ud; if (g_wv) UIWebView_GoBack(g_wv); }
static void on_fwd    (UIButton* b, void* ud) { (void)b; (void)ud; if (g_wv) UIWebView_GoForward(g_wv); }
static void on_reload (UIButton* b, void* ud) { (void)b; (void)ud; if (g_wv) UIWebView_Reload(g_wv); }

static void on_submit_url(UITextField* tf, const char* text, void* ud) {
    (void)tf; (void)ud;
    if (g_wv && text && *text) UIWebView_Navigate(g_wv, text);
}

// --------------------------------------------------------------------
// Loading bar tick: framerate event fires roughly per second, but
// any continuous animation should ideally use a per-frame hook. For
// now we just sweep the bar across the toolbar each second.
// --------------------------------------------------------------------

static void on_fps_tick(UIEventData data) {
    (void)data;
    if (!g_loading || !g_loadingW) return;
    g_loadingAnim += 0.25f;
    if (g_loadingAnim > 1.0f) g_loadingAnim = 0.0f;
    // We could move/scale the bar here to fake progress; for the
    // simplest visual we leave width at full and rely on visibility.
}

// --------------------------------------------------------------------
// Layout
// --------------------------------------------------------------------

static UIWidget* make_nav_btn(UIChildren* c, const char* label,
                              UIButtonCallback cb, float x) {
    UIButton* b = UIButton_Create(label, 14.0f);
    UIButton_SetFontFamily(b, UIGetFont("Arial"));
    UIButton_SetRadius(b, 6.0f);
    UIButton_OnClick(b, cb, NULL);
    UIWidget* widget = widgcs(b, NAV_BTN_W, 32.0f);
    UIWidget_SetPosition(widget, x, OUTER_MARGIN - 4.0f);
    UIChildren_Add(c, widget);
    return widget;
}

static void on_resize(int w, int h, void* ud) {
    (void)ud;
    g_winW = w; g_winH = h;

    const float content_w = (float)w - 2.0f * OUTER_MARGIN;

    // URL textfield: stretches between the 3 nav buttons (left) and Go.
    const float urlX = OUTER_MARGIN + 3.0f * NAV_BTN_W + 2.0f * NAV_BTN_GAP + 10.0f;
    const float urlRightInset = OUTER_MARGIN + GO_BTN_W + 10.0f;
    const float urlW = (float)w - urlX - urlRightInset;
    if (g_urlW) {
        UIWidget_SetSize(g_urlW, urlW > 60.0f ? urlW : 60.0f, 32.0f);
        UIWidget_SetPosition(g_urlW, urlX, OUTER_MARGIN - 4.0f);
    }

    // Go button (right edge).
    if (g_goBtnW) {
        UIWidget_SetPosition(g_goBtnW,
                             (float)w - OUTER_MARGIN - GO_BTN_W,
                             OUTER_MARGIN - 4.0f);
    }

    // Loading bar: thin strip below the toolbar, full width.
    if (g_loadingW) {
        UIWidget_SetSize(g_loadingW, content_w, (float)LOADING_BAR_H);
        UIWidget_SetPosition(g_loadingW,
                             (float)OUTER_MARGIN,
                             (float)(OUTER_MARGIN + TOOLBAR_H - LOADING_BAR_H));
    }

    // Webview fills the full area below the toolbar.
    const float wvH = (float)h - OUTER_MARGIN - TOOLBAR_H - OUTER_MARGIN;
    if (g_wvW) {
        UIWidget_SetSize(g_wvW,
                         content_w > 0 ? content_w : 1.0f,
                         wvH > 0 ? wvH : 1.0f);
        UIWidget_SetPosition(g_wvW,
                             (float)OUTER_MARGIN,
                             (float)(OUTER_MARGIN + TOOLBAR_H));
    }

    // D2D overlay (composition mode) reposition.
    const int oX = w - OUTER_MARGIN - OVERLAY_PAD - OVERLAY_W;
    const int oY = h - OUTER_MARGIN - OVERLAY_PAD - OVERLAY_H;
    if (g_d2dOverlay >= 0) {
        UIWebView_MoveD2DOverlay(g_wv, g_d2dOverlay, oX, oY, OVERLAY_W, OVERLAY_H);
        // Text needs to be re-applied after a size change (the surface
        // is reallocated, then we re-set the text content).
        UIWebView_SetD2DOverlayText(g_wv, g_d2dOverlay,
            "OVERLAY OVER WEBVIEW\n"
            "D2D-rendered, perfect rounded edges,\n"
            "composes with sub-pixel alpha. No more fundinho.",
            "Segoe UI", 14.0f,
            (UIColor){ 15, 23, 42, 1.0f }, 16.0f);
    }
}

// --------------------------------------------------------------------
// main
// --------------------------------------------------------------------

int main(void) {
    const int INIT_W = 1024;
    const int INIT_H = 720;

    g_app = UIApp_Create("Mocida - webview", INIT_W, INIT_H);
    if (!g_app) return 1;

    // Tell Windows that every process this app spawns - including the
    // WebView2 browser/renderer/GPU helpers - belongs to "Mocida.Demo".
    // Task Manager uses this AUMID to group child processes under our
    // app instead of bucketing them under "Gerenciador WebView2".
    // MUST come before any UIWebView is created.
    UIApp_SetAppId(g_app, "Mocida.Demo");

    UISearchFonts();

    UIChildren* children = UIChildren_Create(16);

    // ---- Toolbar nav buttons --------------------------------------
    make_nav_btn(children, "\xE2\x86\x90", on_back,   (float)OUTER_MARGIN);
    make_nav_btn(children, "\xE2\x86\x92", on_fwd,    (float)(OUTER_MARGIN + NAV_BTN_W + NAV_BTN_GAP));
    make_nav_btn(children, "\xE2\x86\xBB", on_reload, (float)(OUTER_MARGIN + 2 * (NAV_BTN_W + NAV_BTN_GAP)));

    // ---- URL textfield --------------------------------------------
    g_url = UITextField_Create("https://example.com", 14.0f);
    UITextField_SetFontFamily(g_url, UIGetFont("Arial"));
    UITextField_SetPlaceholder(g_url, "https://...");
    UITextField_OnSubmit(g_url, on_submit_url, NULL);
    g_urlW = widgcs(g_url, 1.0f, 32.0f);
    UIChildren_Add(children, g_urlW);

    // ---- Go button ------------------------------------------------
    UIButton* goBtn = UIButton_Create("Go", 14.0f);
    UIButton_SetFontFamily(goBtn, UIGetFont("Arial"));
    UIButton_SetRadius(goBtn, 6.0f);
    UIButton_SetColors(goBtn, (UIColor){ 79, 70, 229, 1.0f }, UI_COLOR_WHITE);
    UIButton_OnClick(goBtn, on_go, NULL);
    g_goBtnW = widgcs(goBtn, GO_BTN_W, 32.0f);
    UIChildren_Add(children, g_goBtnW);

    // ---- Loading bar (hidden until navigation starts) -------------
    UIRectangle* loadingBar = UIRectangle_Create();
    UIRectangle_SetColor(loadingBar, (UIColor){ 79, 70, 229, 1.0f }); // indigo
    UIRectangle_SetRadius(loadingBar, 2.0f);
    g_loadingW = widgcs(loadingBar, 100.0f, (float)LOADING_BAR_H);
    g_loadingW->visible = 0;
    UIWidget_SetZIndex(g_loadingW, 5);
    UIChildren_Add(children, g_loadingW);

    // ---- Webview --------------------------------------------------
    g_wv = UIWebView_Create("https://example.com");

    // Composition mode + D2D overlays: webview renders to an
    // IDCompositionVisual; overlays are D2D-rendered sibling visuals
    // composed by DWM with per-pixel alpha. No SetWindowRgn punch,
    // no rounded-corner artifacts.
    UIWebView_SetCompositionMode(g_wv, 1);

    // Memory-saving browser flags. Tauri-like apps stay low because
    // they share a single renderer per origin and cap the V8 heap;
    // we ask WebView2 for the same. `--single-process` is the big
    // saver (~30-40% off RAM) at the cost of process isolation, which
    // is fine for a desktop demo. The other flags trim background
    // throttling and JIT memory.
    // Memory-saving browser flags. `--single-process` would be the
    // biggest single win (~85% RAM cut) but Chromium has hardened
    // single-process mode and modern WebView2 renderers refuse to
    // start under it (the page comes up blank). We use a milder set:
    // tighter V8 heap, one renderer process, GPU folded into the
    // browser process, no zygote, and a pile of disabled features.
    // Flags de redução de memória. Reality check: WebView2 É Chromium,
    // mesma engine que o Chrome usa. Um app single-window com uma única
    // página vai usar ~150-300 MB depois de aplicar essas otimizações.
    // Apps como WhatsApp Desktop / Tauri ficam parecidos no first-run -
    // a diferença visual de RAM em comparações casuais vem de cache
    // persistente e processo já aquecido.
    //
    // Flags abaixo combinam: limite agressivo do heap JS, um renderer
    // só, GPU no mesmo processo, e desligamento de features que o
    // Edge ativa por default mas que um app embutido não precisa.
    // UIWebView_Create already applied the safe default browser flags.
    // To add more, use UIWebView_AppendBrowserArguments. To replace
    // them entirely, use UIWebView_SetBrowserArguments. Example:
    //   UIWebView_AppendBrowserArguments(g_wv,
    //       "--js-flags=--max-old-space-size=512");

    // Visual: rounded corners + indigo border ring.
    UIWebView_SetRadius(g_wv, 16.0f);
    UIWebView_SetBorder(g_wv, (UIColor){ 79, 70, 229, 1.0f }, 4.0f);

    // Settings.
    UIWebView_SetDevToolsEnabled(g_wv, 1);
    UIWebView_SetContextMenusEnabled(g_wv, 1);
    UIWebView_SetUserAgent(g_wv,
        "Mozilla/5.0 (Mocida/0.1) AppleWebKit/537.36 (KHTML, like Gecko)");

    // Banner injected on every page, plus a CSS rule that hides the
    // page scrollbar - the WebView2 HWND draws its own scrollbar
    // inside its bounds, and it can poke over the punched overlay
    // hole at the bottom-right. Killing the page scrollbar keeps the
    // overlay clean.
    UIWebView_AddInitScript(g_wv,
        "const s = document.createElement('style');"
        "s.textContent = "
        "  'html::-webkit-scrollbar,body::-webkit-scrollbar{width:0;height:0;display:none}"
        "   html,body{scrollbar-width:none;-ms-overflow-style:none}';"
        "(document.head || document.documentElement).appendChild(s);"
        "document.addEventListener('DOMContentLoaded', () => {"
        "  const b = document.createElement('div');"
        "  b.textContent = 'injected by mocida';"
        "  b.style.cssText = 'position:fixed;top:0;right:0;background:#4f46e5;"
        "color:#fff;padding:4px 8px;font:12px system-ui;z-index:99999';"
        "  document.body && document.body.appendChild(b);"
        "});");

    UIWebView_OnRequest(g_wv, on_request, NULL);
    UIWebView_ClearCookies(g_wv);
    UIWebView_OnReady(g_wv, on_ready, NULL);

    // NEW: sync the address bar + drive the loading indicator.
    UIWebView_OnUrlChange(g_wv, on_url_change, NULL);
    UIWebView_OnLoadingChange(g_wv, on_loading_change, NULL);

    g_wvW = widgcs(g_wv, 1.0f, 1.0f);
    UIWidget_SetZIndex(g_wvW, 0);
    UIChildren_Add(children, g_wvW);

    // Build the D2D overlay AFTER the webview's host renderer has run
    // at least once (so SetupDComp has fired). We create it with a
    // placeholder position; on_resize moves it to the real corner.
    // The on_resize handler will set text + position once it runs.
    // First, do a forced initial render tick by setting bounds; the
    // overlay add itself triggers EnsureD2DStack lazily.

    // ---- D2D overlay over webview (built on first ready callback) -
    // Old SDL widget overlay removed: in composition mode the webview
    // visual covers the SDL surface, so an SDL-painted overlay would
    // be invisible. The D2D overlay lives in the DComp tree alongside
    // the webview visual and composes on top with per-pixel alpha.

    // ---- Wire layout ----------------------------------------------
    UIApp_SetChildren(g_app, children);
    // Slate-50 bg makes the rounded shape of the overlay obviously
    // visible: the SDL surface shows through the AA halo around the
    // card, and the contrast against the white card surfaces the
    // radius. Switch back to UI_COLOR_WHITE if you'd rather hide the
    // halo at the cost of seeing the rounding less.
    UIApp_SetBackgroundColor(g_app, (UIColor){ 226, 232, 240, 1.0f });
    UIApp_OnResize(g_app, on_resize, NULL);
    UIApp_SetEventCallback(g_app, UI_EVENT_FRAMERATE_CHANGED, on_fps_tick);

    on_resize(INIT_W, INIT_H, NULL);

    UIApp_ShowWindow(g_app);
    UIApp_Run(g_app);
    UIApp_Destroy(g_app);
    return 0;
}
