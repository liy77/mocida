// backdrop_web.c — Emscripten no-op backdrop backend.
//
// Browsers do not expose an OS-level window backdrop API, so AUTO resolves to
// NONE and Apply always reports "not native". The caller (window.c) then falls
// back to the opaque/in-app rendering path.

#include <uikit/backdrop.h>
#include <SDL3/SDL.h>

UIBackdropMaterial UIBackdrop_ResolveAuto(void) {
    return UI_BACKDROP_NONE;
}

int UIBackdrop_NativeAvailable(void) {
    return 0;
}

int UIBackdrop_Apply(SDL_Window* window, UIBackdropMaterial material,
                     UIColor tint, float tintOpacity) {
    (void)window;
    (void)material;
    (void)tint;
    (void)tintOpacity;
    return 0;
}
