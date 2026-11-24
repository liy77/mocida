// Stub SF Symbol loader for non-Apple platforms.
#include <uikit/asset.h>
#include <string.h>
#include <SDL3/SDL.h>

int UIAsset_IsSFSymbolURI(const char* path) {
    if (!path) return 0;
    return SDL_strncasecmp(path, "sf://", 5) == 0
        || SDL_strncasecmp(path, "symbol://", 9) == 0;
}

SDL_Surface* UIAsset_LoadSFSymbolSurface(const char* uri, int pixels) {
    (void)uri;
    (void)pixels;
    return NULL;
}

SDL_Texture* UIAsset_LoadSFSymbolTexture(SDL_Renderer* renderer,
                                         const char* uri,
                                         int pixels) {
    (void)renderer;
    (void)uri;
    (void)pixels;
    return NULL;
}
