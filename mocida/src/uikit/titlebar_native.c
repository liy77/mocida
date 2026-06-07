// titlebar_native.c — keep native OS window decorations (rounded corners +
// drop shadow + Aero-snap) on a CUSTOM-TITLEBAR window.
//
// OndaEngine / any App(titlebar: custom) app paints its own title bar. To do
// that the window is created SDL_WINDOW_BORDERLESS on Windows/Linux, which on
// Win11 strips the DWM frame and therefore the *rounded corners* and the
// *drop shadow* (square, flat window). This file asks the compositor to put
// those back without bringing the caption back:
//
//   Windows 11 (build >= 22000):
//     - DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_ROUND)
//       → DWM rounds the borderless window's corners to the system radius.
//     - DwmExtendFrameIntoClientArea({1,1,1,1}) → a 1px frame extension makes
//       DWM draw the standard window drop shadow around the borderless window.
//       (Margins are tiny so they don't visually intrude; the app clears the
//       whole client area opaque, covering the extended frame.)
//     - WM_NCCALCSIZE is NOT needed for a borderless window (there is no
//       non-client caption to remove). Aero-snap, double-click-maximize and
//       maximize-to-workarea already work through SDL's borderless hit-test
//       (SDL_SetWindowHitTest, wired in app.c → MocidaHitTest), and
//       SDL_MaximizeWindow respects the work area.
//   Windows 10: DWMWA_WINDOW_CORNER_PREFERENCE doesn't exist; the call is a
//     harmless no-op (DwmSetWindowAttribute returns an error which we ignore).
//     Win10 borderless windows are square by OS convention anyway.
//   Linux / other: no-op — borderless + server-side-decoration behaviour
//     varies by compositor and is left to SDL.
//   macOS: implemented in titlebar_cocoa.mm (this file's
//     UIWindow_ApplyNativeDecorations is compiled out under __APPLE__ so the
//     Objective-C++ version provides the symbol).

#include <uikit/window.h>

// --------------------------------------------------------------------------
// UIWindow_IsMacOS — compile-time platform query for the MUI/host (kept here,
// for every platform, so there's a single definition site).
// --------------------------------------------------------------------------
int UIWindow_IsMacOS(void) {
#if defined(__APPLE__)
    return 1;
#else
    return 0;
#endif
}

// On macOS the decoration setup is Objective-C++ (titlebar_cocoa.mm); don't
// define UIWindow_ApplyNativeDecorations here too (duplicate symbol).
#if !defined(__APPLE__)

#if defined(_WIN32)

#include <windows.h>
#include <dwmapi.h>
#include <SDL3/SDL.h>

// Newer-SDK constants, defined locally so older Windows SDK headers still build.
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

// DWM_WINDOW_CORNER_PREFERENCE values (Win11 22H2+).
#define MOCIDA_DWMWCP_DEFAULT    0  // let the system decide
#define MOCIDA_DWMWCP_DONOTROUND 1  // square corners
#define MOCIDA_DWMWCP_ROUND      2  // full rounding (the standard window radius)
#define MOCIDA_DWMWCP_ROUNDSMALL 3  // small rounding (menus/tooltips)

static HWND titlebar_hwnd(SDL_Window* w) {
    if (!w) return NULL;
    SDL_PropertiesID props = SDL_GetWindowProperties(w);
    return (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
}

// Win11 = build >= 22000. RtlGetVersion isn't capped by the compatibility
// shim the way GetVersionEx is (mirrors backdrop_win32.c's helper).
static int titlebar_win_build(void) {
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

void UIWindow_ApplyNativeDecorations(SDL_Window* window) {
    HWND hwnd = titlebar_hwnd(window);
    if (!hwnd) return;

    // Match the dark UI so any OS-drawn edge tints correctly (the shadow + the
    // 1px frame). Harmless if already set by the backdrop path.
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, /*DWMWA_USE_IMMERSIVE_DARK_MODE*/ 20, &dark, sizeof(dark));

    // Win11: ask DWM to round the borderless window to the standard radius.
    if (titlebar_win_build() >= 22000) {
        DWORD pref = MOCIDA_DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
    }

    // A small (1px) frame extension makes DWM render the native drop shadow
    // around the otherwise frame-less window. The app paints over the whole
    // client area opaque, so the extended frame is never visible — only its
    // shadow is. This works on Win10 1809+ and Win11. Note: a *negative*
    // (sheet-of-glass) margin would instead pull Mica/transparency in — that's
    // the backdrop path; here we want the shadow only, so use a tiny positive
    // margin. If a system backdrop (Mica/Acrylic) is later enabled it re-issues
    // its own DwmExtendFrameIntoClientArea and wins, which is fine.
    MARGINS m = { 1, 1, 1, 1 };
    DwmExtendFrameIntoClientArea(hwnd, &m);

    // Nudge DWM to recompute the non-client frame so the corner-preference +
    // shadow take effect immediately (without this they apply on the next
    // size/redraw, which can leave the very first frame square).
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                 SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

#else  // !_WIN32 && !__APPLE__  (Linux / other Unix)

#include <SDL3/SDL.h>

void UIWindow_ApplyNativeDecorations(SDL_Window* window) {
    // Best-effort: under SDL on Linux, borderless + server/client-side
    // decoration behaviour (and rounding/shadow) is the compositor's call.
    // Nothing portable to do here; leave SDL's borderless window as-is.
    (void)window;
}

#endif // platform split

#endif // !__APPLE__
