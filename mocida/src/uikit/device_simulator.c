#include "device_simulator_internal.h"
#include <uikit/app.h>
#include <uikit/window.h>
#include <uikit/asset.h>
#include <uikit/debug.h>
#include <uikit/font.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct UIDeviceSimulatorState {
    UIDeviceProfile profile;
    /** A screenshot was asked for: taken from the render target next frame. */
    int shotPending;
    /** When the screenshot flash ends (SDL ticks). */
    Uint64 flashUntil;
    UIDeviceOrientation orientation;
    SDL_Texture* frameTexture;
    SDL_Texture* renderTarget;
    /** The screen's shape (alpha = mask), cut to the screen rect: rounds the
     *  content's corners. NULL with a frame that has no mask. */
    SDL_Texture* maskTexture;
    float hostScale;
    /** Window pixels per point: the content renders at this density. */
    float density;
    int ownsFrameTexture;
    /** The frame's screen is transparent (a real device frame): draw the
     *  content first and the frame over it (camera island, bezel). */
    int frameOnTop;
};

static float WindowDensity(UIWindow* window) {
    float d = (window && window->sdlWindow) ? SDL_GetWindowPixelDensity(window->sdlWindow) : 1.0f;
    return SDL_clamp(d > 0.0f ? d : 1.0f, 1.0f, 3.0f);
}

// ── Toolbar ──────────────────────────────────────────────────────────────
// A bar above the device (like the iOS Simulator's title bar): the device
// name, orientation and scale on the left; Screenshot, Rotate, Restart and
// Zoom on the right. It outlives a re-enable (a host re-enables the
// simulator on every rotate / scale), so its settings are kept here.
#define SIM_TOOLBAR_H 44

static int g_toolbar = 0;
static UIDeviceSimulatorActionFn g_actionFn = NULL;
static void* g_actionUd = NULL;
static int g_hover = 0, g_pressed = 0;
static char g_lastShot[1024];

typedef struct {
    int action;
    const char* svg;
} SimButton;

#define SIM_SVG(body) "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' width='24' height='24'>" body "</svg>"
static const SimButton kSimButtons[] = {
    { UI_SIM_ACTION_SCREENSHOT, SIM_SVG(
        "<path fill='none' stroke='#e5e5ea' stroke-width='1.8' stroke-linejoin='round' "
        "d='M4 8h3l1.6-2.4h6.8L17 8h3v11H4z'/><circle cx='12' cy='13.2' r='3.4' fill='none' stroke='#e5e5ea' stroke-width='1.8'/>") },
    { UI_SIM_ACTION_ROTATE, SIM_SVG(
        "<rect x='8' y='4' width='8' height='14' rx='1.8' fill='none' stroke='#e5e5ea' stroke-width='1.8' transform='rotate(-30 12 11)'/>"
        "<path fill='none' stroke='#e5e5ea' stroke-width='1.8' stroke-linecap='round' d='M4.5 15.5a8.5 8.5 0 0 0 7 5'/>"
        "<path fill='none' stroke='#e5e5ea' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round' d='M9.5 18.6l2 1.9-2.4 1.4'/>") },
    { UI_SIM_ACTION_RESTART, SIM_SVG(
        "<path fill='none' stroke='#e5e5ea' stroke-width='1.8' stroke-linecap='round' d='M19 12a7 7 0 1 1-2.05-4.95'/>"
        "<path fill='none' stroke='#e5e5ea' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round' d='M17.5 3.8v3.6h-3.6'/>") },
    { UI_SIM_ACTION_ZOOM, SIM_SVG(
        "<path fill='none' stroke='#e5e5ea' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round' "
        "d='M4 9V4h5M20 9V4h-5M4 15v5h5M20 15v5h-5'/>") },
};
#define SIM_BUTTONS ((int)(sizeof(kSimButtons) / sizeof(kSimButtons[0])))
#define SIM_BTN_W 34
#define SIM_BTN_H 30

static SDL_Renderer* g_iconRenderer = NULL;
static SDL_Texture* g_icons[SIM_BUTTONS];
static SDL_Texture* g_labelTex = NULL;
static char g_labelText[256];

void UIApp_SetDeviceSimulatorToolbar(UIApp* app, int show, UIDeviceSimulatorActionFn fn, void* userdata) {
    (void)app;
    g_toolbar = show ? 1 : 0;
    g_actionFn = fn;
    g_actionUd = userdata;
}

int UIApp_DeviceSimulatorToolbarHeight(void) {
    return g_toolbar ? SIM_TOOLBAR_H : 0;
}

void UIApp_DeviceSimulatorHostDisplaySize(UIApp* app, int* w, int* h) {
    SDL_Rect r = {0};
    SDL_DisplayID id = (app && app->window && app->window->sdlWindow)
        ? SDL_GetDisplayForWindow(app->window->sdlWindow) : SDL_GetPrimaryDisplay();
    if (!id || !SDL_GetDisplayUsableBounds(id, &r)) SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &r);
    if (w) *w = r.w;
    if (h) *h = r.h;
}

void UIApp_DeviceSimulatorTakeScreenshot(UIApp* app) {
    UIDeviceSimulatorState* state = (app && app->window) ? (UIDeviceSimulatorState*)app->window->__deviceSimulator : NULL;
    if (state) state->shotPending = 1;
}

const char* UIApp_DeviceSimulatorLastScreenshot(void) {
    return g_lastShot;
}

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
    // Landscape is the portrait device turned 90° clockwise (as the frame is
    // drawn): its top edge (notch / island) ends up on the right and the home
    // bar on the left.
    if (state->orientation == UI_DEVICE_ORIENTATION_LANDSCAPE) {
        out->top    = p->safeLeft;
        out->right  = p->safeTop;
        out->bottom = p->safeRight;
        out->left   = p->safeBottom;
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
    state->density = WindowDensity(window);
    state->renderTarget = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_TARGET,
                                            (int)(w * state->density + 0.5f),
                                            (int)(h * state->density + 0.5f));
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
    if (state->maskTexture) SDL_DestroyTexture(state->maskTexture);
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

float UIWindow_DeviceSimulatorGetDensity(UIWindow* window) {
    UIDeviceSimulatorState* state = GetState(window);
    return state ? state->density : 1.0f;
}

void UIWindow_DeviceSimulatorSyncDensity(UIWindow* window) {
    UIDeviceSimulatorState* state = GetState(window);
    if (!state || WindowDensity(window) == state->density) return;
    if (!RecreateRenderTarget(window, state)) UIWindow_DisableDeviceSimulator(window);
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

// A texture from `path`, shrunk to at most maxW x maxH: frame images are
// several times the size they're shown at, and downscaling once on load
// keeps their thin edges clean (the GPU draw has no mipmaps).
static SDL_Texture* LoadShrunk(SDL_Renderer* r, SDL_Surface* s, int maxW, int maxH) {
    if (!s) return NULL;
    if (maxW > 0 && maxH > 0 && (s->w > maxW || s->h > maxH)) {
        SDL_Surface* scaled = SDL_ScaleSurface(s, maxW, maxH, SDL_SCALEMODE_LINEAR);
        if (scaled) {
            SDL_DestroySurface(s);
            s = scaled;
        }
    }
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    SDL_DestroySurface(s);
    if (t) SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    return t;
}

// The screen mask (white = screen) cut to the screen rect and turned into
// alpha, so ApplyMask can multiply the content's alpha by it.
static SDL_Texture* LoadMask(SDL_Renderer* r, const char* path, const UIDeviceProfile* p) {
    SDL_Surface* src = UIAsset_LoadSurface(path);
    if (!src || p->frameW <= 0 || p->frameH <= 0) {
        if (src) SDL_DestroySurface(src);
        return NULL;
    }
    SDL_Surface* rgba = SDL_ConvertSurface(src, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(src);
    if (!rgba) return NULL;
    const float kx = (float)rgba->w / (float)p->frameW;
    const float ky = (float)rgba->h / (float)p->frameH;
    SDL_Rect cut = { (int)(p->frameScreenX * kx + 0.5f), (int)(p->frameScreenY * ky + 0.5f),
                     (int)(p->frameScreenW * kx + 0.5f), (int)(p->frameScreenH * ky + 0.5f) };
    SDL_Surface* screen = SDL_CreateSurface(cut.w, cut.h, SDL_PIXELFORMAT_RGBA32);
    if (!screen) { SDL_DestroySurface(rgba); return NULL; }
    SDL_SetSurfaceBlendMode(rgba, SDL_BLENDMODE_NONE);
    SDL_BlitSurface(rgba, &cut, screen, NULL);
    SDL_DestroySurface(rgba);
    for (int y = 0; y < screen->h; y++) {
        Uint8* row = (Uint8*)screen->pixels + y * screen->pitch;
        for (int x = 0; x < screen->w; x++) {
            Uint8* px = row + x * 4;
            px[3] = px[0];
            px[0] = px[1] = px[2] = 255;
        }
    }
    SDL_Texture* t = LoadShrunk(r, screen, p->frameScreenW * 2, p->frameScreenH * 2);
    if (!t) return NULL;
    // alpha(dst) *= alpha(mask); the content's color is left alone.
    const SDL_BlendMode multiplyAlpha = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    if (!SDL_SetTextureBlendMode(t, multiplyAlpha)) {
        UI_WARN(UI_CAT_RENDER, "device simulator: this renderer can't mask the screen corners");
        SDL_DestroyTexture(t);
        return NULL;
    }
    return t;
}

void UIApp_EnableDeviceSimulatorFrames(UIApp* app, const UIDeviceProfile* profile,
                                       const char* framePath, const char* maskPath,
                                       float hostScale, UIDeviceOrientation orientation) {
    if (!app || !app->window || !profile) return;
    SDL_Renderer* r = app->window->sdlRenderer;
    SDL_Texture* frame = framePath
        ? LoadShrunk(r, UIAsset_LoadSurface(framePath), profile->frameW * 2, profile->frameH * 2)
        : NULL;
    UIWindow_EnableDeviceSimulator(app->window, profile, frame, 1, hostScale, orientation);
    UIDeviceSimulatorState* state = GetState(app->window);
    if (!state) return;
    state->frameOnTop = frame != NULL;
    if (maskPath) state->maskTexture = LoadMask(r, maskPath, profile);
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

    int rawW = 0, rawH = 0;
    if (window && window->sdlWindow) SDL_GetWindowSize(window->sdlWindow, &rawW, &rawH);
    float hostW = (float)rawW;
    float hostH = (float)rawH;
    const float top = (float)UIApp_DeviceSimulatorToolbarHeight();
    float cx = hostW * 0.5f;
    float cy = top + (hostH - top) * 0.5f;

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

    int rawW = 0, rawH = 0;
    if (window && window->sdlWindow) SDL_GetWindowSize(window->sdlWindow, &rawW, &rawH);
    float hostW = (float)rawW;
    float hostH = (float)rawH;
    const float top = (float)UIApp_DeviceSimulatorToolbarHeight();
    float cx = hostW * 0.5f;
    float cy = top + (hostH - top) * 0.5f;

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

// Rounds the content to the screen's shape: the mask multiplies the render
// target's alpha. The target is in device orientation; in landscape the
// (portrait) mask is turned to match the way ComputeScreenCorners maps it.
static void ApplyMask(SDL_Renderer* r, UIDeviceSimulatorState* state) {
    float tw = 0.0f, th = 0.0f;
    SDL_GetTextureSize(state->renderTarget, &tw, &th);
    SDL_Texture* prev = SDL_GetRenderTarget(r);
    SDL_SetRenderTarget(r, state->renderTarget);
    SDL_SetRenderScale(r, 1.0f, 1.0f);
    const int landscape = state->orientation == UI_DEVICE_ORIENTATION_LANDSCAPE;
    const float pos[4][2] = { {0, 0}, {tw, 0}, {tw, th}, {0, th} };
    const float uvPortrait[4][2]  = { {0, 0}, {1, 0}, {1, 1}, {0, 1} };
    const float uvLandscape[4][2] = { {0, 1}, {0, 0}, {1, 0}, {1, 1} };
    SDL_Vertex v[4];
    for (int i = 0; i < 4; i++) {
        v[i].position.x = pos[i][0];
        v[i].position.y = pos[i][1];
        v[i].color.r = v[i].color.g = v[i].color.b = v[i].color.a = 1.0f;
        v[i].tex_coord.x = landscape ? uvLandscape[i][0] : uvPortrait[i][0];
        v[i].tex_coord.y = landscape ? uvLandscape[i][1] : uvPortrait[i][1];
    }
    const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(r, state->maskTexture, v, 4, idx, 6);
    SDL_SetRenderTarget(r, prev);
}

// ── Screenshot, flash and toolbar drawing ─────────────────────────────────

// Saves the device screen (the render target, before the corner mask) to the
// Desktop (or MOCIDA_SCREENSHOT_DIR), named like the iOS Simulator's.
static void TakeScreenshot(UIWindow* window, UIDeviceSimulatorState* state) {
    SDL_Renderer* r = window->sdlRenderer;
    SDL_Texture* prev = SDL_GetRenderTarget(r);
    SDL_SetRenderTarget(r, state->renderTarget);
    SDL_Surface* shot = SDL_RenderReadPixels(r, NULL);
    SDL_SetRenderTarget(r, prev);
    if (!shot) {
        UI_WARN(UI_CAT_RENDER, "simulator screenshot failed: %s", SDL_GetError());
        return;
    }
    // MOCIDA_SCREENSHOT_DIR (ending in a separator) overrides the Desktop.
    const char* dir = SDL_getenv("MOCIDA_SCREENSHOT_DIR");
    if (!dir || !*dir) dir = SDL_GetUserFolder(SDL_FOLDER_DESKTOP);
    char stamp[64];
    time_t now = time(NULL);
    strftime(stamp, sizeof(stamp), "%Y-%m-%d at %H.%M.%S", localtime(&now));
    snprintf(g_lastShot, sizeof(g_lastShot), "%sSimulator Screenshot - %s - %s.png",
             dir ? dir : "", state->profile.name ? state->profile.name : "Device", stamp);
    if (IMG_SavePNG(shot, g_lastShot)) {
        UI_INFO(UI_CAT_RENDER, "simulator screenshot saved to %s", g_lastShot);
        state->flashUntil = SDL_GetTicks() + 320;
        if (g_actionFn) g_actionFn(UI_SIM_ACTION_SCREENSHOT, g_actionUd);
    } else {
        UI_WARN(UI_CAT_RENDER, "simulator screenshot: couldn't write %s: %s", g_lastShot, SDL_GetError());
        g_lastShot[0] = '\0';
    }
    SDL_DestroySurface(shot);
}

static void DrawFlash(SDL_Renderer* r, UIDeviceSimulatorState* state, const SDL_FPoint corners[4]) {
    const Uint64 now = SDL_GetTicks();
    if (now >= state->flashUntil) return;
    const float a = 0.85f * (float)(state->flashUntil - now) / 320.0f;
    SDL_Vertex v[4];
    for (int i = 0; i < 4; i++) {
        v[i].position = corners[i];
        v[i].color.r = v[i].color.g = v[i].color.b = 1.0f;
        v[i].color.a = a;
        v[i].tex_coord.x = v[i].tex_coord.y = 0.0f;
    }
    const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(r, NULL, v, 4, idx, 6);
}

static float ButtonX(int hostW, int i) {
    return (float)(hostW - 10 - SIM_BUTTONS * SIM_BTN_W - (SIM_BUTTONS - 1) * 4 + i * (SIM_BTN_W + 4));
}

// 1-based index of the toolbar button under (x, y), 0 for none.
static int ButtonAt(UIWindow* window, float x, float y) {
    int hostW = 0, hostH = 0;
    if (window && window->sdlWindow) SDL_GetWindowSize(window->sdlWindow, &hostW, &hostH);
    const float by = (SIM_TOOLBAR_H - SIM_BTN_H) * 0.5f;
    if (y < by || y >= by + SIM_BTN_H) return 0;
    for (int i = 0; i < SIM_BUTTONS; i++) {
        const float bx = ButtonX(hostW, i);
        if (x >= bx && x < bx + SIM_BTN_W) return i + 1;
    }
    return 0;
}

static SDL_Texture* IconTexture(SDL_Renderer* r, int i, int px) {
    if (g_iconRenderer != r) {
        memset(g_icons, 0, sizeof(g_icons));
        g_labelTex = NULL;
        g_labelText[0] = '\0';
        g_iconRenderer = r;
    }
    if (!g_icons[i]) {
        const char* svg = kSimButtons[i].svg;
        SDL_IOStream* io = SDL_IOFromConstMem(svg, strlen(svg));
        SDL_Surface* surf = io ? IMG_LoadSizedSVG_IO(io, px, px) : NULL;
        if (io) SDL_CloseIO(io);
        if (surf) {
            g_icons[i] = SDL_CreateTextureFromSurface(r, surf);
            SDL_DestroySurface(surf);
            if (g_icons[i]) SDL_SetTextureScaleMode(g_icons[i], SDL_SCALEMODE_LINEAR);
        }
    }
    return g_icons[i];
}

// "iPhone 17 Pro  ·  Portrait  ·  75%", rendered at the window's density.
static void DrawLabel(SDL_Renderer* r, UIWindow* window, UIDeviceSimulatorState* state, float maxW) {
    char text[256];
    // Named after the screen as shown: some frames (iPads) stand sideways.
    int sw = 0, sh = 0;
    GetDeviceSize(state, &sw, &sh);
    snprintf(text, sizeof(text), "%s  \xC2\xB7  %s  \xC2\xB7  %d%%",
             state->profile.name ? state->profile.name : "Device",
             sw > sh ? "Landscape" : "Portrait",
             (int)(state->hostScale * 100.0f + 0.5f));
    const float d = SDL_clamp(SDL_GetWindowPixelDensity(window->sdlWindow), 1.0f, 3.0f);
    if (!g_labelTex || strcmp(text, g_labelText) != 0) {
        if (g_labelTex) SDL_DestroyTexture(g_labelTex);
        g_labelTex = NULL;
        snprintf(g_labelText, sizeof(g_labelText), "%s", text);
        static TTF_Font* font = NULL;
        static float fontD = 0.0f;
        if (!font || fontD != d) {
            if (font) TTF_CloseFont(font);
            font = TTF_OpenFont(UIGetFont(NULL), 12.5f * d);
            fontD = d;
        }
        if (font) {
            const SDL_Color c = { 229, 229, 234, 255 };
            SDL_Surface* surf = TTF_RenderText_Blended(font, text, 0, c);
            if (surf) {
                g_labelTex = SDL_CreateTextureFromSurface(r, surf);
                SDL_DestroySurface(surf);
                if (g_labelTex) SDL_SetTextureScaleMode(g_labelTex, SDL_SCALEMODE_LINEAR);
            }
        }
    }
    if (!g_labelTex) return;
    float tw = 0.0f, th = 0.0f;
    SDL_GetTextureSize(g_labelTex, &tw, &th);
    tw /= d;
    th /= d;
    if (tw > maxW) return; // no room next to the buttons
    SDL_FRect dst = { 14.0f, floorf((SIM_TOOLBAR_H - th) * 0.5f), tw, th };
    SDL_RenderTexture(r, g_labelTex, NULL, &dst);
}

static void DrawToolbar(SDL_Renderer* r, UIWindow* window, UIDeviceSimulatorState* state, int hostW) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_FRect bar = { 0, 0, (float)hostW, SIM_TOOLBAR_H };
    SDL_SetRenderDrawColor(r, 37, 37, 40, 255);
    SDL_RenderFillRect(r, &bar);
    SDL_FRect line = { 0, SIM_TOOLBAR_H - 1, (float)hostW, 1 };
    SDL_SetRenderDrawColor(r, 58, 58, 60, 255);
    SDL_RenderFillRect(r, &line);

    const float d = SDL_clamp(SDL_GetWindowPixelDensity(window->sdlWindow), 1.0f, 3.0f);
    const float by = (SIM_TOOLBAR_H - SIM_BTN_H) * 0.5f;
    for (int i = 0; i < SIM_BUTTONS; i++) {
        const float bx = ButtonX(hostW, i);
        if (g_hover == i + 1 || g_pressed == i + 1) {
            SDL_FRect bg = { bx, by, SIM_BTN_W, SIM_BTN_H };
            SDL_SetRenderDrawColor(r, 255, 255, 255, g_pressed == i + 1 ? 46 : 24);
            SDL_RenderFillRect(r, &bg);
        }
        SDL_Texture* icon = IconTexture(r, i, (int)(18 * d));
        if (icon) {
            SDL_FRect dst = { bx + (SIM_BTN_W - 18) * 0.5f, by + (SIM_BTN_H - 18) * 0.5f, 18, 18 };
            SDL_RenderTexture(r, icon, NULL, &dst);
        }
    }
    DrawLabel(r, window, state, ButtonX(hostW, 0) - 24.0f);
}

static void Fire(UIWindow* window, int action) {
    UIDeviceSimulatorState* state = GetState(window);
    if (!state) return;
    if (action == UI_SIM_ACTION_SCREENSHOT) {
        state->shotPending = 1;
    } else if (g_actionFn) {
        g_actionFn(action, g_actionUd);
    } else if (action == UI_SIM_ACTION_ROTATE) {
        UIWindow_DeviceSimulatorToggleOrientation(window);
        state = GetState(window);
        if (!state) return;
        const UIDeviceProfile* p = &state->profile;
        const int land = state->orientation == UI_DEVICE_ORIENTATION_LANDSCAPE;
        SDL_SetWindowSize(window->sdlWindow,
                          (int)((land ? p->frameH : p->frameW) * state->hostScale),
                          (int)((land ? p->frameW : p->frameH) * state->hostScale) + UIApp_DeviceSimulatorToolbarHeight());
    }
}

int UIWindow_DeviceSimulatorToolbarEvent(UIWindow* window, int kind, float x, float y) {
    if (!g_toolbar || !GetState(window)) return 0;
    const int over = y < SIM_TOOLBAR_H;
    switch (kind) {
        case 0: // motion
            g_hover = over ? ButtonAt(window, x, y) : 0;
            return over && !g_pressed;
        case 1: // press
            if (!over) return 0;
            g_pressed = ButtonAt(window, x, y);
            return 1;
        default: { // release
            if (!g_pressed) return 0;
            const int b = g_pressed;
            g_pressed = 0;
            if (ButtonAt(window, x, y) == b) Fire(window, kSimButtons[b - 1].action);
            return 1;
        }
    }
}

void UIWindow_DeviceSimulatorPresentFrame(UIWindow* window, int hostW, int hostH) {
    UIDeviceSimulatorState* state = GetState(window);
    if (!state) return;
    SDL_Renderer* r = window->sdlRenderer;
    if (!r) return;
    if (!state->renderTarget) return;
    if (state->shotPending) {
        state->shotPending = 0;
        TakeScreenshot(window, state);
    }
    const UIDeviceProfile* p = &state->profile;
    const float scale = state->hostScale;
    const float top = (float)UIApp_DeviceSimulatorToolbarHeight();
    const float cx = hostW * 0.5f;
    const float cy = top + (hostH - top) * 0.5f;

    SDL_FRect frameDst = {
        cx - p->frameW * scale * 0.5f,
        cy - p->frameH * scale * 0.5f,
        p->frameW * scale,
        p->frameH * scale
    };

    // Turned about the frame's own middle (SDL takes the center relative to
    // the destination rect; NULL is its middle, which is the window's).
    const double angle = state->orientation == UI_DEVICE_ORIENTATION_LANDSCAPE ? 90.0 : 0.0;
    SDL_FPoint corners[4];
    ComputeScreenCorners(p, cx, cy, scale,
                         state->orientation == UI_DEVICE_ORIENTATION_LANDSCAPE,
                         corners);

    if (state->frameOnTop) {
        if (state->maskTexture) ApplyMask(r, state);
        DrawDeviceTextureInRect(r, state->renderTarget, corners);
        SDL_RenderTextureRotated(r, state->frameTexture, NULL, &frameDst,
                                 angle, NULL, SDL_FLIP_NONE);
    } else {
        if (state->frameTexture) {
            SDL_RenderTextureRotated(r, state->frameTexture, NULL, &frameDst,
                                     angle, NULL, SDL_FLIP_NONE);
        } else {
            DrawFallbackBezel(r, hostW, hostH);
        }
        DrawDeviceTextureInRect(r, state->renderTarget, corners);
    }
    DrawFlash(r, state, corners);
    if (g_toolbar) DrawToolbar(r, window, state, hostW);
}
