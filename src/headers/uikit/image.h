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
    const char* __widget_type;

    float marginLeft;
    float marginTop;
    float marginRight;
    float marginBottom;
    float radius;
    float borderWidth;

    char* source;
    UIImageLoadState loadState;
    int animated;
    int nineSlice;
    UIMarginsObject* nineSliceMargins;

    SDL_Texture* __SDL_texture;
    UIFillMode fillMode;
    UIColor tintColor;
} UIImage;

UIImage* UIImage_Create(const char* source, int animated, int nineSlice,
                         UIMarginsObject* nineSliceMargins, UIFillMode fillMode,
                         UIColor tintColor);

UIImage* UIImage_LoadSource(const char* source, int animated);
void UIImage_Destroy(UIImage* image);

#endif