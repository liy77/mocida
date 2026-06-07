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

// UIKit defines an Objective-C class `UIImage`, which collides with our C
// struct of the same name. The clash only matters in Objective-C(++) TUs
// on iOS (e.g. video_avf.mm, where AVFoundation drags in UIKit) — and those
// TUs don't use our UIImage struct, only UIFillMode above. So hide the
// struct + its prototypes from that exact case. Plain C TUs (image.c,
// window.c, the demo) still get the full definition.
#if !(defined(MOCIDA_IOS) && defined(__OBJC__))

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
    int      antialiasing;     /**< 1 = smooth (linear) scaling [default], 0 = nearest/pixelated. */
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

    int   cache;               /**< 1 = keep http/https downloads in the process-wide
                                    in-memory cache (default); 0 = always refetch and
                                    never store. Mirrors Qt's `Image { cache: }`. */
    void* __download;          /**< Opaque in-flight remote-download job, or NULL.
                                    Set while an http(s) source is being fetched on a
                                    background thread; cleared once consumed. */
} UIImage;

/**
 * Creates a UIImage with full behaviour and appearance configuration.
 * The texture is **not** loaded here - it is created on the first render
 * (lazy load via UIAsset_LoadTexture).
 *
 * @param source            File path or remote URL. Accepts PNG, JPG, BMP,
 *                          GIF, WEBP, SVG (via the vendored plutosvg). A
 *                          local path is resolved relative to the CWD or the
 *                          executable's directory. An `http://` / `https://`
 *                          URL is downloaded on a background thread and decoded
 *                          once it arrives (see UIImage_PumpRemote); by default
 *                          the bytes are kept in an in-memory cache keyed by
 *                          URL so the same image is not refetched while the
 *                          process lives. Toggle that with UIImage_SetCache.
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

/**
 * Reports whether `source` is a remote URL the image widget fetches over
 * the network (an `http://` or `https://` address).
 *
 * @param source Source string (NULL accepted).
 * @return 1 for a remote URL, 0 otherwise.
 */
int UIImage_IsRemoteSource(const char* source);

/**
 * Enables or disables the in-memory cache for this image's remote download.
 *
 * With caching on (the default) the bytes fetched for the image's URL are
 * stored in a process-wide cache keyed by that URL, so reusing the same URL
 * - including after the widget tree is rebuilt / the source is redefined -
 * does not hit the network again. The cache is never written to disk, so a
 * fresh launch always refetches and picks up a changed server image. With
 * caching off the image always refetches and stores nothing.
 *
 * Has no effect on local-file images. Takes effect for the next load (call
 * it before the image first renders).
 *
 * @param image UIImage to configure (NULL safe).
 * @param cache 1 to cache (default), 0 to always refetch.
 */
void UIImage_SetCache(UIImage* image, int cache);

/** Toggle smooth (linear) vs nearest/pixelated scaling. Default 1 (smooth). Applies
 *  to the existing texture immediately and to any future (re)load. */
void UIImage_SetAntialiasing(UIImage* image, int on);

/**
 * Advances the asynchronous load of a remote (http/https) image. Called by
 * the renderer every frame while the texture is not yet ready: it kicks off
 * the background download on first call and, once the bytes have arrived,
 * decodes them into a texture on the render thread.
 *
 * No-op for local-file images and after the texture is loaded. Safe to call
 * repeatedly.
 *
 * @param image    UIImage with a remote source.
 * @param renderer Active SDL renderer (used to create the texture).
 */
void UIImage_PumpRemote(UIImage* image, SDL_Renderer* renderer);

/**
 * Drops every entry from the process-wide remote-image cache, freeing the
 * memory held by previously downloaded URLs. In-flight downloads and already
 * decoded textures are unaffected; subsequent loads refetch.
 */
void UIImage_ClearRemoteCache(void);

#endif // !(MOCIDA_IOS && __OBJC__)

#endif