#include <uikit/window.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL3_ttf/SDL_ttf.h>

void ApplyMargins(SDL_FRect* rect, float marginLeft, float marginTop, float marginRight, float marginBottom) {
    rect->x += marginLeft - marginRight;
    rect->y += marginTop - marginBottom;
}

void DrawRoundedRectFill(SDL_Renderer* renderer, SDL_FRect inner, UIColor color, float radius) {
    SDL_SetRenderDrawColor(renderer, (Uint8)(color.r), (Uint8)(color.g), (Uint8)(color.b), (Uint8)SDL_clamp((int)(color.a * 255), 0, 255));

    if (radius <= 0) {
        SDL_RenderFillRect(renderer, &inner);
        return;
    }

    SDL_FRect center = {
        inner.x + radius,
        inner.y + radius,
        inner.w - 2 * radius,
        inner.h - 2 * radius
    };
    SDL_RenderFillRect(renderer, &center);

    SDL_FRect top = { inner.x + radius, inner.y, inner.w - 2 * radius, radius };
    SDL_FRect bottom = { inner.x + radius, inner.y + inner.h - radius, inner.w - 2 * radius, radius };
    SDL_FRect left = { inner.x, inner.y + radius, radius, inner.h - 2 * radius };
    SDL_FRect right = { inner.x + inner.w - radius, inner.y + radius, radius, inner.h - 2 * radius };

    SDL_RenderFillRect(renderer, &top);
    SDL_RenderFillRect(renderer, &bottom);
    SDL_RenderFillRect(renderer, &left);
    SDL_RenderFillRect(renderer, &right);

    float steps = radius * 2;
    float step_size = radius / steps;

    for (int i = 0; i < steps; ++i) {
        float dy = (i + 0.5f) * step_size;
        float dx = sqrtf(radius * radius - dy * dy);
        float w = dx * 2;
        float h = step_size;

        SDL_FRect tl = { inner.x + radius - dx, inner.y + radius - dy, w, h };
        SDL_FRect tr = { inner.x + inner.w - radius - dx, inner.y + radius - dy, w, h };
        SDL_FRect bl = { inner.x + radius - dx, inner.y + inner.h - radius + dy - h, w, h };
        SDL_FRect br = { inner.x + inner.w - radius - dx, inner.y + inner.h - radius + dy - h, w, h };

        SDL_RenderFillRect(renderer, &tl);
        SDL_RenderFillRect(renderer, &tr);
        SDL_RenderFillRect(renderer, &bl);
        SDL_RenderFillRect(renderer, &br);
    }
}

void DrawRoundedRectWithAlpha(SDL_Renderer* renderer, SDL_FRect rect, UIColor color, float radius, int borderWidth, UIColor borderColor) {
    if (!renderer || rect.w <= 0 || rect.h <= 0) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    if (borderWidth > 0) {
        SDL_SetRenderDrawColor(renderer, (Uint8)(borderColor.r), (Uint8)(borderColor.g), (Uint8)(borderColor.b), (Uint8)SDL_clamp((int)(borderColor.a * 255), 0, 255));
        DrawRoundedRectFill(renderer, rect, borderColor, radius);
    }

    SDL_FRect inner = {
        rect.x + borderWidth,
        rect.y + borderWidth,
        rect.w - 2 * borderWidth,
        rect.h - 2 * borderWidth
    };

    if (inner.w <= 0 || inner.h <= 0) return;

    radius = SDL_max(0, radius - borderWidth);
    DrawRoundedRectFill(renderer, inner, color, radius);
}

int UIWindow_Render(UIWindow* window) {
    if (!window || !window->sdlRenderer) return -1;

    int renderWidth, renderHeight;
    SDL_GetWindowSize(window->sdlWindow, &renderWidth, &renderHeight);

    static Uint64 lastCounter = 0;
    static Uint64 frameCount = 0;
    static float currentFPS = 0.0f;

    static Uint64 frequency = 0;
    if (frequency == 0) frequency = SDL_GetPerformanceFrequency();


    frameCount++;
    Uint64 currentCounter = SDL_GetPerformanceCounter();

    if (lastCounter == 0) lastCounter = currentCounter;

    if ((currentCounter - lastCounter) >= frequency) { // a cada 1 segundo
        currentFPS = (float)frameCount / ((currentCounter - lastCounter) / (float)frequency);
        lastCounter = currentCounter;
        frameCount = 0;

        window->framerate = currentFPS;
    }

    int upscaleFactor = 2;

    SDL_Texture* smoothTexture = SDL_CreateTexture(
        window->sdlRenderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        renderWidth * upscaleFactor,
        renderHeight * upscaleFactor
    );

    SDL_SetTextureScaleMode(smoothTexture, SDL_SCALEMODE_LINEAR);
    SDL_SetRenderTarget(window->sdlRenderer, smoothTexture);
    SDL_SetRenderScale(window->sdlRenderer, (float)upscaleFactor, (float)upscaleFactor);

    SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_BLEND);

    SDL_SetRenderDrawColor(
        window->sdlRenderer,
        (Uint8)(window->backgroundColor.r),
        (Uint8)(window->backgroundColor.g),
        (Uint8)(window->backgroundColor.b),
        (Uint8)SDL_clamp((int)(window->backgroundColor.a * 255), 0, 255)
    );

    SDL_RenderClear(window->sdlRenderer);

    if (window->children != NULL) {
        UIChildren_SortByZ(window->children);

        for (int i = 0; i < window->children->count; ++i) {
            UIWidget* el = window->children->children[i];
            if (!el || !el->visible || !el->data) continue;

            if (el->alignment != NULL) {
                UIAlignment_Align(el);
            }

            UIWidgetBase* base = (UIWidgetBase*)el->data;
            if (base == NULL || base->__widget_type == NULL) continue;

            if (strcmp(base->__widget_type, UI_WIDGET_RECTANGLE) == 0) {
                if (el->width == NULL || el->height == NULL) {
                    fprintf(stderr, "Rectangle width/height cannot be NULL\n");
                    exit(1);
                }

                UIRectangle* rect = (UIRectangle*)el->data;
                SDL_FRect rectF = { el->x, el->y, *el->width, *el->height };

                ApplyMargins(&rectF, rect->marginLeft, rect->marginTop, rect->marginRight, rect->marginBottom);

                if (rectF.w > 0 && rectF.h > 0) {
                    DrawRoundedRectWithAlpha(
                        window->sdlRenderer,
                        rectF,
                        rect->color,
                        rect->radius,
                        (int)rect->borderWidth,
                        rect->borderColor
                    );
                }
            } else if (strcmp(base->__widget_type, UI_WIDGET_TEXT) == 0) {
                UIText* textWidget = (UIText*)el->data;

                if (!textWidget->text || !textWidget->fontFamily || strcmp(textWidget->fontFamily, "") == 0 ||
                    strcmp(textWidget->text, "") == 0 || textWidget->textLength == 0 || textWidget->fontSize == 0)
                    continue;

                if (textWidget->__SDL_textTexture == NULL) {
                    if (TTF_Init() != 1) {
                        printf("Error initializing SDL_ttf: %s\n", SDL_GetError());
                        return 1;
                    }

                    TTF_Font* font = TTF_OpenFont(textWidget->fontFamily, textWidget->fontSize);
                    if (!font) {
                        printf("Error loading font: %s\n", SDL_GetError());
                        continue;
                    }

                    SDL_Color colorSDL = {
                        (Uint8)(textWidget->color.r),
                        (Uint8)(textWidget->color.g),
                        (Uint8)(textWidget->color.b),
                        (Uint8)SDL_clamp((int)(textWidget->color.a * 255), 0, 255)
                    };

                    SDL_Surface* surface = TTF_RenderText_Blended(font, textWidget->text, textWidget->textLength, colorSDL);
                    if (surface == NULL) {
                        printf("Error rendering texture: %s\n", SDL_GetError());
                        TTF_CloseFont(font);
                        continue;
                    }

                    SDL_Texture* texture = SDL_CreateTextureFromSurface(window->sdlRenderer, surface);
                    if (texture == NULL) {
                        printf("Error creating texture: %s\n", SDL_GetError());
                        SDL_DestroySurface(surface);
                        TTF_CloseFont(font);
                        continue;
                    }

                    textWidget->__SDL_textTexture = texture;
                    SDL_DestroySurface(surface);
                    TTF_CloseFont(font);
                }

                float w = 0, h = 0;
                if (!el->width || !el->height) {
                    float tex_w = 0, tex_h = 0;
                    if (SDL_GetTextureSize(textWidget->__SDL_textTexture, &tex_w, &tex_h) != 1) {
                        fprintf(stderr, "Error getting texture size: %s\n", SDL_GetError());
                        continue;
                    }

                    if (!el->width) {
                        w = tex_w;
                        el->width = malloc(sizeof(float));
                        if (!el->width) {
                            fprintf(stderr, "Error allocating memory for width\n");
                            continue;
                        }
                        *el->width = w;
                    } else {
                        w = *el->width;
                    }

                    if (!el->height) {
                        h = tex_h;
                        el->height = malloc(sizeof(float));
                        if (!el->height) {
                            fprintf(stderr, "Error allocating memory for height\n");
                            continue;
                        }
                        *el->height = h;
                    } else {
                        h = *el->height;
                    }
                } else {
                    w = *el->width;
                    h = *el->height;
                }

                SDL_FRect subTextRect = {
                    el->x,
                    el->y,
                    w + textWidget->paddingLeft + textWidget->paddingRight,
                    h + textWidget->paddingTop + textWidget->paddingBottom
                };

                ApplyMargins(&subTextRect, textWidget->marginLeft, textWidget->marginTop, textWidget->marginRight, textWidget->marginBottom);

                SDL_SetRenderDrawColor(
                    window->sdlRenderer,
                    (Uint8)(textWidget->background->color.r),
                    (Uint8)(textWidget->background->color.g),
                    (Uint8)(textWidget->background->color.b),
                    (Uint8)SDL_clamp((int)(textWidget->background->color.a * 255), 0, 255)
                );

                DrawRoundedRectWithAlpha(
                    window->sdlRenderer,
                    subTextRect,
                    textWidget->background->color,
                    textWidget->background->radius,
                    (int)textWidget->background->borderWidth,
                    textWidget->background->borderColor
                );

                SDL_FRect mainTextRect = {
                    subTextRect.x + (subTextRect.w - w) / 2,
                    subTextRect.y + (subTextRect.h - h) / 2,
                    w,
                    h
                };

                SDL_RenderTexture(window->sdlRenderer, textWidget->__SDL_textTexture, NULL, &mainTextRect);
            }
        }
    }

    // render de volta na tela principal com suavização
    SDL_SetRenderTarget(window->sdlRenderer, NULL);
    SDL_SetRenderScale(window->sdlRenderer, 1.0f, 1.0f);

    SDL_FRect dstRect = { 0, 0, (float)renderWidth, (float)renderHeight };
    SDL_RenderTexture(window->sdlRenderer, smoothTexture, NULL, &dstRect);

    SDL_DestroyTexture(smoothTexture);
    SDL_RenderPresent(window->sdlRenderer);
    return 0;
}

UIWindow* UIWindow_Create(const char* title, int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO) != 1) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        return NULL;
    }

    UIWindow* window = (UIWindow*)malloc(sizeof(UIWindow));
    if (!window) return NULL;

    SDL_Window* sdlWindow = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!sdlWindow) {
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
        free(window);
        return NULL;
    }

    SDL_Renderer* sdlRenderer = SDL_CreateRenderer(sdlWindow, NULL);
    if (!sdlRenderer) {
        fprintf(stderr, "SDL_CreateRenderer Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(sdlWindow);
        free(window);
        return NULL;
    }

    window->title = _strdup(title);
    window->x = 0;
    window->y = 0;
    window->z = 0;
    window->visible = 1;
    window->scaleMode = UIWindowWindowed;
    window->width = width;
    window->height = height;
    window->backgroundColor = UIColorWhite;
    window->children = NULL;
    window->sdlWindow = sdlWindow;
    window->sdlRenderer = sdlRenderer;
    window->framerate = 0.0f;

    SDL_SetRenderDrawBlendMode(sdlRenderer, SDL_BLENDMODE_BLEND);

    return window;
}