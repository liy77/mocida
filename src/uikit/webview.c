#include <uikit/webview.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <WebView2.h>

#include <uikit/webview_dcomp.h>

/* ----------------------------------------------------------------- */
/* Diagnostic logging.                                                */
/*                                                                    */
/* When test_webview.exe is launched by double-click or any path that */
/* detaches stdio, fprintf(stderr, ...) goes nowhere. To make the     */
/* trace visible regardless, every WVLOG() also goes through          */
/* OutputDebugStringA so DebugView / Visual Studio's Output window    */
/* picks it up.                                                       */
/*                                                                    */
/* Set the env var MOCIDA_WV_LOG=0 to silence.                        */
/* ----------------------------------------------------------------- */
static int wv_log_enabled(void) {
    static int cached = -1;
    if (cached == -1) {
        char buf[8];
        DWORD n = GetEnvironmentVariableA("MOCIDA_WV_LOG", buf, sizeof(buf));
        cached = (n > 0 && buf[0] == '0') ? 0 : 1;
    }
    return cached;
}

static FILE* wv_logfile(void) {
    static FILE* fp = NULL;
    static int tried = 0;
    if (!tried) {
        tried = 1;
        fp = fopen("mocida_webview.log", "w");
        if (fp) {
            fputs("=== mocida webview log opened ===\n", fp);
            fflush(fp);
        }
    }
    return fp;
}

static void wv_log(const char* fmt, ...) {
    if (!wv_log_enabled()) return;
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    /* stderr - visible when launched from a console */
    fputs(line, stderr);
    fputc('\n', stderr);
    fflush(stderr);

    /* OutputDebugString - visible in DebugView / VS Output window even
       when stdio is detached. */
    OutputDebugStringA(line);
    OutputDebugStringA("\r\n");

    /* File - bulletproof fallback that survives double-click launches.
       Written next to the cwd of the process. */
    FILE* fp = wv_logfile();
    if (fp) {
        fputs(line, fp);
        fputc('\n', fp);
        fflush(fp);
    }
}

#define WVLOG(...) wv_log(__VA_ARGS__)


// ---------------------------------------------------------------------
// State
// ---------------------------------------------------------------------

typedef enum {
    UI_WV_IDLE,
    UI_WV_CREATING_ENV,
    UI_WV_CREATING_CTRL,
    UI_WV_READY,
    UI_WV_FAILED
} UIWebViewState;

struct UIWebView {
    const char* __widget_type;

    char* url;            // owned UTF-8 URL (or NULL)
    char* pendingNav;     // queued navigate while not ready

    HWND  parentHwnd;     // SDL window's HWND
    HWND  hostHwnd;       // child HWND we own; WebView2 lives inside it.
                          // Lets us clip via SetWindowRgn so higher-z
                          // widgets above the webview stay visible.

    ICoreWebView2Environment* env;
    ICoreWebView2Controller*  controller;
    ICoreWebView2*            core;

    UIWebViewState state;

    // Last bounds applied (in widget/window coords) so we skip
    // put_Bounds calls when nothing changed.
    RECT lastBounds;

    UIWebViewReadyCallback onReady;
    void* userdata;

    // ---- Visual customisation -------------------------------------
    float   cornerRadius;       // 0 = sharp corners
    int     hasBorder;          // 0 = no border painted
    float   borderWidth;        // border thickness in px
    UIColor borderColor;        // border fill colour

    // ---- Pending settings (applied once controller is ready) ------
    char* pendingUA;            // owned UTF-8 or NULL
    int   pendingDevTools;      // -1 = unset, 0 = off, 1 = on
    int   pendingContextMenus;  // -1 = unset, 0 = off, 1 = on
    int   pendingClearCookies;  // 1 = clear once ready

    // ---- Init scripts ---------------------------------------------
    // These accumulate and re-run on every page load. Pre-init they
    // sit here and are applied when the controller becomes ready.
    char** initScripts;
    int    initScriptCount;
    int    initScriptCap;
    int    initScriptsApplied;  // 1 once we've sent them post-READY

    // ---- One-shot scripts queued before READY ---------------------
    char** pendingExec;
    int    pendingExecCount;
    int    pendingExecCap;

    // ---- Request interception -------------------------------------
    UIWebViewRequestCallback onRequest;
    void* onRequestUserdata;
    EventRegistrationToken   requestToken;
    int requestHandlerRegistered;
    void* requestHandler;       // ResReqHandler* (forward-decl below)

    // ---- Browser environment customisation ------------------------
    char* browserArgs; // additionalBrowserArguments, NULL = defaults
    int   isolated;    // 1 = dedicated env; 0 (default) = shared singleton

    // ---- Composition mode (DirectComposition via C++ wrapper) -----
    int                                     useCompositionMode;
    ICoreWebView2CompositionController*     compController;
    UIWebViewDComp*                         dcomp;
    int                                     currentCursor; // UICursor enum value
    EventRegistrationToken                  cursorToken;
    void*                                   cursorHandler;
    int                                     cursorHandlerRegistered;

    // ---- Process crash callback -----------------------------------
    UIWebViewProcessFailedCallback onProcessFailed;
    void*                          onProcessFailedUserdata;
    EventRegistrationToken         processFailedToken;
    void*                          processFailedHandler;
    int                            processFailedHandlerRegistered;

    // ---- Navigation events ----------------------------------------
    UIWebViewUrlChangeCallback onUrlChange;
    void*                      onUrlChangeUserdata;
    UIWebViewLoadingCallback   onLoading;
    void*                      onLoadingUserdata;

    EventRegistrationToken     sourceChangedToken;
    EventRegistrationToken     navStartToken;
    EventRegistrationToken     navCompleteToken;
    int                        navHandlersInstalled;
    void*                      sourceChangedHandler; // SourceChangedHandler*
    void*                      navStartHandler;      // NavStartHandler*
    void*                      navCompleteHandler;   // NavCompleteHandler*
};

// One-shot window class registration used for the host child HWND.
static const wchar_t* kHostClass = L"MocidaWebViewHost";
static int g_hostClassRegistered = 0;

/* ---------------------------------------------------------------
 * Shared WebView2 environment singleton.
 *
 * Default behaviour: ALL non-isolated UIWebViews reuse a single
 * ICoreWebView2Environment created lazily by the first webview to
 * start. This cuts ~100-150 MB per additional webview because
 * Chromium reuses the browser/network/utility processes.
 *
 * Crash semantics (same as a Chrome window with many tabs):
 *   - Renderer crash:  affects only the offending webview.
 *   - GPU crash:       brief glitch, Chromium recovers automatically.
 *   - Browser crash:   rare; all shared webviews go down together.
 *
 * Pass UIWebView_SetIsolated(wv, 1) before init to force a dedicated
 * environment (heavier but fully crash-isolated).
 * --------------------------------------------------------------- */
typedef enum {
    UI_SHARED_ENV_NONE = 0,
    UI_SHARED_ENV_CREATING,
    UI_SHARED_ENV_READY,
    UI_SHARED_ENV_FAILED
} UISharedEnvState;

static ICoreWebView2Environment* g_sharedEnv      = NULL;
static UISharedEnvState          g_sharedEnvState = UI_SHARED_ENV_NONE;
static UIWebView**               g_sharedEnvWaiters = NULL;
static int                       g_sharedEnvWaitersCount = 0;
static int                       g_sharedEnvWaitersCap = 0;

static void shared_env_enqueue(UIWebView* wv) {
    if (g_sharedEnvWaitersCount >= g_sharedEnvWaitersCap) {
        int newCap = g_sharedEnvWaitersCap == 0 ? 4 : g_sharedEnvWaitersCap * 2;
        UIWebView** grown = (UIWebView**)realloc(g_sharedEnvWaiters,
                                                 sizeof(UIWebView*) * (size_t)newCap);
        if (!grown) return;
        g_sharedEnvWaiters = grown;
        g_sharedEnvWaitersCap = newCap;
    }
    g_sharedEnvWaiters[g_sharedEnvWaitersCount++] = wv;
}

static void KickOffControllerCreation(UIWebView* wv, ICoreWebView2Environment* env);

static void shared_env_drain_waiters(void) {
    int count = g_sharedEnvWaitersCount;
    UIWebView** waiters = g_sharedEnvWaiters;
    g_sharedEnvWaiters = NULL;
    g_sharedEnvWaitersCount = 0;
    g_sharedEnvWaitersCap = 0;
    for (int i = 0; i < count; i++) {
        UIWebView* w = waiters[i];
        if (!w) continue;
        if (g_sharedEnvState == UI_SHARED_ENV_READY && g_sharedEnv) {
            w->env = g_sharedEnv;
            ICoreWebView2Environment_AddRef(g_sharedEnv);
            KickOffControllerCreation(w, g_sharedEnv);
        } else {
            w->state = UI_WV_FAILED;
        }
    }
    free(waiters);
}

static void RegisterHostClassOnce(void) {
    if (g_hostClassRegistered) return;
    WNDCLASSEXW wc = { 0 };
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.hCursor       = NULL; // SDL/Mocida drives the cursor
    wc.lpszClassName = kHostClass;
    wc.hbrBackground = NULL;
    RegisterClassExW(&wc);
    g_hostClassRegistered = 1;
}

// ---------------------------------------------------------------------
// UTF-8 -> UTF-16 helper (caller frees with free()).
// ---------------------------------------------------------------------

static LPWSTR Utf8ToWide(const char* s) {
    if (!s) return NULL;
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    LPWSTR w = (LPWSTR)malloc(sizeof(WCHAR) * (size_t)n);
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

// ---------------------------------------------------------------------
// Forward decls of the callback helpers (defined below).
// ---------------------------------------------------------------------

static HRESULT BuildAndStart(UIWebView* wv);
static void    ApplyPendingNavigation(UIWebView* wv);
static void    ApplyPendingSettings(UIWebView* wv);
static void    ApplyPendingScripts(UIWebView* wv);
static void    EnsureRequestHandlerInstalled(UIWebView* wv);
static int     SetupDComp(UIWebView* wv);
static void    TeardownDComp(UIWebView* wv);
static void    EnsureNavHandlersInstalled(UIWebView* wv);
static void    RemoveNavHandlers(UIWebView* wv);
static void    EnsureCursorHandlerInstalled(UIWebView* wv);
static void    RemoveCursorHandler(UIWebView* wv);
static int     SystemCursorIdToUICursor(UINT32 idc);
static void    EnsureProcessFailedHandlerInstalled(UIWebView* wv);
static void    RemoveProcessFailedHandler(UIWebView* wv);

// Utility: UTF-16 -> UTF-8 (caller supplies dst). Truncates safely.
static void WideToUtf8(LPCWSTR src, char* dst, size_t cap) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    int n = WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, (int)cap, NULL, NULL);
    if (n <= 0) dst[0] = '\0';
    else if ((size_t)n >= cap) dst[cap - 1] = '\0';
}

// ---------------------------------------------------------------------
// COM event handlers. WebView2 expects two interfaces:
//   * ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler
//   * ICoreWebView2CreateCoreWebView2ControllerCompletedHandler
//
// We implement them in C by hand-rolling a vtable. The handler structs
// embed a back-pointer to the UIWebView they belong to.
// ---------------------------------------------------------------------

typedef struct {
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl* lpVtbl;
    LONG refCount;
    UIWebView* owner;
} EnvHandler;

typedef struct {
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl* lpVtbl;
    LONG refCount;
    UIWebView* owner;
} CtrlHandler;

typedef struct {
    ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandlerVtbl* lpVtbl;
    LONG refCount;
    UIWebView* owner;
} CompCtrlHandler;

// Common QueryInterface used by both handlers - both inherit from
// IUnknown which is the only interface we need to respond to.
static HRESULT STDMETHODCALLTYPE Handler_QueryInterface(void* self, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown)) { *ppv = self; return S_OK; }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE EnvHandler_AddRef(EnvHandler* self)  { return (ULONG)InterlockedIncrement(&self->refCount); }
static ULONG STDMETHODCALLTYPE EnvHandler_Release(EnvHandler* self) {
    LONG n = InterlockedDecrement(&self->refCount);
    if (n == 0) free(self);
    return (ULONG)n;
}
static ULONG STDMETHODCALLTYPE CtrlHandler_AddRef(CtrlHandler* self)  { return (ULONG)InterlockedIncrement(&self->refCount); }
static ULONG STDMETHODCALLTYPE CtrlHandler_Release(CtrlHandler* self) {
    LONG n = InterlockedDecrement(&self->refCount);
    if (n == 0) free(self);
    return (ULONG)n;
}
static ULONG STDMETHODCALLTYPE CompCtrlHandler_AddRef(CompCtrlHandler* self)  { return (ULONG)InterlockedIncrement(&self->refCount); }
static ULONG STDMETHODCALLTYPE CompCtrlHandler_Release(CompCtrlHandler* self) {
    LONG n = InterlockedDecrement(&self->refCount);
    if (n == 0) free(self);
    return (ULONG)n;
}

// Env completion: called by WebView2 once the environment exists.
static HRESULT STDMETHODCALLTYPE EnvHandler_Invoke(EnvHandler* self,
                                                  HRESULT errorCode,
                                                  ICoreWebView2Environment* env) {
    UIWebView* wv = self->owner;
    WVLOG("[webview] EnvHandler_Invoke entered (hr=0x%08lX, env=%p)",
          (unsigned long)errorCode, (void*)env);
    if (!wv) return S_OK;
    if (FAILED(errorCode) || !env) {
        WVLOG("[webview] environment creation FAILED (hr=0x%08lX). "
              "Likely: WebView2 runtime missing, user-data-folder unwritable, "
              "or WebView2Loader.dll absent next to the exe.",
              (unsigned long)errorCode);
        wv->state = UI_WV_FAILED;
        return S_OK;
    }

    WVLOG("[webview] environment OK, requesting controller for parent=%p (compMode=%d)",
          (void*)(wv->hostHwnd ? wv->hostHwnd : wv->parentHwnd), wv->useCompositionMode);
    wv->env = env;
    ICoreWebView2Environment_AddRef(env);

    // If this webview is part of the shared-env path, publish the env
    // so subsequent waiters reuse it instead of triggering a duplicate
    // CreateCoreWebView2EnvironmentWithOptions (which would either
    // collide on the user data folder or just waste resources).
    if (!wv->isolated && g_sharedEnvState == UI_SHARED_ENV_CREATING && !g_sharedEnv) {
        g_sharedEnv = env;
        ICoreWebView2Environment_AddRef(env);
        g_sharedEnvState = UI_SHARED_ENV_READY;
        WVLOG("[webview] shared environment published (waiters=%d)",
              g_sharedEnvWaitersCount);
        shared_env_drain_waiters();
    }

    KickOffControllerCreation(wv, env);
    return S_OK;
}

// Extracted from EnvHandler_Invoke so the shared-env path can call it
// directly when waking up a queued waiter (no second env creation).
static void KickOffControllerCreation(UIWebView* wv, ICoreWebView2Environment* env) {
    if (!wv || !env) return;

    if (wv->useCompositionMode) {
        ICoreWebView2Environment3* env3 = NULL;
        HRESULT q = ICoreWebView2Environment_QueryInterface(env,
            &IID_ICoreWebView2Environment3, (void**)&env3);
        if (FAILED(q) || !env3) {
            WVLOG("[webview] ICoreWebView2Environment3 unavailable (hr=0x%08lX); "
                  "falling back to HWND mode", (unsigned long)q);
            wv->useCompositionMode = 0;
        } else {
            CompCtrlHandler* cch = (CompCtrlHandler*)calloc(1, sizeof(CompCtrlHandler));
            if (!cch) { ICoreWebView2Environment3_Release(env3); wv->state = UI_WV_FAILED; return; }
            static ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandlerVtbl compVtbl;
            compVtbl.QueryInterface = (HRESULT (STDMETHODCALLTYPE*)(ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler*, REFIID, void**))Handler_QueryInterface;
            compVtbl.AddRef         = (ULONG (STDMETHODCALLTYPE*)(ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler*))CompCtrlHandler_AddRef;
            compVtbl.Release        = (ULONG (STDMETHODCALLTYPE*)(ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler*))CompCtrlHandler_Release;
            {
                extern HRESULT STDMETHODCALLTYPE CompCtrlHandler_Invoke(CompCtrlHandler* self, HRESULT, ICoreWebView2CompositionController*);
                compVtbl.Invoke = (HRESULT (STDMETHODCALLTYPE*)(ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler*, HRESULT, ICoreWebView2CompositionController*))CompCtrlHandler_Invoke;
            }
            cch->refCount = 1;
            cch->owner    = wv;
            cch->lpVtbl   = &compVtbl;
            wv->state = UI_WV_CREATING_CTRL;
            HRESULT hr = ICoreWebView2Environment3_CreateCoreWebView2CompositionController(
                env3, wv->parentHwnd,
                (ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler*)cch);
            ICoreWebView2Environment3_Release(env3);
            if (FAILED(hr)) {
                WVLOG("[webview] CreateCoreWebView2CompositionController failed (hr=0x%08lX)",
                      (unsigned long)hr);
                wv->state = UI_WV_FAILED;
                CompCtrlHandler_Release(cch);
            }
            return;
        }
    }

    CtrlHandler* ch = (CtrlHandler*)calloc(1, sizeof(CtrlHandler));
    if (!ch) { wv->state = UI_WV_FAILED; return; }
    static ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl ctrlVtbl;
    ctrlVtbl.QueryInterface = (HRESULT (STDMETHODCALLTYPE*)(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*, REFIID, void**))Handler_QueryInterface;
    ctrlVtbl.AddRef         = (ULONG (STDMETHODCALLTYPE*)(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*))CtrlHandler_AddRef;
    ctrlVtbl.Release        = (ULONG (STDMETHODCALLTYPE*)(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*))CtrlHandler_Release;
    ch->refCount = 1; ch->owner = wv; ch->lpVtbl = &ctrlVtbl;
    {
        extern HRESULT STDMETHODCALLTYPE CtrlHandler_Invoke(CtrlHandler* self, HRESULT, ICoreWebView2Controller*);
        ctrlVtbl.Invoke = (HRESULT (STDMETHODCALLTYPE*)(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*, HRESULT, ICoreWebView2Controller*))CtrlHandler_Invoke;
    }
    wv->state = UI_WV_CREATING_CTRL;
    HRESULT hr = ICoreWebView2Environment_CreateCoreWebView2Controller(
        env, wv->hostHwnd ? wv->hostHwnd : wv->parentHwnd,
        (ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*)ch);
    if (FAILED(hr)) {
        WVLOG("[webview] CreateCoreWebView2Controller failed (hr=0x%08lX)",
              (unsigned long)hr);
        wv->state = UI_WV_FAILED;
        CtrlHandler_Release(ch);
    }
}

// Ctrl completion: called once the controller is ready. From here we
// can grab the ICoreWebView2 (the "core"), apply queued navigation,
// and let the rest of the codebase position it via put_Bounds.
HRESULT STDMETHODCALLTYPE CtrlHandler_Invoke(CtrlHandler* self,
                                             HRESULT errorCode,
                                             ICoreWebView2Controller* controller) {
    UIWebView* wv = self->owner;
    WVLOG("[webview] CtrlHandler_Invoke entered (hr=0x%08lX, controller=%p)",
          (unsigned long)errorCode, (void*)controller);
    if (!wv) return S_OK;
    if (FAILED(errorCode) || !controller) {
        WVLOG("[webview] controller creation FAILED (hr=0x%08lX)",
              (unsigned long)errorCode);
        wv->state = UI_WV_FAILED;
        return S_OK;
    }

    wv->controller = controller;
    ICoreWebView2Controller_AddRef(controller);

    ICoreWebView2* core = NULL;
    if (SUCCEEDED(ICoreWebView2Controller_get_CoreWebView2(controller, &core)) && core) {
        wv->core = core; // already AddRef'd by get_
    }

    // Make it visible. Bounds get set by the renderer on the next frame.
    ICoreWebView2Controller_put_IsVisible(controller, TRUE);

    // Force-apply the most recent bounds the renderer recorded. Without
    // this, the controller stays at its default 0x0 size: the renderer
    // gates put_Bounds on `lastBounds != current_bounds`, but lastBounds
    // was already captured during a pre-READY tick (when the controller
    // didn't exist yet), so no later tick would trigger the apply.
    if (wv->lastBounds.right > wv->lastBounds.left &&
        wv->lastBounds.bottom > wv->lastBounds.top) {
        RECT inner = { 0, 0,
                       wv->lastBounds.right  - wv->lastBounds.left,
                       wv->lastBounds.bottom - wv->lastBounds.top };
        ICoreWebView2Controller_put_Bounds(controller, inner);
        WVLOG("[webview] applied initial controller bounds %dx%d",
              (int)(inner.right), (int)(inner.bottom));
    }

    wv->state = UI_WV_READY;
    WVLOG("[webview] state = READY (core=%p)", (void*)wv->core);

    // Apply everything that was queued while the controller didn't
    // exist yet. Order matters: settings first (UA influences nav),
    // then scripts (init scripts apply on the upcoming navigation),
    // then request handler, then navigation.
    ApplyPendingSettings(wv);
    ApplyPendingScripts(wv);
    EnsureRequestHandlerInstalled(wv);
    EnsureNavHandlersInstalled(wv);
    ApplyPendingNavigation(wv);

    if (wv->onReady) wv->onReady(wv, wv->userdata);
    return S_OK;
}

// ---------------------------------------------------------------------
// Composition controller path. Called when the user opted into
// composition mode. After we have the ICoreWebView2CompositionController
// we:
//   1. Promote it to the standard ICoreWebView2Controller (via QI) so
//      put_Bounds / put_IsVisible work just like in HWND mode.
//   2. Pull the ICoreWebView2 core for navigation/scripts/etc.
//   3. Set up DirectComposition (device, target, root visual) and bind
//      the visual to the controller via put_RootVisualTarget so
//      WebView2 renders into it.
//   4. Apply the same pending settings / scripts / nav as the HWND
//      path so the public API behaves identically.
// ---------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CompCtrlHandler_Invoke(CompCtrlHandler* self,
                                                HRESULT errorCode,
                                                ICoreWebView2CompositionController* compController) {
    UIWebView* wv = self->owner;
    WVLOG("[webview] CompCtrlHandler_Invoke entered (hr=0x%08lX, compController=%p)",
          (unsigned long)errorCode, (void*)compController);
    if (!wv) return S_OK;
    if (FAILED(errorCode) || !compController) {
        WVLOG("[webview] composition controller creation FAILED (hr=0x%08lX)",
              (unsigned long)errorCode);
        wv->state = UI_WV_FAILED;
        return S_OK;
    }

    wv->compController = compController;
    ICoreWebView2CompositionController_AddRef(compController);

    // QI for ICoreWebView2Controller - same instance, different vtable.
    ICoreWebView2Controller* ctrl = NULL;
    HRESULT hr = ICoreWebView2CompositionController_QueryInterface(compController,
        &IID_ICoreWebView2Controller, (void**)&ctrl);
    if (FAILED(hr) || !ctrl) {
        WVLOG("[webview] QI for ICoreWebView2Controller failed (hr=0x%08lX)", (unsigned long)hr);
        wv->state = UI_WV_FAILED;
        return S_OK;
    }
    wv->controller = ctrl; // already AddRef'd by QI

    ICoreWebView2* core = NULL;
    if (SUCCEEDED(ICoreWebView2Controller_get_CoreWebView2(ctrl, &core)) && core) {
        wv->core = core;
    }

    ICoreWebView2Controller_put_IsVisible(ctrl, TRUE);

    // Stand up DirectComposition. If it fails the whole composition
    // path bails - the user is informed via the log.
    if (!SetupDComp(wv)) {
        WVLOG("[webview] DComp setup failed; webview will not render");
        wv->state = UI_WV_FAILED;
        return S_OK;
    }

    // Apply lastBounds the same way the HWND path does.
    if (wv->lastBounds.right > wv->lastBounds.left &&
        wv->lastBounds.bottom > wv->lastBounds.top) {
        RECT inner = { 0, 0,
                       wv->lastBounds.right  - wv->lastBounds.left,
                       wv->lastBounds.bottom - wv->lastBounds.top };
        ICoreWebView2Controller_put_Bounds(ctrl, inner);
        WVLOG("[webview/comp] applied initial controller bounds %dx%d",
              (int)inner.right, (int)inner.bottom);
    }

    wv->state = UI_WV_READY;
    WVLOG("[webview/comp] state = READY (core=%p)", (void*)wv->core);

    ApplyPendingSettings(wv);
    ApplyPendingScripts(wv);
    EnsureRequestHandlerInstalled(wv);
    EnsureNavHandlersInstalled(wv);
    EnsureCursorHandlerInstalled(wv);
    EnsureProcessFailedHandlerInstalled(wv);
    ApplyPendingNavigation(wv);

    if (wv->onReady) wv->onReady(wv, wv->userdata);
    return S_OK;
}

// ---------------------------------------------------------------------
// DirectComposition setup: build the DComp pipeline (via the C++ shim
// in webview_dcomp.cpp - dcomp.h is hostile to C compilation) and hand
// its root visual to WebView2 via put_RootVisualTarget.
// ---------------------------------------------------------------------
static int SetupDComp(UIWebView* wv) {
    if (!wv || !wv->parentHwnd || !wv->compController) return 0;

    wv->dcomp = UIWebViewDComp_Create(wv->parentHwnd);
    if (!wv->dcomp) {
        WVLOG("[webview/comp] UIWebViewDComp_Create failed");
        return 0;
    }

    void* visual = UIWebViewDComp_GetRootVisualAsIUnknown(wv->dcomp);
    if (!visual) {
        WVLOG("[webview/comp] no root visual returned");
        TeardownDComp(wv);
        return 0;
    }

    HRESULT hr = ICoreWebView2CompositionController_put_RootVisualTarget(
        wv->compController, (IUnknown*)visual);
    if (FAILED(hr)) {
        WVLOG("[webview/comp] put_RootVisualTarget failed hr=0x%08lX", (unsigned long)hr);
        TeardownDComp(wv);
        return 0;
    }

    UIWebViewDComp_Commit(wv->dcomp);
    WVLOG("[webview/comp] DComp pipeline wired (visual=%p)", visual);
    return 1;
}

static void TeardownDComp(UIWebView* wv) {
    if (!wv) return;
    if (wv->compController) {
        ICoreWebView2CompositionController_put_RootVisualTarget(wv->compController, NULL);
    }
    if (wv->dcomp) {
        UIWebViewDComp_Destroy(wv->dcomp);
        wv->dcomp = NULL;
    }
}

// ---------------------------------------------------------------------
// Pending-settings application. Called from CtrlHandler_Invoke after
// the controller is ready, and from setters that are called AFTER the
// initial READY transition (those clear the pending flag immediately).
// ---------------------------------------------------------------------

static void ApplyPendingSettings(UIWebView* wv) {
    if (!wv || !wv->core) return;
    ICoreWebView2Settings* settings = NULL;
    if (FAILED(ICoreWebView2_get_Settings(wv->core, &settings)) || !settings) {
        WVLOG("[webview] get_Settings failed");
        return;
    }

    if (wv->pendingDevTools >= 0) {
        ICoreWebView2Settings_put_AreDevToolsEnabled(settings,
            wv->pendingDevTools ? TRUE : FALSE);
        WVLOG("[webview] applied DevToolsEnabled=%d", wv->pendingDevTools);
        wv->pendingDevTools = -1;
    }
    if (wv->pendingContextMenus >= 0) {
        ICoreWebView2Settings_put_AreDefaultContextMenusEnabled(settings,
            wv->pendingContextMenus ? TRUE : FALSE);
        WVLOG("[webview] applied ContextMenusEnabled=%d", wv->pendingContextMenus);
        wv->pendingContextMenus = -1;
    }

    // UserAgent lives on ICoreWebView2Settings2 - query for the newer
    // interface and only apply if it's available.
    if (wv->pendingUA) {
        ICoreWebView2Settings2* s2 = NULL;
        HRESULT hr = ICoreWebView2Settings_QueryInterface(settings,
            &IID_ICoreWebView2Settings2, (void**)&s2);
        if (SUCCEEDED(hr) && s2) {
            LPWSTR wide = Utf8ToWide(wv->pendingUA);
            if (wide) {
                ICoreWebView2Settings2_put_UserAgent(s2, wide);
                WVLOG("[webview] applied UserAgent=%s", wv->pendingUA);
                free(wide);
            }
            ICoreWebView2Settings2_Release(s2);
        } else {
            WVLOG("[webview] ICoreWebView2Settings2 not available (hr=0x%08lX)",
                  (unsigned long)hr);
        }
        free(wv->pendingUA);
        wv->pendingUA = NULL;
    }

    ICoreWebView2Settings_Release(settings);

    if (wv->pendingClearCookies) {
        ICoreWebView2_2* core2 = NULL;
        if (SUCCEEDED(ICoreWebView2_QueryInterface(wv->core,
                                                  &IID_ICoreWebView2_2,
                                                  (void**)&core2)) && core2) {
            ICoreWebView2CookieManager* mgr = NULL;
            if (SUCCEEDED(ICoreWebView2_2_get_CookieManager(core2, &mgr)) && mgr) {
                ICoreWebView2CookieManager_DeleteAllCookies(mgr);
                ICoreWebView2CookieManager_Release(mgr);
                WVLOG("[webview] cleared all cookies");
            }
            ICoreWebView2_2_Release(core2);
        }
        wv->pendingClearCookies = 0;
    }
}

// ---------------------------------------------------------------------
// AddScriptToExecuteOnDocumentCreated requires a completion handler.
// We don't care about the result, so this is a minimal IUnknown.
// ---------------------------------------------------------------------

typedef struct {
    ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandlerVtbl* lpVtbl;
    LONG refCount;
} AddScriptHandler;

static ULONG STDMETHODCALLTYPE AddScriptHandler_AddRef(AddScriptHandler* self) {
    return (ULONG)InterlockedIncrement(&self->refCount);
}
static ULONG STDMETHODCALLTYPE AddScriptHandler_Release(AddScriptHandler* self) {
    LONG n = InterlockedDecrement(&self->refCount);
    if (n == 0) free(self);
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE AddScriptHandler_Invoke(AddScriptHandler* self,
                                                        HRESULT hr,
                                                        LPCWSTR id) {
    (void)self; (void)id;
    if (FAILED(hr)) WVLOG("[webview] init script registration failed (hr=0x%08lX)",
                          (unsigned long)hr);
    return S_OK;
}

static void ApplyPendingScripts(UIWebView* wv) {
    if (!wv || !wv->core) return;

    // Init scripts: re-applied even after a prior READY because they
    // are persistent registrations, but we only need to send them once
    // per controller lifetime. `initScriptsApplied` guards repeats.
    if (!wv->initScriptsApplied) {
        static ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandlerVtbl ascVtbl;
        ascVtbl.QueryInterface = (HRESULT (STDMETHODCALLTYPE*)(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler*, REFIID, void**))Handler_QueryInterface;
        ascVtbl.AddRef         = (ULONG (STDMETHODCALLTYPE*)(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler*))AddScriptHandler_AddRef;
        ascVtbl.Release        = (ULONG (STDMETHODCALLTYPE*)(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler*))AddScriptHandler_Release;
        ascVtbl.Invoke         = (HRESULT (STDMETHODCALLTYPE*)(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler*, HRESULT, LPCWSTR))AddScriptHandler_Invoke;

        for (int i = 0; i < wv->initScriptCount; i++) {
            LPWSTR wide = Utf8ToWide(wv->initScripts[i]);
            if (!wide) continue;
            AddScriptHandler* h = (AddScriptHandler*)calloc(1, sizeof(AddScriptHandler));
            if (h) {
                h->refCount = 1;
                h->lpVtbl   = &ascVtbl;
                ICoreWebView2_AddScriptToExecuteOnDocumentCreated(wv->core, wide,
                    (ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler*)h);
            }
            free(wide);
        }
        wv->initScriptsApplied = 1;
        WVLOG("[webview] applied %d init script(s)", wv->initScriptCount);
    }

    // One-shot scripts.
    for (int i = 0; i < wv->pendingExecCount; i++) {
        LPWSTR wide = Utf8ToWide(wv->pendingExec[i]);
        if (wide) {
            ICoreWebView2_ExecuteScript(wv->core, wide, NULL);
            free(wide);
        }
        free(wv->pendingExec[i]);
        wv->pendingExec[i] = NULL;
    }
    wv->pendingExecCount = 0;
}

// ---------------------------------------------------------------------
// Request interception: WebResourceRequested event handler.
// Fires per network request; we extract the URL, call the user's
// callback, and synthesize a 403 response when the callback says block.
// ---------------------------------------------------------------------

typedef struct {
    ICoreWebView2WebResourceRequestedEventHandlerVtbl* lpVtbl;
    LONG refCount;
    UIWebView* owner;
} ResReqHandler;

static ULONG STDMETHODCALLTYPE ResReqHandler_AddRef(ResReqHandler* self) {
    return (ULONG)InterlockedIncrement(&self->refCount);
}
static ULONG STDMETHODCALLTYPE ResReqHandler_Release(ResReqHandler* self) {
    LONG n = InterlockedDecrement(&self->refCount);
    if (n == 0) free(self);
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE ResReqHandler_Invoke(ResReqHandler* self,
                                                     ICoreWebView2* sender,
                                                     ICoreWebView2WebResourceRequestedEventArgs* args) {
    (void)sender;
    UIWebView* wv = self->owner;
    if (!wv || !wv->onRequest || !args) return S_OK;

    ICoreWebView2WebResourceRequest* req = NULL;
    if (FAILED(ICoreWebView2WebResourceRequestedEventArgs_get_Request(args, &req)) || !req) {
        return S_OK;
    }

    LPWSTR wide = NULL;
    if (SUCCEEDED(ICoreWebView2WebResourceRequest_get_Uri(req, &wide)) && wide) {
        char utf8[2048];
        WideToUtf8(wide, utf8, sizeof(utf8));
        CoTaskMemFree(wide);

        int block = wv->onRequest(wv, utf8, wv->onRequestUserdata);
        if (block && wv->env) {
            // Synthesize an empty 403 response so the request fails
            // visibly in the page (rather than hanging).
            ICoreWebView2WebResourceResponse* resp = NULL;
            if (SUCCEEDED(ICoreWebView2Environment_CreateWebResourceResponse(
                    wv->env, NULL, 403, L"Blocked by host",
                    L"Content-Type: text/plain", &resp)) && resp) {
                ICoreWebView2WebResourceRequestedEventArgs_put_Response(args, resp);
                ICoreWebView2WebResourceResponse_Release(resp);
                WVLOG("[webview] blocked %s", utf8);
            }
        }
    }

    ICoreWebView2WebResourceRequest_Release(req);
    return S_OK;
}

static void EnsureRequestHandlerInstalled(UIWebView* wv) {
    if (!wv || !wv->core || wv->requestHandlerRegistered) return;
    if (!wv->onRequest) return; // nothing to install yet

    static ICoreWebView2WebResourceRequestedEventHandlerVtbl reqVtbl;
    reqVtbl.QueryInterface = (HRESULT (STDMETHODCALLTYPE*)(ICoreWebView2WebResourceRequestedEventHandler*, REFIID, void**))Handler_QueryInterface;
    reqVtbl.AddRef         = (ULONG (STDMETHODCALLTYPE*)(ICoreWebView2WebResourceRequestedEventHandler*))ResReqHandler_AddRef;
    reqVtbl.Release        = (ULONG (STDMETHODCALLTYPE*)(ICoreWebView2WebResourceRequestedEventHandler*))ResReqHandler_Release;
    reqVtbl.Invoke         = (HRESULT (STDMETHODCALLTYPE*)(ICoreWebView2WebResourceRequestedEventHandler*, ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs*))ResReqHandler_Invoke;

    ResReqHandler* h = (ResReqHandler*)calloc(1, sizeof(ResReqHandler));
    if (!h) return;
    h->refCount = 1;
    h->lpVtbl   = &reqVtbl;
    h->owner    = wv;

    // Match every request - the user filter happens inside the callback.
    ICoreWebView2_AddWebResourceRequestedFilter(wv->core, L"*",
        COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);

    HRESULT hr = ICoreWebView2_add_WebResourceRequested(wv->core,
        (ICoreWebView2WebResourceRequestedEventHandler*)h,
        &wv->requestToken);

    if (FAILED(hr)) {
        WVLOG("[webview] add_WebResourceRequested failed (hr=0x%08lX)", (unsigned long)hr);
        ResReqHandler_Release(h);
        return;
    }

    wv->requestHandler = h;
    wv->requestHandlerRegistered = 1;
    WVLOG("[webview] request interceptor installed");
}

// ---------------------------------------------------------------------
// Navigation event handlers (SourceChanged, NavigationStarting,
// NavigationCompleted). These let callers observe URL changes and
// loading state from the webview - drives address-bar sync and a
// browser-style progress bar.
// ---------------------------------------------------------------------

typedef struct {
    ICoreWebView2SourceChangedEventHandlerVtbl* lpVtbl;
    LONG refCount;
    UIWebView* owner;
} SourceChangedHandler;

typedef struct {
    ICoreWebView2NavigationStartingEventHandlerVtbl* lpVtbl;
    LONG refCount;
    UIWebView* owner;
} NavStartHandler;

typedef struct {
    ICoreWebView2NavigationCompletedEventHandlerVtbl* lpVtbl;
    LONG refCount;
    UIWebView* owner;
} NavCompleteHandler;

#define DEFINE_NAV_REFCOUNT(T)                                              \
    static ULONG STDMETHODCALLTYPE T##_AddRef(T* self) {                    \
        return (ULONG)InterlockedIncrement(&self->refCount);                \
    }                                                                       \
    static ULONG STDMETHODCALLTYPE T##_Release(T* self) {                   \
        LONG n = InterlockedDecrement(&self->refCount);                     \
        if (n == 0) free(self);                                             \
        return (ULONG)n;                                                    \
    }
DEFINE_NAV_REFCOUNT(SourceChangedHandler)
DEFINE_NAV_REFCOUNT(NavStartHandler)
DEFINE_NAV_REFCOUNT(NavCompleteHandler)

static HRESULT STDMETHODCALLTYPE SourceChangedHandler_Invoke(
        SourceChangedHandler* self,
        ICoreWebView2* sender,
        ICoreWebView2SourceChangedEventArgs* args) {
    (void)sender; (void)args;
    UIWebView* wv = self->owner;
    if (!wv || !wv->onUrlChange || !wv->core) return S_OK;

    LPWSTR wide = NULL;
    if (SUCCEEDED(ICoreWebView2_get_Source(wv->core, &wide)) && wide) {
        char utf8[2048];
        WideToUtf8(wide, utf8, sizeof(utf8));
        CoTaskMemFree(wide);
        // Update our cached url too so UIWebView_GetUrl matches reality.
        free(wv->url);
        wv->url = _strdup(utf8);
        wv->onUrlChange(wv, utf8, wv->onUrlChangeUserdata);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE NavStartHandler_Invoke(
        NavStartHandler* self,
        ICoreWebView2* sender,
        ICoreWebView2NavigationStartingEventArgs* args) {
    (void)sender; (void)args;
    UIWebView* wv = self->owner;
    if (!wv || !wv->onLoading) return S_OK;
    wv->onLoading(wv, 1, wv->onLoadingUserdata);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE NavCompleteHandler_Invoke(
        NavCompleteHandler* self,
        ICoreWebView2* sender,
        ICoreWebView2NavigationCompletedEventArgs* args) {
    (void)sender; (void)args;
    UIWebView* wv = self->owner;
    if (!wv || !wv->onLoading) return S_OK;
    wv->onLoading(wv, 0, wv->onLoadingUserdata);
    return S_OK;
}

static void EnsureNavHandlersInstalled(UIWebView* wv) {
    if (!wv || !wv->core || wv->navHandlersInstalled) return;
    // Skip if no callback at all has been registered; the handlers can
    // be installed lazily later by the OnXxx setters.
    if (!wv->onUrlChange && !wv->onLoading) return;

    if (wv->onUrlChange) {
        static ICoreWebView2SourceChangedEventHandlerVtbl scVtbl;
        scVtbl.QueryInterface = (HRESULT (STDMETHODCALLTYPE*)(ICoreWebView2SourceChangedEventHandler*, REFIID, void**))Handler_QueryInterface;
        scVtbl.AddRef         = (ULONG (STDMETHODCALLTYPE*)(ICoreWebView2SourceChangedEventHandler*))SourceChangedHandler_AddRef;
        scVtbl.Release        = (ULONG (STDMETHODCALLTYPE*)(ICoreWebView2SourceChangedEventHandler*))SourceChangedHandler_Release;
        scVtbl.Invoke         = (HRESULT (STDMETHODCALLTYPE*)(ICoreWebView2SourceChangedEventHandler*, ICoreWebView2*, ICoreWebView2SourceChangedEventArgs*))SourceChangedHandler_Invoke;
        SourceChangedHandler* h = (SourceChangedHandler*)calloc(1, sizeof(SourceChangedHandler));
        if (h) {
            h->refCount = 1; h->lpVtbl = &scVtbl; h->owner = wv;
            ICoreWebView2_add_SourceChanged(wv->core,
                (ICoreWebView2SourceChangedEventHandler*)h, &wv->sourceChangedToken);
            wv->sourceChangedHandler = h;
        }
    }

    if (wv->onLoading) {
        static ICoreWebView2NavigationStartingEventHandlerVtbl nsVtbl;
        nsVtbl.QueryInterface = (HRESULT (STDMETHODCALLTYPE*)(ICoreWebView2NavigationStartingEventHandler*, REFIID, void**))Handler_QueryInterface;
        nsVtbl.AddRef         = (ULONG (STDMETHODCALLTYPE*)(ICoreWebView2NavigationStartingEventHandler*))NavStartHandler_AddRef;
        nsVtbl.Release        = (ULONG (STDMETHODCALLTYPE*)(ICoreWebView2NavigationStartingEventHandler*))NavStartHandler_Release;
        nsVtbl.Invoke         = (HRESULT (STDMETHODCALLTYPE*)(ICoreWebView2NavigationStartingEventHandler*, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*))NavStartHandler_Invoke;
        NavStartHandler* nsH = (NavStartHandler*)calloc(1, sizeof(NavStartHandler));
        if (nsH) {
            nsH->refCount = 1; nsH->lpVtbl = &nsVtbl; nsH->owner = wv;
            ICoreWebView2_add_NavigationStarting(wv->core,
                (ICoreWebView2NavigationStartingEventHandler*)nsH, &wv->navStartToken);
            wv->navStartHandler = nsH;
        }

        static ICoreWebView2NavigationCompletedEventHandlerVtbl ncVtbl;
        ncVtbl.QueryInterface = (HRESULT (STDMETHODCALLTYPE*)(ICoreWebView2NavigationCompletedEventHandler*, REFIID, void**))Handler_QueryInterface;
        ncVtbl.AddRef         = (ULONG (STDMETHODCALLTYPE*)(ICoreWebView2NavigationCompletedEventHandler*))NavCompleteHandler_AddRef;
        ncVtbl.Release        = (ULONG (STDMETHODCALLTYPE*)(ICoreWebView2NavigationCompletedEventHandler*))NavCompleteHandler_Release;
        ncVtbl.Invoke         = (HRESULT (STDMETHODCALLTYPE*)(ICoreWebView2NavigationCompletedEventHandler*, ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*))NavCompleteHandler_Invoke;
        NavCompleteHandler* ncH = (NavCompleteHandler*)calloc(1, sizeof(NavCompleteHandler));
        if (ncH) {
            ncH->refCount = 1; ncH->lpVtbl = &ncVtbl; ncH->owner = wv;
            ICoreWebView2_add_NavigationCompleted(wv->core,
                (ICoreWebView2NavigationCompletedEventHandler*)ncH, &wv->navCompleteToken);
            wv->navCompleteHandler = ncH;
        }
    }

    wv->navHandlersInstalled = 1;
    WVLOG("[webview] navigation handlers installed (url=%d loading=%d)",
          wv->onUrlChange ? 1 : 0, wv->onLoading ? 1 : 0);
}

static void RemoveNavHandlers(UIWebView* wv) {
    if (!wv || !wv->core || !wv->navHandlersInstalled) return;
    if (wv->sourceChangedHandler) {
        ICoreWebView2_remove_SourceChanged(wv->core, wv->sourceChangedToken);
        SourceChangedHandler_Release((SourceChangedHandler*)wv->sourceChangedHandler);
        wv->sourceChangedHandler = NULL;
    }
    if (wv->navStartHandler) {
        ICoreWebView2_remove_NavigationStarting(wv->core, wv->navStartToken);
        NavStartHandler_Release((NavStartHandler*)wv->navStartHandler);
        wv->navStartHandler = NULL;
    }
    if (wv->navCompleteHandler) {
        ICoreWebView2_remove_NavigationCompleted(wv->core, wv->navCompleteToken);
        NavCompleteHandler_Release((NavCompleteHandler*)wv->navCompleteHandler);
        wv->navCompleteHandler = NULL;
    }
    wv->navHandlersInstalled = 0;
}

// ---------------------------------------------------------------------
// Composition-mode cursor handling. WebView2 doesn't manage the cursor
// natively when running in composition mode; it fires CursorChanged on
// the controller and the host has to apply the right cursor when the
// mouse is over the webview. We map the system cursor ID to mocida's
// UICursor enum so PickHoverCursor / UICursor_Apply does the rest.
// ---------------------------------------------------------------------

typedef struct {
    ICoreWebView2CursorChangedEventHandlerVtbl* lpVtbl;
    LONG refCount;
    UIWebView* owner;
} CursorChangedHandler;

static ULONG STDMETHODCALLTYPE CursorChangedHandler_AddRef(CursorChangedHandler* self) {
    return (ULONG)InterlockedIncrement(&self->refCount);
}
static ULONG STDMETHODCALLTYPE CursorChangedHandler_Release(CursorChangedHandler* self) {
    LONG n = InterlockedDecrement(&self->refCount);
    if (n == 0) free(self);
    return (ULONG)n;
}

static int SystemCursorIdToUICursor(UINT32 idc) {
    /* Standard IDC_* values from WinUser.h. UICursor enum from cursor.h. */
    switch (idc) {
        case 32513: return 2;  /* IDC_IBEAM    -> UI_CURSOR_TEXT       */
        case 32649: return 1;  /* IDC_HAND     -> UI_CURSOR_POINTER    */
        case 32515: return 3;  /* IDC_CROSS    -> UI_CURSOR_CROSSHAIR  */
        case 32646: return 4;  /* IDC_SIZEALL  -> UI_CURSOR_MOVE       */
        case 32648: return 5;  /* IDC_NO       -> UI_CURSOR_NOT_ALLOWED*/
        case 32514: return 6;  /* IDC_WAIT     -> UI_CURSOR_WAIT       */
        case 32650: return 7;  /* IDC_APPSTART -> UI_CURSOR_PROGRESS   */
        case 32644: return 8;  /* IDC_SIZEWE   -> UI_CURSOR_EW_RESIZE  */
        case 32645: return 9;  /* IDC_SIZENS   -> UI_CURSOR_NS_RESIZE  */
        case 32642: return 10; /* IDC_SIZENWSE -> UI_CURSOR_NWSE_RESIZE*/
        case 32643: return 11; /* IDC_SIZENESW -> UI_CURSOR_NESW_RESIZE*/
        default:    return 0;  /* anything else: arrow                  */
    }
}

static HRESULT STDMETHODCALLTYPE CursorChangedHandler_Invoke(
        CursorChangedHandler* self,
        ICoreWebView2CompositionController* sender,
        IUnknown* args) {
    (void)args;
    UIWebView* wv = self->owner;
    if (!wv || !sender) return S_OK;
    UINT32 idc = 0;
    if (SUCCEEDED(ICoreWebView2CompositionController_get_SystemCursorId(sender, &idc))) {
        wv->currentCursor = SystemCursorIdToUICursor(idc);
    }
    return S_OK;
}

static void EnsureCursorHandlerInstalled(UIWebView* wv) {
    if (!wv || !wv->compController || wv->cursorHandlerRegistered) return;

    static ICoreWebView2CursorChangedEventHandlerVtbl vt;
    vt.QueryInterface = (HRESULT (STDMETHODCALLTYPE*)(ICoreWebView2CursorChangedEventHandler*, REFIID, void**))Handler_QueryInterface;
    vt.AddRef         = (ULONG (STDMETHODCALLTYPE*)(ICoreWebView2CursorChangedEventHandler*))CursorChangedHandler_AddRef;
    vt.Release        = (ULONG (STDMETHODCALLTYPE*)(ICoreWebView2CursorChangedEventHandler*))CursorChangedHandler_Release;
    vt.Invoke         = (HRESULT (STDMETHODCALLTYPE*)(ICoreWebView2CursorChangedEventHandler*, ICoreWebView2CompositionController*, IUnknown*))CursorChangedHandler_Invoke;

    CursorChangedHandler* h = (CursorChangedHandler*)calloc(1, sizeof(CursorChangedHandler));
    if (!h) return;
    h->refCount = 1; h->lpVtbl = &vt; h->owner = wv;

    HRESULT hr = ICoreWebView2CompositionController_add_CursorChanged(wv->compController,
        (ICoreWebView2CursorChangedEventHandler*)h, &wv->cursorToken);
    if (FAILED(hr)) {
        WVLOG("[webview] add_CursorChanged failed hr=0x%08lX", (unsigned long)hr);
        CursorChangedHandler_Release(h);
        return;
    }
    wv->cursorHandler = h;
    wv->cursorHandlerRegistered = 1;
}

static void RemoveCursorHandler(UIWebView* wv) {
    if (!wv || !wv->compController || !wv->cursorHandlerRegistered) return;
    ICoreWebView2CompositionController_remove_CursorChanged(wv->compController, wv->cursorToken);
    if (wv->cursorHandler) {
        CursorChangedHandler_Release((CursorChangedHandler*)wv->cursorHandler);
        wv->cursorHandler = NULL;
    }
    wv->cursorHandlerRegistered = 0;
}

// ---------------------------------------------------------------------
// ProcessFailed event handler. WebView2 fires this when one of the
// underlying Chromium processes dies. Most useful for the RENDERER
// case where the page goes blank and the host wants to show a
// "Reload?" UI. We map the WebView2 enum onto our own so the public
// header doesn't need to drag WebView2.h.
// ---------------------------------------------------------------------

typedef struct {
    ICoreWebView2ProcessFailedEventHandlerVtbl* lpVtbl;
    LONG refCount;
    UIWebView* owner;
} ProcessFailedHandler;

static ULONG STDMETHODCALLTYPE ProcessFailedHandler_AddRef(ProcessFailedHandler* self) {
    return (ULONG)InterlockedIncrement(&self->refCount);
}
static ULONG STDMETHODCALLTYPE ProcessFailedHandler_Release(ProcessFailedHandler* self) {
    LONG n = InterlockedDecrement(&self->refCount);
    if (n == 0) free(self);
    return (ULONG)n;
}

static UIWebViewProcessKind map_failed_kind(COREWEBVIEW2_PROCESS_FAILED_KIND k) {
    switch (k) {
        case COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED:           return UI_WEBVIEW_PROCESS_BROWSER;
        case COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED:            return UI_WEBVIEW_PROCESS_RENDERER;
        case COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE:      return UI_WEBVIEW_PROCESS_RENDERER_UNRESPONSIVE;
        case COREWEBVIEW2_PROCESS_FAILED_KIND_FRAME_RENDER_PROCESS_EXITED:      return UI_WEBVIEW_PROCESS_FRAME_RENDERER;
        case COREWEBVIEW2_PROCESS_FAILED_KIND_UTILITY_PROCESS_EXITED:           return UI_WEBVIEW_PROCESS_UTILITY;
        case COREWEBVIEW2_PROCESS_FAILED_KIND_SANDBOX_HELPER_PROCESS_EXITED:    return UI_WEBVIEW_PROCESS_SANDBOX;
        case COREWEBVIEW2_PROCESS_FAILED_KIND_GPU_PROCESS_EXITED:               return UI_WEBVIEW_PROCESS_GPU;
        default:                                                                return UI_WEBVIEW_PROCESS_OTHER;
    }
}

static HRESULT STDMETHODCALLTYPE ProcessFailedHandler_Invoke(
        ProcessFailedHandler* self,
        ICoreWebView2* sender,
        ICoreWebView2ProcessFailedEventArgs* args) {
    (void)sender;
    UIWebView* wv = self->owner;
    if (!wv || !wv->onProcessFailed || !args) return S_OK;
    COREWEBVIEW2_PROCESS_FAILED_KIND k = COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;
    ICoreWebView2ProcessFailedEventArgs_get_ProcessFailedKind(args, &k);
    UIWebViewProcessKind ui = map_failed_kind(k);
    WVLOG("[webview] ProcessFailed kind=%d (mapped=%d)", (int)k, (int)ui);
    wv->onProcessFailed(wv, ui, wv->onProcessFailedUserdata);
    return S_OK;
}

static void EnsureProcessFailedHandlerInstalled(UIWebView* wv) {
    if (!wv || !wv->core || wv->processFailedHandlerRegistered) return;
    if (!wv->onProcessFailed) return;

    static ICoreWebView2ProcessFailedEventHandlerVtbl pf;
    pf.QueryInterface = (HRESULT (STDMETHODCALLTYPE*)(ICoreWebView2ProcessFailedEventHandler*, REFIID, void**))Handler_QueryInterface;
    pf.AddRef         = (ULONG (STDMETHODCALLTYPE*)(ICoreWebView2ProcessFailedEventHandler*))ProcessFailedHandler_AddRef;
    pf.Release        = (ULONG (STDMETHODCALLTYPE*)(ICoreWebView2ProcessFailedEventHandler*))ProcessFailedHandler_Release;
    pf.Invoke         = (HRESULT (STDMETHODCALLTYPE*)(ICoreWebView2ProcessFailedEventHandler*, ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*))ProcessFailedHandler_Invoke;

    ProcessFailedHandler* h = (ProcessFailedHandler*)calloc(1, sizeof(ProcessFailedHandler));
    if (!h) return;
    h->refCount = 1; h->lpVtbl = &pf; h->owner = wv;

    HRESULT hr = ICoreWebView2_add_ProcessFailed(wv->core,
        (ICoreWebView2ProcessFailedEventHandler*)h, &wv->processFailedToken);
    if (FAILED(hr)) {
        WVLOG("[webview] add_ProcessFailed failed hr=0x%08lX", (unsigned long)hr);
        ProcessFailedHandler_Release(h);
        return;
    }
    wv->processFailedHandler = h;
    wv->processFailedHandlerRegistered = 1;
}

static void RemoveProcessFailedHandler(UIWebView* wv) {
    if (!wv || !wv->core || !wv->processFailedHandlerRegistered) return;
    ICoreWebView2_remove_ProcessFailed(wv->core, wv->processFailedToken);
    if (wv->processFailedHandler) {
        ProcessFailedHandler_Release((ProcessFailedHandler*)wv->processFailedHandler);
        wv->processFailedHandler = NULL;
    }
    wv->processFailedHandlerRegistered = 0;
}

// ---------------------------------------------------------------------
// Initialisation: create the environment, which then chains to the
// controller (see EnvHandler_Invoke above).
// ---------------------------------------------------------------------

// Creates the host HWND once and parents it to the SDL window.
static int EnsureHostHwnd(UIWebView* wv) {
    if (!wv || !wv->parentHwnd) {
        WVLOG("[webview] EnsureHostHwnd: wv=%p parentHwnd=%p (cannot proceed)",
              (void*)wv, (void*)(wv ? wv->parentHwnd : NULL));
        return 0;
    }
    if (wv->hostHwnd) return 1;
    RegisterHostClassOnce();
    wv->hostHwnd = CreateWindowExW(
        0, kHostClass, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, 1, 1,
        wv->parentHwnd,
        NULL,
        GetModuleHandleW(NULL),
        NULL);
    if (!wv->hostHwnd) {
        WVLOG("[webview] CreateWindowExW for host HWND failed, GetLastError=%lu",
              GetLastError());
    } else {
        WVLOG("[webview] host HWND created %p, parent=%p",
              (void*)wv->hostHwnd, (void*)wv->parentHwnd);
    }
    return wv->hostHwnd != NULL;
}

static HRESULT BuildAndStart(UIWebView* wv) {
    WVLOG("[webview] BuildAndStart called (wv=%p, parentHwnd=%p, state=%d, compMode=%d, isolated=%d)",
          (void*)wv, (void*)(wv ? wv->parentHwnd : NULL),
          wv ? (int)wv->state : -1, wv ? wv->useCompositionMode : 0,
          wv ? wv->isolated : 0);
    if (!wv || !wv->parentHwnd) return E_INVALIDARG;
    if (wv->state != UI_WV_IDLE) return S_OK; // already in flight
    if (!wv->useCompositionMode && !EnsureHostHwnd(wv)) return E_FAIL;

    // ----- Shared environment path -----------------------------------
    // Non-isolated webviews reuse a process-wide environment singleton.
    // The first webview to start kicks off creation; later webviews
    // either piggyback on the existing env immediately or queue up
    // until the in-flight creation completes.
    if (!wv->isolated) {
        if (g_sharedEnvState == UI_SHARED_ENV_READY && g_sharedEnv) {
            WVLOG("[webview] reusing shared env (%p)", (void*)g_sharedEnv);
            wv->env = g_sharedEnv;
            ICoreWebView2Environment_AddRef(g_sharedEnv);
            wv->state = UI_WV_CREATING_CTRL;
            KickOffControllerCreation(wv, g_sharedEnv);
            return S_OK;
        }
        if (g_sharedEnvState == UI_SHARED_ENV_CREATING) {
            WVLOG("[webview] shared env in flight; queueing waiter");
            wv->state = UI_WV_CREATING_ENV;
            shared_env_enqueue(wv);
            return S_OK;
        }
        if (g_sharedEnvState == UI_SHARED_ENV_FAILED) {
            wv->state = UI_WV_FAILED;
            return E_FAIL;
        }
        // First non-isolated webview: mark creating and fall through to
        // the env-creation flow. EnvHandler_Invoke publishes the result
        // into g_sharedEnv and drains the waiter queue.
        g_sharedEnvState = UI_SHARED_ENV_CREATING;
    }

    EnvHandler* eh = (EnvHandler*)calloc(1, sizeof(EnvHandler));
    if (!eh) return E_OUTOFMEMORY;
    static ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl envVtbl;
    envVtbl.QueryInterface = (HRESULT (STDMETHODCALLTYPE*)(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*, REFIID, void**))Handler_QueryInterface;
    envVtbl.AddRef         = (ULONG (STDMETHODCALLTYPE*)(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*))EnvHandler_AddRef;
    envVtbl.Release        = (ULONG (STDMETHODCALLTYPE*)(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*))EnvHandler_Release;
    envVtbl.Invoke         = (HRESULT (STDMETHODCALLTYPE*)(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*, HRESULT, ICoreWebView2Environment*))EnvHandler_Invoke;
    eh->refCount = 1;
    eh->owner    = wv;
    eh->lpVtbl   = &envVtbl;

    wv->state = UI_WV_CREATING_ENV;

    ICoreWebView2EnvironmentOptions* options = NULL;
    if (wv->browserArgs && *wv->browserArgs) {
        options = (ICoreWebView2EnvironmentOptions*)UIWebViewOptions_Create(wv->browserArgs);
        WVLOG("[webview] env options created with args: %s", wv->browserArgs);
    }

    WVLOG("[webview] calling CreateCoreWebView2EnvironmentWithOptions ...");
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        NULL, NULL, options,
        (ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*)eh);
    WVLOG("[webview] CreateCoreWebView2EnvironmentWithOptions returned 0x%08lX",
          (unsigned long)hr);

    if (options) UIWebViewOptions_Release(options);
    if (FAILED(hr)) {
        WVLOG("[webview] env creation refused synchronously.");
        wv->state = UI_WV_FAILED;
        if (!wv->isolated) {
            g_sharedEnvState = UI_SHARED_ENV_FAILED;
            shared_env_drain_waiters(); // they all fail too
        }
        EnvHandler_Release(eh);
    }
    return hr;
}

static void ApplyPendingNavigation(UIWebView* wv) {
    if (!wv || !wv->core) return;
    const char* target = wv->pendingNav ? wv->pendingNav : wv->url;
    if (!target || !*target) {
        WVLOG("[webview] ApplyPendingNavigation: nothing to navigate to");
        return;
    }
    LPWSTR wide = Utf8ToWide(target);
    if (wide) {
        WVLOG("[webview] navigating to %s", target);
        ICoreWebView2_Navigate(wv->core, wide);
        free(wide);
    }
    free(wv->pendingNav);
    wv->pendingNav = NULL;
}

// ---------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------

// Default browser arguments applied to every new UIWebView. Safe set:
// won't break modern web pages (no JS heap cap, no memory-pressure-off,
// no single-process); just disables features that an embedded app
// rarely needs (extensions, default apps, telemetry, etc).
static const char* kDefaultBrowserArgs =
    "--renderer-process-limit=1 "
    "--process-per-site "
    "--disable-background-timer-throttling "
    "--disable-backgrounding-occluded-windows "
    "--disable-extensions "
    "--disable-component-extensions-with-background-pages "
    "--disable-default-apps "
    "--disable-domain-reliability "
    "--no-pings "
    "--no-default-browser-check "
    "--disable-component-update "
    "--disable-breakpad "
    "--disable-features=AutofillServerCommunication,"
    "OptimizationHints,MediaRouter,DialMediaRouteProvider,"
    "InterestFeedContentSuggestions,Translate,"
    "GlobalMediaControls,LiveCaption,HardwareMediaKeyHandling";

const char* UIWebView_GetDefaultBrowserArguments(void) {
    return kDefaultBrowserArgs;
}

UIWebView* UIWebView_Create(const char* initialUrl) {
    UIWebView* wv = (UIWebView*)calloc(1, sizeof(UIWebView));
    if (!wv) return NULL;
    wv->__widget_type = UI_WIDGET_WEBVIEW;
    if (initialUrl && *initialUrl) wv->url = _strdup(initialUrl);
    wv->state = UI_WV_IDLE;

    // Pending-settings sentinels mean "not configured yet" so we can
    // distinguish between "user did not call SetX" and "user set X = 0".
    wv->pendingDevTools     = -1;
    wv->pendingContextMenus = -1;

    // Apply the safe default browser flags. SetBrowserArguments()
    // replaces them; AppendBrowserArguments() extends them.
    wv->browserArgs = _strdup(kDefaultBrowserArgs);

    WVLOG("[webview] UIWebView_Create url=%s wv=%p (with default args)",
          initialUrl ? initialUrl : "(null)", (void*)wv);
    return wv;
}

UIWebView* UIWebView_Navigate(UIWebView* wv, const char* url) {
    if (!wv) return wv;
    WVLOG("[webview] UIWebView_Navigate called url=%s state=%d core=%p",
          url ? url : "(null)", wv->state, (void*)wv->core);
    free(wv->url);
    wv->url = (url && *url) ? _strdup(url) : NULL;
    if (wv->state == UI_WV_READY && wv->core) {
        LPWSTR wide = Utf8ToWide(url);
        if (wide) {
            HRESULT hr = ICoreWebView2_Navigate(wv->core, wide);
            WVLOG("[webview] ICoreWebView2_Navigate hr=0x%08lX",
                  (unsigned long)hr);
            free(wide);
        } else {
            WVLOG("[webview] Utf8ToWide returned NULL for url=%s",
                  url ? url : "(null)");
        }
    } else {
        WVLOG("[webview] queueing navigation for when controller is ready");
        free(wv->pendingNav);
        wv->pendingNav = (url && *url) ? _strdup(url) : NULL;
    }
    return wv;
}

const char* UIWebView_GetUrl(UIWebView* wv) {
    return wv ? wv->url : NULL;
}

UIWebView* UIWebView_Reload(UIWebView* wv) {
    if (wv && wv->core) ICoreWebView2_Reload(wv->core);
    return wv;
}

UIWebView* UIWebView_GoBack(UIWebView* wv) {
    if (wv && wv->core) ICoreWebView2_GoBack(wv->core);
    return wv;
}

UIWebView* UIWebView_GoForward(UIWebView* wv) {
    if (wv && wv->core) ICoreWebView2_GoForward(wv->core);
    return wv;
}

UIWebView* UIWebView_OnReady(UIWebView* wv, UIWebViewReadyCallback cb, void* userdata) {
    if (!wv) return wv;
    wv->onReady  = cb;
    wv->userdata = userdata;
    if (wv->state == UI_WV_READY && cb) cb(wv, userdata);
    return wv;
}

UIWebView* UIWebView_SetCompositionMode(UIWebView* wv, int enabled) {
    if (!wv) return wv;
    if (wv->state != UI_WV_IDLE) {
        WVLOG("[webview] SetCompositionMode called after init - ignored. "
              "Call before the first render.");
        return wv;
    }
    wv->useCompositionMode = enabled ? 1 : 0;
    return wv;
}

UIWebView* UIWebView_SetBrowserArguments(UIWebView* wv, const char* args) {
    if (!wv) return wv;
    if (wv->state != UI_WV_IDLE) {
        WVLOG("[webview] SetBrowserArguments called after env init - ignored.");
        return wv;
    }
    free(wv->browserArgs);
    wv->browserArgs = (args && *args) ? _strdup(args) : NULL;
    return wv;
}

UIWebView* UIWebView_AppendBrowserArguments(UIWebView* wv, const char* extra) {
    if (!wv || !extra || !*extra) return wv;
    if (wv->state != UI_WV_IDLE) {
        WVLOG("[webview] AppendBrowserArguments called after env init - ignored.");
        return wv;
    }
    const size_t curLen = wv->browserArgs ? strlen(wv->browserArgs) : 0;
    const size_t addLen = strlen(extra);
    char* combined = (char*)malloc(curLen + 1 + addLen + 1);
    if (!combined) return wv;
    if (curLen > 0) {
        memcpy(combined, wv->browserArgs, curLen);
        combined[curLen] = ' ';
        memcpy(combined + curLen + 1, extra, addLen + 1);
    } else {
        memcpy(combined, extra, addLen + 1);
    }
    free(wv->browserArgs);
    wv->browserArgs = combined;
    return wv;
}

UIWebView* UIWebView_SetIsolated(UIWebView* wv, int enabled) {
    if (!wv) return wv;
    if (wv->state != UI_WV_IDLE) {
        WVLOG("[webview] SetIsolated called after init - ignored.");
        return wv;
    }
    wv->isolated = enabled ? 1 : 0;
    return wv;
}

UIWebView* UIWebView_OnProcessFailed(UIWebView* wv,
                                     UIWebViewProcessFailedCallback cb,
                                     void* userdata) {
    if (!wv) return wv;
    wv->onProcessFailed = cb;
    wv->onProcessFailedUserdata = userdata;
    if (wv->state == UI_WV_READY && wv->core) {
        if (cb) EnsureProcessFailedHandlerInstalled(wv);
        else    RemoveProcessFailedHandler(wv);
    }
    return wv;
}

void UIWebView_Destroy(UIWebView* wv) {
    if (!wv) return;

    // Unhook the request interceptor before tearing down core so the
    // ResReqHandler's owner pointer doesn't outlive us.
    if (wv->requestHandlerRegistered && wv->core) {
        ICoreWebView2_remove_WebResourceRequested(wv->core, wv->requestToken);
        wv->requestHandlerRegistered = 0;
    }
    if (wv->requestHandler) {
        ResReqHandler* h = (ResReqHandler*)wv->requestHandler;
        h->owner = NULL; // defensive; in-flight invocations will no-op
        ResReqHandler_Release(h);
        wv->requestHandler = NULL;
    }

    // Nav handlers (SourceChanged / NavigationStarting/Completed).
    RemoveNavHandlers(wv);
    RemoveCursorHandler(wv);
    RemoveProcessFailedHandler(wv);

    // Tear DComp down BEFORE the controller so put_RootVisualTarget(NULL)
    // happens while the controller is still valid.
    TeardownDComp(wv);
    if (wv->compController) {
        ICoreWebView2CompositionController_Release(wv->compController);
        wv->compController = NULL;
    }

    if (wv->controller) {
        ICoreWebView2Controller_Close(wv->controller);
        ICoreWebView2Controller_Release(wv->controller);
    }
    if (wv->core) ICoreWebView2_Release(wv->core);
    if (wv->env)  ICoreWebView2Environment_Release(wv->env);
    if (wv->hostHwnd) DestroyWindow(wv->hostHwnd);

    free(wv->url);
    free(wv->pendingNav);
    free(wv->pendingUA);
    free(wv->browserArgs);

    for (int i = 0; i < wv->initScriptCount; i++) free(wv->initScripts[i]);
    free(wv->initScripts);

    for (int i = 0; i < wv->pendingExecCount; i++) free(wv->pendingExec[i]);
    free(wv->pendingExec);

    free(wv);
}

// ---------------------------------------------------------------------
// Visual customisation
// ---------------------------------------------------------------------

UIWebView* UIWebView_SetRadius(UIWebView* wv, float radius) {
    if (!wv) return wv;
    if (radius < 0.0f) radius = 0.0f;
    if (wv->cornerRadius != radius) {
        wv->cornerRadius = radius;
        // Force the next renderer tick to re-apply bounds + region.
        wv->lastBounds = (RECT){ 0, 0, 0, 0 };
    }
    return wv;
}

UIWebView* UIWebView_SetBorder(UIWebView* wv, UIColor color, float width) {
    if (!wv) return wv;
    if (width < 0.0f) width = 0.0f;
    wv->borderColor = color;
    wv->borderWidth = width;
    wv->hasBorder   = (width > 0.0f) ? 1 : 0;
    wv->lastBounds  = (RECT){ 0, 0, 0, 0 }; // force reapply
    return wv;
}

// ---------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------

UIWebView* UIWebView_SetUserAgent(UIWebView* wv, const char* ua) {
    if (!wv) return wv;
    free(wv->pendingUA);
    wv->pendingUA = (ua && *ua) ? _strdup(ua) : NULL;
    if (wv->state == UI_WV_READY && wv->core) ApplyPendingSettings(wv);
    return wv;
}

UIWebView* UIWebView_SetDevToolsEnabled(UIWebView* wv, int enabled) {
    if (!wv) return wv;
    wv->pendingDevTools = enabled ? 1 : 0;
    if (wv->state == UI_WV_READY && wv->core) ApplyPendingSettings(wv);
    return wv;
}

UIWebView* UIWebView_SetContextMenusEnabled(UIWebView* wv, int enabled) {
    if (!wv) return wv;
    wv->pendingContextMenus = enabled ? 1 : 0;
    if (wv->state == UI_WV_READY && wv->core) ApplyPendingSettings(wv);
    return wv;
}

// ---------------------------------------------------------------------
// Scripts
// ---------------------------------------------------------------------

static int append_str(char*** arr, int* count, int* cap, const char* s) {
    if (*count >= *cap) {
        int newCap = (*cap == 0) ? 4 : (*cap * 2);
        char** grown = (char**)realloc(*arr, sizeof(char*) * (size_t)newCap);
        if (!grown) return 0;
        *arr = grown;
        *cap = newCap;
    }
    (*arr)[*count] = _strdup(s);
    if (!(*arr)[*count]) return 0;
    (*count)++;
    return 1;
}

UIWebView* UIWebView_AddInitScript(UIWebView* wv, const char* js) {
    if (!wv || !js) return wv;
    if (!append_str(&wv->initScripts, &wv->initScriptCount, &wv->initScriptCap, js)) {
        return wv;
    }
    // If we already passed READY once, this new script needs to be
    // sent to the controller now. Clear the "already applied" flag so
    // ApplyPendingScripts re-walks - it's harmless because the prior
    // scripts that were already registered persist in WebView2.
    if (wv->state == UI_WV_READY && wv->core) {
        // Apply only this new one directly to avoid re-sending old ones.
        static ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandlerVtbl ascVtbl;
        ascVtbl.QueryInterface = (HRESULT (STDMETHODCALLTYPE*)(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler*, REFIID, void**))Handler_QueryInterface;
        ascVtbl.AddRef         = (ULONG (STDMETHODCALLTYPE*)(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler*))AddScriptHandler_AddRef;
        ascVtbl.Release        = (ULONG (STDMETHODCALLTYPE*)(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler*))AddScriptHandler_Release;
        ascVtbl.Invoke         = (HRESULT (STDMETHODCALLTYPE*)(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler*, HRESULT, LPCWSTR))AddScriptHandler_Invoke;

        LPWSTR wide = Utf8ToWide(js);
        if (wide) {
            AddScriptHandler* h = (AddScriptHandler*)calloc(1, sizeof(AddScriptHandler));
            if (h) {
                h->refCount = 1;
                h->lpVtbl   = &ascVtbl;
                ICoreWebView2_AddScriptToExecuteOnDocumentCreated(wv->core, wide,
                    (ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler*)h);
            }
            free(wide);
        }
    }
    return wv;
}

UIWebView* UIWebView_ExecuteScript(UIWebView* wv, const char* js) {
    if (!wv || !js) return wv;
    if (wv->state == UI_WV_READY && wv->core) {
        LPWSTR wide = Utf8ToWide(js);
        if (wide) {
            ICoreWebView2_ExecuteScript(wv->core, wide, NULL);
            free(wide);
        }
    } else {
        append_str(&wv->pendingExec, &wv->pendingExecCount, &wv->pendingExecCap, js);
    }
    return wv;
}

// ---------------------------------------------------------------------
// Cookies
// ---------------------------------------------------------------------

UIWebView* UIWebView_ClearCookies(UIWebView* wv) {
    if (!wv) return wv;
    wv->pendingClearCookies = 1;
    if (wv->state == UI_WV_READY && wv->core) ApplyPendingSettings(wv);
    return wv;
}

// ---------------------------------------------------------------------
// Request interception
// ---------------------------------------------------------------------

UIWebView* UIWebView_OnRequest(UIWebView* wv, UIWebViewRequestCallback cb, void* userdata) {
    if (!wv) return wv;
    wv->onRequest = cb;
    wv->onRequestUserdata = userdata;

    if (!cb && wv->requestHandlerRegistered && wv->core) {
        ICoreWebView2_remove_WebResourceRequested(wv->core, wv->requestToken);
        wv->requestHandlerRegistered = 0;
        if (wv->requestHandler) {
            ResReqHandler_Release((ResReqHandler*)wv->requestHandler);
            wv->requestHandler = NULL;
        }
        return wv;
    }
    if (cb && wv->state == UI_WV_READY) EnsureRequestHandlerInstalled(wv);
    return wv;
}

UIWebView* UIWebView_OnUrlChange(UIWebView* wv, UIWebViewUrlChangeCallback cb, void* userdata) {
    if (!wv) return wv;
    wv->onUrlChange = cb;
    wv->onUrlChangeUserdata = userdata;
    if (wv->state == UI_WV_READY) {
        RemoveNavHandlers(wv);
        EnsureNavHandlersInstalled(wv);
    }
    return wv;
}

UIWebView* UIWebView_OnLoadingChange(UIWebView* wv, UIWebViewLoadingCallback cb, void* userdata) {
    if (!wv) return wv;
    wv->onLoading = cb;
    wv->onLoadingUserdata = userdata;
    if (wv->state == UI_WV_READY) {
        RemoveNavHandlers(wv);
        EnsureNavHandlersInstalled(wv);
    }
    return wv;
}

// ---------------------------------------------------------------------
// D2D-rendered overlays. Forwarded to the DComp wrapper (which lives
// in webview_dcomp.cpp because dcomp.h + WRL want C++).
// ---------------------------------------------------------------------

static UIWVDCompColor uic_to_dcomp(UIColor c) {
    UIWVDCompColor o; o.r = c.r; o.g = c.g; o.b = c.b; o.a = c.a;
    return o;
}

int UIWebView_AddD2DOverlay(UIWebView* wv, int x, int y, int w, int h,
                            float radius, UIColor fill) {
    if (!wv || !wv->dcomp) {
        WVLOG("[webview] AddD2DOverlay: composition mode not active - no-op");
        return -1;
    }
    return UIWebViewDComp_AddOverlay(wv->dcomp, x, y, w, h, radius, uic_to_dcomp(fill));
}

void UIWebView_SetD2DOverlayText(UIWebView* wv, int handle,
                                 const char* utf8Text, const char* family,
                                 float fontSize, UIColor color, float pad) {
    if (!wv || !wv->dcomp) return;
    UIWebViewDComp_SetOverlayText(wv->dcomp, handle, utf8Text, family,
                                  fontSize, uic_to_dcomp(color), pad);
}

void UIWebView_MoveD2DOverlay(UIWebView* wv, int handle,
                              int x, int y, int w, int h) {
    if (!wv || !wv->dcomp) return;
    UIWebViewDComp_MoveOverlay(wv->dcomp, handle, x, y, w, h);
}

void UIWebView_RemoveD2DOverlay(UIWebView* wv, int handle) {
    if (!wv || !wv->dcomp) return;
    UIWebViewDComp_RemoveOverlay(wv->dcomp, handle);
}

// ---------------------------------------------------------------------
// Internal accessors for window.c (visual metrics for rendering)
// ---------------------------------------------------------------------

void UIWebView_GetVisuals(const UIWebView* wv, float* outRadius,
                          float* outBorderWidth, UIColor* outBorderColor,
                          int* outHasBorder);

void UIWebView_GetVisuals(const UIWebView* wv, float* outRadius,
                          float* outBorderWidth, UIColor* outBorderColor,
                          int* outHasBorder) {
    if (!wv) {
        if (outRadius)      *outRadius      = 0;
        if (outBorderWidth) *outBorderWidth = 0;
        if (outHasBorder)   *outHasBorder   = 0;
        if (outBorderColor) { outBorderColor->r = 0; outBorderColor->g = 0; outBorderColor->b = 0; outBorderColor->a = 0; }
        return;
    }
    if (outRadius)      *outRadius      = wv->cornerRadius;
    if (outBorderWidth) *outBorderWidth = wv->borderWidth;
    if (outBorderColor) *outBorderColor = wv->borderColor;
    if (outHasBorder)   *outHasBorder   = wv->hasBorder;
}

// ---------------------------------------------------------------------
// Internal entrypoints used by the renderer. Declared here (not in the
// public header) so window.c can drive layout + lazy init.
// ---------------------------------------------------------------------

// `clipRects` are rectangles (in the same coordinate space as x, y)
// that should be PUNCHED OUT of the webview so widgets behind them
// (well, in front in z-order) remain visible. The optional `clipRadii`
// parallel array carries each rect's corner radius - when > 0 the
// punch is performed via CreateRoundRectRgn so the punched shape
// matches the rounded widget exactly. Pass NULL/0 to disable.
void UIWebView_RendererTick(UIWebView* wv, HWND parentHwnd,
                            int x, int y, int w, int h,
                            const RECT* clipRects, const int* clipRadii,
                            int clipCount);

void UIWebView_RendererTick(UIWebView* wv, HWND parentHwnd,
                            int x, int y, int w, int h,
                            const RECT* clipRects, const int* clipRadii,
                            int clipCount) {
    if (!wv) return;
    if (wv->state == UI_WV_IDLE && parentHwnd) {
        WVLOG("[webview] first RendererTick: parentHwnd=%p, bounds=(%d,%d,%dx%d)",
              (void*)parentHwnd, x, y, w, h);
        wv->parentHwnd = parentHwnd;
        BuildAndStart(wv);
        return;
    }

    // ---- Composition path ----------------------------------------
    if (wv->useCompositionMode) {
        (void)clipRects; (void)clipRadii; (void)clipCount; // future work
        if (!wv->dcomp) return;

        const int bw = wv->hasBorder ? (int)wv->borderWidth : 0;
        const int innerW = (w - 2 * bw) > 0 ? (w - 2 * bw) : 0;
        const int innerH = (h - 2 * bw) > 0 ? (h - 2 * bw) : 0;

        // Mask color: use the configured border colour so the rounded
        // corner mask blends with the SDL-painted border ring. When no
        // border is set, fall back to black with alpha 0 (no-op mask).
        UIWVDCompColor maskCol = { 0, 0, 0, 0.0f };
        if (wv->hasBorder) {
            maskCol.r = wv->borderColor.r;
            maskCol.g = wv->borderColor.g;
            maskCol.b = wv->borderColor.b;
            maskCol.a = wv->borderColor.a;
        }

        RECT r = { x, y, x + w, y + h };
        if (memcmp(&r, &wv->lastBounds, sizeof(RECT)) != 0) {
            UIWebViewDComp_SetBounds(wv->dcomp, x, y, w, h, bw,
                                     wv->cornerRadius, maskCol);
            if (wv->state == UI_WV_READY && wv->controller) {
                RECT inner = { 0, 0, innerW, innerH };
                ICoreWebView2Controller_put_Bounds(wv->controller, inner);
            }
            wv->lastBounds = r;
        }
        return;
    }

    if (!wv->hostHwnd) return;

    // Inset by the configured border width. The border itself is
    // painted onto the SDL surface by window.c BEFORE this tick runs,
    // so insetting the host HWND leaves the border ring visible.
    const int bw = wv->hasBorder ? (int)wv->borderWidth : 0;
    const int hostX = x + bw;
    const int hostY = y + bw;
    const int hostW = (w - 2 * bw) > 0 ? (w - 2 * bw) : 0;
    const int hostH = (h - 2 * bw) > 0 ? (h - 2 * bw) : 0;

    RECT r = { hostX, hostY, hostX + hostW, hostY + hostH };
    if (memcmp(&r, &wv->lastBounds, sizeof(RECT)) != 0) {
        SetWindowPos(wv->hostHwnd, NULL, hostX, hostY, hostW, hostH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (wv->state == UI_WV_READY && wv->controller) {
            // put_Bounds is relative to the controller's parent HWND.
            // Since the parent is our host, we want a (0, 0, w, h) rect.
            RECT inner = { 0, 0, hostW, hostH };
            ICoreWebView2Controller_put_Bounds(wv->controller, inner);
        }
        wv->lastBounds = r;
    }

    // Build a window region that excludes higher-z overlapping widgets.
    // Coordinates passed in clipRects are in widget/window space; we
    // translate to host-local (subtract host origin, not widget x,y -
    // border insets shift the host origin).
    HRGN rgn = NULL;
    if (wv->cornerRadius > 0.0f && hostW > 0 && hostH > 0) {
        // Subtract border width from the radius so the rounded edge of
        // the webview content sits just inside the border frame.
        int r_in = (int)wv->cornerRadius - bw;
        if (r_in < 0) r_in = 0;
        int dx = r_in * 2;
        int dy = r_in * 2;
        if (dx > hostW) dx = hostW;
        if (dy > hostH) dy = hostH;
        rgn = (dx > 0 && dy > 0)
            ? CreateRoundRectRgn(0, 0, hostW, hostH, dx, dy)
            : CreateRectRgn(0, 0, hostW, hostH);
    } else {
        rgn = CreateRectRgn(0, 0, hostW, hostH);
    }

    if (rgn && clipRects && clipCount > 0) {
        for (int i = 0; i < clipCount; i++) {
            RECT c = clipRects[i];
            c.left   -= hostX; c.right  -= hostX;
            c.top    -= hostY; c.bottom -= hostY;
            if (c.right  < 0 || c.bottom < 0) continue;
            if (c.left   > hostW || c.top    > hostH) continue;
            if (c.left   < 0) c.left   = 0;
            if (c.top    < 0) c.top    = 0;
            if (c.right  > hostW) c.right  = hostW;
            if (c.bottom > hostH) c.bottom = hostH;
            int radius = (clipRadii && clipRadii[i] > 0) ? clipRadii[i] : 0;
            HRGN hole = NULL;
            // Inflate the punched region. The widget's SDL paint uses
            // sub-pixel analytic-coverage AA that extends ~1px beyond
            // the integer region edge; the WebView2 child HWND is
            // pixel-quantised via SetWindowRgn and cannot match
            // sub-pixel paint. Inflating by a few pixels absorbs the
            // mismatch - the SDL surface (which the caller controls
            // via UIApp_SetBackgroundColor) shows in the ring around
            // the painted card. Match app bg to card colour to make
            // the ring invisible.
            const int infl = 4;
            int hleft   = c.left   - infl;
            int htop    = c.top    - infl;
            int hright  = c.right  + infl;
            int hbottom = c.bottom + infl;
            if (radius > 0) {
                // CreateRoundRectRgn wants the ellipse diameter, not the
                // radius. Cap so the diameter never exceeds the rect.
                int dx = (radius + infl) * 2;
                int dy = (radius + infl) * 2;
                int rectW = hright - hleft;
                int rectH = hbottom - htop;
                if (dx > rectW) dx = rectW;
                if (dy > rectH) dy = rectH;
                hole = CreateRoundRectRgn(hleft, htop, hright, hbottom, dx, dy);
            } else {
                hole = CreateRectRgn(hleft, htop, hright, hbottom);
            }
            if (hole) {
                CombineRgn(rgn, rgn, hole, RGN_DIFF);
                DeleteObject(hole);
            }
        }
    }
    // SetWindowRgn takes ownership of the rgn handle on success.
    if (rgn) SetWindowRgn(wv->hostHwnd, rgn, FALSE);
}

// ---------------------------------------------------------------------
// Composition-mode mouse input forwarding.
//
// In HWND mode the WebView2 child window receives input natively. In
// composition mode the visual has no message loop, so the host must
// forward every mouse event via ICoreWebView2CompositionController::
// SendMouseInput. Without this, the page is visible but completely
// unresponsive (clicks, scroll, hover all dead).
//
// app.c calls these from its SDL event handlers.
// ---------------------------------------------------------------------

// Walks children for the first webview in composition mode whose bounds
// contain (x, y). Returns NULL if none.
static UIWebView* find_comp_webview_at(UIChildren* children, float x, float y) {
    if (!children) return NULL;
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible || !w->data || !w->width || !w->height) continue;
        UIWidgetBase* b = (UIWidgetBase*)w->data;
        if (strcmp(b->__widget_type, UI_WIDGET_WEBVIEW) != 0) continue;
        UIWebView* wv = (UIWebView*)b;
        if (!wv->useCompositionMode || !wv->compController) continue;
        if (wv->state != UI_WV_READY) continue;
        const float ww = *w->width, hh = *w->height;
        if (x < w->x || x >= w->x + ww || y < w->y || y >= w->y + hh) continue;
        return wv;
    }
    /* Also accept events outside any widget's drawn bounds if there's
       just a single comp webview; this prevents losing motion when the
       cursor hovers a fractional-pixel boundary. */
    (void)x; (void)y;
    return NULL;
}

static POINT widget_to_visual_local(UIChildren* children, UIWebView* wv,
                                    float x, float y) {
    POINT pt = { 0, 0 };
    if (!children || !wv) return pt;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || w->data != wv) continue;
        const int bw = wv->hasBorder ? (int)wv->borderWidth : 0;
        pt.x = (LONG)(x - w->x - bw);
        pt.y = (LONG)(y - w->y - bw);
        return pt;
    }
    return pt;
}

void UIWebView_DispatchMouseMotion(UIChildren* children, float x, float y) {
    UIWebView* wv = find_comp_webview_at(children, x, y);
    if (!wv) return;
    POINT pt = widget_to_visual_local(children, wv, x, y);
    ICoreWebView2CompositionController_SendMouseInput(wv->compController,
        COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE,
        COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE,
        0, pt);
}

// Returns the UICursor value the WebView2 reported in the most recent
// CursorChanged event for the webview under (x, y). app.c's hover-cursor
// picker uses this so the page's link/text/grab cursors take effect.
int UIWebView_HoverCursorAt(UIChildren* children, float x, float y) {
    UIWebView* wv = find_comp_webview_at(children, x, y);
    if (!wv) return 0; /* UI_CURSOR_DEFAULT */
    return wv->currentCursor;
}

static COREWEBVIEW2_MOUSE_EVENT_KIND sdl_button_to_kind(int sdlButton, int down) {
    /* SDL: 1=left, 2=middle, 3=right, 4=x1, 5=x2 */
    switch (sdlButton) {
        case 1: return down ? COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOWN
                            : COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP;
        case 2: return down ? COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_DOWN
                            : COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_UP;
        case 3: return down ? COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_DOWN
                            : COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_UP;
        case 4:
        case 5: return down ? COREWEBVIEW2_MOUSE_EVENT_KIND_X_BUTTON_DOWN
                            : COREWEBVIEW2_MOUSE_EVENT_KIND_X_BUTTON_UP;
        default: return COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE;
    }
}

void UIWebView_DispatchMouseDown(UIChildren* children, float x, float y, int sdlButton) {
    UIWebView* wv = find_comp_webview_at(children, x, y);
    if (!wv) return;
    POINT pt = widget_to_visual_local(children, wv, x, y);
    UINT32 data = (sdlButton == 4) ? 1 : (sdlButton == 5) ? 2 : 0;
    ICoreWebView2CompositionController_SendMouseInput(wv->compController,
        sdl_button_to_kind(sdlButton, 1),
        COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE,
        data, pt);
}

void UIWebView_DispatchMouseUp(UIChildren* children, float x, float y, int sdlButton) {
    UIWebView* wv = find_comp_webview_at(children, x, y);
    if (!wv) return;
    POINT pt = widget_to_visual_local(children, wv, x, y);
    UINT32 data = (sdlButton == 4) ? 1 : (sdlButton == 5) ? 2 : 0;
    ICoreWebView2CompositionController_SendMouseInput(wv->compController,
        sdl_button_to_kind(sdlButton, 0),
        COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE,
        data, pt);
}

void UIWebView_DispatchMouseWheel(UIChildren* children, float x, float y,
                                  float dx, float dy) {
    UIWebView* wv = find_comp_webview_at(children, x, y);
    if (!wv) return;
    POINT pt = widget_to_visual_local(children, wv, x, y);
    /* dy positive = wheel away from user. WHEEL_DELTA = 120. */
    if (dy != 0.0f) {
        INT32 delta = (INT32)(dy * 120.0f);
        ICoreWebView2CompositionController_SendMouseInput(wv->compController,
            COREWEBVIEW2_MOUSE_EVENT_KIND_WHEEL,
            COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE,
            (UINT32)delta, pt);
    }
    if (dx != 0.0f) {
        INT32 delta = (INT32)(dx * 120.0f);
        ICoreWebView2CompositionController_SendMouseInput(wv->compController,
            COREWEBVIEW2_MOUSE_EVENT_KIND_HORIZONTAL_WHEEL,
            COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE,
            (UINT32)delta, pt);
    }
}

#elif defined(__linux__) && defined(MOCIDA_HAS_WEBKITGTK)

// =====================================================================
// Linux backend — WebKitGTK 2 (GTK3, webkit2gtk-4.x).
//
// Architecture:
//
//   WebKitWebView (off-screen GtkOffscreenWindow)
//        |
//        +-- load_uri / reload / back/forward     (navigation)
//        +-- user_content_manager (init scripts)
//        +-- run_javascript                       (scripting)
//        +-- website_data_manager_clear (cookies)
//
//   Snapshot pipeline (per render):
//     gtk_widget_get_window + cairo_image_surface  (BGRA32)
//        => SDL_Texture (STREAMING)               // shown by window.c
//
//   GLib event loop:
//     pumped from UIWebView_RendererTick_Linux every frame via
//     g_main_context_iteration(NULL, FALSE) — non-blocking, drains
//     whatever signals fired since the last tick.
//
// What this MVP delivers:
//   * Create / Destroy / Navigate / Reload / GoBack / GoForward
//   * GetUrl
//   * AddInitScript / ExecuteScript (queue before ready, flush after)
//   * ClearCookies (queue before ready)
//   * Load-state + URL-change callbacks
//   * Rendered page shown as an SDL texture inside the widget bounds
//
// What is NOT yet implemented (returns no-op / stub):
//   * Mouse / keyboard / wheel input forwarding (the view is display-
//     only). Implementing this requires GdkEvent synthesis with
//     widget-local coords + handling focus traversal — planned but
//     not in the MVP. Use Windows for interactive workflows for now.
//   * UIWebView_AddD2DOverlay / SetD2DOverlayText / SetCompositionMode
//     (Win32-specific composition path; no Linux equivalent).
//   * UIWebView_SetIsolated / OnProcessFailed (multi-process model is
//     a WebView2 concept; WebKitGTK has its own).
//   * UIWebView_SetRadius / SetBorder (could be done via SDL-side
//     composition on top of the snapshot, but not wired up).
//   * UIWebView_OnRequest (request interception via WebKitGTK's
//     SubpathSubpath/WebFilter API — phase 2).
// =====================================================================

#include <webkit2/webkit2.h>
#include <gtk/gtk.h>
#include <SDL3/SDL.h>
#include <uikit/debug.h>

// A queued init script or pending JS to run once the view is ready.
typedef struct WVPendingScript {
    char* js;
    int   isInit;      // 1 = AddInitScript (forever), 0 = ExecuteScript (once)
    struct WVPendingScript* next;
} WVPendingScript;

struct UIWebView {
    const char* __widget_type;

    // ---- WebKit / GTK ----
    GtkWidget*       offscreenWindow;   // GtkOffscreenWindow holding the view
    WebKitWebView*   view;              // the actual webview widget
    WebKitUserContentManager* ucm;      // for init scripts
    int              ready;             // 1 once the first load-changed fires
    int              loading;           // 1 between load-started and load-finished

    // ---- Pending state (queued before view is ready) ----
    char*            pendingUrl;        // navigate target queued before init
    WVPendingScript* pendingScripts;    // singly-linked list, FIFO drained on ready
    int              pendingClearCookies;

    // ---- SDL render side ----
    SDL_Texture*     texture;           // BGRA32 streaming, recreated on size change
    int              texW, texH;        // current texture size
    Uint32           lastSnapshotTick;  // SDL_GetTicks at last upload, used for throttle

    // ---- Async snapshot pipeline ----
    cairo_surface_t* latestSnapshot;    // owned; replaced when a fresh one arrives
    int              snapshotPending;   // 1 between request and callback fire
    int              requestedW, requestedH; // last size we asked the view to render at

    // ---- Public callbacks ----
    UIWebViewReadyCallback        onReady;
    void*                         onReadyUserdata;
    UIWebViewUrlChangeCallback    onUrlChange;
    void*                         onUrlChangeUserdata;
    UIWebViewLoadingCallback      onLoading;
    void*                         onLoadingUserdata;
    UIWebViewRequestCallback      onRequest;
    void*                         onRequestUserdata;
    UIWebViewProcessFailedCallback onProcessFailed;
    void*                         onProcessFailedUserdata;

    // ---- Visuals (consumed by the SDL render path) ----
    float            cornerRadius;
    float            borderWidth;
    UIColor          borderColor;
    int              hasBorder;
    int              contextMenusEnabled;   // 1 = show default menu (default), 0 = suppress
};

// One-shot GTK init. Same shape as the GStreamer init in video.c — must
// run on the main thread before any WebKit/GTK call. We pass a real
// argv[0] because some GTK option groups dereference argv on init.
static int g_gtkInited = 0;

static void EnsureGtkInit(void) {
    if (g_gtkInited) return;

    // Force WebKit's renderer + cairo + Mesa onto the CPU path. The
    // default DMA-BUF / EGL accelerated path expects a real DRM device
    // + working EGL; under WSLg neither is available (the compositor
    // exposes a software Mesa "dzn" Vulkan ICD and no DRM node). The
    // GL path then fails with "libEGL warning: failed to get driver
    // name for fd -1" and either crashes or returns empty snapshots.
    //
    // Six escape hatches (documented in WebKitGTK NEWS files), each
    // covering a layer that could re-enable GL:
    //   WEBKIT_DISABLE_COMPOSITING_MODE  - kills the AC compositor
    //   WEBKIT_DISABLE_DMABUF_RENDERER   - skips the DMA-BUF GPU path
    //   WEBKIT_FORCE_SANDBOX             - 0 = single-process, lets
    //                                       us skip the GPU helper
    //   WEBKIT_FORCE_COMPOSITING_MODE    - 0 = honour DISABLE above
    //   LIBGL_ALWAYS_SOFTWARE            - forces Mesa swrast
    //   GDK_GL                           - disable=hard-no for GTK
    //
    // setenv with overwrite=0 means a user-provided value wins.
    setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 0);
    setenv("WEBKIT_DISABLE_DMABUF_RENDERER",  "1", 0);
    // 2.42+ renamed WEBKIT_FORCE_SANDBOX (which only enables) to the
    // explicit DANGEROUS-suffixed name. Set both for compatibility.
    setenv("WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS", "1", 0);
    setenv("WEBKIT_FORCE_COMPOSITING_MODE",   "0", 0);
    setenv("LIBGL_ALWAYS_SOFTWARE",           "1", 0);
    setenv("GDK_GL",                          "disable", 0);

    int    fake_argc   = 1;
    char   progName[]  = "mocida";
    char*  fake_argv[] = { progName, NULL };
    char** argv_p      = fake_argv;
    if (!gtk_init_check(&fake_argc, &argv_p)) {
        UI_ERROR(UI_CAT_RENDER, "GTK init failed — UIWebView will not render");
        return;
    }
    g_gtkInited = 1;
}

// Public early-init hook, mirrors UIVideo_PreInit. Mocida apps that want
// to be defensive call this at the top of main() before SDL_Init. Same
// rationale as gst_init: getting in before SDL avoids GLib state being
// touched by the wrong thread order under WSLg.
void UIWebView_PreInit_Linux(void) { EnsureGtkInit(); }

// Forward decl: drain pending scripts / nav / cookie ops once the view
// reports it can accept them. Called from the load-changed signal on
// first WEBKIT_LOAD_COMMITTED.
static void FlushPending(UIWebView* wv);

// load-changed signal: fires for every state transition (STARTED,
// REDIRECTED, COMMITTED, FINISHED). We use it to:
//   - mark `loading` for UIWebView_OnLoadingChange consumers
//   - mark `ready` and flush pending ops on the first COMMITTED
static void OnLoadChanged(WebKitWebView* view, WebKitLoadEvent ev, gpointer ud) {
    (void)view;
    UIWebView* wv = (UIWebView*)ud;
    if (!wv) return;
    if (ev == WEBKIT_LOAD_STARTED || ev == WEBKIT_LOAD_REDIRECTED) {
        if (!wv->loading) {
            wv->loading = 1;
            if (wv->onLoading) wv->onLoading(wv, 1, wv->onLoadingUserdata);
        }
    } else if (ev == WEBKIT_LOAD_FINISHED) {
        if (wv->loading) {
            wv->loading = 0;
            if (wv->onLoading) wv->onLoading(wv, 0, wv->onLoadingUserdata);
        }
        if (!wv->ready) {
            wv->ready = 1;
            FlushPending(wv);
            if (wv->onReady) wv->onReady(wv, wv->onReadyUserdata);
        }
    } else if (ev == WEBKIT_LOAD_COMMITTED && !wv->ready) {
        // Some sites never fire FINISHED for streaming pages; treat
        // COMMITTED as "ready enough" so a queued ExecuteScript can run.
        wv->ready = 1;
        FlushPending(wv);
        if (wv->onReady) wv->onReady(wv, wv->onReadyUserdata);
    }
}

// notify::uri signal: forward to UIWebViewUrlChangeCallback. Fires on
// every URL transition, including in-page hash changes and history
// nav. The URI is owned by WebKit; we hand it to the callback as a
// borrowed string, matching the Win32 contract.
static void OnUriChanged(GObject* obj, GParamSpec* spec, gpointer ud) {
    (void)spec;
    UIWebView* wv = (UIWebView*)ud;
    if (!wv) return;
    const gchar* uri = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(obj));
    if (uri && wv->onUrlChange) wv->onUrlChange(wv, uri, wv->onUrlChangeUserdata);
}

// decide-policy signal: the only UI-process-side mechanism in
// webkit2gtk-4.1 that can BLOCK a load by URL. Fires for top-level /
// subframe navigations (NAVIGATION_ACTION, NEW_WINDOW_ACTION) and for
// resource responses (RESPONSE). It does NOT fire for every
// fetch/XHR/img subresource the way the Win32 WebResourceRequested "*"
// filter does — so the Linux block surface is navigations + responses,
// a documented subset of the Win32 contract. resource-load-started /
// sent-request are observe-only (void return in the 4.1 GIR) and cannot
// cancel, so they are not usable here. We extract the request URL, hand
// it to the user callback, and ignore() the decision (aborting the
// load) if the callback returns 1. Returning TRUE marks the decision
// handled; returning FALSE lets WebKit apply its own default policy
// (important for RESPONSE decisions that may be downloads).
static gboolean OnDecidePolicy(WebKitWebView* view,
                               WebKitPolicyDecision* decision,
                               WebKitPolicyDecisionType type,
                               gpointer ud) {
    (void)view;
    UIWebView* wv = (UIWebView*)ud;
    if (!wv || !wv->onRequest) return FALSE;   // no handler: WebKit defaults

    const char* url = NULL;
    if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION ||
        type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
        WebKitNavigationPolicyDecision* nav =
            WEBKIT_NAVIGATION_POLICY_DECISION(decision);
        WebKitNavigationAction* act =
            webkit_navigation_policy_decision_get_navigation_action(nav);
        WebKitURIRequest* req = act ? webkit_navigation_action_get_request(act) : NULL;
        if (req) url = webkit_uri_request_get_uri(req);
    } else if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) {
        WebKitResponsePolicyDecision* resp =
            WEBKIT_RESPONSE_POLICY_DECISION(decision);
        WebKitURIRequest* req = webkit_response_policy_decision_get_request(resp);
        if (req) url = webkit_uri_request_get_uri(req);
    }

    if (url && wv->onRequest(wv, url, wv->onRequestUserdata) == 1) {
        webkit_policy_decision_ignore(decision);   // BLOCK: abort the load
        return TRUE;                                // handled
    }
    return FALSE;   // ALLOW: let WebKit apply its default policy
}

// web-process-terminated signal (Since 2.20): the renderer/web process
// died. WebKitWebProcessTerminationReason has exactly three values in
// 2.52 (CRASHED, EXCEEDED_MEMORY_LIMIT, TERMINATED_BY_API) — there is
// no "unresponsive" value, so UI_WEBVIEW_PROCESS_RENDERER_UNRESPONSIVE
// is never produced by this backend.
static void OnWebProcessTerminated(WebKitWebView* view,
                                   WebKitWebProcessTerminationReason reason,
                                   gpointer ud) {
    (void)view;
    UIWebView* wv = (UIWebView*)ud;
    if (!wv) return;
    // The snapshot pipeline will stop producing fresh frames; mark
    // not-loading and let the app react.
    if (wv->loading) {
        wv->loading = 0;
        if (wv->onLoading) wv->onLoading(wv, 0, wv->onLoadingUserdata);
    }
    if (!wv->onProcessFailed) return;
    UIWebViewProcessKind kind;
    switch (reason) {
        case WEBKIT_WEB_PROCESS_CRASHED:
        case WEBKIT_WEB_PROCESS_EXCEEDED_MEMORY_LIMIT:
            // Renderer/web process gone; page is blank, app may reload.
            kind = UI_WEBVIEW_PROCESS_RENDERER;
            break;
        case WEBKIT_WEB_PROCESS_TERMINATED_BY_API:
            // Deliberate teardown via API, not a crash.
            kind = UI_WEBVIEW_PROCESS_OTHER;
            break;
        default:
            kind = UI_WEBVIEW_PROCESS_OTHER;
            break;
    }
    wv->onProcessFailed(wv, kind, wv->onProcessFailedUserdata);
}

// context-menu signal: fires on right-click before WebKit builds its
// default menu. Returning TRUE suppresses the default menu; FALSE lets
// it show. We read the live flag so the public setter takes effect
// whether called before or after the view is ready. (Note: under the
// current offscreen/snapshot render model right-clicks are not yet
// forwarded to the view, so this is correct but not observable until
// input forwarding lands.)
static gboolean OnContextMenu(WebKitWebView* view,
                              WebKitContextMenu* menu,
                              GdkEvent* event,
                              WebKitHitTestResult* hit,
                              gpointer ud) {
    (void)view; (void)menu; (void)event; (void)hit;
    UIWebView* wv = (UIWebView*)ud;
    if (!wv) return FALSE;
    return wv->contextMenusEnabled ? FALSE : TRUE;  // TRUE = suppress
}

static void RunJsCallback(GObject* src, GAsyncResult* res, gpointer ud) {
    (void)ud;
    WebKitWebView* view = WEBKIT_WEB_VIEW(src);
    GError* err = NULL;
    // WebKitGTK 2.40+ deprecated webkit_web_view_run_javascript in
    // favour of evaluate_javascript, which returns a JSCValue instead
    // of a WebKitJavascriptResult. We don't read the result so just
    // drop whichever shape this build of WebKit produces.
#if WEBKIT_CHECK_VERSION(2, 40, 0)
    JSCValue* r = webkit_web_view_evaluate_javascript_finish(view, res, &err);
    if (r) g_object_unref(r);
#else
    WebKitJavascriptResult* r =
        webkit_web_view_run_javascript_finish(view, res, &err);
    if (r) webkit_javascript_result_unref(r);
#endif
    if (err) {
        UI_WARN(UI_CAT_RENDER, "UIWebView JS error: %s", err->message);
        g_error_free(err);
    }
}

// Cross-version JS execution helper. Selects evaluate_javascript on
// WebKitGTK 2.40+ (current API) and falls back to run_javascript on
// older releases. Same callback shape, same fire-and-forget semantics.
static void RunJavaScript(WebKitWebView* view, const char* script) {
    if (!view || !script) return;
#if WEBKIT_CHECK_VERSION(2, 40, 0)
    webkit_web_view_evaluate_javascript(view, script, -1,
                                        NULL, NULL,
                                        NULL,
                                        RunJsCallback, NULL);
#else
    webkit_web_view_run_javascript(view, script, NULL,
                                   RunJsCallback, NULL);
#endif
}

static void FlushPending(UIWebView* wv) {
    if (!wv || !wv->view) return;
    if (wv->pendingClearCookies) {
        WebKitWebsiteDataManager* dm =
            webkit_web_context_get_website_data_manager(
                webkit_web_view_get_context(wv->view));
        if (dm) {
            webkit_website_data_manager_clear(
                dm, WEBKIT_WEBSITE_DATA_COOKIES, 0, NULL, NULL, NULL);
        }
        wv->pendingClearCookies = 0;
    }
    WVPendingScript* s = wv->pendingScripts;
    wv->pendingScripts = NULL;
    while (s) {
        WVPendingScript* next = s->next;
        if (s->isInit) {
            WebKitUserScript* us = webkit_user_script_new(
                s->js,
                WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
                NULL, NULL);
            webkit_user_content_manager_add_script(wv->ucm, us);
            webkit_user_script_unref(us);
        } else {
            RunJavaScript(wv->view, s->js);
        }
        free(s->js);
        free(s);
        s = next;
    }
}

static void QueueScript(UIWebView* wv, const char* js, int isInit) {
    if (!wv || !js) return;
    WVPendingScript* s = (WVPendingScript*)calloc(1, sizeof(*s));
    if (!s) return;
    s->js = _strdup(js);
    s->isInit = isInit;
    // Append at end so they fire in the order the user added them.
    if (!wv->pendingScripts) {
        wv->pendingScripts = s;
    } else {
        WVPendingScript* tail = wv->pendingScripts;
        while (tail->next) tail = tail->next;
        tail->next = s;
    }
}

UIWebView* UIWebView_Create(const char* initialUrl) {
    EnsureGtkInit();
    if (!g_gtkInited) return NULL;

    UIWebView* wv = (UIWebView*)calloc(1, sizeof(*wv));
    if (!wv) return NULL;
    wv->__widget_type = UI_WIDGET_WEBVIEW;
    wv->cornerRadius = 0.0f;
    wv->borderWidth = 0.0f;
    wv->borderColor = (UIColor){ 0, 0, 0, 0.0f };
    wv->contextMenusEnabled = 1;   // default: context menus enabled

    wv->offscreenWindow = gtk_offscreen_window_new();
    wv->ucm = webkit_user_content_manager_new();
    wv->view = WEBKIT_WEB_VIEW(
        webkit_web_view_new_with_user_content_manager(wv->ucm));

    // Even with the WEBKIT_DISABLE_* env vars set in EnsureGtkInit,
    // pin the hardware-acceleration policy to NEVER here so any
    // settings-level override (user-passed WebKit settings, system
    // GSettings on the host distro) can't flip us back into the GL
    // path that crashes under WSLg.
    {
        WebKitSettings* set = webkit_web_view_get_settings(wv->view);
        webkit_settings_set_hardware_acceleration_policy(
            set, WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);
    }

    // The offscreen window manages widget realization without showing
    // an X11 / Wayland surface — perfect for capturing pixels into an
    // SDL_Texture without flashing a second window.
    gtk_container_add(GTK_CONTAINER(wv->offscreenWindow),
                      GTK_WIDGET(wv->view));
    // Start at a reasonable default size; UIWebView_RendererTick_Linux
    // will resize on demand to match the widget bounds.
    gtk_window_set_default_size(GTK_WINDOW(wv->offscreenWindow), 640, 480);
    gtk_widget_show_all(wv->offscreenWindow);

    g_signal_connect(wv->view, "load-changed",
                     G_CALLBACK(OnLoadChanged), wv);
    g_signal_connect(wv->view, "notify::uri",
                     G_CALLBACK(OnUriChanged), wv);
    g_signal_connect(wv->view, "decide-policy",
                     G_CALLBACK(OnDecidePolicy), wv);
    g_signal_connect(wv->view, "web-process-terminated",
                     G_CALLBACK(OnWebProcessTerminated), wv);
    g_signal_connect(wv->view, "context-menu",
                     G_CALLBACK(OnContextMenu), wv);

    if (initialUrl && *initialUrl) {
        webkit_web_view_load_uri(wv->view, initialUrl);
    } else {
        // Blank page; ready signal will still fire.
        webkit_web_view_load_html(wv->view,
            "<!doctype html><html><body></body></html>", NULL);
    }
    return wv;
}

UIWebView* UIWebView_Navigate(UIWebView* wv, const char* url) {
    if (!wv) return NULL;
    if (!wv->view) {
        free(wv->pendingUrl);
        wv->pendingUrl = url ? _strdup(url) : NULL;
        return wv;
    }
    if (url && *url) webkit_web_view_load_uri(wv->view, url);
    return wv;
}

const char* UIWebView_GetUrl(UIWebView* wv) {
    if (!wv || !wv->view) return wv ? wv->pendingUrl : NULL;
    return webkit_web_view_get_uri(wv->view);
}

UIWebView* UIWebView_Reload(UIWebView* wv) {
    if (wv && wv->view) webkit_web_view_reload(wv->view);
    return wv;
}
UIWebView* UIWebView_GoBack(UIWebView* wv) {
    if (wv && wv->view && webkit_web_view_can_go_back(wv->view))
        webkit_web_view_go_back(wv->view);
    return wv;
}
UIWebView* UIWebView_GoForward(UIWebView* wv) {
    if (wv && wv->view && webkit_web_view_can_go_forward(wv->view))
        webkit_web_view_go_forward(wv->view);
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

UIWebView* UIWebView_AddInitScript(UIWebView* wv, const char* js) {
    if (!wv || !js) return wv;
    if (wv->ucm && wv->ready) {
        WebKitUserScript* us = webkit_user_script_new(
            js, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
            WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, NULL, NULL);
        webkit_user_content_manager_add_script(wv->ucm, us);
        webkit_user_script_unref(us);
    } else {
        QueueScript(wv, js, 1);
    }
    return wv;
}
UIWebView* UIWebView_ExecuteScript(UIWebView* wv, const char* js) {
    if (!wv || !js) return wv;
    if (wv->view && wv->ready) {
        RunJavaScript(wv->view, js);
    } else {
        QueueScript(wv, js, 0);
    }
    return wv;
}
UIWebView* UIWebView_ClearCookies(UIWebView* wv) {
    if (!wv) return wv;
    if (wv->view && wv->ready) {
        WebKitWebsiteDataManager* dm =
            webkit_web_context_get_website_data_manager(
                webkit_web_view_get_context(wv->view));
        if (dm) webkit_website_data_manager_clear(
            dm, WEBKIT_WEBSITE_DATA_COOKIES, 0, NULL, NULL, NULL);
    } else {
        wv->pendingClearCookies = 1;
    }
    return wv;
}

// Visual setters: consumed by the SDL render path via UIWebView_GetVisuals.
UIWebView* UIWebView_SetRadius(UIWebView* wv, float r) {
    if (wv) wv->cornerRadius = (r < 0.0f) ? 0.0f : r;
    return wv;
}
UIWebView* UIWebView_SetBorder(UIWebView* wv, UIColor c, float w) {
    if (!wv) return wv;
    wv->borderColor = c;
    wv->borderWidth = (w < 0.0f) ? 0.0f : w;
    wv->hasBorder   = (wv->borderWidth > 0.0f);
    return wv;
}

void UIWebView_GetVisuals(const UIWebView* wv, float* radius, float* borderWidth,
                          UIColor* borderColor, int* hasBorder) {
    if (!wv) return;
    if (radius)      *radius      = wv->cornerRadius;
    if (borderWidth) *borderWidth = wv->borderWidth;
    if (borderColor) *borderColor = wv->borderColor;
    if (hasBorder)   *hasBorder   = wv->hasBorder;
}

// Async snapshot callback. Fires on the GTK main thread when the
// snapshot we requested below completes. We just stash the surface
// for the next render tick to upload; the actual SDL_UpdateTexture
// happens on the SDL thread (which IS the GTK thread here — we drive
// the main loop manually from RendererTick).
static void OnSnapshotReady(GObject* src, GAsyncResult* res, gpointer ud) {
    UIWebView* wv = (UIWebView*)ud;
    if (!wv) return;
    wv->snapshotPending = 0;
    GError* err = NULL;
    cairo_surface_t* surf = webkit_web_view_get_snapshot_finish(
        WEBKIT_WEB_VIEW(src), res, &err);
    if (err) {
        // Log domain+code+message — generic "There was an error creating
        // the snapshot" hides the real cause; the GError's domain points
        // at which subsystem (GdkGL, WPE, cairo) actually choked.
        static int loggedOnce = 0;
        if (!loggedOnce) {
            UI_WARN(UI_CAT_RENDER,
                    "UIWebView snapshot error (first occurrence): "
                    "domain=%s code=%d msg=%s",
                    g_quark_to_string(err->domain), err->code, err->message);
            loggedOnce = 1;
        }
        g_error_free(err);
    }
    if (!surf) return;
    if (wv->latestSnapshot) cairo_surface_destroy(wv->latestSnapshot);
    wv->latestSnapshot = surf;
    // Log first success so we can see in the field whether the
    // snapshot pipeline ever completes a round-trip.
    static int loggedFirstSuccess = 0;
    if (!loggedFirstSuccess) {
        UI_INFO(UI_CAT_RENDER,
                "UIWebView first snapshot OK (%dx%d)",
                cairo_image_surface_get_width(surf),
                cairo_image_surface_get_height(surf));
        loggedFirstSuccess = 1;
    }
}

// Renderer hook called once per frame from window.c's UIWebView branch.
//
// Pipeline:
//   1. Pump GLib so any signals (load-changed, snapshot-finished, JS
//      callbacks) that fired since last tick get delivered.
//   2. If a new snapshot finished, upload it to the SDL streaming
//      texture and discard the cairo surface.
//   3. Resize the offscreen window if the destination size changed.
//   4. Kick off a new snapshot request if none is in flight. WebKit
//      will deliver it via OnSnapshotReady on a future tick.
//
// Why webkit_web_view_get_snapshot() instead of
// gtk_offscreen_window_get_surface(): the offscreen-window API only
// captures GTK's own draw pass; WebKitGTK renders the page via its
// internal GPU process and copies the result into its own surface,
// which the offscreen window never sees. The webview's snapshot API
// (added exactly for this purpose) reaches into WebKit's renderer
// and hands us the actually-composited page bitmap as a cairo
// surface — same format (ARGB32 = BGRA on little-endian), zero
// extra copies on our side.
SDL_Texture* UIWebView_GetSnapshotTexture_Linux(UIWebView* wv, SDL_Renderer* r,
                                                int w, int h) {
    if (!wv || !wv->view || !r || w <= 0 || h <= 0) return NULL;

    // 1. Drain the GLib main loop, advancing every webkit / cairo
    //    source that's pending. Non-blocking.
    while (g_main_context_iteration(NULL, FALSE)) {
        // keep draining
    }

    // 2. If a snapshot is sitting in latestSnapshot, upload + free.
    if (wv->latestSnapshot) {
        int sw = cairo_image_surface_get_width(wv->latestSnapshot);
        int sh = cairo_image_surface_get_height(wv->latestSnapshot);
        if (sw > 0 && sh > 0) {
            if (!wv->texture || wv->texW != sw || wv->texH != sh) {
                if (wv->texture) SDL_DestroyTexture(wv->texture);
                wv->texture = SDL_CreateTexture(r, SDL_PIXELFORMAT_BGRA32,
                                                SDL_TEXTUREACCESS_STREAMING,
                                                sw, sh);
                wv->texW = sw;
                wv->texH = sh;
            }
            if (wv->texture) {
                cairo_surface_flush(wv->latestSnapshot);
                const unsigned char* pixels =
                    cairo_image_surface_get_data(wv->latestSnapshot);
                int stride = cairo_image_surface_get_stride(wv->latestSnapshot);
                if (pixels && stride > 0) {
                    SDL_UpdateTexture(wv->texture, NULL, pixels, stride);
                    wv->lastSnapshotTick = SDL_GetTicks();
                }
            }
        }
        cairo_surface_destroy(wv->latestSnapshot);
        wv->latestSnapshot = NULL;
    }

    // 3. Resize the offscreen GTK window so WebKit reflows at the
    //    requested resolution. We only resize when the size CHANGES,
    //    not every frame — gtk_window_resize triggers a full WebKit
    //    layout + render which then races snapshot requests we'd fire
    //    in the next few frames (the docs are explicit: snapshot can
    //    return WEBKIT_SNAPSHOT_ERROR_FAILED_TO_CREATE while the view
    //    is mid-allocation). The `resizedAt` tick lets us hold off on
    //    requesting a new snapshot for a beat after a resize.
    if (wv->requestedW != w || wv->requestedH != h) {
        gtk_window_resize(GTK_WINDOW(wv->offscreenWindow), w, h);
        gtk_widget_set_size_request(GTK_WIDGET(wv->view), w, h);
        gtk_widget_queue_resize(GTK_WIDGET(wv->view));
        wv->requestedW = w;
        wv->requestedH = h;
        wv->lastSnapshotTick = SDL_GetTicks(); // reuse field as resize-debounce timer
    }

    // 4. Throttle snapshot requests. WebKit's get_snapshot is async +
    //    surprisingly expensive (full software composite when HW accel
    //    is off, as it is on WSLg). Firing one every render tick at
    //    1700+ FPS thrashes WebKit's compositor and produces a steady
    //    stream of FAILED_TO_CREATE errors that never let a real
    //    snapshot complete. lastSnapshotTick is set both on successful
    //    uploads AND on resizes (above) so a single >=150ms gate gives
    //    WebKit a beat to settle either way before the next request.
    //    ~150 ms = ~7 effective Hz of webview refresh — plenty for any
    //    text-heavy UI use case, cheap enough not to peg CPU.
    const Uint32 nowTick = SDL_GetTicks();
    const Uint32 sinceLast = nowTick - wv->lastSnapshotTick;
    const Uint32 quietMs = 150;
    if (!wv->snapshotPending && sinceLast >= quietMs) {
        wv->snapshotPending = 1;
        webkit_web_view_get_snapshot(
            wv->view,
            WEBKIT_SNAPSHOT_REGION_VISIBLE,
            WEBKIT_SNAPSHOT_OPTIONS_NONE,
            NULL,                       // cancellable
            OnSnapshotReady, wv);
    }

    return wv->texture;
}

void UIWebView_Destroy(UIWebView* wv) {
    if (!wv) return;
    if (wv->offscreenWindow) {
        // gtk_widget_destroy drops the view + ucm refs in the right
        // order (children first, then the offscreen window itself).
        gtk_widget_destroy(wv->offscreenWindow);
    }
    if (wv->latestSnapshot) {
        cairo_surface_destroy(wv->latestSnapshot);
        wv->latestSnapshot = NULL;
    }
    if (wv->texture) SDL_DestroyTexture(wv->texture);
    while (wv->pendingScripts) {
        WVPendingScript* n = wv->pendingScripts->next;
        free(wv->pendingScripts->js);
        free(wv->pendingScripts);
        wv->pendingScripts = n;
    }
    free(wv->pendingUrl);
    free(wv);
}

// ----- Stubs for Win32-specific APIs (no Linux equivalent / phase 2) ----
UIWebView* UIWebView_SetCompositionMode(UIWebView* wv, int e)        { (void)e; return wv; }
UIWebView* UIWebView_SetBrowserArguments(UIWebView* wv, const char* a){(void)a; return wv; }
UIWebView* UIWebView_AppendBrowserArguments(UIWebView* wv, const char* a){(void)a; return wv; }
const char* UIWebView_GetDefaultBrowserArguments(void)               { return ""; }
UIWebView* UIWebView_SetUserAgent(UIWebView* wv, const char* s) {
    if (wv && wv->view && s) {
        WebKitSettings* set = webkit_web_view_get_settings(wv->view);
        webkit_settings_set_user_agent(set, s);
    }
    return wv;
}
UIWebView* UIWebView_SetDevToolsEnabled(UIWebView* wv, int e) {
    if (wv && wv->view) {
        WebKitSettings* set = webkit_web_view_get_settings(wv->view);
        webkit_settings_set_enable_developer_extras(set, e ? TRUE : FALSE);
    }
    return wv;
}
UIWebView* UIWebView_SetContextMenusEnabled(UIWebView* wv, int enabled) {
    if (wv) wv->contextMenusEnabled = enabled ? 1 : 0;
    return wv;
}
UIWebView* UIWebView_SetIsolated(UIWebView* wv, int e)            { (void)e; return wv; }
UIWebView* UIWebView_OnProcessFailed(UIWebView* wv, UIWebViewProcessFailedCallback cb, void* ud) {
    if (wv) { wv->onProcessFailed = cb; wv->onProcessFailedUserdata = ud; }
    return wv;
}
UIWebView* UIWebView_OnRequest(UIWebView* wv, UIWebViewRequestCallback cb, void* ud) {
    if (wv) { wv->onRequest = cb; wv->onRequestUserdata = ud; }
    return wv;
}
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

// =====================================================================
// Input forwarding (mouse motion / down / up / wheel) into the
// off-screen WebKitWebView.
//
// We mirror the Win32 contract (find_comp_webview_at +
// widget_to_visual_local) but the delivery mechanism is different:
// instead of WebView2's CompositionController.SendMouseInput we
// synthesize a classic GdkEvent and route it into the view's internal
// WebKitWebViewBase via gtk_widget_event(). In GTK3 that calls the
// widget's button_press_event / motion_notify_event / scroll_event
// vfuncs directly, regardless of whether the GdkWindow is mapped on
// screen — which is exactly what we need for an off-screen window.
//
// Every event MUST carry a valid `window` (g_object_ref'd into the
// event; gdk_event_free unrefs it) and a GdkDevice, or WebKit drops it.
// =====================================================================

// Find the topmost ready UIWebView whose widget bounds contain (x,y).
static UIWebView* find_wv_at_linux(UIChildren* children, float x, float y) {
    if (!children) return NULL;
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible || !w->data || !w->width || !w->height) continue;
        UIWidgetBase* b = (UIWidgetBase*)w->data;
        if (strcmp(b->__widget_type, UI_WIDGET_WEBVIEW) != 0) continue;
        UIWebView* wv = (UIWebView*)b;
        if (!wv->ready || !wv->view) continue;
        const float ww = *w->width, hh = *w->height;
        if (x < w->x || x >= w->x + ww || y < w->y || y >= w->y + hh) continue;
        return wv;
    }
    return NULL;
}

// Translate window coords to view-local (page) coords for wv. The
// snapshot texture is drawn inset by the border width (window.c), so we
// subtract the same border here to land 1:1 on the page.
static void wv_local_linux(UIChildren* children, UIWebView* wv,
                           float x, float y, double* lx, double* ly) {
    *lx = 0; *ly = 0;
    if (!children || !wv) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || w->data != (UIWidgetData)wv) continue;
        const int bw = wv->hasBorder ? (int)wv->borderWidth : 0;
        *lx = (double)(x - w->x - bw);
        *ly = (double)(y - w->y - bw);
        return;
    }
}

// Grab the view's realized GdkWindow + pointer device, or return 0.
static int wv_input_ctx(UIWebView* wv, GdkWindow** outWin, GdkDevice** outPtr) {
    GtkWidget* tw = GTK_WIDGET(wv->view);
    if (!gtk_widget_get_realized(tw)) gtk_widget_realize(tw);
    GdkWindow* gw = gtk_widget_get_window(tw);
    if (!gw) return 0;
    GdkDisplay* dpy = gdk_window_get_display(gw);
    GdkSeat* seat = gdk_display_get_default_seat(dpy);
    if (outWin) *outWin = gw;
    if (outPtr) *outPtr = gdk_seat_get_pointer(seat);
    return 1;
}

void UIWebView_DispatchMouseMotion(UIChildren* children, float x, float y) {
    UIWebView* wv = find_wv_at_linux(children, x, y);
    if (!wv) return;
    GdkWindow* gw; GdkDevice* dev;
    if (!wv_input_ctx(wv, &gw, &dev)) return;
    double lx, ly; wv_local_linux(children, wv, x, y, &lx, &ly);
    GdkEvent* ev = gdk_event_new(GDK_MOTION_NOTIFY);
    ev->motion.window  = g_object_ref(gw);
    ev->motion.send_event = TRUE;
    ev->motion.time    = (guint32)(g_get_monotonic_time() / 1000);
    ev->motion.x = lx; ev->motion.y = ly;
    ev->motion.x_root = lx; ev->motion.y_root = ly;
    ev->motion.state   = 0;
    ev->motion.is_hint = FALSE;
    gdk_event_set_device(ev, dev);
    gtk_widget_event(GTK_WIDGET(wv->view), ev);
    gdk_event_free(ev);
}

static void wv_send_button(UIChildren* children, float x, float y,
                           int sdlButton, int down) {
    UIWebView* wv = find_wv_at_linux(children, x, y);
    if (!wv) return;
    GdkWindow* gw; GdkDevice* dev;
    if (!wv_input_ctx(wv, &gw, &dev)) return;
    double lx, ly; wv_local_linux(children, wv, x, y, &lx, &ly);
    guint btn = (sdlButton >= 1 && sdlButton <= 3) ? (guint)sdlButton : 1;
    GdkEvent* ev = gdk_event_new(down ? GDK_BUTTON_PRESS : GDK_BUTTON_RELEASE);
    ev->button.window = g_object_ref(gw);
    ev->button.send_event = TRUE;
    ev->button.time   = (guint32)(g_get_monotonic_time() / 1000);
    ev->button.x = lx; ev->button.y = ly;
    ev->button.x_root = lx; ev->button.y_root = ly;
    ev->button.button = btn;
    // On release advertise the button as having been held, which some
    // hit-test paths consult when finishing a click.
    ev->button.state  = down ? 0 : (guint)(GDK_BUTTON1_MASK << (btn - 1));
    gdk_event_set_device(ev, dev);
    gtk_widget_event(GTK_WIDGET(wv->view), ev);
    gdk_event_free(ev);
}

void UIWebView_DispatchMouseDown(UIChildren* c, float x, float y, int b) {
    wv_send_button(c, x, y, b, 1);
}
void UIWebView_DispatchMouseUp(UIChildren* c, float x, float y, int b) {
    wv_send_button(c, x, y, b, 0);
}

void UIWebView_DispatchMouseWheel(UIChildren* children, float x, float y,
                                  float dx, float dy) {
    UIWebView* wv = find_wv_at_linux(children, x, y);
    if (!wv || (dx == 0.0f && dy == 0.0f)) return;
    GdkWindow* gw; GdkDevice* dev;
    if (!wv_input_ctx(wv, &gw, &dev)) return;
    double lx, ly; wv_local_linux(children, wv, x, y, &lx, &ly);
    GdkEvent* ev = gdk_event_new(GDK_SCROLL);
    ev->scroll.window = g_object_ref(gw);
    ev->scroll.send_event = TRUE;
    ev->scroll.time   = (guint32)(g_get_monotonic_time() / 1000);
    ev->scroll.x = lx; ev->scroll.y = ly;
    ev->scroll.x_root = lx; ev->scroll.y_root = ly;
    ev->scroll.direction = GDK_SCROLL_SMOOTH;
    // SDL wheel.y>0 = away from user (page should scroll up); GDK smooth
    // delta_y>0 scrolls content down -> negate. ~3 px/notch feel. There
    // is NO 120 WHEEL_DELTA on Linux (that is a Win32 convention).
    ev->scroll.delta_x = -(double)dx * 3.0;
    ev->scroll.delta_y = -(double)dy * 3.0;
    ev->scroll.state  = 0;
    gdk_event_set_device(ev, dev);
    gtk_widget_event(GTK_WIDGET(wv->view), ev);
    gdk_event_free(ev);
}

// WebKitGTK has no simple per-widget cursor query; honest MVP returns
// default. (To support page cursors you'd connect WebKitWebView's
// "mouse-target-changed" and stash a UICursor in struct UIWebView.)
int UIWebView_HoverCursorAt(UIChildren* c, float x, float y) {
    (void)c; (void)x; (void)y; return 0;
}

#else // !_WIN32 && !(linux + WebKitGTK)

#include <SDL3/SDL.h>

/** Non-Windows stub of UIWebView. Stores only what is needed so calls
 *  don't crash; rendering and navigation are no-ops. */
struct UIWebView {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_WEBVIEW). */
    char* url;                 /**< Heap-owned URL string (mirrors the real backend). */
};

UIWebView* UIWebView_Create(const char* initialUrl) {
    // No native webview backend was compiled in for this platform. The
    // most common cause is building on Linux without WebKitGTK (the
    // libwebkit2gtk-4.x dev package was absent at configure time). Surface
    // this loudly the first time an app tries to open a webview: log the
    // cause and pop a native error dialog so it isn't a silent failure.
    // The widget itself still draws a matching error placeholder in
    // window.c so the message persists after the dialog is dismissed.
    static int warned = 0;
    if (!warned) {
        warned = 1;
#if defined(__linux__)
        const char* title = "WebView unavailable";
        const char* body  =
            "The WebKitGTK backend is not available.\n\n"
            "Mocida was built without WebKitGTK, so the webview cannot be "
            "displayed.\n\n"
            "Install the library and rebuild:\n"
            "    sudo apt install libwebkit2gtk-4.1-dev";
#else
        const char* title = "WebView unavailable";
        const char* body  =
            "No webview backend is available for this platform.";
#endif
        fprintf(stderr, "[mocida] UIWebView: %s. %s\n", title, body);
        fflush(stderr);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, body, NULL);
    }
    UIWebView* wv = (UIWebView*)calloc(1, sizeof(UIWebView));
    if (!wv) return NULL;
    wv->__widget_type = UI_WIDGET_WEBVIEW;
    if (initialUrl) wv->url = _strdup(initialUrl);
    return wv;
}
UIWebView* UIWebView_Navigate(UIWebView* wv, const char* u) {
    if (wv) { free(wv->url); wv->url = u ? _strdup(u) : NULL; }
    return wv;
}
const char* UIWebView_GetUrl(UIWebView* wv) { return wv ? wv->url : NULL; }
UIWebView* UIWebView_Reload   (UIWebView* wv) { return wv; }
UIWebView* UIWebView_GoBack   (UIWebView* wv) { return wv; }
UIWebView* UIWebView_GoForward(UIWebView* wv) { return wv; }
UIWebView* UIWebView_OnReady  (UIWebView* wv, UIWebViewReadyCallback cb, void* ud) {
    (void)cb; (void)ud; return wv;
}
UIWebView* UIWebView_SetCompositionMode(UIWebView* wv, int e) { (void)e; return wv; }
UIWebView* UIWebView_SetBrowserArguments(UIWebView* wv, const char* a) { (void)a; return wv; }
UIWebView* UIWebView_AppendBrowserArguments(UIWebView* wv, const char* a) { (void)a; return wv; }
const char* UIWebView_GetDefaultBrowserArguments(void) { return ""; }
UIWebView* UIWebView_SetRadius(UIWebView* wv, float r) { (void)r; return wv; }
UIWebView* UIWebView_SetBorder(UIWebView* wv, UIColor c, float w) { (void)c; (void)w; return wv; }
UIWebView* UIWebView_SetUserAgent(UIWebView* wv, const char* s) { (void)s; return wv; }
UIWebView* UIWebView_SetDevToolsEnabled(UIWebView* wv, int e) { (void)e; return wv; }
UIWebView* UIWebView_SetContextMenusEnabled(UIWebView* wv, int e) { (void)e; return wv; }
UIWebView* UIWebView_AddInitScript(UIWebView* wv, const char* s) { (void)s; return wv; }
UIWebView* UIWebView_ExecuteScript(UIWebView* wv, const char* s) { (void)s; return wv; }
UIWebView* UIWebView_ClearCookies(UIWebView* wv) { return wv; }
UIWebView* UIWebView_OnRequest(UIWebView* wv, UIWebViewRequestCallback cb, void* ud) {
    (void)cb; (void)ud; return wv;
}
UIWebView* UIWebView_OnUrlChange(UIWebView* wv, UIWebViewUrlChangeCallback cb, void* ud) {
    (void)cb; (void)ud; return wv;
}
UIWebView* UIWebView_OnLoadingChange(UIWebView* wv, UIWebViewLoadingCallback cb, void* ud) {
    (void)cb; (void)ud; return wv;
}
int UIWebView_AddD2DOverlay(UIWebView* wv, int x, int y, int w, int h, float r, UIColor c) {
    (void)wv; (void)x; (void)y; (void)w; (void)h; (void)r; (void)c; return -1;
}
void UIWebView_DispatchMouseMotion(UIChildren* c, float x, float y) { (void)c; (void)x; (void)y; }
void UIWebView_DispatchMouseDown  (UIChildren* c, float x, float y, int b) { (void)c; (void)x; (void)y; (void)b; }
void UIWebView_DispatchMouseUp    (UIChildren* c, float x, float y, int b) { (void)c; (void)x; (void)y; (void)b; }
void UIWebView_DispatchMouseWheel (UIChildren* c, float x, float y, float dx, float dy) { (void)c; (void)x; (void)y; (void)dx; (void)dy; }
int  UIWebView_HoverCursorAt      (UIChildren* c, float x, float y) { (void)c; (void)x; (void)y; return 0; }
UIWebView* UIWebView_SetIsolated(UIWebView* wv, int e) { (void)e; return wv; }
UIWebView* UIWebView_OnProcessFailed(UIWebView* wv, UIWebViewProcessFailedCallback cb, void* ud) {
    (void)cb; (void)ud; return wv;
}
void UIWebView_SetD2DOverlayText(UIWebView* wv, int h, const char* t, const char* f, float s, UIColor c, float p) {
    (void)wv; (void)h; (void)t; (void)f; (void)s; (void)c; (void)p;
}
void UIWebView_MoveD2DOverlay(UIWebView* wv, int h, int x, int y, int w, int hh) {
    (void)wv; (void)h; (void)x; (void)y; (void)w; (void)hh;
}
void UIWebView_RemoveD2DOverlay(UIWebView* wv, int h) { (void)wv; (void)h; }
void UIWebView_Destroy(UIWebView* wv) {
    if (wv) { free(wv->url); free(wv); }
}

#endif
