#include <uikit/asset.h>
#include <uikit/debug.h>
#include <uikit/bundle.h>
#include <stdio.h>
#include <string.h>

// Maximum length of a resolved path. Generous enough for most file
// systems on Windows (MAX_PATH = 260 + headroom).
#define ASSET_PATH_BUF 1024

// Helper: builds up to 3 valid candidates for `path`, in order of
// preference. Returns how many were written (0..3).
static int BuildCandidates(const char* path,
                           char buf2[ASSET_PATH_BUF],
                           char buf3[ASSET_PATH_BUF],
                           const char* out[3]) {
    if (!path) return 0;
    // Resolve a mocida:// virtual path to its real file first; non-virtual
    // paths pass straight through.
    path = UIApp_ResolveBundle(path);
    if (!path) return 0;
    int n = 0;

    // (1) The original path (resolved relative to the CWD).
    out[n++] = path;

    // (2 and 3) Relative to the binary's directory.
    const char* base = SDL_GetBasePath();
    if (base) {
        int w2 = snprintf(buf2, ASSET_PATH_BUF, "%s%s", base, path);
        if (w2 > 0 && w2 < ASSET_PATH_BUF) out[n++] = buf2;

        // SDL_GetBasePath already ends with a separator, so "../" works
        // both on Windows and Unix.
        int w3 = snprintf(buf3, ASSET_PATH_BUF, "%s../%s", base, path);
        if (w3 > 0 && w3 < ASSET_PATH_BUF) out[n++] = buf3;
    }

    return n;
}

SDL_Texture* UIAsset_LoadTexture(SDL_Renderer* renderer, const char* path) {
    if (!renderer || !path) return NULL;

    char buf2[ASSET_PATH_BUF];
    char buf3[ASSET_PATH_BUF];
    const char* candidates[3] = { NULL, NULL, NULL };
    int count = BuildCandidates(path, buf2, buf3, candidates);

    SDL_Texture* tex = NULL;
    for (int i = 0; i < count; i++) {
        // Quiet on the first attempts - we only log if ALL of them fail.
        tex = IMG_LoadTexture(renderer, candidates[i]);
        if (tex) return tex;
    }

    UI_ERROR(UI_CAT_ASSET, "UIAsset_LoadTexture: could not find '%s' (tried %d path(s))",
             path, count);
    return NULL;
}

SDL_Surface* UIAsset_LoadSurface(const char* path) {
    if (!path) return NULL;

    char buf2[ASSET_PATH_BUF];
    char buf3[ASSET_PATH_BUF];
    const char* candidates[3] = { NULL, NULL, NULL };
    int count = BuildCandidates(path, buf2, buf3, candidates);

    for (int i = 0; i < count; i++) {
        SDL_Surface* s = IMG_Load(candidates[i]);
        if (s) return s;
    }

    UI_ERROR(UI_CAT_ASSET, "UIAsset_LoadSurface: could not find '%s' (tried %d path(s))",
             path, count);
    return NULL;
}

SDL_Surface* UIAsset_LoadSurfaceFromMemory(const void* data, size_t size) {
    if (!data || size == 0) return NULL;
    // SDL_IOFromConstMem wraps the buffer; IMG_Load_IO with closeio=true
    // releases that wrapper once decoding finishes. The buffer itself
    // belongs to the caller and is never touched.
    SDL_IOStream* io = SDL_IOFromConstMem(data, size);
    if (!io) {
        UI_ERROR(UI_CAT_ASSET,
                 "UIAsset_LoadSurfaceFromMemory: SDL_IOFromConstMem failed: %s",
                 SDL_GetError());
        return NULL;
    }
    SDL_Surface* s = IMG_Load_IO(io, true);
    if (!s) {
        UI_ERROR(UI_CAT_ASSET,
                 "UIAsset_LoadSurfaceFromMemory: IMG_Load_IO failed: %s",
                 SDL_GetError());
    }
    return s;
}
