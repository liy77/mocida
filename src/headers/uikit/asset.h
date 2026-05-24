#ifndef UIKIT_ASSET_H
#define UIKIT_ASSET_H

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

/**
 * Resolves an asset path by trying, in order:
 *   1. The path as-is (resolved relative to the CWD).
 *   2. <SDL_GetBasePath()>/<path>            (relative to the executable).
 *   3. <SDL_GetBasePath()>/../<path>         (executable's parent dir -
 *      useful when the .exe lives in build/ and assets are at the
 *      project root).
 *
 * Returns the loaded texture via SDL_image, or NULL if none of the
 * candidates resolved. Lets UIImage / UIApp_SetWindowIcon keep working
 * no matter who launched the program.
 *
 * @param renderer Active SDL renderer.
 * @param path     Asset path (PNG, JPG, BMP, SVG, etc.).
 * @return Newly allocated texture (caller-owned), or NULL.
 */
SDL_Texture* UIAsset_LoadTexture(SDL_Renderer* renderer, const char* path);

/**
 * Variant returning an SDL_Surface (caller-owned - remember to call
 * SDL_DestroySurface).
 *
 * @param path Asset path.
 * @return Newly allocated surface, or NULL.
 */
SDL_Surface* UIAsset_LoadSurface(const char* path);

/**
 * Decodes an image from an in-memory byte buffer (PNG, JPG, BMP, GIF,
 * WEBP, SVG — anything SDL_image accepts). Useful for embedding
 * assets directly inside the executable so they don't ship as files.
 *
 * @param data Pointer to the raw image bytes.
 * @param size Number of bytes at `data`.
 * @return Newly-allocated SDL_Surface (caller-owned, free with
 *         SDL_DestroySurface), or NULL on decode failure.
 */
SDL_Surface* UIAsset_LoadSurfaceFromMemory(const void* data, size_t size);

#endif // UIKIT_ASSET_H
