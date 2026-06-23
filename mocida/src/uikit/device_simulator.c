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
    // Landscape assumes a 90° clockwise rotation from portrait.
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
    UIDeviceSimulatorState* state = GetState(window);
    if (!state) {
        if (w) *w = 0;
        if (h) *h = 0;
        return;
    }
    GetDeviceSize(state, w, h);
}

void UIWindow_DeviceSimulatorGetSafeArea(UIWindow* window, UIScreenInsets* out) {
    UIDeviceSimulatorState* state = GetState(window);
    if (!state) {
        if (out) memset(out, 0, sizeof(*out));
        return;
    }
    GetSafeArea(state, out);
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
    if (!RecreateRenderTarget(window, state)) {
        UIWindow_DisableDeviceSimulator(window);
        return;
    }
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
    if (!app || !app->window) { if (dx) *dx = wx; if (dy) *dy = wy; return; }
    UIWindow_DeviceSimulatorWindowToDevice(app->window, wx, wy, dx, dy);
}

void UIApp_DeviceSimulatorDeviceToWindow(UIApp* app, float dx, float dy,
                                         float* wx, float* wy) {
    if (!app || !app->window) { if (wx) *wx = dx; if (wy) *wy = dy; return; }
    UIWindow_DeviceSimulatorDeviceToWindow(app->window, dx, dy, wx, wy);
}

void UIWindow_DeviceSimulatorWindowToDevice(UIWindow* window,
                                            float wx, float wy,
                                            float* dx, float* dy) {
    UIDeviceSimulatorState* state = GetState(window);
    if (!state || !dx || !dy) { if (dx) *dx = wx; if (dy) *dy = wy; return; }
    const UIDeviceProfile* p = &state->profile;
    const float scale = state->hostScale;
    const int landscape = state->orientation == UI_DEVICE_ORIENTATION_LANDSCAPE;

    float hostW = (float)p->frameW * scale;
    float hostH = (float)p->frameH * scale;
    if (landscape) { float t = hostW; hostW = hostH; hostH = t; }
    float cx = hostW * 0.5f;
    float cy = hostH * 0.5f;

    // Inverse of SDL clockwise 90° rotation: (u,v) -> (v,-u); identity for portrait.
    float fx, fy;
    if (landscape) {
        fx = p->frameW * 0.5f + (wy - cy) / scale;
        fy = p->frameH * 0.5f - (wx - cx) / scale;
    } else {
        fx = p->frameW * 0.5f + (wx - cx) / scale;
        fy = p->frameH * 0.5f + (wy - cy) / scale;
    }

    int dw = 0, dh = 0;
    GetDeviceSize(state, &dw, &dh);
    if (landscape) {
        *dx = ((float)p->frameScreenY + (float)p->frameScreenH - fy) *
              ((float)dw / (float)p->frameScreenH);
        *dy = (fx - (float)p->frameScreenX) *
              ((float)dh / (float)p->frameScreenW);
    } else {
        *dx = (fx - (float)p->frameScreenX) *
              ((float)dw / (float)p->frameScreenW);
        *dy = (fy - (float)p->frameScreenY) *
              ((float)dh / (float)p->frameScreenH);
    }
}

void UIWindow_DeviceSimulatorDeviceToWindow(UIWindow* window,
                                            float dx, float dy,
                                            float* wx, float* wy) {
    UIDeviceSimulatorState* state = GetState(window);
    if (!state || !wx || !wy) { if (wx) *wx = dx; if (wy) *wy = dy; return; }
    const UIDeviceProfile* p = &state->profile;
    const float scale = state->hostScale;
    const int landscape = state->orientation == UI_DEVICE_ORIENTATION_LANDSCAPE;
    int dw = 0, dh = 0;
    GetDeviceSize(state, &dw, &dh);

    float fx, fy;
    if (landscape) {
        fx = (float)p->frameScreenX + dy * ((float)p->frameScreenW / (float)dh);
        fy = (float)p->frameScreenY + (float)p->frameScreenH -
             dx * ((float)p->frameScreenH / (float)dw);
    } else {
        fx = (float)p->frameScreenX + dx * ((float)p->frameScreenW / (float)dw);
        fy = (float)p->frameScreenY + dy * ((float)p->frameScreenH / (float)dh);
    }

    float hostW = (float)p->frameW * scale;
    float hostH = (float)p->frameH * scale;
    if (landscape) { float t = hostW; hostW = hostH; hostH = t; }
    float cx = hostW * 0.5f;
    float cy = hostH * 0.5f;

    // SDL clockwise 90° rotation: (u,v) -> (-v, u); identity for portrait.
    if (landscape) {
        *wx = cx - (fy - p->frameH * 0.5f) * scale;
        *wy = cy + (fx - p->frameW * 0.5f) * scale;
    } else {
        *wx = cx + (fx - p->frameW * 0.5f) * scale;
        *wy = cy + (fy - p->frameH * 0.5f) * scale;
    }
}

static void DrawFallbackBezel(SDL_Renderer* r, int hostW, int hostH) {
    SDL_FRect full = { 0, 0, (float)hostW, (float)hostH };
    SDL_SetRenderDrawColor(r, 30, 30, 35, 255);
    SDL_RenderFillRect(r, &full);
    SDL_SetRenderDrawColor(r, 60, 60, 70, 255);
    SDL_RenderRect(r, &full);
}

static void ComputeScreenCorners(const UIDeviceProfile* p,
                                 float cx, float cy, float scale,
                                 int landscape,
                                 SDL_FPoint out[4]) {
    const float fcx = p->frameW * 0.5f;
    const float fcy = p->frameH * 0.5f;
    const float x0 = (float)p->frameScreenX;
    const float y0 = (float)p->frameScreenY;
    const float x1 = x0 + (float)p->frameScreenW;
    const float y1 = y0 + (float)p->frameScreenH;

    const float rx0 = (x0 - fcx) * scale;
    const float ry0 = (y0 - fcy) * scale;
    const float rx1 = (x1 - fcx) * scale;
    const float ry1 = (y1 - fcy) * scale;

    if (landscape) {
        // SDL clockwise 90°: (u,v) -> (-v, u)
        // top-left    = bottom-left of unrotated frame
        out[0].x = cx - ry1; out[0].y = cy + rx0;
        // top-right   = top-left of unrotated frame
        out[1].x = cx - ry0; out[1].y = cy + rx0;
        // bottom-right = top-right of unrotated frame
        out[2].x = cx - ry0; out[2].y = cy + rx1;
        // bottom-left  = bottom-right of unrotated frame
        out[3].x = cx - ry1; out[3].y = cy + rx1;
    } else {
        out[0].x = cx + rx0; out[0].y = cy + ry0;
        out[1].x = cx + rx1; out[1].y = cy + ry0;
        out[2].x = cx + rx1; out[2].y = cy + ry1;
        out[3].x = cx + rx0; out[3].y = cy + ry1;
    }
}

static void DrawDeviceTextureInRect(SDL_Renderer* r, SDL_Texture* tex,
                                    const SDL_FPoint corners[4]) {
    SDL_Vertex verts[4];
    for (int i = 0; i < 4; ++i) {
        verts[i].position = corners[i];
        verts[i].color.r = 1.0f; verts[i].color.g = 1.0f;
        verts[i].color.b = 1.0f; verts[i].color.a = 1.0f;
    }
    verts[0].tex_coord.x = 0.0f; verts[0].tex_coord.y = 0.0f;
    verts[1].tex_coord.x = 1.0f; verts[1].tex_coord.y = 0.0f;
    verts[2].tex_coord.x = 1.0f; verts[2].tex_coord.y = 1.0f;
    verts[3].tex_coord.x = 0.0f; verts[3].tex_coord.y = 1.0f;

    const int indices[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(r, tex, verts, 4, indices, 6);
}

void UIWindow_DeviceSimulatorPresentFrame(UIWindow* window, int hostW, int hostH) {
    UIDeviceSimulatorState* state = GetState(window);
    if (!state) return;
    SDL_Renderer* r = window->sdlRenderer;
    if (!r) return;
    if (!state->renderTarget) return;
    const UIDeviceProfile* p = &state->profile;
    const float scale = state->hostScale;
    const float cx = hostW * 0.5f;
    const float cy = hostH * 0.5f;

    SDL_FRect frameDst = {
        cx - p->frameW * scale * 0.5f,
        cy - p->frameH * scale * 0.5f,
        p->frameW * scale,
        p->frameH * scale
    };

    if (state->frameTexture) {
        const double angle = state->orientation == UI_DEVICE_ORIENTATION_LANDSCAPE ? 90.0 : 0.0;
        const SDL_FPoint center = { cx, cy };
        SDL_RenderTextureRotated(r, state->frameTexture, NULL, &frameDst,
                                 angle, &center, SDL_FLIP_NONE);
    } else {
        DrawFallbackBezel(r, hostW, hostH);
    }

    SDL_FPoint corners[4];
    ComputeScreenCorners(p, cx, cy, scale,
                         state->orientation == UI_DEVICE_ORIENTATION_LANDSCAPE,
                         corners);
    DrawDeviceTextureInRect(r, state->renderTarget, corners);
}
