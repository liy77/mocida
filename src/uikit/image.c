#include <uikit/image.h>

UIImage* UIImage_Create(const char* source, int animated, int nineSlice,
                          UIMarginsObject* nineSliceMargins, UIFillMode fillMode,
                          UIColor tintColor) {
    UIImage* image = (UIImage*)malloc(sizeof(UIImage));
    if (image == NULL) {
        fprintf(stderr, "Failed to allocate memory for UIImage\n");
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
        fprintf(stderr, "Source is NULL\n");
        return NULL; // Invalid source
    }

    UIImage* image = UIImage_Create(source, animated, 0, NULL, FILL_NONE, UI_COLOR_TRANSPARENT);
    return image;
}