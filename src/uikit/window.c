#include <uikit/window.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL3_ttf/SDL_ttf.h>

typedef struct FontCacheEntry {
    char *path;
    float size;
    TTF_Font *font;
    struct FontCacheEntry *next;
} FontCacheEntry;

static FontCacheEntry *g_fontCache = NULL;

static TTF_Font* GetFont(const char *path, float size) {
    FontCacheEntry *e = g_fontCache;
    while (e) {
        if (e->size == size && strcmp(e->path, path) == 0)
            return e->font;
        e = e->next;
    }
    TTF_Font *f = TTF_OpenFont(path, size);
    if (!f) {
        fprintf(stderr, "TTF_OpenFont error: %s\n", SDL_GetError());
        return NULL;
    }
    e = malloc(sizeof(FontCacheEntry));
    if (!e) {
        fprintf(stderr, "Failed to allocate memory for FontCacheEntry\n");
        TTF_CloseFont(f);
        return NULL;
    }
    e->path = _strdup(path);
    if (!e->path) {
        fprintf(stderr, "Failed to allocate memory for font path\n");
        free(e);
        TTF_CloseFont(f);
        return NULL;
    }
    e->size = size;
    e->font = f;
    e->next = g_fontCache;
    g_fontCache = e;
    return f;
}

// Smooth Texture Cache
static SDL_Texture *g_smoothTexture = NULL;
static int g_smoothW = 0, g_smoothH = 0;

static inline void ApplyMargins(SDL_FRect* r, float ml, float mt, float mr, float mb) {
    r->x += ml - mr;
    r->y += mt - mb;
}

static void DrawRoundedRectFill(SDL_Renderer* rend, SDL_FRect inner, UIColor c, float radius) {
    SDL_SetRenderDrawColor(rend, (Uint8)c.r, (Uint8)c.g, (Uint8)c.b,
                           (Uint8)SDL_clamp((int)(c.a * 255), 0, 255));
    if (radius <= 0) {
        SDL_RenderFillRect(rend, &inner);
        return;
    }
    // Center rect
    SDL_FRect center = { inner.x + radius, inner.y + radius,
                         inner.w - 2*radius, inner.h - 2*radius };
    SDL_RenderFillRect(rend, &center);
    // Edges
    SDL_FRect edges[4] = {
        {inner.x+radius, inner.y, inner.w-2*radius, radius},
        {inner.x+radius, inner.y+inner.h-radius, inner.w-2*radius, radius},
        {inner.x, inner.y+radius, radius, inner.h-2*radius},
        {inner.x+inner.w-radius, inner.y+radius, radius, inner.h-2*radius}
    };
    for (int i=0;i<4;i++) SDL_RenderFillRect(rend, &edges[i]);
    // Corners
    int steps = (int)(radius*2);
    float step = radius/steps;
    for (int i=0;i<steps;i++) {
        float dy = (i+0.5f)*step;
        float dx = sqrtf(radius*radius - dy*dy);
        float w = dx*2, h = step;
        SDL_FRect rects[4] = {
            {inner.x+radius-dx, inner.y+radius-dy, w, h},
            {inner.x+inner.w-radius-dx, inner.y+radius-dy, w, h},
            {inner.x+radius-dx, inner.y+inner.h-radius+dy-h, w, h},
            {inner.x+inner.w-radius-dx, inner.y+inner.h-radius+dy-h, w, h}
        };
        for (int j=0;j<4;j++) SDL_RenderFillRect(rend, &rects[j]);
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
    int rw, rh;
    SDL_GetWindowSize(window->sdlWindow, &rw, &rh);

    // Initialize TTF once
    static int ttfInited = 0;
    if (!ttfInited) {
        if (TTF_Init() != 1)
            fprintf(stderr, "TTF_Init error: %s\n", SDL_GetError());
        ttfInited = 1;
    }

    // FPS calculation
    static Uint64 lastCounter = 0, frameCount = 0, freq = 0;
    if (!freq) freq = SDL_GetPerformanceFrequency();
    frameCount++;
    Uint64 cur = SDL_GetPerformanceCounter();
    if (!lastCounter) lastCounter = cur;
    if (cur - lastCounter >= freq) {
        window->framerate = (float)frameCount / ((cur - lastCounter) / (float)freq);
        lastCounter = cur;
        frameCount = 0;
        if (window->events) {
            UIEventData ev = {0};
            ev.framerate.fps = window->framerate;
            ev.children = window->children;
            ev.type = UI_EVENT_FRAMERATE_CHANGED;
            UIWindow_EmitEvent(window, UI_EVENT_FRAMERATE_CHANGED, ev);
        }
    }

    // Prepare or resize smooth texture
    int upscale = 2;
    int tw = rw * upscale, th = rh * upscale;
    if (!g_smoothTexture || g_smoothW != tw || g_smoothH != th) {
        if (g_smoothTexture) SDL_DestroyTexture(g_smoothTexture);
        g_smoothTexture = SDL_CreateTexture(window->sdlRenderer,
            SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, tw, th);
        if (!g_smoothTexture) {
            fprintf(stderr, "Failed to create smooth texture: %s\n", SDL_GetError());
            return -1;
        }
        g_smoothW = tw;
        g_smoothH = th;
        SDL_SetTextureScaleMode(g_smoothTexture, SDL_SCALEMODE_LINEAR);
    }

    SDL_SetRenderTarget(window->sdlRenderer, g_smoothTexture);
    SDL_SetRenderScale(window->sdlRenderer, (float)upscale, (float)upscale);
    SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(window->sdlRenderer,
        (Uint8)window->backgroundColor.r,
        (Uint8)window->backgroundColor.g,
        (Uint8)window->backgroundColor.b,
        (Uint8)SDL_clamp((int)(window->backgroundColor.a * 255), 0, 255));
    SDL_RenderClear(window->sdlRenderer);

    if (window->children) {
        UIChildren_SortByZ(window->children);
        for (int i = 0; i < window->children->count; i++) {
            UIWidget *el = window->children->children[i];
            if (!el || !el->visible || !el->data) continue;
            if (el->alignment) UIAlignment_Align(el);
            UIWidgetBase *base = (UIWidgetBase*)el->data;

            if (!strcmp(base->__widget_type, UI_WIDGET_RECTANGLE)) {
                UIRectangle *rect = (UIRectangle*)base;
                SDL_FRect rf = {el->x, el->y, *el->width, *el->height};
                ApplyMargins(&rf, rect->marginLeft, rect->marginTop,
                             rect->marginRight, rect->marginBottom);
                if (rf.w > 0 && rf.h > 0)
                    DrawRoundedRectWithAlpha(window->sdlRenderer, rf,
                        rect->color, rect->radius,
                        (int)rect->borderWidth, rect->borderColor);
            }
            else if (!strcmp(base->__widget_type, UI_WIDGET_TEXT)) {
                UIText *twid = (UIText*)base;
                if (!twid->text || !twid->fontFamily || !*twid->text || !twid->textLength || !twid->fontSize)
                    continue;
                if (!twid->__SDL_textTexture) {
                    TTF_Font *font = GetFont(twid->fontFamily, twid->fontSize);
                    if (font) {
                        SDL_Color sc = {(Uint8)twid->color.r,
                                        (Uint8)twid->color.g,
                                        (Uint8)twid->color.b,
                                        (Uint8)SDL_clamp((int)(twid->color.a * 255), 0, 255)};
                        SDL_Surface *surf = TTF_RenderText_Blended(
                            font, twid->text, twid->textLength, sc);
                        if (surf) {
                            twid->__SDL_textTexture = SDL_CreateTextureFromSurface(
                                window->sdlRenderer, surf);
                            SDL_DestroySurface(surf);
                        }
                    }
                }
                if (!twid->__SDL_textTexture) continue;

                // Query texture size
                float txw, txh;
                SDL_GetTextureSize(twid->__SDL_textTexture, &txw, &txh);

                // Apply padding
                float pw = txw + twid->paddingLeft + twid->paddingRight;
                float ph = txh + twid->paddingTop + twid->paddingBottom;
                SDL_FRect bg = {el->x, el->y, pw, ph};

                // Draw background with padding
                SDL_SetRenderDrawColor(
                    window->sdlRenderer,
                    (Uint8)twid->background->color.r,
                    (Uint8)twid->background->color.g,
                    (Uint8)twid->background->color.b,
                    (Uint8)SDL_clamp((int)(twid->background->color.a * 255), 0, 255));
                DrawRoundedRectWithAlpha(window->sdlRenderer, bg,
                    twid->background->color,
                    twid->background->radius,
                    (int)twid->background->borderWidth,
                    twid->background->borderColor);

                // Draw text inside padded area
                SDL_FRect dst = {bg.x + twid->paddingLeft,
                                 bg.y + twid->paddingTop,
                                 txw, txh};
                SDL_RenderTexture(window->sdlRenderer, twid->__SDL_textTexture,
                                  NULL, &dst);
            }
        }
    }

    SDL_SetRenderTarget(window->sdlRenderer, NULL);
    SDL_SetRenderScale(window->sdlRenderer, 1.0f, 1.0f);
    SDL_FRect screenRect = {0, 0, (float)rw, (float)rh};
    SDL_RenderTexture(window->sdlRenderer, g_smoothTexture, NULL, &screenRect);
    SDL_RenderPresent(window->sdlRenderer);
    return 0;
}

void UIWindow_EmitEvent(UIWindow* window, UI_EVENT event, UIEventData data) {
    if (!window || !window->events) return;

    const unsigned int* max_events_ptr = (unsigned int*)UIWindow_GetProperty(window, UI_PROP_MAX_EVENTS);
    const unsigned int MAX_EVENTS = max_events_ptr ? *max_events_ptr : 0;
    if (event >= MAX_EVENTS) return;

    UIEventCallbackData* callbackData = window->events[event];
    if (callbackData != NULL) {
        UIEventCallback cb = callbackData->cb;
        cb(data);
    } else {
        fprintf(stderr, "No callback registered for event %u\n", event);
    }
}

UIWindow* UIWindow_Create(const char* title, int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO) != 1) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        return NULL;
    }

    UIWindow* window = (UIWindow*)malloc(sizeof(UIWindow));
    if (window == NULL) return NULL;

    window->title = NULL;
    window->x = 0;
    window->y = 0;
    window->z = 0;
    window->visible = 1;
    window->scaleMode = UIWindowWindowed;
    window->width = width;
    window->height = height;
    window->backgroundColor = UIColorWhite;
    window->children = NULL;
    window->sdlWindow = NULL;
    window->sdlRenderer = NULL;
    window->framerate = 0.0f;
    window->events = NULL;
    
    // Initialize UI properties
    window->__ui_props.capacity = 10; // Initial capacity
    window->__ui_props.count = 0;
    window->__ui_props.props = (UIProp**)calloc(window->__ui_props.capacity, sizeof(UIProp*));
    if (!window->__ui_props.props) {
        fprintf(stderr, "Failed to allocate memory for UI properties\n");
        free(window);
        return NULL;
    }
    
    unsigned int* max_events = malloc(sizeof(unsigned int));
    if (!max_events) {
        fprintf(stderr, "Failed to allocate memory for max_events\n");
        free(window->__ui_props.props);
        free(window);
        return NULL;
    }
    *max_events = 10;
    UIWindow_SetProperty(window, UI_PROP_MAX_EVENTS, max_events);

    const unsigned int MAX_EVENTS = *max_events;
    window->events = (UIEventCallbackData**)calloc(MAX_EVENTS, sizeof(UIEventCallbackData*));
    if (!window->events) {
        fprintf(stderr, "Failed to allocate memory for events\n");
        free(max_events);
        free(window->__ui_props.props);
        free(window);
        return NULL;
    }

    SDL_Window* sdlWindow = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!sdlWindow) {
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
        free(window->events);
        free(window->__ui_props.props);
        free(window);
        return NULL;
    }

    SDL_Renderer* sdlRenderer = SDL_CreateRenderer(sdlWindow, NULL);
    if (!sdlRenderer) {
        fprintf(stderr, "SDL_CreateRenderer Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(sdlWindow);
        free(window->events);
        free(window->__ui_props.props);
        free(window);
        return NULL;
    }

    window->title = _strdup(title);
    if (!window->title) {
        fprintf(stderr, "Failed to allocate memory for title\n");
        SDL_DestroyRenderer(sdlRenderer);
        SDL_DestroyWindow(sdlWindow);
        free(window->events);
        free(window->__ui_props.props);
        free(window);
        return NULL;
    }
    
    window->sdlWindow = sdlWindow;
    window->sdlRenderer = sdlRenderer;

    SDL_SetRenderDrawBlendMode(sdlRenderer, SDL_BLENDMODE_BLEND);

    return window;
}

void UIWindow_SetEventCallback(UIWindow* window, UI_EVENT event, UIEventCallback callback) {
    if (!window || !callback) return;

    const unsigned int* max_events_ptr = (unsigned int*)UIWindow_GetProperty(window, UI_PROP_MAX_EVENTS);
    const unsigned int MAX_EVENTS = max_events_ptr ? *max_events_ptr : 0;
    if (event >= MAX_EVENTS) return;

    if (window->events == NULL) {
        window->events = (UIEventCallbackData**)calloc(MAX_EVENTS, sizeof(UIEventCallbackData*));
        if (!window->events) {
            fprintf(stderr, "Failed to allocate memory for events\n");
            return;
        }
    }

    // Free previous callback if it exists
    if (window->events[event]) {
        free(window->events[event]);
        window->events[event] = NULL;
    }

    UIEventCallbackData* callbackData = malloc(sizeof(UIEventCallbackData));
    if (!callbackData) {
        fprintf(stderr, "Failed to allocate memory for callback data\n");
        return;
    }
    
    callbackData->cb = callback;
    window->events[event] = callbackData;
}

void UIWindow_SetProperty(UIWindow* window, const char* property, void* value) {
    if (!window || !property || !value) return;

    // Verify if the property already exists
    for (unsigned int i = 0; i < window->__ui_props.count; i++) {
        if (window->__ui_props.props[i] && strcmp(window->__ui_props.props[i]->key, property) == 0) {
            window->__ui_props.props[i]->value = value;
            return;
        }
    }
    
    // Verify if we need to expand the properties array
    if (window->__ui_props.count >= window->__ui_props.capacity) {
        unsigned int new_capacity = window->__ui_props.capacity * 2;
        UIProp** new_props = (UIProp**)realloc(window->__ui_props.props, new_capacity * sizeof(UIProp*));
        if (!new_props) {
            fprintf(stderr, "Failed to reallocate memory for UI properties\n");
            return;
        }
        window->__ui_props.props = new_props;
        window->__ui_props.capacity = new_capacity;
        
        // Initialize new properties to NULL
        for (unsigned int i = window->__ui_props.count; i < new_capacity; i++) {
            window->__ui_props.props[i] = NULL;
        }
    }
    
    // Add the new property
    UIProp* new_prop = (UIProp*)malloc(sizeof(UIProp));
    if (!new_prop) {
        fprintf(stderr, "Failed to allocate memory for new UIProp\n");
        return;
    }
    
    new_prop->key = _strdup(property);
    if (!new_prop->key) {
        fprintf(stderr, "Failed to allocate memory for property key\n");
        free(new_prop);
        return;
    }
    
    new_prop->value = value;
    window->__ui_props.props[window->__ui_props.count++] = new_prop;
}

void* UIWindow_GetProperty(UIWindow* window, const char* property) {
    if (!window || !property) return NULL;

    for (unsigned int i = 0; i < window->__ui_props.count; i++) {
        if (window->__ui_props.props[i] && strcmp(window->__ui_props.props[i]->key, property) == 0) {
            return window->__ui_props.props[i]->value;
        }
    }
    return NULL;
}

void UIWindow_Destroy(UIWindow* window) {
    if (!window) return;
    
    // Destroy smooth texture
    if (g_smoothTexture) {
        SDL_DestroyTexture(g_smoothTexture);
        g_smoothTexture = NULL;
        g_smoothW = g_smoothH = 0;
    }
    
    // Free font cache
    FontCacheEntry *e = g_fontCache;
    while (e) {
        TTF_CloseFont(e->font);
        free(e->path);
        FontCacheEntry *n = e->next;
        free(e);
        e = n;
    }
    g_fontCache = NULL;
    TTF_Quit();

    // Destroy childs
    if (window->children) {
        UIChildren_Destroy(window->children);
        window->children = NULL;
    }

    // Free events
    if (window->events) {
        const unsigned int* max_events_ptr = (unsigned int*)UIWindow_GetProperty(window, UI_PROP_MAX_EVENTS);
        const unsigned int MAX_EVENTS = max_events_ptr ? *max_events_ptr : 0;
        for (unsigned int i = 0; i < MAX_EVENTS; i++) {
            if (window->events[i]) {
                free(window->events[i]);
            }
        }
        free(window->events);
        window->events = NULL;
    }

    // Free properties
    if (window->__ui_props.props != NULL) {
        for (unsigned int i = 0; i < window->__ui_props.count; i++) {
            if (window->__ui_props.props[i]) {
                if (window->__ui_props.props[i]->key) {
                    // If the property is UI_PROP_MAX_EVENTS, free the value
                    if (strcmp(window->__ui_props.props[i]->key, UI_PROP_MAX_EVENTS) == 0) {
                        free(window->__ui_props.props[i]->value);
                    }
                    free(window->__ui_props.props[i]->key);
                }
                free(window->__ui_props.props[i]);
            }
        }
        free(window->__ui_props.props);
        window->__ui_props.props = NULL;
    }

    if (window->sdlRenderer) {
        SDL_DestroyRenderer(window->sdlRenderer);
        window->sdlRenderer = NULL;
    }
    
    if (window->sdlWindow) {
        SDL_DestroyWindow(window->sdlWindow);
        window->sdlWindow = NULL;
    }
    
    free(window->title);
    free(window);
}