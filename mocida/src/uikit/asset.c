#include <uikit/asset.h>
#include <uikit/debug.h>
#include <uikit/bundle.h>
#include <stdio.h>
#include <string.h>

// Maximum length of a resolved path. Generous enough for most file
// systems on Windows (MAX_PATH = 260 + headroom).
#define ASSET_PATH_BUF 1024

// True for an absolute filesystem path: Unix `/…`, Windows `\…` / `C:\…` /
// `C:/…`. Hosts register `mocida://` keys to absolute files so assets
// resolve regardless of CWD.
static int is_absolute_path(const char* path) {
    if (!path || !*path) return 0;
    if (path[0] == '/' || path[0] == '\\') return 1;
#if defined(_WIN32) || defined(_WIN64)
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))
        && path[1] == ':'
        && (path[2] == '/' || path[2] == '\\')) {
        return 1;
    }
#endif
    return 0;
}

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
    (void)buf3; // reserved for a future third relative candidate

#if defined(MOCIDA_WEB)
    /* MEMFS is rooted at "/". Boot stages "/media/x.png". Accept absolute
       paths, and for relative ones also try the absolute MEMFS form. */
    if (path[0] == '/' || path[0] == '\\') {
        out[n++] = path;
        return n;
    }
    out[n++] = path;
    {
        int w = snprintf(buf2, ASSET_PATH_BUF, "/%s", path);
        if (w > 0 && w < ASSET_PATH_BUF) out[n++] = buf2;
    }
    return n;
#else
    // Absolute paths: use as-is. Relative `..` segments are still rejected
    // below so a relative name can't climb out of the asset root.
    if (is_absolute_path(path)) {
        out[n++] = path;
        return n;
    }

    for (const char* p = path; *p; p++) {
        if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\\' || p[2] == '\0')) {
            return 0;
        }
    }

    // (1) The original path (resolved relative to the CWD).
    out[n++] = path;

    // (2) Relative to the binary's directory.
    const char* base = SDL_GetBasePath();
    if (base) {
        int w2 = snprintf(buf2, ASSET_PATH_BUF, "%s%s", base, path);
        if (w2 > 0 && w2 < ASSET_PATH_BUF) out[n++] = buf2;
    }

    return n;
#endif
}

static SDL_Texture* LoadTextureCandidates(SDL_Renderer* renderer, const char* path, int quiet) {
    if (!renderer || !path) return NULL;

    if (UIAsset_IsSFSymbolURI(path)) {
        SDL_Texture* tex = UIAsset_LoadSFSymbolTexture(renderer, path, 128);
        if (tex) return tex;
        if (!quiet) {
            UI_ERROR(UI_CAT_ASSET, "UIAsset_LoadTexture: SF Symbol '%s' unavailable", path);
        }
        return NULL;
    }

    char buf2[ASSET_PATH_BUF];
    char buf3[ASSET_PATH_BUF];
    const char* candidates[3] = { NULL, NULL, NULL };
    int count = BuildCandidates(path, buf2, buf3, candidates);

    for (int i = 0; i < count; i++) {
        SDL_Texture* tex = IMG_LoadTexture(renderer, candidates[i]);
        if (tex) return tex;
    }

    if (!quiet) {
        UI_ERROR(UI_CAT_ASSET, "UIAsset_LoadTexture: could not find '%s' (tried %d path(s))",
                 path, count);
    }
    return NULL;
}

SDL_Texture* UIAsset_LoadTexture(SDL_Renderer* renderer, const char* path) {
    return LoadTextureCandidates(renderer, path, 0);
}

SDL_Texture* UIAsset_TryLoadTexture(SDL_Renderer* renderer, const char* path) {
    return LoadTextureCandidates(renderer, path, 1);
}

static int IsSvgPath(const char* path) {
    const size_t n = path ? strlen(path) : 0;
    return n > 4 && SDL_strcasecmp(path + n - 4, ".svg") == 0;
}

SDL_Texture* UIAsset_CreateTextureFromSurface(SDL_Renderer* renderer,
                                              SDL_Surface* surf,
                                              int maxEdge) {
    if (!renderer || !surf) return NULL;
    SDL_Surface* owned = NULL;
    SDL_Surface* src = surf;
    if (maxEdge > 0) {
        const int sw = src->w;
        const int sh = src->h;
        const int longer = sw > sh ? sw : sh;
        if (longer > maxEdge && longer > 0) {
            const int dw = SDL_max(1, (sw * maxEdge + longer / 2) / longer);
            const int dh = SDL_max(1, (sh * maxEdge + longer / 2) / longer);
            owned = SDL_ScaleSurface(src, dw, dh, SDL_SCALEMODE_LINEAR);
            if (owned) src = owned;
        }
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, src);
    if (owned) SDL_DestroySurface(owned);
    if (tex) SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    return tex;
}

static SDL_Texture* LoadTextureSizedCandidates(SDL_Renderer* renderer,
                                               const char* path,
                                               int pixels,
                                               int quiet) {
    if (!renderer || !path) return NULL;
    if (UIAsset_IsSFSymbolURI(path)) {
        SDL_Texture* tex = UIAsset_LoadSFSymbolTexture(renderer, path, pixels > 0 ? pixels : 128);
        if (tex) return tex;
        if (!quiet) {
            UI_ERROR(UI_CAT_ASSET, "UIAsset_LoadTextureSized: SF Symbol '%s' unavailable", path);
        }
        return NULL;
    }
    const char* resolved = UIApp_ResolveBundle(path);
    if (pixels > 0 && resolved && IsSvgPath(resolved)) {
        char buf2[ASSET_PATH_BUF];
        char buf3[ASSET_PATH_BUF];
        const char* candidates[3] = { NULL, NULL, NULL };
        const int count = BuildCandidates(path, buf2, buf3, candidates);
        for (int i = 0; i < count; i++) {
            SDL_IOStream* io = SDL_IOFromFile(candidates[i], "rb");
            if (!io) continue;
            // Height 0 keeps the aspect ratio; the draw path then fits it.
            SDL_Surface* s = IMG_LoadSizedSVG_IO(io, pixels, 0);
            SDL_CloseIO(io);
            if (!s) continue;
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, s);
            SDL_DestroySurface(s);
            if (tex) return tex;
        }
    }

    /* Bitmaps: decode at full size only when needed. Marketing screenshots are
       often 2560×1440 — uploading several uncompressed RGBA (~14MB each) into
       the wasm heap caused "memory access out of bounds" on the site. When the
       caller asks for a draw-sized texture, downscale before upload. */
    if (pixels > 0) {
        char buf2[ASSET_PATH_BUF];
        char buf3[ASSET_PATH_BUF];
        const char* candidates[3] = { NULL, NULL, NULL };
        const int count = BuildCandidates(path, buf2, buf3, candidates);
        for (int i = 0; i < count; i++) {
            SDL_Surface* s = IMG_Load(candidates[i]);
            if (!s) continue;
            SDL_Texture* tex = UIAsset_CreateTextureFromSurface(renderer, s, pixels);
            SDL_DestroySurface(s);
            if (tex) return tex;
        }
        if (quiet) return NULL;
        /* Fall through to the loud path only when the caller asked for it. */
    }
    return quiet ? UIAsset_TryLoadTexture(renderer, path)
                 : UIAsset_LoadTexture(renderer, path);
}

SDL_Texture* UIAsset_LoadTextureSized(SDL_Renderer* renderer, const char* path, int pixels) {
    return LoadTextureSizedCandidates(renderer, path, pixels, 0);
}

SDL_Texture* UIAsset_TryLoadTextureSized(SDL_Renderer* renderer, const char* path, int pixels) {
    return LoadTextureSizedCandidates(renderer, path, pixels, 1);
}

SDL_Surface* UIAsset_LoadSurface(const char* path) {
    if (!path) return NULL;

    if (UIAsset_IsSFSymbolURI(path)) {
        SDL_Surface* surf = UIAsset_LoadSFSymbolSurface(path, 128);
        if (surf) return surf;
        UI_ERROR(UI_CAT_ASSET, "UIAsset_LoadSurface: SF Symbol '%s' unavailable", path);
        return NULL;
    }

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
