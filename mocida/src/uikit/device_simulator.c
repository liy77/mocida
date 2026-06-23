#include "device_simulator_internal.h"
#include <uikit/app.h>
#include <uikit/window.h>
#include <uikit/asset.h>
#include <uikit/debug.h>
#include <stdlib.h>
#include <string.h>

struct UIDeviceSimulatorState {
    UIDeviceProfile profile;
    UIDeviceOrientation orientation;
    SDL_Texture* frameTexture;
    SDL_Texture* renderTarget;
    float hostScale;
    int ownsFrameTexture;
};

static UIDeviceSimulatorState* GetState(UIWindow* window) {
    return window ? (UIDeviceSimulatorState*)window->__deviceSimulator : NULL;
}

static void GetDeviceSize(const UIDeviceSimulatorState* state, int* w, int* h) {
    if (!state || !w || !h) return;
    if (state->orientation == UI_DEVICE_ORIENTATION_LANDSCAPE) {
        *w = state->profile.height;
        *h = state->profile.width;
    } else {
        *w = state->profile.width;
        *h = state->profile.height;
    }
}

static void GetSafeArea(const UIDeviceSimulatorState* state, UIScreenInsets* out) {
    if (!state || !out) return;
    const UIDeviceProfile* p = &state->profile;
    // Landscape assumes a 90° counter-clockwise rotation from portrait.
    if (state->orientation == UI_DEVICE_ORIENTATION_LANDSCAPE) {
        out->top    = p->safeRight;
        out->bottom = p->safeLeft;
        out->left   = p->safeTop;
        out->right  = p->safeBottom;
    } else {
        out->top    = p->safeTop;
        out->bottom = p->safeBottom;
        out->left   = p->safeLeft;
        out->right  = p->safeRight;
    }
}

static int RecreateRenderTarget(UIWindow* window, UIDeviceSimulatorState* state) {
    SDL_Renderer* r = window ? window->sdlRenderer : NULL;
    if (!r || !state) return 0;
    if (state->renderTarget) {
        SDL_DestroyTexture(state->renderTarget);
        state->renderTarget = NULL;
    }
    int w = 0, h = 0;
    GetDeviceSize(state, &w, &h);
    state->renderTarget = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_TARGET, w, h);
    if (!state->renderTarget) {
        UI_ERROR(UI_CAT_RENDER, "failed to create device simulator render target: %s", SDL_GetError());
        return 0;
    }
    SDL_SetTextureScaleMode(state->renderTarget, SDL_SCALEMODE_LINEAR);
    return 1;
}

static void DestroyState(UIDeviceSimulatorState* state) {
    if (!state) return;
    if (state->renderTarget) SDL_DestroyTexture(state->renderTarget);
    if (state->frameTexture && state->ownsFrameTexture) SDL_DestroyTexture(state->frameTexture);
    free((void*)state->profile.name);
    free(state);
}

void UIWindow_EnableDeviceSimulator(UIWindow* window,
                                    const UIDeviceProfile* profile,
                                    SDL_Texture* frameTexture,
                                    int ownsFrameTexture,
                                    float hostScale,
                                    UIDeviceOrientation orientation) {
    if (!window || !profile) return;
    UIWindow_DisableDeviceSimulator(window);
    UIDeviceSimulatorState* state = (UIDeviceSimulatorState*)calloc(1, sizeof(*state));
    if (!state) return;
    state->profile = *profile;
    state->profile.name = profile->name ? strdup(profile->name) : NULL;
    state->orientation = orientation;
    state->frameTexture = frameTexture;
    state->ownsFrameTexture = ownsFrameTexture ? 1 : 0;
    state->hostScale = hostScale > 0.0f ? hostScale : 1.0f;
    if (!RecreateRenderTarget(window, state)) {
        DestroyState(state);
        return;
    }
    window->__deviceSimulator = state;
}

void UIWindow_DisableDeviceSimulator(UIWindow* window) {
    if (!window) return;
    UIDeviceSimulatorState* state = GetState(window);
    window->__deviceSimulator = NULL;
    DestroyState(state);
}

int UIWindow_IsDeviceSimulatorEnabled(UIWindow* window) {
    return GetState(window) != NULL;
}

UIDeviceSimulatorState* UIWindow_GetDeviceSimulatorState(UIWindow* window) {
    return GetState(window);
}

void UIWindow_DeviceSimulatorGetDeviceSize(UIWindow* window, int* w, int* h) {
    if (!GetState(window)) {
        if (w) *w = 0;
        if (h) *h = 0;
        return;
    }
    GetDeviceSize(GetState(window), w, h);
}

void UIWindow_DeviceSimulatorGetSafeArea(UIWindow* window, UIScreenInsets* out) {
    if (!GetState(window)) {
        if (out) memset(out, 0, sizeof(*out));
        return;
    }
    GetSafeArea(GetState(window), out);
}

SDL_Texture* UIWindow_DeviceSimulatorGetRenderTarget(UIWindow* window) {
    UIDeviceSimulatorState* state = GetState(window);
    return state ? state->renderTarget : NULL;
}

float UIWindow_DeviceSimulatorGetHostScale(UIWindow* window) {
    UIDeviceSimulatorState* state = GetState(window);
    return state ? state->hostScale : 1.0f;
}

void UIWindow_DeviceSimulatorSetOrientation(UIWindow* window,
                                            UIDeviceOrientation orientation) {
    UIDeviceSimulatorState* state = GetState(window);
    if (!state || state->orientation == orientation) return;
    state->orientation = orientation;
    RecreateRenderTarget(window, state);
}

void UIWindow_DeviceSimulatorToggleOrientation(UIWindow* window) {
    UIDeviceSimulatorState* state = GetState(window);
    if (!state) return;
    UIWindow_DeviceSimulatorSetOrientation(window,
        state->orientation == UI_DEVICE_ORIENTATION_PORTRAIT
            ? UI_DEVICE_ORIENTATION_LANDSCAPE
            : UI_DEVICE_ORIENTATION_PORTRAIT);
}

// UIApp wrappers
void UIApp_EnableDeviceSimulator(UIApp* app, const UIDeviceProfile* profile,
                                 SDL_Texture* frameTexture, float hostScale,
                                 UIDeviceOrientation orientation) {
    if (!app || !app->window) return;
    UIWindow_EnableDeviceSimulator(app->window, profile, frameTexture, 0,
                                   hostScale, orientation);
}

void UIApp_EnableDeviceSimulatorFromFile(UIApp* app, const UIDeviceProfile* profile,
                                         const char* framePath, float hostScale,
                                         UIDeviceOrientation orientation) {
    if (!app || !app->window) return;
    SDL_Texture* tex = NULL;
    if (framePath) {
        tex = UIAsset_LoadTexture(app->window->sdlRenderer, framePath);
    }
    UIWindow_EnableDeviceSimulator(app->window, profile, tex, 1,
                                   hostScale, orientation);
}

void UIApp_DisableDeviceSimulator(UIApp* app) {
    if (!app) return;
    UIWindow_DisableDeviceSimulator(app->window);
}

bool UIApp_IsDeviceSimulatorEnabled(UIApp* app) {
    return app && app->window && UIWindow_IsDeviceSimulatorEnabled(app->window);
}

const UIDeviceProfile* UIApp_GetCurrentDeviceProfile(UIApp* app) {
    UIDeviceSimulatorState* state = app && app->window ? GetState(app->window) : NULL;
    return state ? &state->profile : NULL;
}

UIDeviceOrientation UIApp_GetCurrentDeviceOrientation(UIApp* app) {
    UIDeviceSimulatorState* state = app && app->window ? GetState(app->window) : NULL;
    return state ? state->orientation : UI_DEVICE_ORIENTATION_PORTRAIT;
}

void UIApp_SetDeviceOrientation(UIApp* app, UIDeviceOrientation orientation) {
    if (!app || !app->window) return;
    UIWindow_DeviceSimulatorSetOrientation(app->window, orientation);
}

// Stub implementations for functions declared in headers but not yet fully implemented.
// These will be replaced by later tasks.

bool UIApp_GetDeviceSimulatorViewport(UIApp* app, SDL_FRect* outViewport) {
    (void)app; (void)outViewport;
    return false;
}

SDL_Texture* UIApp_GetDeviceSimulatorRenderTarget(UIApp* app) {
    (void)app;
    return NULL;
}

void UIApp_DeviceSimulatorWindowToDevice(UIApp* app, float wx, float wy,
                                         float* dx, float* dy) {
    (void)app;
    if (dx) *dx = wx;
    if (dy) *dy = wy;
}

void UIApp_DeviceSimulatorDeviceToWindow(UIApp* app, float dx, float dy,
                                         float* wx, float* wy) {
    (void)app;
    if (wx) *wx = dx;
    if (wy) *wy = dy;
}

void UIWindow_DeviceSimulatorWindowToDevice(UIWindow* window,
                                            float wx, float wy,
                                            float* dx, float* dy) {
    (void)window;
    if (dx) *dx = wx;
    if (dy) *dy = wy;
}

void UIWindow_DeviceSimulatorDeviceToWindow(UIWindow* window,
                                            float dx, float dy,
                                            float* wx, float* wy) {
    (void)window;
    if (wx) *wx = dx;
    if (wy) *wy = dy;
}

void UIWindow_DeviceSimulatorPresentFrame(UIWindow* window, int hostW, int hostH) {
    (void)window; (void)hostW; (void)hostH;
}
