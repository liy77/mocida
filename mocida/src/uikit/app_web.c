#include <emscripten.h>
#include <emscripten/eventloop.h>
#include <SDL3/SDL.h>
#include <uikit/app.h>

extern void UIApp_RunOneFrame(UIApp* app, Uint32 dtMs);

static UIApp* s_webApp = NULL;
static Uint64 s_webLastTickPC = 0;
static Uint64 s_webFreq = 0;
static double s_accumMs = 0.0;

void UIApp_ClearWebApp(UIApp* app) {
    if (s_webApp == app) s_webApp = NULL;
}

static void mocida_web_loop(void) {
    if (!s_webApp) {
        emscripten_cancel_main_loop();
        return;
    }
    if (s_webFreq == 0) s_webFreq = SDL_GetPerformanceFrequency();

    const Uint64 nowPC = SDL_GetPerformanceCounter();
    Uint32 dtMs = 0;
    if (s_webLastTickPC != 0) {
        dtMs = (Uint32)((nowPC - s_webLastTickPC) * 1000ULL / s_webFreq);
    }
    s_webLastTickPC = nowPC;

    // The browser drives this callback (typically via requestAnimationFrame,
    // capped at the display refresh rate). Respect UIApp_SetTargetFPS by
    // accumulating time and only rendering when enough time has passed for
    // the requested cap. This keeps the measured FPS stable and avoids the
    // drift caused by simple frame skipping.
    int targetFps = s_webApp->targetFps;
    if (targetFps > 0) {
        const double targetMs = 1000.0 / (double)targetFps;
        s_accumMs += (double)dtMs;
        if (s_accumMs < targetMs) {
            return;  // not enough time elapsed for the next rendered frame
        }
        // Render one frame and consume one target interval. Cap the
        // accumulator to two intervals so a long stall doesn't produce a
        // burst of catch-up frames.
        s_accumMs -= targetMs;
        if (s_accumMs > targetMs * 2.0) {
            s_accumMs = targetMs * 2.0;
        }
        dtMs = (Uint32)targetMs;  // stable delta for animations
    } else {
        s_accumMs = 0.0;
    }

    UIApp_RunOneFrame(s_webApp, dtMs);
    if (!s_webApp) return;
    // The browser canvas is the window. A desktop "hidden" flag must not
    // cancel the page loop, or the first frame leaves a blank canvas.
    if (!s_webApp->window->visible) {
        s_webApp->window->visible = 1;
    }
    if (!s_webApp->window->visible && !s_webApp->runInBackground) {
        emscripten_cancel_main_loop();
        s_webApp = NULL;
    }
}

void UIApp_RunWeb(UIApp* app) {
    s_webApp = app;
    s_webLastTickPC = SDL_GetPerformanceCounter();
    s_accumMs = 0.0;
    s_webFreq = SDL_GetPerformanceFrequency();
    // simulate_infinite_loop=1 unwinds via a JS exception that Rust +
    // -fwasm-exceptions can swallow, leaving a scheduled loop that never
    // runs (black canvas). Schedule the loop, then unwind with the
    // Emscripten helper that is safe under wasm exceptions.
    emscripten_set_main_loop(mocida_web_loop, 0, 0);
    emscripten_unwind_to_js_event_loop();
}
