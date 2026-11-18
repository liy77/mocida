#ifndef UIKIT_UPSCALE_H
#define UIKIT_UPSCALE_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DLSS, FSR, Apple MetalFX and a custom upscaler library.
 *
 * MetalFX is the system upscaler on macOS 13 and later. It needs no
 * package: `UIUpscale_Available(UI_UPSCALE_METALFX)` is 1 when the GPU
 * supports it, and `UIWindow_Render` encodes a spatial scaler before
 * present.
 *
 * Mocidad does not ship NVIDIA NGX or AMD FidelityFX. The game installs
 * those libraries (or imports its own). A custom library may export:
 *
 *   int Mocidad_Upscale(int quality, float sharpness,
 *                       int src_w, int src_h, int dst_w, int dst_h);
 */

enum UIUpscaleMode {
    UI_UPSCALE_OFF = 0,
    UI_UPSCALE_DLSS = 1,
    UI_UPSCALE_FSR = 2,
    UI_UPSCALE_CUSTOM = 3,
    /** Apple MetalFX spatial scaler. macOS only. No library to install. */
    UI_UPSCALE_METALFX = 4,
};

enum UIUpscaleQuality {
    UI_UPSCALE_NATIVE = 0,
    UI_UPSCALE_ULTRA_QUALITY = 1,
    UI_UPSCALE_QUALITY = 2,
    UI_UPSCALE_BALANCED = 3,
    UI_UPSCALE_PERFORMANCE = 4,
    UI_UPSCALE_ULTRA_PERFORMANCE = 5,
};

/** Loads `path` as the library for `mode`. Returns 1 when a known entry exists. */
int UIUpscale_Load(const char* path, int mode);

void UIUpscale_Unload(int mode);

/** 1 when that mode's library is loaded and recognized. */
int UIUpscale_Available(int mode);

/**
 * Selects the mode and quality. `sharpness` is 0..1.
 * Display size is the window. The internal size is written for the renderer.
 * A custom library's `Mocidad_Upscale` is called with those sizes.
 * Returns 1 when the mode is off, or its library is available.
 */
int UIUpscale_Configure(int mode, int quality, float sharpness, int display_w, int display_h);

void UIUpscale_RenderSize(int* w, int* h);

/** Current mode (`UI_UPSCALE_OFF` until configured). */
int UIUpscale_Mode(void);

/**
 * When MetalFX is the active mode, encode a spatial upscale of the
 * current frame before present. Other modes are a no-op here: their
 * vendor library runs inside the renderer's own command buffer.
 */
void UIUpscale_ApplyFrame(SDL_Renderer* renderer);
float UIUpscale_Scale(void);
const char* UIUpscale_Error(void);
const char* UIUpscale_Library(int mode);

#ifdef __cplusplus
}
#endif

#endif
