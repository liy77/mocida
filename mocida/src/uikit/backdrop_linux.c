// backdrop_linux.c — KDE Plasma (KWin) window blur.
//
// Two transports:
//   * X11  — the _KDE_NET_WM_BLUR_BEHIND_REGION property. An empty region
//            requests a blur of the whole window. Implemented here in full,
//            using libX11 loaded at runtime (no hard link dependency: SDL3
//            already pulls X11 in, and we resolve the three symbols we need
//            via SDL_LoadObject so non-X11 builds still link).
//   * Wayland — KWin's org_kde_kwin_blur protocol. Binding it requires the
//            wayland-scanner-generated marshalling code; until that is wired,
//            native-Wayland sessions gracefully fall back (return 0) to the
//            in-app Glass rendering. Detection still happens so AUTO resolves
//            correctly on KDE.
//
// Outside Linux this translation unit is empty.

#if defined(__linux__) && !defined(__APPLE__)

#include <uikit/backdrop.h>
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

// ---- minimal Xlib surface (avoid including Xlib.h / linking libX11) ----
typedef struct _XDisplay XDisplay;
typedef unsigned long XID;
typedef XID XWindow;
typedef unsigned long XAtom;

typedef XAtom (*XInternAtom_t)(XDisplay*, const char*, int);
typedef int   (*XChangeProperty_t)(XDisplay*, XWindow, XAtom, XAtom, int, int,
                                   const unsigned char*, int);
typedef int   (*XDeleteProperty_t)(XDisplay*, XWindow, XAtom);
typedef int   (*XFlush_t)(XDisplay*);

#define MOCIDA_XA_CARDINAL 6   /* X.h: XA_CARDINAL */
#define MOCIDA_PropModeReplace 0

static struct {
    int loaded;
    int ok;
    SDL_SharedObject* lib;
    XInternAtom_t     InternAtom;
    XChangeProperty_t ChangeProperty;
    XDeleteProperty_t DeleteProperty;
    XFlush_t          Flush;
} g_x11;

static void load_x11(void) {
    if (g_x11.loaded) return;
    g_x11.loaded = 1;
    g_x11.lib = SDL_LoadObject("libX11.so.6");
    if (!g_x11.lib) g_x11.lib = SDL_LoadObject("libX11.so");
    if (!g_x11.lib) return;
    g_x11.InternAtom     = (XInternAtom_t)    SDL_LoadFunction(g_x11.lib, "XInternAtom");
    g_x11.ChangeProperty = (XChangeProperty_t)SDL_LoadFunction(g_x11.lib, "XChangeProperty");
    g_x11.DeleteProperty = (XDeleteProperty_t)SDL_LoadFunction(g_x11.lib, "XDeleteProperty");
    g_x11.Flush          = (XFlush_t)         SDL_LoadFunction(g_x11.lib, "XFlush");
    g_x11.ok = g_x11.InternAtom && g_x11.ChangeProperty &&
               g_x11.DeleteProperty && g_x11.Flush;
}

static int env_has(const char* name, const char* needle) {
    const char* v = getenv(name);
    return v && needle && strstr(v, needle) != NULL;
}

static int is_kde(void) {
    return env_has("XDG_CURRENT_DESKTOP", "KDE")
        || env_has("XDG_SESSION_DESKTOP", "KDE")
        || getenv("KDE_FULL_SESSION") != NULL
        || getenv("KDE_SESSION_VERSION") != NULL;
}

static int is_wayland(void) {
    return env_has("XDG_SESSION_TYPE", "wayland") || getenv("WAYLAND_DISPLAY") != NULL;
}

UIBackdropMaterial UIBackdrop_ResolveAuto(void) {
    return is_kde() ? UI_BACKDROP_KDE_BLUR_WINDOW : UI_BACKDROP_NONE;
}

int UIBackdrop_NativeAvailable(void) {
    return is_kde();
}

// X11: set/clear _KDE_NET_WM_BLUR_BEHIND_REGION (empty region = whole window).
static int apply_x11(SDL_Window* window, int enable) {
    load_x11();
    if (!g_x11.ok) return 0;

    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    XDisplay* dpy = (XDisplay*)SDL_GetPointerProperty(
        props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
    XWindow win = (XWindow)SDL_GetNumberProperty(
        props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    if (!dpy || !win) return 0;

    XAtom atom = g_x11.InternAtom(dpy, "_KDE_NET_WM_BLUR_BEHIND_REGION", 0);
    if (!atom) return 0;

    if (enable) {
        // Zero rectangles => blur the entire window surface.
        g_x11.ChangeProperty(dpy, win, atom, MOCIDA_XA_CARDINAL, 32,
                             MOCIDA_PropModeReplace, (const unsigned char*)NULL, 0);
    } else {
        g_x11.DeleteProperty(dpy, win, atom);
    }
    g_x11.Flush(dpy);
    return 1;
}

int UIBackdrop_Apply(SDL_Window* window, UIBackdropMaterial material,
                     UIColor tint, float tintOpacity) {
    (void)tint; (void)tintOpacity;
    if (!window) return 0;

    const int enable = (material != UI_BACKDROP_NONE);

    if (is_wayland()) {
        // org_kde_kwin_blur not yet bound — fall back to in-app rendering.
        // (Detection above keeps AUTO correct; the host paints the Glass tint.)
        return 0;
    }
    return apply_x11(window, enable);
}

#endif // __linux__ && !__APPLE__
