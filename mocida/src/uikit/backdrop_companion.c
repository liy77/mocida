// backdrop_companion.c — a helper window that carries a real DWM acrylic blur
// directly BEHIND the main window, so it shows through the main window's
// transparent (alpha-0) holes (e.g. glass sidebars).
//
// Why this exists: SDL's D3D11/Vulkan swapchain can't host a clean DWM system
// backdrop in its own client — applied to the SDL window, Mica/Acrylic render
// sharp (alpha passthrough) or flat (the throttled ACCENT tint). A *plain* Win32
// window, however, shows a proper Explorer-style acrylic. So we put one exactly
// behind the SDL window: the SDL window's opaque pixels cover it, and its alpha-0
// holes reveal the companion's acrylic. This is the same trick several SDL/OpenGL
// apps use to get a real OS backdrop.
//
// Windows only. On other platforms every entry point is a no-op (the native
// per-window backdrop_*.{c,mm} path handles those OSes directly).

#include <uikit/backdrop.h>

#ifdef _WIN32

#include <windows.h>
#include <dwmapi.h>
#include <SDL3/SDL.h>

// DWM constants (older SDKs omit them).
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#define MOCIDA_DWMWCP_ROUND          2
#define MOCIDA_DWMSBT_MAINWINDOW      2 /* Mica     */
#define MOCIDA_DWMSBT_TRANSIENTWINDOW 3 /* Acrylic  */
#define MOCIDA_DWMSBT_TABBEDWINDOW    4 /* Mica Alt */

// Real acrylic via Windows.UI.Composition (backdrop_composition.cpp) — renders the
// blurred desktop into the companion's own DComp visual, so it shows even while the
// companion is fully occluded (the DWM system backdrop would be culled there).
extern void* UIBackdropComposition_Create(HWND hwnd, unsigned int tintRGB, float tintOpacity);
extern void  UIBackdropComposition_Resize(void* handle, int w, int h);
extern void  UIBackdropComposition_Destroy(void* handle);

#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

static const wchar_t* COMPANION_CLASS = L"MocidaBackdropCompanion";

static HWND   g_companion = NULL;  // single instance
static HWND   g_owner     = NULL;  // the SDL window it tracks
static ATOM   g_class     = 0;
static void*  g_comp      = NULL;  // Windows.UI.Composition backdrop handle

static HWND sdl_hwnd(SDL_Window* w) {
    if (!w) return NULL;
    SDL_PropertiesID props = SDL_GetWindowProperties(w);
    return (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
}

// The companion is purely decorative: it never takes focus or input. Make every
// hit transparent so a stray click (it should always be behind the owner) falls
// through, and skip background erase races.
static LRESULT CALLBACK CompanionProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCHITTEST:    return HTTRANSPARENT; // clicks fall through to the owner
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND:   return 1; // the composition visual is the content
        default:              return DefWindowProcW(h, msg, wp, lp);
    }
}

int UIBackdropCompanion_Enable(SDL_Window* owner, UIBackdropMaterial material,
                               UIColor tint, float tintOpacity) {
    (void)material;
    HWND ownerHwnd = sdl_hwnd(owner);
    if (!ownerHwnd) return 0;

    if (g_companion) { // already up
        g_owner = ownerHwnd;
        UIBackdropCompanion_Sync(owner);
        return 1;
    }

    if (!g_class) {
        WNDCLASSEXW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = CompanionProc;
        wc.hInstance     = GetModuleHandleW(NULL);
        wc.hCursor       = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = NULL; // no GDI paint — the DComp visual is the content
        wc.lpszClassName = COMPANION_CLASS;
        g_class = RegisterClassExW(&wc);
        if (!g_class) return 0;
    }

    // Borderless popup, no taskbar/alt-tab, never activates. NOREDIRECTIONBITMAP =
    // no GDI redirection surface, so the DirectComposition visual tree (the host-
    // backdrop acrylic) is the window's content.
    HWND hwnd = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        COMPANION_CLASS, L"", WS_POPUP,
        0, 0, 16, 16, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!hwnd) return 0;

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    int corner = MOCIDA_DWMWCP_ROUND; // match the owner's rounded corners
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    g_companion = hwnd;
    g_owner     = ownerHwnd;
    UIBackdropCompanion_Sync(owner);

    // Attach the Windows.UI.Composition host-backdrop acrylic (the real blur).
    unsigned int tintRGB = (((unsigned)tint.r & 0xFF) << 16) |
                           (((unsigned)tint.g & 0xFF) << 8)  |
                           ((unsigned)tint.b & 0xFF);
    g_comp = UIBackdropComposition_Create(hwnd, tintRGB, tintOpacity);

    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    return g_comp ? 1 : 0;
}

void UIBackdropCompanion_Sync(SDL_Window* owner) {
    if (!g_companion) return;
    HWND ownerHwnd = sdl_hwnd(owner);
    if (!ownerHwnd) ownerHwnd = g_owner;
    if (!ownerHwnd) return;

    // Hide the companion when the owner is minimised (otherwise it lingers).
    if (IsIconic(ownerHwnd)) {
        if (IsWindowVisible(g_companion)) ShowWindow(g_companion, SW_HIDE);
        return;
    }
    if (!IsWindowVisible(g_companion)) ShowWindow(g_companion, SW_SHOWNOACTIVATE);

    RECT r;
    if (!GetWindowRect(ownerHwnd, &r)) return;
    int w = r.right - r.left, h = r.bottom - r.top;

    // Park the companion exactly under the owner, re-asserted every frame so no
    // other window slips between them: same screen rect, inserted directly BELOW
    // the owner in z-order (hwndInsertAfter = owner).
    SetWindowPos(g_companion, ownerHwnd, r.left, r.top, w, h,
                 SWP_NOACTIVATE);

    if (g_comp) UIBackdropComposition_Resize(g_comp, w, h);
}

void UIBackdropCompanion_Disable(void) {
    if (g_comp) {
        UIBackdropComposition_Destroy(g_comp);
        g_comp = NULL;
    }
    if (g_companion) {
        DestroyWindow(g_companion);
        g_companion = NULL;
    }
    g_owner = NULL;
}

#else // !_WIN32 — no-op everywhere else

int  UIBackdropCompanion_Enable(SDL_Window* owner, UIBackdropMaterial material,
                                UIColor tint, float tintOpacity) {
    (void)owner; (void)material; (void)tint; (void)tintOpacity; return 0;
}
void UIBackdropCompanion_Sync(SDL_Window* owner) { (void)owner; }
void UIBackdropCompanion_Disable(void) {}

#endif // _WIN32
