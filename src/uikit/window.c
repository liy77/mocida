#include <uikit/window.h>
#include <curl/curl.h>

/**
 * FontCacheEntry structure representing a cached font.
 * @private
 */
typedef struct FontCacheEntry {
    char *path;
    float size;
    TTF_Font *font;
    struct FontCacheEntry *next;
} FontCacheEntry;

static FontCacheEntry *g_fontCache = NULL;

// Configuration for quality vs performance control
// Significantly reduced to prioritize performance
#define USE_ANTIALIASING 0       // Disable anti-aliasing for maximum performance
#define UPSCALE_FACTOR 1         // No upscaling (was 4)
#define SUPERSAMPLING 1          // No supersampling (was 8)
#define MAX_CIRCLE_CACHE 16
#define USE_BATCHED_RENDERING 1  // Enable batch rendering for better performance
#define DISABLE_VSYNC 1          // Disable vsync for maximum FPS
#define MAX_BATCH_SIZE 1024      // Maximum rectangles in a batch

// Texture cache
static SDL_Texture *g_smoothTexture = NULL;
static int g_smoothW = 0, g_smoothH = 0;

// Circle cache
static SDL_Texture* g_circleCache[MAX_CIRCLE_CACHE] = {NULL};
static int g_circleCacheSizes[MAX_CIRCLE_CACHE] = {0};

// Pre-allocated frequently used rectangles
static SDL_FRect g_tempRect1 = {0};
static SDL_FRect g_tempRect2 = {0};

// Flag for border drawing in shader
static int g_drawBorder = 0;
static float g_borderWidth = 0;
static UIColor g_borderColor = {0};

// Forward declarations to fix compilation errors
static SDL_Texture* GetCachedCircleTexture(SDL_Renderer* renderer, int size);
void CleanupCircleCache(void);

#if USE_BATCHED_RENDERING
typedef struct {
    SDL_FRect rect;
    SDL_Color color;
} RenderBatchItem;

static RenderBatchItem g_rectBatch[MAX_BATCH_SIZE];
static int g_batchSize = 0;

// Helper function to flush the batch
static void FlushRenderBatch(SDL_Renderer* renderer) {
    if (g_batchSize > 0) {
        for (int i = 0; i < g_batchSize; i++) {
            SDL_SetRenderDrawColor(renderer, 
                                  g_rectBatch[i].color.r, 
                                  g_rectBatch[i].color.g, 
                                  g_rectBatch[i].color.b, 
                                  g_rectBatch[i].color.a);
            SDL_RenderFillRect(renderer, &g_rectBatch[i].rect);
        }
        g_batchSize = 0;
    }
}

// Function to add a rectangle to the batch
static void BatchRect(SDL_Renderer* renderer, SDL_FRect* rect, SDL_Color color) {
    // Check if batch is full
    if (g_batchSize >= MAX_BATCH_SIZE) {
        FlushRenderBatch(renderer);
    }
    
    // Add to batch
    g_rectBatch[g_batchSize].rect = *rect;
    g_rectBatch[g_batchSize].color = color;
    g_batchSize++;
}
#endif

static TTF_Font* GetFont(const char *path, float size) {
    // Check if font is already cached
    FontCacheEntry *e = g_fontCache;
    while (e) {
        if (e->size == size && strcmp(e->path, path) == 0)
            return e->font;
        e = e->next;
    }
    
    // Load new font
    TTF_Font *f = TTF_OpenFont(path, size);
    if (!f) return NULL;
    
    // Add to cache
    e = malloc(sizeof(FontCacheEntry));
    if (!e) {
        TTF_CloseFont(f);
        return NULL;
    }
    
    e->path = _strdup(path);
    if (!e->path) {
        free(e);
        TTF_CloseFont(f);
        return NULL;
    }
    
    e->size = size;
    e->font = f;
    e->next = g_fontCache;
    g_fontCache = e;
    
    // Apply optimization settings to the font
    TTF_SetFontHinting(f, TTF_HINTING_MONO); // Faster than NORMAL
    
    return f;
}

static inline void ApplyMargins(SDL_FRect* r, float ml, float mt, float mr, float mb) {
    // Fast single-instruction margin application
    r->x += ml;
    r->y += mt;
    r->w -= (ml + mr);
    r->h -= (mt + mb);
}

// Version optimized for speed, not quality
static SDL_Texture* GetCachedCircleTexture(SDL_Renderer* renderer, int size) {
    // Check cache
    for (int i = 0; i < MAX_CIRCLE_CACHE; i++) {
        if (g_circleCacheSizes[i] == size && g_circleCache[i] != NULL) {
            return g_circleCache[i];
        }
    }
    
    // Find slot or replace oldest one
    int slotToUse = 0;
    for (int i = 0; i < MAX_CIRCLE_CACHE; i++) {
        if (g_circleCache[i] == NULL) {
            slotToUse = i;
            break;
        }
    }
    
    // Free old texture if needed
    if (g_circleCache[slotToUse] != NULL) {
        SDL_DestroyTexture(g_circleCache[slotToUse]);
        g_circleCache[slotToUse] = NULL;
    }
    
    // Reduced size for better performance - we don't need high quality
    const int TEXTURE_SIZE = size;
    
    // Create new texture
    SDL_Texture* texture = SDL_CreateTexture(renderer, 
                                           SDL_PIXELFORMAT_RGBA8888, 
                                           SDL_TEXTUREACCESS_TARGET, 
                                           TEXTURE_SIZE, TEXTURE_SIZE);
    
    if (!texture) return NULL;
    
    // Set properties
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    
    // Render to texture
    SDL_Texture* prevTarget = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, texture);
    
    // Clear with transparency
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    
    // Draw white circle (will be tinted when used)
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    
    // Center and radius
    float centerX = TEXTURE_SIZE * 0.5f;
    float centerY = TEXTURE_SIZE * 0.5f;
    float radius = TEXTURE_SIZE * 0.5f;
    
    // Optimized method for circle rendering
    int radiusInt = (int)radius;
    
    // Use an optimized algorithm with fewer operations
    // This is much faster than the standard midpoint circle algorithm
    for (int y = -radiusInt; y <= radiusInt; y++) {
        int widthAtY = (int)(sqrtf(radius * radius - y * y) + 0.5f);
        
        // Draw horizontal line at this height - much faster than individual points
        if (widthAtY > 0) {
            SDL_FRect line = {
                centerX - widthAtY, // Start x
                centerY + y,       // Y position
                widthAtY * 2,      // Width
                1.0f               // Height (just 1 pixel)
            };
            SDL_RenderFillRect(renderer, &line);
        }
    }
    
    // Restore render target
    SDL_SetRenderTarget(renderer, prevTarget);
    
    // Add to cache
    g_circleCache[slotToUse] = texture;
    g_circleCacheSizes[slotToUse] = size;
    
    return texture;
}

// Highly optimized version for high performance
void DrawRoundedRectFill(SDL_Renderer* rend, SDL_FRect rect, UIColor color, float radius) {
    // For very small radii, rendering a single rectangle is more efficient
    if (radius <= 1.0f) {
        SDL_Color sdlColor = {(Uint8)color.r, (Uint8)color.g, (Uint8)color.b, 
                            (Uint8)SDL_clamp((int)(color.a * 255), 0, 255)};
        
        #if USE_BATCHED_RENDERING
        // Use batch rendering
        BatchRect(rend, &rect, sdlColor);
        #else
        // Direct rendering
        SDL_SetRenderDrawColor(rend, sdlColor.r, sdlColor.g, sdlColor.b, sdlColor.a);
        SDL_RenderFillRect(rend, &rect);
        #endif
        
        return;
    }
    
    // For rounded rectangles, use simplified approach
    // If radius is large compared to size, use simpler approach
    if (radius > 0.4f * SDL_min(rect.w, rect.h)) {
        // Simplified method for large corners - just a center rectangle
        SDL_FRect centerRect = {
            rect.x + radius,
            rect.y + radius,
            rect.w - 2 * radius, 
            rect.h - 2 * radius
        };
        
        if (centerRect.w > 0 && centerRect.h > 0) {
            SDL_Color sdlColor = {(Uint8)color.r, (Uint8)color.g, (Uint8)color.b, 
                                (Uint8)SDL_clamp((int)(color.a * 255), 0, 255)};
            
            #if USE_BATCHED_RENDERING
            // Flush previous batch to switch to texture rendering
            FlushRenderBatch(rend);
            #endif
            
            SDL_SetRenderDrawColor(rend, sdlColor.r, sdlColor.g, sdlColor.b, sdlColor.a);
            SDL_RenderFillRect(rend, &centerRect);
            
            // Use a single pre-rendered texture for all corners
            int cornerSize = (int)ceilf(radius * 2);
            SDL_Texture* cornerTexture = GetCachedCircleTexture(rend, cornerSize);
            
            if (cornerTexture) {
                // Set color for texture
                SDL_SetTextureColorMod(cornerTexture, sdlColor.r, sdlColor.g, sdlColor.b);
                SDL_SetTextureAlphaMod(cornerTexture, sdlColor.a);
                
                // Render all 4 corners using same texture
                // Top left
                g_tempRect1.x = rect.x;
                g_tempRect1.y = rect.y;
                g_tempRect1.w = radius;
                g_tempRect1.h = radius;
                SDL_RenderTexture(rend, cornerTexture, NULL, &g_tempRect1);
                
                // Top right
                g_tempRect1.x = rect.x + rect.w - radius;
                SDL_RenderTexture(rend, cornerTexture, NULL, &g_tempRect1);
                
                // Bottom left
                g_tempRect1.x = rect.x;
                g_tempRect1.y = rect.y + rect.h - radius;
                SDL_RenderTexture(rend, cornerTexture, NULL, &g_tempRect1);
                
                // Bottom right
                g_tempRect1.x = rect.x + rect.w - radius;
                SDL_RenderTexture(rend, cornerTexture, NULL, &g_tempRect1);
            }
        }
    } else {
        // For small corners, use 5-rectangle filling (more efficient)
        SDL_Color sdlColor = {(Uint8)color.r, (Uint8)color.g, (Uint8)color.b, 
                            (Uint8)SDL_clamp((int)(color.a * 255), 0, 255)};
        
        #if USE_BATCHED_RENDERING
        // Use batch rendering for all rectangles
        // Center rectangle
        g_tempRect1.x = rect.x + radius;
        g_tempRect1.y = rect.y + radius;
        g_tempRect1.w = rect.w - 2*radius;
        g_tempRect1.h = rect.h - 2*radius;
        BatchRect(rend, &g_tempRect1, sdlColor);
        
        // Top
        g_tempRect1.x = rect.x + radius;
        g_tempRect1.y = rect.y;
        g_tempRect1.w = rect.w - 2*radius;
        g_tempRect1.h = radius;
        BatchRect(rend, &g_tempRect1, sdlColor);
        
        // Bottom
        g_tempRect1.x = rect.x + radius;
        g_tempRect1.y = rect.y + rect.h - radius;
        g_tempRect1.w = rect.w - 2*radius;
        g_tempRect1.h = radius;
        BatchRect(rend, &g_tempRect1, sdlColor);
        
        // Left
        g_tempRect1.x = rect.x;
        g_tempRect1.y = rect.y + radius;
        g_tempRect1.w = radius;
        g_tempRect1.h = rect.h - 2*radius;
        BatchRect(rend, &g_tempRect1, sdlColor);
        
        // Right
        g_tempRect1.x = rect.x + rect.w - radius;
        g_tempRect1.y = rect.y + radius;
        g_tempRect1.w = radius;
        g_tempRect1.h = rect.h - 2*radius;
        BatchRect(rend, &g_tempRect1, sdlColor);
        
        // Process batch at the end
        FlushRenderBatch(rend);
        #else
        // Direct rendering
        SDL_SetRenderDrawColor(rend, sdlColor.r, sdlColor.g, sdlColor.b, sdlColor.a);
        
        // Center rectangle
        g_tempRect1.x = rect.x + radius;
        g_tempRect1.y = rect.y + radius;
        g_tempRect1.w = rect.w - 2*radius;
        g_tempRect1.h = rect.h - 2*radius;
        SDL_RenderFillRect(rend, &g_tempRect1);
        
        // Top
        g_tempRect1.x = rect.x + radius;
        g_tempRect1.y = rect.y;
        g_tempRect1.w = rect.w - 2*radius;
        g_tempRect1.h = radius;
        SDL_RenderFillRect(rend, &g_tempRect1);
        
        // Bottom
        g_tempRect1.x = rect.x + radius;
        g_tempRect1.y = rect.y + rect.h - radius;
        g_tempRect1.w = rect.w - 2*radius;
        g_tempRect1.h = radius;
        SDL_RenderFillRect(rend, &g_tempRect1);
        
        // Left
        g_tempRect1.x = rect.x;
        g_tempRect1.y = rect.y + radius;
        g_tempRect1.w = radius;
        g_tempRect1.h = rect.h - 2*radius;
        SDL_RenderFillRect(rend, &g_tempRect1);
        
        // Right
        g_tempRect1.x = rect.x + rect.w - radius;
        g_tempRect1.y = rect.y + radius;
        g_tempRect1.w = radius;
        g_tempRect1.h = rect.h - 2*radius;
        SDL_RenderFillRect(rend, &g_tempRect1);
        #endif
    }
}

// Version optimized for speed, not quality
void DrawCircle(SDL_Renderer* renderer, float centerX, float centerY, float radius, UIColor color) {
    // Use pre-rendered texture for circles
    int size = (int)ceilf(radius * 2);
    SDL_Texture* circleTexture = GetCachedCircleTexture(renderer, size);
    
    if (circleTexture) {
        // Set color
        SDL_SetTextureColorMod(circleTexture, (Uint8)color.r, (Uint8)color.g, (Uint8)color.b);
        SDL_SetTextureAlphaMod(circleTexture, (Uint8)SDL_clamp((int)(color.a * 255), 0, 255));
        
        // Render
        g_tempRect1.x = centerX - radius;
        g_tempRect1.y = centerY - radius;
        g_tempRect1.w = radius * 2;
        g_tempRect1.h = radius * 2;
        SDL_RenderTexture(renderer, circleTexture, NULL, &g_tempRect1);
    }
}

// Optimized version for performance
void DrawRoundedRectWithBorder(SDL_Renderer* renderer, SDL_FRect rect, 
                              UIColor fillColor, float radius, 
                              int borderWidth, UIColor borderColor) {
    if (!renderer || rect.w <= 0 || rect.h <= 0) return;

    // Draw border if needed
    if (borderWidth > 0) {
        DrawRoundedRectFill(renderer, rect, borderColor, radius);
        
        // Draw interior
        SDL_FRect inner = {
            rect.x + borderWidth,
            rect.y + borderWidth,
            rect.w - 2 * borderWidth,
            rect.h - 2 * borderWidth
        };
        
        if (inner.w > 0 && inner.h > 0) {
            float innerRadius = SDL_max(0, radius - borderWidth);
            DrawRoundedRectFill(renderer, inner, fillColor, innerRadius);
        }
    } else {
        // No border, just draw the main rectangle
        DrawRoundedRectFill(renderer, rect, fillColor, radius);
    }
}

// Cleanup circle cache
void CleanupCircleCache(void) {
    for (int i = 0; i < MAX_CIRCLE_CACHE; i++) {
        if (g_circleCache[i] != NULL) {
            SDL_DestroyTexture(g_circleCache[i]);
            g_circleCache[i] = NULL;
            g_circleCacheSizes[i] = 0;
        }
    }
}

// Additional optimization hints for SDL3
void OptimizeSDLForHighPerformance(SDL_Renderer* renderer) {
    // Set best performance driver hints (valid for SDL3)
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
    SDL_SetHint(SDL_HINT_RENDER_LINE_METHOD, "3");
    
    #if DISABLE_VSYNC
    // Disable VSync for maximum performance
    SDL_GL_SetSwapInterval(0);
    #endif
    
    // Suppress unused parameter warning
    (void)renderer;
}

// Cleanup all resources used by the UI system
void UICleanupAll(void) {
    // Clean texture caches
    CleanupCircleCache();
    
    if (g_smoothTexture) {
        SDL_DestroyTexture(g_smoothTexture);
        g_smoothTexture = NULL;
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
    
    // Shutdown TTF system
    TTF_Quit();
}

// Main rendering function - optimized for high performance
int UIWindow_Render(UIWindow* window) {
    if (!window || !window->sdlRenderer) return -1;
    
    static int rw = 0, rh = 0;
    
    // Optimization: Check if window size changed before querying
    static Uint64 lastResize = 0;
    Uint64 now = SDL_GetTicks();
    
    // Update size only every 500ms if not set yet
    if (rw == 0 || rh == 0 || (now - lastResize) > 500) {
        SDL_GetWindowSize(window->sdlWindow, &rw, &rh);
        lastResize = now;
    }

    // Initialize TTF once
    static int ttfInited = 0;
    if (!ttfInited) {
        if (TTF_Init() != 1) {
            fprintf(stderr, "TTF_Init error: %s\n", SDL_GetError());
        }
        ttfInited = 1;
        
        // Configure global font quality
        TTF_SetFontHinting(NULL, TTF_HINTING_MONO); // Faster than NORMAL
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

    // Direct rendering to screen when possible for better performance
    if (USE_ANTIALIASING) {
        // Create or resize smooth texture if needed
        int tw = rw * UPSCALE_FACTOR, th = rh * UPSCALE_FACTOR;
        if (!g_smoothTexture || g_smoothW != tw || g_smoothH != th) {
            if (g_smoothTexture) SDL_DestroyTexture(g_smoothTexture);
            g_smoothTexture = SDL_CreateTexture(window->sdlRenderer,
                SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, tw, th);
            if (!g_smoothTexture) {
                fprintf(stderr, "Failed to create smooth texture\n");
                return -1;
            }
            g_smoothW = tw;
            g_smoothH = th;
            SDL_SetTextureScaleMode(g_smoothTexture, SDL_SCALEMODE_LINEAR);
        }
        
        // Render to intermediate texture
        SDL_SetRenderTarget(window->sdlRenderer, g_smoothTexture);
        SDL_SetRenderScale(window->sdlRenderer, (float)UPSCALE_FACTOR, (float)UPSCALE_FACTOR);
    } else {
        // Direct rendering to screen (faster)
        SDL_SetRenderTarget(window->sdlRenderer, NULL);
        SDL_SetRenderScale(window->sdlRenderer, 1.0f, 1.0f);
    }
    
    // Configure blending and clear buffer
    SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(window->sdlRenderer,
        (Uint8)window->backgroundColor.r,
        (Uint8)window->backgroundColor.g,
        (Uint8)window->backgroundColor.b,
        (Uint8)SDL_clamp((int)(window->backgroundColor.a * 255), 0, 255));
    SDL_RenderClear(window->sdlRenderer);

    // Render all UI elements
    if (window->children) {
        UIChildren_SortByZ(window->children);
        
        for (int i = 0; i < window->children->count; i++) {
            UIWidget *el = window->children->children[i];
            if (!el || !el->visible || !el->data) continue;
            
            if (el->alignment) UIAlignment_Align(el);
            UIWidgetBase *base = (UIWidgetBase*)el->data;

            if (!strcmp(base->__widget_type, UI_WIDGET_RECTANGLE)) {
                UIRectangle *rect = (UIRectangle*)base;
                g_tempRect1.x = el->x;
                g_tempRect1.y = el->y;
                g_tempRect1.w = *el->width;
                g_tempRect1.h = *el->height;
                
                ApplyMargins(&g_tempRect1, rect->marginLeft, rect->marginTop,
                           rect->marginRight, rect->marginBottom);
                
                // Detect if it's a circle (width == height and radius is half width)
                if (g_tempRect1.w > 0 && g_tempRect1.h > 0) {
                    if (fabsf(g_tempRect1.w - g_tempRect1.h) < 0.5f && 
                        fabsf(rect->radius - g_tempRect1.w/2) < 0.5f) {
                        // Circle
                        float centerX = g_tempRect1.x + g_tempRect1.w/2;
                        float centerY = g_tempRect1.y + g_tempRect1.h/2;
                        float radius = g_tempRect1.w/2;
                        
                        // Draw border if needed
                        if (rect->borderWidth > 0) {
                            DrawCircle(window->sdlRenderer, centerX, centerY, 
                                     radius, rect->borderColor);
                            radius -= rect->borderWidth;
                        }
                        
                        // Draw inner circle
                        if (radius > 0) {
                            DrawCircle(window->sdlRenderer, centerX, centerY, 
                                     radius, rect->color);
                        }
                    } else {
                        // Normal rounded rectangle
                        DrawRoundedRectWithBorder(window->sdlRenderer, g_tempRect1,
                                               rect->color, rect->radius,
                                               (int)rect->borderWidth, rect->borderColor);
                    }
                }
            }
            else if (!strcmp(base->__widget_type, UI_WIDGET_TEXT)) {
                UIText *twid = (UIText*)base;
                if (!twid->text || !twid->fontFamily || !*twid->text || !twid->textLength || twid->fontSize <= 0.0f)
                    continue;
                
                // Only recreate texture if necessary
                if (!twid->__SDL_textTexture) {
                    // Use normal font size, no upscaling
                    TTF_Font *font = GetFont(twid->fontFamily, twid->fontSize);
                    if (font) {
                        // Configure rendering quality
                        TTF_SetFontHinting(font, TTF_HINTING_NORMAL);  // Faster than LIGHT
                        
                        SDL_Color sc = {(Uint8)twid->color.r,
                                      (Uint8)twid->color.g,
                                      (Uint8)twid->color.b,
                                      (Uint8)SDL_clamp((int)(twid->color.a * 255), 0, 255)};
                        
                        // Use faster rendering for text
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
                
                // Set scale mode for text
                SDL_SetTextureScaleMode(twid->__SDL_textTexture, SDL_SCALEMODE_NEAREST);  // Faster than LINEAR
                
                // Query texture size
                int txw, txh;
                SDL_GetTextureSize(twid->__SDL_textTexture, &txw, &txh);

                // Apply padding
                float pw = txw + twid->paddingLeft + twid->paddingRight;
                float ph = txh + twid->paddingTop + twid->paddingBottom;
                g_tempRect1.x = el->x;
                g_tempRect1.y = el->y; 
                g_tempRect1.w = pw;
                g_tempRect1.h = ph;

                // Draw background
                if (fabsf(twid->background->radius - g_tempRect1.w/2) < 0.5f && 
                    fabsf(g_tempRect1.w - g_tempRect1.h) < 0.5f) {
                    // Background is a circle
                    float centerX = g_tempRect1.x + g_tempRect1.w/2;
                    float centerY = g_tempRect1.y + g_tempRect1.h/2;
                    DrawCircle(window->sdlRenderer, centerX, centerY, 
                             g_tempRect1.w/2, twid->background->color);
                } else {
                    // Background is a rounded rectangle
                    DrawRoundedRectWithBorder(window->sdlRenderer, g_tempRect1,
                                           twid->background->color,
                                           twid->background->radius,
                                           (int)twid->background->borderWidth,
                                           twid->background->borderColor);
                }

                // Draw text inside padded area
                g_tempRect2.x = g_tempRect1.x + twid->paddingLeft;
                g_tempRect2.y = g_tempRect1.y + twid->paddingTop;
                g_tempRect2.w = txw; 
                g_tempRect2.h = txh;
                SDL_RenderTexture(window->sdlRenderer, twid->__SDL_textTexture,
                               NULL, &g_tempRect2);
            }
            else if (strcmp(base->__widget_type, UI_WIDGET_IMAGE) == 0) {
                // Handle image rendering with high quality
                UIImage *img = (UIImage*)base;
                if (!img->__SDL_texture || !img->source) continue;

                // Direct rendering - no intermediate textures
                g_tempRect1.x = el->x;
                g_tempRect1.y = el->y;
                g_tempRect1.w = *el->width;
                g_tempRect1.h = *el->height;
                SDL_RenderTexture(window->sdlRenderer, img->__SDL_texture, NULL, &g_tempRect1);
            }
        }
        
        // Ensure any batched drawing is completed
        #if USE_BATCHED_RENDERING
        FlushRenderBatch(window->sdlRenderer);
        #endif
    }

    // If using anti-aliasing, copy to final destination
    if (USE_ANTIALIASING) {
        // Switch to default render target
        SDL_SetRenderTarget(window->sdlRenderer, NULL);
        SDL_SetRenderScale(window->sdlRenderer, 1.0f, 1.0f);
        
        // Use high-quality downscaling
        SDL_SetTextureScaleMode(g_smoothTexture, SDL_SCALEMODE_LINEAR);
        
        g_tempRect1.x = 0;
        g_tempRect1.y = 0;
        g_tempRect1.w = (float)rw;
        g_tempRect1.h = (float)rh;
        SDL_RenderTexture(window->sdlRenderer, g_smoothTexture, NULL, &g_tempRect1);
    }
    
    // Present final frame
    SDL_RenderPresent(window->sdlRenderer);
    
    return 0;
}

// Function to emit events - minimal change from original
void UIWindow_EmitEvent(UIWindow* window, UI_EVENT event, UIEventData data) {
    if (!window || !window->events) return;

    const unsigned int* max_events_ptr = (unsigned int*)UIWindow_GetProperty(window, UI_PROP_MAX_EVENTS);
    const unsigned int MAX_EVENTS = max_events_ptr ? *max_events_ptr : 0;
    if (event >= MAX_EVENTS) return;

    UIEventCallbackData* callbackData = window->events[event];
    if (callbackData != NULL) {
        callbackData->cb(data);
    }
}

// Function to retrieve UI property value
void* UIWindow_GetProperty(UIWindow* window, const char* property) {
    if (!window || !property) return NULL;

    // Fast lookup for common properties
    if (strcmp(property, UI_PROP_MAX_EVENTS) == 0 && window->__ui_props.count > 0) {
        // Assuming MAX_EVENTS is usually the first property
        if (window->__ui_props.props[0] && strcmp(window->__ui_props.props[0]->key, UI_PROP_MAX_EVENTS) == 0) {
            return window->__ui_props.props[0]->value;
        }
    }

    // Default lookup for other properties
    for (unsigned int i = 0; i < window->__ui_props.count; i++) {
        if (window->__ui_props.props[i] && strcmp(window->__ui_props.props[i]->key, property) == 0) {
            return window->__ui_props.props[i]->value;
        }
    }
    return NULL;
}

// Function to set UI property
void UIWindow_SetProperty(UIWindow* window, const char* property, void* value) {
    if (!window || !property || !value) return;

    // Check if property already exists
    for (unsigned int i = 0; i < window->__ui_props.count; i++) {
        if (window->__ui_props.props[i] && strcmp(window->__ui_props.props[i]->key, property) == 0) {
            window->__ui_props.props[i]->value = value;
            return;
        }
    }
    
    // Expand properties array if needed
    if (window->__ui_props.count >= window->__ui_props.capacity) {
        unsigned int new_capacity = window->__ui_props.capacity * 2;
        UIProp** new_props = (UIProp**)realloc(window->__ui_props.props, new_capacity * sizeof(UIProp*));
        if (!new_props) {
            fprintf(stderr, "Failed to reallocate memory for UI properties\n");
            return;
        }
        window->__ui_props.props = new_props;
        window->__ui_props.capacity = new_capacity;
        
        // Initialize new properties as NULL
        for (unsigned int i = window->__ui_props.count; i < new_capacity; i++) {
            window->__ui_props.props[i] = NULL;
        }
    }
    
    // Add new property
    UIProp* new_prop = (UIProp*)malloc(sizeof(UIProp));
    if (!new_prop) return;
    
    char* key_copy = _strdup(property);
    if (!key_copy) {
        free(new_prop);
        return;
    }
    
    new_prop->key = key_copy;
    new_prop->value = value;
    window->__ui_props.props[window->__ui_props.count++] = new_prop;
}

// Create a new UI window with optimized settings
UIWindow* UIWindow_Create(const char* title, int width, int height) {
    // Initialize SDL with options optimized for performance
    if (SDL_Init(SDL_INIT_VIDEO) != 1) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        return NULL;
    }
    
    // Configure OpenGL attributes for better performance before creating window
    // Note: In SDL3, some attributes might need to be updated
    if (SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) != 1) {
        fprintf(stderr, "Failed to set SDL_GL_DOUBLEBUFFER: %s\n", SDL_GetError());
        // Continue anyway - not critical
    }
    
    // More conservative settings to avoid compatibility issues
    if (SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16) != 1) {
        fprintf(stderr, "Failed to set SDL_GL_DEPTH_SIZE: %s\n", SDL_GetError());
    }
    
    if (SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0) != 1) {
        fprintf(stderr, "Failed to set SDL_GL_STENCIL_SIZE: %s\n", SDL_GetError());
    }
    
    // Use more compatible OpenGL version settings - SDL3 might have different requirements
    if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) != 1) {
        fprintf(stderr, "Failed to set GL_CONTEXT_MAJOR_VERSION: %s\n", SDL_GetError());
    }
    
    if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3) != 1) {
        fprintf(stderr, "Failed to set GL_CONTEXT_MINOR_VERSION: %s\n", SDL_GetError());
    }
    
    if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) != 1) {
        fprintf(stderr, "Failed to set GL_CONTEXT_PROFILE_MASK: %s\n", SDL_GetError());
    }
    
    // Configure refresh rate (don't limit frame rate to achieve over 1000 FPS)
    #if DISABLE_VSYNC
    if (SDL_GL_SetSwapInterval(0) != 1) {
        fprintf(stderr, "Failed to disable VSync: %s\n", SDL_GetError());
    }
    #endif

    // Allocate window structure
    UIWindow* window = (UIWindow*)malloc(sizeof(UIWindow));
    if (window == NULL) {
        fprintf(stderr, "Failed to allocate memory for UIWindow\n");
        return NULL;
    }

    // Initialize with default values
    memset(window, 0, sizeof(UIWindow));
    window->width = width;
    window->height = height;
    window->backgroundColor = UI_COLOR_WHITE;
    window->visible = 1;
    
    // Initialize UI properties
    window->__ui_props.capacity = 10;
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

    window->events = (UIEventCallbackData**)calloc(*max_events, sizeof(UIEventCallbackData*));
    if (!window->events) {
        fprintf(stderr, "Failed to allocate memory for events\n");
        free(max_events);
        free(window->__ui_props.props);
        free(window);
        return NULL;
    }

    // Create window with simpler flags to avoid compatibility issues
    uint32_t window_flags = SDL_WINDOW_RESIZABLE;
    
    // Get available display mode
    SDL_DisplayID displayID = SDL_GetPrimaryDisplay();
    if (displayID == 0) {
        fprintf(stderr, "Failed to get primary display: %s\n", SDL_GetError());
        // Continue anyway, using default settings
    } else {
        // We successfully got the display, we can query more information if needed
        fprintf(stderr, "Using primary display: %u\n", displayID);
    }
    
    SDL_Window* sdlWindow = SDL_CreateWindow(title, width, height, window_flags);
    if (!sdlWindow) {
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
        free(window->events);
        free(window->__ui_props.props);
        free(window);
        return NULL;
    }

    // In SDL3, CreateRenderer has different parameters
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
        fprintf(stderr, "Failed to duplicate window title\n");
        SDL_DestroyRenderer(sdlRenderer);
        SDL_DestroyWindow(sdlWindow);
        free(window->events);
        free(window->__ui_props.props);
        free(window);
        return NULL;
    }
    
    window->sdlWindow = sdlWindow;
    window->sdlRenderer = sdlRenderer;

    // Configure blending
    if (SDL_SetRenderDrawBlendMode(sdlRenderer, SDL_BLENDMODE_BLEND) != 1) {
        fprintf(stderr, "Failed to set blend mode: %s\n", SDL_GetError());
        // Continue anyway - not critical
    }
    
    // Apply additional optimizations
    OptimizeSDLForHighPerformance(sdlRenderer);

    return window;
}

void UIWindow_SetEventCallback(UIWindow* window, UI_EVENT event, UIEventCallback callback) {
    if (!window || !callback) return;

    const unsigned int* max_events_ptr = (unsigned int*)UIWindow_GetProperty(window, UI_PROP_MAX_EVENTS);
    const unsigned int MAX_EVENTS = max_events_ptr ? *max_events_ptr : 0;
    if (event >= MAX_EVENTS) return;

    if (window->events == NULL) {
        window->events = (UIEventCallbackData**)calloc(MAX_EVENTS, sizeof(UIEventCallbackData*));
        if (!window->events) return;
    }

    // Free previous callback if it exists
    if (window->events[event]) {
        free(window->events[event]);
        window->events[event] = NULL;
    }

    UIEventCallbackData* callbackData = malloc(sizeof(UIEventCallbackData));
    if (!callbackData) return;
    
    callbackData->cb = callback;
    window->events[event] = callbackData;
}

void UIWindow_Destroy(UIWindow* window) {
    if (!window) return;
    
    // Clean smooth texture
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

    // Destroy children
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
                // If the property is UI_PROP_MAX_EVENTS, free the value
                if (strcmp(window->__ui_props.props[i]->key, UI_PROP_MAX_EVENTS) == 0) {
                    free(window->__ui_props.props[i]->value);
                }
                free((void*)window->__ui_props.props[i]->key);
            }
            free(window->__ui_props.props[i]);
        }
        
        free(window->__ui_props.props);
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
    CleanupCircleCache();
    free(window);
}