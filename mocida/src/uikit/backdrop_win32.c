// backdrop_win32.c — Windows DWM system backdrops (Mica / Mica Alt / Acrylic).
//
// Win11 22H2+ exposes DWMWA_SYSTEMBACKDROP_TYPE: a one-call switch for Mica
// (DWMSBT_MAINWINDOW), Mica Alt (DWMSBT_TABBEDWINDOW) and Acrylic
// (DWMSBT_TRANSIENTWINDOW). On Win10 1809+ we fall back to the undocumented
// SetWindowCompositionAttribute + ACCENT_ENABLE_ACRYLICBLURBEHIND.
//
// On non-Windows builds this whole translation unit is empty — the platform's
// own backdrop_*.c / .mm provides the symbols.

#ifdef _WIN32

#include <uikit/backdrop.h>
#include <uikit/window.h> // UIWindow_WantsTransparent

#include <windows.h>
#include <dwmapi.h>
#include <SDL3/SDL.h>

// ---- DWM constants (define locally; older SDK headers omit them) ----
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif

// DWM_SYSTEMBACKDROP_TYPE values.
#define MOCIDA_DWMSBT_AUTO            0
#define MOCIDA_DWMSBT_NONE            1
#define MOCIDA_DWMSBT_MAINWINDOW      2 /* Mica       */
#define MOCIDA_DWMSBT_TRANSIENTWINDOW 3 /* Acrylic    */
#define MOCIDA_DWMSBT_TABBEDWINDOW    4 /* Mica Alt   */

// ---- Win10 ACCENT fallback (undocumented user32 API) ----
typedef enum {
    ACCENT_DISABLED                   = 0,
    ACCENT_ENABLE_GRADIENT            = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND          = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND   = 4
} MOCIDA_ACCENT_STATE;

typedef struct {
    MOCIDA_ACCENT_STATE AccentState;
    DWORD               AccentFlags;
    DWORD               GradientColor; /* 0xAABBGGRR */
    DWORD               AnimationId;
} MOCIDA_ACCENT_POLICY;

typedef struct {
    DWORD  Attrib; /* WCA_ACCENT_POLICY == 19 */
    PVOID  pvData;
    SIZE_T cbData;
} MOCIDA_WINCOMPATTRDATA;

typedef BOOL(WINAPI* SetWindowCompositionAttribute_t)(HWND, MOCIDA_WINCOMPATTRDATA*);

static HWND backdrop_hwnd(SDL_Window* w) {
    if (!w) return NULL;
    SDL_PropertiesID props = SDL_GetWindowProperties(w);
    return (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
}

// Win11 = build >= 22000. RtlGetVersion is the only call that isn't lied to by
// the compatibility shim (GetVersionEx caps at 6.2 without a manifest).
static int win_build(void) {
    typedef LONG(WINAPI* RtlGetVersion_t)(PRTL_OSVERSIONINFOW);
    static int cached = -1;
    if (cached >= 0) return cached;
    cached = 0;
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    if (nt) {
        RtlGetVersion_t fn = (RtlGetVersion_t)(void*)GetProcAddress(nt, "RtlGetVersion");
        if (fn) {
            RTL_OSVERSIONINFOW vi = { 0 };
            vi.dwOSVersionInfoSize = sizeof(vi);
            if (fn(&vi) == 0) cached = (int)vi.dwBuildNumber;
        }
    }
    return cached;
}

static int is_win11(void) { return win_build() >= 22000; }

static DWORD accent_gradient(UIColor tint, float tintOpacity) {
    int a = (int)(tint.a * tintOpacity * 255.0f);
    if (a < 0) a = 0; if (a > 255) a = 255;
    DWORD r = (DWORD)(tint.r & 0xFF);
    DWORD g = (DWORD)(tint.g & 0xFF);
    DWORD b = (DWORD)(tint.b & 0xFF);
    // 0xAABBGGRR
    return ((DWORD)a << 24) | (b << 16) | (g << 8) | r;
}

static int apply_accent(HWND hwnd, MOCIDA_ACCENT_STATE state, UIColor tint, float tintOpacity) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return 0;
    SetWindowCompositionAttribute_t fn =
        (SetWindowCompositionAttribute_t)(void*)GetProcAddress(user32, "SetWindowCompositionAttribute");
    if (!fn) return 0;

    MOCIDA_ACCENT_POLICY policy = { 0 };
    policy.AccentState   = state;
    policy.AccentFlags   = 2; /* draw all borders */
    policy.GradientColor = accent_gradient(tint, tintOpacity);

    MOCIDA_WINCOMPATTRDATA data = { 0 };
    data.Attrib = 19; /* WCA_ACCENT_POLICY */
    data.pvData = &policy;
    data.cbData = sizeof(policy);
    return fn(hwnd, &data) ? 1 : 0;
}

UIBackdropMaterial UIBackdrop_ResolveAuto(void) {
    if (is_win11()) return UI_BACKDROP_MICA;
    if (win_build() >= 17763) return UI_BACKDROP_ACRYLIC_LEGACY; /* Win10 1809+ */
    return UI_BACKDROP_NONE;
}

int UIBackdrop_NativeAvailable(void) {
    return win_build() >= 17763; /* DWM acrylic/mica era */
}

int UIBackdrop_Apply(SDL_Window* window, UIBackdropMaterial material,
                     UIColor tint, float tintOpacity) {
    HWND hwnd = backdrop_hwnd(window);
    if (!hwnd) return 0;

    // Match the dark UI so the OS-drawn titlebar/backdrop tints correctly.
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    if (material == UI_BACKDROP_NONE) {
        MARGINS m0 = { 0, 0, 0, 0 };
        DwmExtendFrameIntoClientArea(hwnd, &m0);
        int sbt = MOCIDA_DWMSBT_NONE;
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &sbt, sizeof(sbt));
        apply_accent(hwnd, ACCENT_DISABLED, tint, tintOpacity);
        return 0;
    }

    // ACRYLIC on a TRANSPARENT (per-pixel alpha) window → the classic blur-behind
    // (SetWindowCompositionAttribute / ACCENT_ENABLE_ACRYLICBLURBEHIND). With real
    // alpha pixels present (SDL_WINDOW_TRANSPARENT), DWM composites a blurred +
    // tinted copy of whatever is behind the window INTO those alpha pixels — the
    // visible acrylic, with explicit tint/opacity control. This is the ONLY path
    // that actually blurs here: the DWMSBT system backdrop does NOT composite onto
    // an SDL alpha window (it leaves the holes showing the sharp desktop). The
    // legacy "ACCENT is dead on Win11" lore only held on an OPAQUE window, where
    // there was no alpha for DWM to blend the blur into. NO frame extension on this
    // path — DwmExtendFrameIntoClientArea flattens the ACCENT blur.
    if ((material == UI_BACKDROP_ACRYLIC || material == UI_BACKDROP_ACRYLIC_LEGACY)
        && UIWindow_WantsTransparent()) {
        // The SDL window can't host the blur in its own client (DWMSBT → sharp,
        // ACCENT → throttled flat/sharp on Win11 — both verified). The visible
        // acrylic comes from the backdrop COMPANION window behind us
        // (UIBackdropCompanion_*, driven from the host); a plain DWMSBT window
        // blurs cleanly. All we do here is report native so UIWindow_SetBackdrop
        // zeroes the clear → the app's alpha-0 holes stay see-through and reveal
        // the companion's acrylic. No DWM effect on the SDL window itself.
        return 1;
    }

    // Win11 22H2+: the DWMWA_SYSTEMBACKDROP_TYPE switch for Mica / Mica Alt (and
    // Acrylic on an OPAQUE window), with the frame extended into the whole client
    // so the backdrop reaches the app's transparent (alpha-0) pixels. Acrylic ==
    // DWMSBT_TRANSIENTWINDOW. (DWMSBT uses the system tint recipe; the explicit
    // tint/opacity is honored only by the ACCENT paths.)
    if (is_win11()) {
        MARGINS m = { -1, -1, -1, -1 };
        DwmExtendFrameIntoClientArea(hwnd, &m);
        int sbt;
        switch (material) {
            case UI_BACKDROP_ACRYLIC:
            case UI_BACKDROP_ACRYLIC_LEGACY:
                sbt = MOCIDA_DWMSBT_TRANSIENTWINDOW; break;
            case UI_BACKDROP_MICA_ALT:
                sbt = MOCIDA_DWMSBT_TABBEDWINDOW;    break;
            default: /* Mica */
                sbt = MOCIDA_DWMSBT_MAINWINDOW;      break;
        }
        HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &sbt, sizeof(sbt));
        if (SUCCEEDED(hr)) return 1;
    }

    // Win10 1809+ (no DWMWA_SYSTEMBACKDROP_TYPE): the undocumented acrylic
    // blur-behind, which still works there and honors the explicit tint/opacity.
    return apply_accent(hwnd, ACCENT_ENABLE_ACRYLICBLURBEHIND, tint, tintOpacity);
}

#endif // _WIN32
