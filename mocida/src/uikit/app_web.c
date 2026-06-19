#include <emscripten.h>
#include <SDL3/SDL.h>
#include <uikit/app.h>

extern void UIApp_RunOneFrame(UIApp* app, Uint32 dtMs);

static UIApp* s_webApp = NULL;
static Uint64 s_webLastTickPC = 0;
static Uint64 s_webFreq = 0;

void UIApp_ClearWebApp(UIApp* app) {
    if (s_webApp == app) s_webApp = NULL;
}

static void mocida_web_loop(void) {
    if (!s_webApp) {
        emscripten_cancel_main_loop();
        return;
    }
    if (s_webFreq == 0) s_webFreq = SDL_GetPerformanceFrequency();
    Uint64 nowPC = SDL_GetPerformanceCounter();
    Uint32 dtMs = 0;
    if (s_webLastTickPC != 0) {
        dtMs = (Uint32)((nowPC - s_webLastTickPC) * 1000ULL / s_webFreq);
    }
    s_webLastTickPC = nowPC;

    UIApp_RunOneFrame(s_webApp, dtMs);
    if (!s_webApp) return;
    if (!s_webApp->window->visible && !s_webApp->runInBackground) {
        emscripten_cancel_main_loop();
        s_webApp = NULL;
    }
}

void UIApp_RunWeb(UIApp* app) {
    s_webApp = app;
    s_webLastTickPC = SDL_GetPerformanceCounter();
    s_webFreq = SDL_GetPerformanceFrequency();
    // emscripten_set_main_loop with simulate_infinite_loop=1 does not return;
    // it throws a JavaScript exception to transfer control to the browser.
    // Consequently, any UIApp_Destroy call after UIApp_RunWeb in the host's
    // main() is unreachable on web. Cleanup happens implicitly when the user
    // navigates away or closes the page (the browser reclaims the WASM heap).
    emscripten_set_main_loop(mocida_web_loop, 0, 1);
}
