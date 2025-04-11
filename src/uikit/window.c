#include <uikit/window.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL3_ttf/SDL_ttf.h>

void DrawRoundedRectFill(SDL_Renderer* renderer, SDL_FRect inner, UIColor color, float radius) {
    if (radius <= 0) {
        SDL_RenderFillRect(renderer, &inner);
        return;
    }

    SDL_SetRenderDrawColor(renderer, (Uint8)(color.r), (Uint8)(color.g), (Uint8)(color.b), (Uint8)SDL_clamp((int)(color.a * 255), 0, 255));

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

    SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_BLEND);

    if (window->backgroundColor) {
        SDL_SetRenderDrawColor(
            window->sdlRenderer,
            (Uint8)(window->backgroundColor->r),
            (Uint8)(window->backgroundColor->g),
            (Uint8)(window->backgroundColor->b),
            (Uint8)SDL_clamp((int)(window->backgroundColor->a * 255), 0, 255)
        );
    } else {
        SDL_SetRenderDrawColor(window->sdlRenderer, 0, 0, 0, 255);
    }

    SDL_RenderClear(window->sdlRenderer);

    if (window->children != NULL) {
        UIChildren_SortByZ(window->children); // Sort children by z-index

        for (int i = 0; i < window->children->count; ++i) {
            UIWidget* el = window->children->children[i];
            if (!el || !el->visible || !el->data) continue;

            UIWidgetBase* base = (UIWidgetBase*)el->data;
            if (!base || !base->__widget_type) continue;

            if (strcmp(base->__widget_type, UI_WIDGET_RECTANGLE) == 0) {
                UIRectangle* rect = (UIRectangle*)el->data;
                SDL_FRect rectF = {
                    el->x + rect->marginLeft,
                    el->y + rect->marginTop,
                    rect->width - (rect->marginLeft + rect->marginRight),
                    rect->height - (rect->marginTop + rect->marginBottom)
                };

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

                if (
                    !textWidget->text || !textWidget->fontFamily ||
                    strcmp(textWidget->fontFamily, "") == 0 ||
                    strcmp(textWidget->text, "") == 0 ||
                    textWidget->textLength == 0 || textWidget->fontSize == 0
                ) continue;

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
                        (Uint8)(textWidget->color->r),
                        (Uint8)(textWidget->color->g),
                        (Uint8)(textWidget->color->b),
                        (Uint8)SDL_clamp((int)(textWidget->color->a * 255), 0, 255)
                    };
                
                    SDL_Surface* surface = NULL;
                    surface = TTF_RenderText_Blended(font, textWidget->text, textWidget->textLength, colorSDL);
                    if (surface == NULL) {
                        printf("Error rendering texture: %s\n", SDL_GetError());
                        TTF_CloseFont(font);
                        continue;
                    }
                
                    SDL_Texture* texture = SDL_CreateTextureFromSurface(window->sdlRenderer, surface);
                    if (texture == NULL) {
                        printf("Error creating texture: %s\n", SDL_GetError());
                        SDL_DestroySurface(surface);
                        SDL_DestroyTexture(texture);
                        TTF_CloseFont(font);
                        continue;
                    }
                    textWidget->__SDL_textTexture = texture;
                    SDL_DestroySurface(surface);
                    TTF_CloseFont(font);
                }
                
                float w, h;
                if (SDL_GetTextureSize(textWidget->__SDL_textTexture, &w, &h) != 1) {
                    printf("Error getting font texture size: %s\n", SDL_GetError());
                    continue;
                }    
                
                SDL_FRect subTextRect = {
                    el->x + textWidget->marginLeft,
                    el->y + textWidget->marginTop,
                    w + (textWidget->marginLeft + textWidget->marginRight),
                    h + (textWidget->marginTop + textWidget->marginBottom)
                };

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
                    w - (textWidget->paddingLeft + textWidget->paddingRight),
                    h - (textWidget->paddingTop + textWidget->paddingBottom)
                };

                SDL_RenderTexture(window->sdlRenderer, textWidget->__SDL_textTexture, NULL, &mainTextRect);
            }
        }
    }

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

    SDL_Window* sdlWindow = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE);
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
    window->backgroundColor = NULL;
    window->children = NULL;
    window->sdlWindow = sdlWindow;
    window->sdlRenderer = sdlRenderer;

    SDL_SetRenderDrawBlendMode(sdlRenderer, SDL_BLENDMODE_BLEND);

    return window;
}