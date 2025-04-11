#include <uikit/window.h>
#include <uikit/widget.h> // Ensure this header contains the definition of UIWidgetData
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

void DrawRoundedRectWithAlpha(SDL_Renderer* renderer, SDL_FRect rect, UIColor color, int radius, int borderWidth, UIColor borderColor) {
    if (!renderer || rect.w <= 0 || rect.h <= 0) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // Draw border if necessary
    if (borderWidth > 0) {
        // Draw border first (full rectangle)
        SDL_SetRenderDrawColor(renderer, borderColor.r, borderColor.g, borderColor.b, (Uint8)SDL_clamp((int)(borderColor.a * 255), 0, 255));
        DrawRoundedRectWithAlpha(renderer, rect, borderColor, radius, 0, borderColor);  // recursive call without border
    }

    // Compute inner rectangle (actual content)
    SDL_FRect inner = {
        rect.x + borderWidth,
        rect.y + borderWidth,
        rect.w - 2 * borderWidth,
        rect.h - 2 * borderWidth
    };

    if (inner.w <= 0 || inner.h <= 0) return;

    radius = SDL_max(0, radius - borderWidth);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, (Uint8)SDL_clamp((int)(color.a * 255), 0, 255));

    if (radius <= 0) {
        SDL_RenderFillRect(renderer, &inner);
        return;
    }

    // Center fill
    SDL_FRect center = {
        inner.x + radius,
        inner.y + radius,
        inner.w - 2 * radius,
        inner.h - 2 * radius
    };
    SDL_RenderFillRect(renderer, &center);

    // Sides
    SDL_FRect top = { inner.x + (float)radius, inner.y, inner.w - 2 * (float)radius, (float)radius };
    SDL_FRect bottom = { inner.x + (float)radius, inner.y + inner.h - (float)radius, inner.w - 2 * (float)radius, (float)radius };
    SDL_FRect left = { inner.x, inner.y + (float)radius, (float)radius, inner.h - 2 * (float)radius };
    SDL_FRect right = { inner.x + inner.w - (float)radius, inner.y + (float)radius, (float)radius, inner.h - 2 * (float)radius };

    SDL_RenderFillRect(renderer, &top);
    SDL_RenderFillRect(renderer, &bottom);
    SDL_RenderFillRect(renderer, &left);
    SDL_RenderFillRect(renderer, &right);

    // Rounded corners with anti-aliasing (higher steps for smoother curves)
    int steps = radius * 2; // more steps = smoother corners
    float step_size = (float)radius / steps;

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

int UIWindow_Render(UIWindow* window) {
    if (!window || !window->sdlRenderer) return -1;

    // Clear the screen with a default color (black)
    SDL_SetRenderDrawColor(window->sdlRenderer, 0, 0, 0, 255);
    SDL_RenderClear(window->sdlRenderer);

    // Draw background color if set
    if (window->backgroundColor) {
        SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(
            window->sdlRenderer,
            window->backgroundColor->r,
            window->backgroundColor->g,
            window->backgroundColor->b,
            (Uint8)SDL_clamp((int)roundf(window->backgroundColor->a * 255), 0, 255)
        );
        SDL_RenderClear(window->sdlRenderer);
    }

    // Draw children if they exist
    if (window->children != NULL) {
        for (int i = 0; i < window->children->count; ++i) {
            UIWidget* el = window->children->children[i];
            if (el != NULL && el->visible) {
                if (el->data == NULL) {
                    printf("No data for child %d\n", i); // Debug output
                    continue; // Skip if no data
                }

                UIWidgetBase* base = (UIWidgetBase*)el->data;

                if (base == NULL) {
                    printf("No base for child %d\n", i); // Debug output
                    continue; // Skip if no base
                }

                char* type = base->__widget_type;

                if (type == NULL) {
                    printf("Widget type is NULL for child %d\n", i); // Debug output
                    continue; // Skip if no widget type
                }

                if (strcmp(type, UI_WIDGET_RECTANGLE) == 0) {
                    // Desenhar retângulo
                    UIRectangle* rect = (UIRectangle*)el->data;
                    SDL_SetRenderDrawColor(window->sdlRenderer, rect->color.r, rect->color.g, rect->color.b, (Uint8)SDL_clamp((int)(rect->color.a * 255), 0, 255));
                    SDL_FRect rectF = {
                        el->x + rect->marginLeft,
                        el->y + rect->marginTop,
                        rect->width - (rect->marginLeft + rect->marginRight),
                        rect->height - (rect->marginTop + rect->marginBottom)
                    };

                    if (rectF.w > 0 && rectF.h > 0) {
                        DrawRoundedRectWithAlpha(window->sdlRenderer, rectF, rect->color, (int)rect->radius, (int)rect->borderWidth, rect->borderColor);
                    }
                } else if (strcmp(type, UI_WIDGET_TEXT) == 0) {
                    // Desenhar texto (placeholder para implementação futura)
                }
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