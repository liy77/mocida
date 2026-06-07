// backdrop_companion.c — attaches the Windows.UI.Composition acrylic backdrop to
// the MAIN window. (Named "companion" historically — an earlier design used a
// separate helper window; that hit DWM occlusion + foreground-only host-backdrop
// walls, so we now compose on the main window directly.)
//
// The window must be transparent: SDL then renders into a COMPOSITION swap chain
// (D3D11 renderer patch), which backdrop_composition.cpp hosts in a
// Windows.UI.Composition visual tree WITH a host-backdrop acrylic visual behind
// it. Because the main window is the foreground window, the host-backdrop samples
// + blurs the real desktop; the app's alpha-0 holes reveal it. Windows only.

#include <uikit/backdrop.h>

#ifdef _WIN32

#include <windows.h>
#include <SDL3/SDL.h>

// Implemented in backdrop_composition.cpp (C++/WinRT).
extern void* UIBackdropComposition_AttachToWindow(HWND hwnd, void* swapchain,
                                                  unsigned int tintRGB, float tintOpacity);
extern void  UIBackdropComposition_Resize(void* handle, int w, int h);
extern void  UIBackdropComposition_Destroy(void* handle);

static void* g_comp = NULL; // single instance (OndaEngine is single-window)

static HWND sdl_hwnd(SDL_Window* w) {
    if (!w) return NULL;
    return (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(w),
                                        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
}

int UIBackdropCompanion_Enable(SDL_Window* owner, UIBackdropMaterial material,
                               UIColor tint, float tintOpacity) {
    (void)material;
    if (g_comp) return 1;
    HWND hwnd = sdl_hwnd(owner);
    if (!hwnd) return 0;

    // SDL exposes the (composition) swap chain via a renderer property.
    SDL_Renderer* r = SDL_GetRenderer(owner);
    if (!r) return 0;
    void* swapchain = SDL_GetPointerProperty(SDL_GetRendererProperties(r),
                                             SDL_PROP_RENDERER_D3D11_SWAPCHAIN_POINTER, NULL);
    if (!swapchain) return 0; // not a D3D11 composition window — no acrylic

    unsigned int tintRGB = (((unsigned)tint.r & 0xFF) << 16) |
                           (((unsigned)tint.g & 0xFF) << 8)  |
                           ((unsigned)tint.b & 0xFF);
    g_comp = UIBackdropComposition_AttachToWindow(hwnd, swapchain, tintRGB, tintOpacity);
    if (g_comp) UIBackdropCompanion_Sync(owner);
    return g_comp ? 1 : 0;
}

void UIBackdropCompanion_Sync(SDL_Window* owner) {
    if (!g_comp || !owner) return;
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(owner, &w, &h);
    UIBackdropComposition_Resize(g_comp, w, h);
}

void UIBackdropCompanion_Disable(void) {
    if (g_comp) {
        UIBackdropComposition_Destroy(g_comp);
        g_comp = NULL;
    }
}

#else // !_WIN32 — no-op everywhere else

int  UIBackdropCompanion_Enable(SDL_Window* owner, UIBackdropMaterial material,
                                UIColor tint, float tintOpacity) {
    (void)owner; (void)material; (void)tint; (void)tintOpacity; return 0;
}
void UIBackdropCompanion_Sync(SDL_Window* owner) { (void)owner; }
void UIBackdropCompanion_Disable(void) {}

#endif // _WIN32
