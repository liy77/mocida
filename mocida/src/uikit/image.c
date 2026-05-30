#include <uikit/image.h>
#include <uikit/asset.h>
#include <uikit/debug.h>

UIImage* UIImage_Create(const char* source, int animated, int nineSlice,
                          UIMarginsObject* nineSliceMargins, UIFillMode fillMode,
                          UIColor tintColor) {
    UIImage* image = (UIImage*)malloc(sizeof(UIImage));
    if (image == NULL) {
        UI_ERROR(UI_CAT_IMAGE, "out of memory allocating UIImage");
        return NULL; // Memory allocation failed
    }

    image->source = _strdup(source); // Duplicate the source string
    image->animated = animated;
    image->nineSlice = nineSlice;
    image->nineSliceMargins = nineSliceMargins;
    image->fillMode = fillMode;
    image->tintColor = tintColor;
    image->__widget_type = UI_WIDGET_IMAGE; // Set the widget type
    image->radius = 0; // Default radius
    image->borderWidth = 0; // Default border width
    image->marginLeft = 0; // Default left margin
    image->marginTop = 0; // Default top margin
    image->marginRight = 0; // Default right margin
    image->marginBottom = 0; // Default bottom margin
    image->loadState = IMAGE_LOAD_IN_PROGRESS; // Default load state
    image->__SDL_texture = NULL; // Default SDL texture

    return image;
}

UIImage* UIImage_LoadSource(const char* source, int animated) {
    if (source == NULL) {
        UI_WARN(UI_CAT_IMAGE, "UIImage_LoadSource: source is NULL");
        return NULL; // Invalid source
    }

    UIImage* image = UIImage_Create(source, animated, 0, NULL, FILL_NONE, UI_COLOR_TRANSPARENT);
    return image;
}

UIImage* UIImage_FromMemory(SDL_Renderer* renderer,
                            const void* data, size_t size,
                            UIFillMode fillMode, UIColor tintColor) {
    if (!renderer || !data || size == 0) {
        UI_WARN(UI_CAT_IMAGE,
                "UIImage_FromMemory: invalid args (renderer=%p data=%p size=%zu)",
                (void*)renderer, data, size);
        return NULL;
    }
    SDL_Surface* surf = UIAsset_LoadSurfaceFromMemory(data, size);
    if (!surf) return NULL;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_DestroySurface(surf);
    if (!tex) {
        UI_ERROR(UI_CAT_IMAGE,
                 "UIImage_FromMemory: SDL_CreateTextureFromSurface failed: %s",
                 SDL_GetError());
        return NULL;
    }
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);

    // Empty source string keeps the renderer's lazy-load path from
    // trying to reload it from disk (it short-circuits when
    // __SDL_texture is already set, but better to be explicit).
    UIImage* image = UIImage_Create("", 0, 0, NULL, fillMode, tintColor);
    if (!image) {
        SDL_DestroyTexture(tex);
        return NULL;
    }
    image->__SDL_texture = tex;
    image->loadState     = IMAGE_LOAD_SUCCESS;
    return image;
}

void UIImage_Destroy(UIImage* image) {
    if (!image) return;
    if (image->__SDL_texture) {
        SDL_DestroyTexture(image->__SDL_texture);
        image->__SDL_texture = NULL;
    }
    if (image->__roundedCache) {
        SDL_DestroyTexture(image->__roundedCache);
        image->__roundedCache = NULL;
    }
    free(image->source);
    // nineSliceMargins is a borrowed pointer (caller-owned) - don't free it.
    free(image);
}