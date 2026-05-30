#ifndef UIKIT_IMAGE_H
#define UIKIT_IMAGE_H

#include <uikit/widget.h>
#include <SDL3_image/SDL_image.h>
#include <uikit/color.h>

/**
 * UIFillMode enumeration representing different fill modes for images.
 * It includes options for no fill, stretch, scale, tile, center,
 * fit, fit width, fit height, and cover.
 * @enum UIFillMode
 */
typedef enum {
    FILL_NONE = 0,
    FILL_STRETCH,
    FILL_SCALE,
    FILL_TILE,
    FILL_CENTER,
    FILL_FIT,
    FILL_FIT_WIDTH,
    FILL_FIT_HEIGHT,
    FILL_COVER
} UIFillMode;

/**
 * UIImageLoadState enumeration representing the loading state of an image.
 * It includes options for success, failure, and in progress.
 * @enum UIImageLoadState
 */
typedef enum {
    IMAGE_LOAD_SUCCESS,
    IMAGE_LOAD_FAILURE,
    IMAGE_LOAD_IN_PROGRESS
} UIImageLoadState;

/**
 * UIImage structure representing an image widget.
 * It contains properties for margins, radius, border width,
 * source, loaded state, animation state,
 * nine-slice mode, and texture.
 */
typedef struct {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_IMAGE). */

    float marginLeft;          /**< Left outer margin. */
    float marginTop;           /**< Top outer margin. */
    float marginRight;         /**< Right outer margin. */
    float marginBottom;        /**< Bottom outer margin. */
    float radius;              /**< Corner radius for the rendered image. */
    float borderWidth;         /**< Border thickness (pixels). */

    char* source;              /**< Heap-owned path to the source file. */
    UIImageLoadState loadState;/**< Current load result (success / failure / in-progress). */
    int animated;              /**< 1 marks an animated GIF (reserved). */
    int nineSlice;             /**< 1 enables nine-slice scaling (reserved). */
    UIMarginsObject* nineSliceMargins; /**< Borrowed nine-slice margins; not owned. */

    SDL_Texture* __SDL_texture;/**< Lazily-created GPU texture; NULL until first render. */
    UIFillMode fillMode;       /**< How the texture fills the widget rect. */
    UIColor tintColor;         /**< Tint applied via SDL_SetTextureColorMod (alpha 0 = no tint). */

    /* Cached composite for the rounded/bordered render path. Building the
     * rounded image (mask + border ring via render-target compositing) is
     * expensive, so the result is cached here and re-blitted each frame;
     * it is only recomposited when one of the signature inputs below
     * changes. NULL until the first rounded render. */
    SDL_Texture* __roundedCache;
    int   __rcW, __rcH, __rcFillMode;
    float __rcRadius, __rcBorder, __rcRotation, __rcOp;
    void* __rcSrc;             /**< source-texture identity the cache was built from */
} UIImage;

/**
 * Creates a UIImage with full behaviour and appearance configuration.
 * The texture is **not** loaded here - it is created on the first render
 * (lazy load via UIAsset_LoadTexture).
 *
 * @param source            File path. Accepts PNG, JPG, BMP, GIF, WEBP,
 *                          SVG (via the vendored plutosvg). The path is
 *                          resolved relative to the CWD or the
 *                          executable's directory.
 * @param animated          Reserved for future animated GIF support
 *                          (only the first frame is loaded for now).
 * @param nineSlice         Reserved for nine-slice scaling (not yet
 *                          implemented).
 * @param nineSliceMargins  Nine-slice margins (borrowed pointer, NULL
 *                          accepted). Not owned - the caller manages
 *                          the object's lifetime.
 * @param fillMode          Fill strategy. See UIFillMode.
 * @param tintColor         Tint applied via SDL_SetTextureColorMod.
 *                          Use UI_COLOR_TRANSPARENT (alpha 0) to
 *                          disable it - the renderer preserves the
 *                          original colours in that case.
 * @return Pointer to the UIImage, or NULL on allocation failure.
 */
UIImage* UIImage_Create(const char* source, int animated, int nineSlice,
                         UIMarginsObject* nineSliceMargins, UIFillMode fillMode,
                         UIColor tintColor);

/**
 * Shortcut that creates a UIImage with sensible defaults for simple
 * display: nineSlice off, fillMode = FILL_NONE, no tint.
 *
 * @param source    Path to the image file.
 * @param animated  1 to mark the image as an animated GIF (reserved).
 * @return Pointer to the UIImage, or NULL on failure.
 */
UIImage* UIImage_LoadSource(const char* source, int animated);

/**
 * Creates a UIImage from an in-memory byte buffer (PNG, JPG, SVG, ...).
 * Used to embed assets directly in the binary so they don't have to
 * ship as files alongside the .exe.
 *
 * The texture is created **eagerly** from `renderer` — pass the active
 * SDL renderer (e.g. `app->window->sdlRenderer`). Unlike UIImage_Create
 * which lazy-loads at first render, this constructor needs the renderer
 * up front because there is no source path to retry on later frames.
 *
 * @param renderer Active SDL renderer.
 * @param data     Pointer to the raw image bytes.
 * @param size     Number of bytes at `data`.
 * @param fillMode Fill strategy (see UIFillMode).
 * @param tintColor Tint applied via SDL_SetTextureColorMod
 *                  (UI_COLOR_TRANSPARENT disables tinting).
 * @return UIImage with the texture pre-populated, or NULL on failure.
 */
UIImage* UIImage_FromMemory(SDL_Renderer* renderer,
                            const void* data, size_t size,
                            UIFillMode fillMode, UIColor tintColor);

/**
 * Releases all resources of a UIImage: the SDL texture (if loaded) and
 * the source string. nineSliceMargins is NOT freed (borrowed pointer).
 *
 * @param image Pointer to the UIImage. NULL is safe.
 */
void UIImage_Destroy(UIImage* image);

#endif