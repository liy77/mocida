#ifndef UIKIT_ASSET_H
#define UIKIT_ASSET_H

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * True when `path` is an SF Symbol URI (`sf://play.fill` / `symbol://…`).
 * On Apple platforms these rasterise via AppKit/UIKit system symbols; elsewhere
 * the loaders return NULL (fall back to a PNG `mocida://` asset in the host).
 */
int UIAsset_IsSFSymbolURI(const char* path);

/** Rasterise an SF Symbol URI to an RGBA surface (caller frees). */
SDL_Surface* UIAsset_LoadSFSymbolSurface(const char* uri, int pixels);

/** Rasterise an SF Symbol URI straight to a texture (caller frees). */
SDL_Texture* UIAsset_LoadSFSymbolTexture(SDL_Renderer* renderer,
                                         const char* uri,
                                         int pixels);

/**
 * Resolves an asset path by trying, in order:
 *   1. SF Symbol URI (`sf://name`) on Apple — see UIAsset_LoadSFSymbolTexture.
 *   2. Absolute paths (`/…`, `C:\…`) — used as-is (after `mocida://` resolve).
 *   3. The path as-is (resolved relative to the CWD).
 *   4. <SDL_GetBasePath()>/<path>            (relative to the executable).
 *
 * Relative names that contain `..` are rejected (path traversal). Absolute
 * paths are always accepted so hosts can register `mocida://` keys to real
 * files without depending on CWD.
 *
 * Returns the loaded texture via SDL_image, or NULL if none of the
 * candidates resolved. Lets UIImage / UIApp_SetWindowIcon keep working
 * no matter who launched the program.
 *
 * @param renderer Active SDL renderer.
 * @param path     Asset path (PNG, JPG, BMP, SVG, etc.), `mocida://`, or `sf://`.
 * @return Newly allocated texture (caller-owned), or NULL.
 */
SDL_Texture* UIAsset_LoadTexture(SDL_Renderer* renderer, const char* path);
/** Like LoadTexture but never logs — for web MEMFS poll retries after first paint. */
SDL_Texture* UIAsset_TryLoadTexture(SDL_Renderer* renderer, const char* path);

/**
 * Like UIAsset_LoadTexture, but an `.svg` is rasterised so its larger side is
 * `pixels` device pixels (crisp at the size it's shown) instead of at its own
 * viewBox size. Bitmaps larger than `pixels` on the long edge are downscaled
 * before upload (avoids multi‑MB RGBA spikes on wasm).
 */
SDL_Texture* UIAsset_LoadTextureSized(SDL_Renderer* renderer, const char* path, int pixels);
/** Quiet variant — no ERROR log on miss (web MEMFS poll / remote fallback). */
SDL_Texture* UIAsset_TryLoadTextureSized(SDL_Renderer* renderer, const char* path, int pixels);
/**
 * Upload `surf` as a texture, optionally downscaling so the long edge is at
 * most `maxEdge` (0 = no scale). Does not destroy `surf`.
 */
SDL_Texture* UIAsset_CreateTextureFromSurface(SDL_Renderer* renderer,
                                              SDL_Surface* surf,
                                              int maxEdge);

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

#ifdef __cplusplus
}
#endif

#endif // UIKIT_ASSET_H
