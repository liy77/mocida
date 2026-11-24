// icon.c — UIIcon helpers (SF Symbol on Apple / PNG fallback elsewhere).

#include <uikit/icon.h>
#include <uikit/asset.h>
#include <uikit/font.h>
#include <uikit/debug.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

int UIIcon_LooksLikeSymbolName(const char* name) {
    if (!name || !*name) return 0;
    if (UIAsset_IsSFSymbolURI(name)) return 0;
    if (SDL_strncasecmp(name, "mocida://", 9) == 0) return 0;
    if (SDL_strncasecmp(name, "http://", 7) == 0) return 0;
    if (SDL_strncasecmp(name, "https://", 8) == 0) return 0;
    if (SDL_strncasecmp(name, "glyph://", 8) == 0) return 0;
    if (strchr(name, '/') || strchr(name, '\\')) return 0;
    if (name[0] == '.') return 0;
    {
        const char* dot = strrchr(name, '.');
        if (dot && dot[1]) {
            if (SDL_strcasecmp(dot, ".png") == 0 || SDL_strcasecmp(dot, ".jpg") == 0
                || SDL_strcasecmp(dot, ".jpeg") == 0 || SDL_strcasecmp(dot, ".svg") == 0
                || SDL_strcasecmp(dot, ".gif") == 0 || SDL_strcasecmp(dot, ".webp") == 0
                || SDL_strcasecmp(dot, ".bmp") == 0 || SDL_strcasecmp(dot, ".ico") == 0) {
                return 0;
            }
        }
    }
    for (const unsigned char* p = (const unsigned char*)name; *p; p++) {
        if (!(isalnum(*p) || *p == '.' || *p == '_' || *p == '-')) return 0;
    }
    return 1;
}

/** Strip sf:// or symbol:// → bare symbol name (borrowed into out, or NULL). */
static const char* BareSymbolName(const char* name, char* tmp, size_t tmpCap) {
    if (!name) return NULL;
    if (SDL_strncasecmp(name, "sf://", 5) == 0) return name + 5;
    if (SDL_strncasecmp(name, "symbol://", 9) == 0) return name + 9;
    if (UIIcon_LooksLikeSymbolName(name)) return name;
    (void)tmp; (void)tmpCap;
    return NULL;
}

/**
 * Built-in SF Symbol → mocida://icons PNG map for Android / Windows / Linux.
 * Hosts should register those keys (astra-gui already does).
 */
static const char* BuiltinPngFallback(const char* symbol) {
    if (!symbol || !*symbol) return NULL;
    // Exact / common aliases used by Astra GUI and friends.
    if (SDL_strcasecmp(symbol, "play.fill") == 0 || SDL_strcasecmp(symbol, "play") == 0)
        return "mocida://icons/play.png";
    if (SDL_strcasecmp(symbol, "folder.fill") == 0 || SDL_strcasecmp(symbol, "folder") == 0)
        return "mocida://icons/folder.png";
    if (SDL_strcasecmp(symbol, "terminal") == 0 || SDL_strcasecmp(symbol, "terminal.fill") == 0
        || SDL_strcasecmp(symbol, "apple.terminal") == 0)
        return "mocida://icons/terminal.png";
    if (SDL_strcasecmp(symbol, "arrow.triangle.2.circlepath") == 0
        || SDL_strcasecmp(symbol, "arrow.clockwise") == 0
        || SDL_strcasecmp(symbol, "arrow.trianglehead.2.clockwise.rotate.90") == 0)
        return "mocida://icons/update.png";
    if (SDL_strcasecmp(symbol, "rectangle.split.2x1") == 0
        || SDL_strcasecmp(symbol, "square.split.2x1") == 0)
        return "mocida://icons/split.png";
    if (SDL_strcasecmp(symbol, "chevron.left.forwardslash.chevron.right") == 0
        || SDL_strcasecmp(symbol, "curlybraces") == 0
        || SDL_strcasecmp(symbol, "chevron.left.slash.chevron.right") == 0)
        return "mocida://icons/code.png";
    return NULL;
}

char* UIIcon_ResolveSourceEx(const char* name, const char* fallback) {
    if (!name || !*name) {
        return (fallback && *fallback) ? _strdup(fallback) : NULL;
    }

    // Already a concrete image / remote — never rewrite to SF.
    if (SDL_strncasecmp(name, "mocida://", 9) == 0
        || SDL_strncasecmp(name, "http://", 7) == 0
        || SDL_strncasecmp(name, "https://", 8) == 0
        || (!UIAsset_IsSFSymbolURI(name) && !UIIcon_LooksLikeSymbolName(name))) {
        return _strdup(name);
    }

    char tmp[8];
    const char* symbol = BareSymbolName(name, tmp, sizeof(tmp));

#if defined(__APPLE__)
    // Apple: prefer SF Symbols. Explicit fallback only if caller forced a
    // non-symbol name (handled above) — symbol names always go sf://.
    if (symbol && *symbol) {
        size_t n = strlen(symbol);
        char* out = (char*)malloc(n + 6);
        if (!out) return NULL;
        memcpy(out, "sf://", 5);
        memcpy(out + 5, symbol, n + 1);
        return out;
    }
    return _strdup(name);
#else
    // Android / Windows / Linux / web: never use SF Symbols.
    if (fallback && *fallback) return _strdup(fallback);
    if (symbol && *symbol) {
        const char* png = BuiltinPngFallback(symbol);
        if (png) return _strdup(png);
        // Last resort: mocida://icons/<symbol>.png (host may register it).
        size_t n = strlen(symbol);
        char* out = (char*)malloc(n + 20);
        if (!out) return NULL;
        snprintf(out, n + 20, "mocida://icons/%s.png", symbol);
        return out;
    }
    return _strdup(name);
#endif
}

char* UIIcon_ResolveSource(const char* name) {
    return UIIcon_ResolveSourceEx(name, NULL);
}

UIImage* UIIcon_Create(const char* name, UIColor tint) {
    return UIIcon_CreateWithFallback(name, NULL, tint);
}

UIImage* UIIcon_CreateWithFallback(const char* name, const char* fallback, UIColor tint) {
    char* src = UIIcon_ResolveSourceEx(name, fallback);
    if (!src) {
        UI_WARN(UI_CAT_IMAGE, "UIIcon_Create: empty name");
        return NULL;
    }
    UIColor use = (tint.a <= 0.0f) ? UI_COLOR_WHITE : tint;
    UIImage* img = UIImage_Create(src, 0, 0, NULL, FILL_FIT, use);
    free(src);
    if (img) img->antialiasing = 1;
    return img;
}

UIImage* UIIcon_CreateGlyph(SDL_Renderer* renderer,
                             const char* glyph,
                             const char* fontFamily,
                             float pointSize,
                             UIColor tint) {
    if (!renderer || !glyph || !*glyph) return NULL;
    float size = pointSize > 0.0f ? pointSize : 16.0f;

    char* fontPath = UIGetFont(fontFamily && fontFamily[0] ? fontFamily : NULL);
    if (!fontPath) {
        UI_WARN(UI_CAT_IMAGE, "UIIcon_CreateGlyph: font '%s' not found",
                fontFamily ? fontFamily : "(default)");
        return NULL;
    }

    TTF_Font* font = TTF_OpenFont(fontPath, size);
    if (!font) {
        UI_WARN(UI_CAT_IMAGE, "UIIcon_CreateGlyph: TTF_OpenFont failed: %s", SDL_GetError());
        return NULL;
    }

    UIColor use = (tint.a <= 0.0f) ? UI_COLOR_WHITE : tint;
    SDL_Color fg = {
        (Uint8)use.r,
        (Uint8)use.g,
        (Uint8)use.b,
        (Uint8)(use.a * 255.0f + 0.5f)
    };

    SDL_Surface* glyphSurf = TTF_RenderText_Blended(font, glyph, 0, fg);
    TTF_CloseFont(font);
    if (!glyphSurf) {
        UI_WARN(UI_CAT_IMAGE, "UIIcon_CreateGlyph: render failed: %s", SDL_GetError());
        return NULL;
    }

    int side = glyphSurf->w > glyphSurf->h ? glyphSurf->w : glyphSurf->h;
    if (side < 1) side = 1;
    SDL_Surface* square = SDL_CreateSurface(side, side, SDL_PIXELFORMAT_RGBA32);
    if (!square) {
        SDL_DestroySurface(glyphSurf);
        return NULL;
    }
    SDL_FillSurfaceRect(square, NULL, SDL_MapSurfaceRGBA(square, 0, 0, 0, 0));
    SDL_Rect dst = {
        (side - glyphSurf->w) / 2,
        (side - glyphSurf->h) / 2,
        glyphSurf->w,
        glyphSurf->h
    };
    SDL_BlitSurface(glyphSurf, NULL, square, &dst);
    SDL_DestroySurface(glyphSurf);

    SDL_IOStream* io = SDL_IOFromDynamicMem();
    if (!io) {
        SDL_DestroySurface(square);
        return NULL;
    }
    if (!IMG_SavePNG_IO(square, io, false)) {
        UI_WARN(UI_CAT_IMAGE, "UIIcon_CreateGlyph: PNG encode failed: %s", SDL_GetError());
        SDL_CloseIO(io);
        SDL_DestroySurface(square);
        return NULL;
    }
    SDL_DestroySurface(square);

    Sint64 sz = SDL_GetIOSize(io);
    if (sz <= 0) {
        SDL_CloseIO(io);
        return NULL;
    }
    unsigned char* png = (unsigned char*)SDL_malloc((size_t)sz);
    if (!png) {
        SDL_CloseIO(io);
        return NULL;
    }
    SDL_SeekIO(io, 0, SDL_IO_SEEK_SET);
    size_t nread = SDL_ReadIO(io, png, (size_t)sz);
    SDL_CloseIO(io);
    if (nread != (size_t)sz) {
        SDL_free(png);
        return NULL;
    }

    UIImage* img = UIImage_FromMemory(renderer, png, (size_t)sz, FILL_FIT, use);
    SDL_free(png);
    if (img) img->antialiasing = 1;
    return img;
}
