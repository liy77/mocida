#include <uikit/window.h>
#include <uikit/button.h>
#include <uikit/mouse_area.h>
#include <uikit/container.h>
#include <uikit/controls.h>
#include <uikit/textfield.h>
#include <uikit/textarea.h>
#include <uikit/webview.h>
#include <uikit/stack.h>
#include <uikit/glass.h>
#include <uikit/backdrop.h>
#include <uikit/dialog.h>
#include <uikit/tab.h>
#include <uikit/popup.h>
#include <uikit/video.h>
#include <uikit/file_drop.h>
#include <uikit/asset.h>
#include <uikit/overlay.h>
#include <uikit/debug.h>
#include <uikit/perf.h>
#include <uikit/bundle.h>
#include <uikit/screen.h>
#ifndef MOCIDA_IOS
#include <curl/curl.h>   /* vestigial; not linked on iOS */
#endif

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

// Tracks the most recently created (or explicitly activated) window so
// widget-level focus helpers (UITextField_SetFocus, etc.) can reach
// SDL_StartTextInput without the caller having to pass it in. The
// framework is single-window in practice; multi-window apps should call
// UIWindow_SetActive() explicitly.
static UIWindow* g_activeWindow = NULL;

// Configuration for quality vs performance control
#define USE_ANTIALIASING 1       // (legacy - always on now via analytic coverage)
#define UPSCALE_FACTOR 1         // No full-frame upscaling (uses screen size)
#define MAX_CIRCLE_CACHE 16
#define MAX_BATCH_SIZE 1024      // Maximum rectangles in a batch

// Bounds for the AA samples-per-side count used by analytic coverage.
#define MOCIDA_MIN_AA_SAMPLES 1
#define MOCIDA_MAX_AA_SAMPLES 16

// MSAA / quality. g_aaSamplesPerSide is samples-per-side: 1 = no AA,
// 4 = 4x4 = 16 SPP (default), 8 = 64 SPP (ultra). Public setter below.
// g_aaHintsApplied prevents re-applying OpenGL hints at the wrong time.
static int g_aaSamplesPerSide = 4;
static int g_aaHintsApplied   = 0;

// Custom (client-side) window decorations. When set BEFORE UIWindow_Create
// (via UIWindow_RequestCustomTitlebar), the window is created BORDERLESS so the
// app paints its own title bar; the app then marks a draggable region + resize
// borders via SDL_SetWindowHitTest (wired in app.c). Default 0 = native chrome.
static int g_customTitlebar = 0;

void UIWindow_RequestCustomTitlebar(int on) { g_customTitlebar = on ? 1 : 0; }
int  UIWindow_WantsCustomTitlebar(void)     { return g_customTitlebar; }

// Transparent (per-pixel alpha) window. Required for an OS system backdrop
// (Mica/Acrylic) to composite through the app's transparent pixels: the window
// must be created with SDL_WINDOW_TRANSPARENT so SDL builds a composition
// swapchain (DirectComposition on the D3D11 renderer) that DWM can blend behind.
// Set BEFORE UIWindow_Create. NOTE: only the D3D11 renderer honors this on
// Windows — the Vulkan/GL SDL renderers create an opaque swapchain regardless.
// Default 0 = opaque window. Mirrors the custom-titlebar request flag.
static int g_wantTransparent = 0;

void UIWindow_RequestTransparent(int on) { g_wantTransparent = on ? 1 : 0; }
int  UIWindow_WantsTransparent(void)     { return g_wantTransparent; }

// Full-frame AA pipeline. Matches UIAAMode in app.h:
//   0 NONE, 1 COVERAGE (default - no postfx), 2 SSAA_2X, 3 SSAA_4X,
//   4 FXAA, 5 TAA.
static int   g_aaMode   = 1;
static float g_taaBlend = 0.5f;

// Text supersampling factor. Glyphs are ALWAYS rasterized at g_textSS× the
// logical point size and drawn into a logical-sized destination rect. The
// caller lays out in logical units (it divides the rasterized texture size by
// g_textSS), never letting SDL upscale a small glyph texture - SDL's scaled
// blit shaves/crops glyph edge rows. Net effect:
//   * Coverage mode (frame drawn 1:1): the 2x glyph is LINEAR-downscaled to
//     the logical box -> supersampled, crisper text (helps synthetic bold).
//   * SSAA modes (frame drawn at 2x via SetRenderScale): the logical box maps
//     to 2x physical px, so the 2x glyph lands ~1:1 -> stays crisp instead of
//     being upscaled and blurred.
// Kept constant (not tied to the AA mode) so cached text textures never need
// rebuilding when the AA mode changes at runtime.
// 2 = glyphs rasterized at 2x and laid out in logical units → supersampled,
// crisper text (helps synthetic bold). The flicker that was once blamed on this
// turned out to be the line-number gutter rebuilding a texture every frame (now
// cached — see GetLineNumTexture); text textures are cached, so 2x rasterizes
// once and does not churn. Safe to keep on.
static int   g_textSS   = 2;

// TAA state. The implementation history is in the GIT log:
//
//   v1: uniform lerp on a GPU history texture - ghosting on motion.
//   v2: CPU readback + per-pixel motion mask - correct but cost a full
//       GPU→CPU transfer (~5-20ms on Vulkan/D3D) and a CPU pixel loop.
//   v3 (current): back on GPU with a single history target, blended
//       via SDL_BLENDMODE_BLEND + alpha mod. No readback. Two GPU draw
//       calls per frame. Ghosting is bounded by g_taaBlend; for typical
//       UI values (0.05-0.2) it's invisible on the mostly-static content
//       that dominates a UI scene. Motion-mask logic would require
//       readback again and is intentionally dropped.
static SDL_Texture* g_taaHistory = NULL;  // persistent GPU history target
static int g_taaHistoryW = 0, g_taaHistoryH = 0;
static int g_taaHistoryReady = 0;
// Legacy CPU buffers - retained as NULL for ABI parity; CleanupTaaHistory
// still frees them if a prior code path ever allocated them.
static Uint8* g_taaHistoryCpu = NULL;
static Uint8* g_taaScratchCpu = NULL;
static int    g_taaCpuW = 0, g_taaCpuH = 0;
// Motion threshold is now a no-op (motion detection needs readback,
// which is what v3 was created to remove). The setter stays for API
// compatibility but has no effect on the render path.
static int    g_taaMotionThreshold = 24;

// Texture cache
static SDL_Texture *g_smoothTexture = NULL;
static int g_smoothW = 0, g_smoothH = 0;

// Circle cache
static SDL_Texture* g_circleCache[MAX_CIRCLE_CACHE] = {NULL};
static int g_circleCacheSizes[MAX_CIRCLE_CACHE] = {0};

// Shadow cache. Entries are keyed by width/height/radius/blur/spread
// rounded to integers (granularity is fine enough for practical reuse -
// color is applied via SetTextureColorMod and is not part of the key).
// Implicit LRU: when the cache is full, the oldest slot is evicted.
#define MAX_SHADOW_CACHE 16
/** One slot in the rounded-rect drop-shadow texture cache. */
typedef struct {
    int w;             /**< Shape width without blur padding. */
    int h;             /**< Shape height without blur padding. */
    int radius;        /**< Corner radius the cached texture was generated for. */
    int blur;          /**< Blur radius (Gaussian sigma) baked into the texture. */
    int spread;        /**< Outward expansion in pixels applied before blur. */
    SDL_Texture* tex;  /**< ALPHA-only texture (RGB is always white). */
    int padding;       /**< Pixels added around the shape (= max(0, blur+spread)). */
    Uint64 lastUsed;   /**< Monotonic tick used for LRU eviction. */
} ShadowCacheEntry;
static ShadowCacheEntry g_shadowCache[MAX_SHADOW_CACHE] = {0};
static Uint64 g_shadowCacheTick = 0;

// Pre-allocated frequently used rectangles
static SDL_FRect g_tempRect1 = {0};
static SDL_FRect g_tempRect2 = {0};

// Forward declarations to fix compilation errors
static SDL_Texture* GetCachedCircleTexture(SDL_Renderer* renderer, int size);
void        CleanupCircleCache(void);
static void CleanupShadowCache(void);
static void CleanupTaaHistory(void);

/** One filled rectangle queued in the batched-rect render path. */
typedef struct {
    SDL_FRect rect;    /**< Destination rect in renderer space. */
    SDL_Color color;   /**< Fill color (premultiplied alpha respected). */
} RenderBatchItem;

static RenderBatchItem g_rectBatch[MAX_BATCH_SIZE];
static int g_batchSize = 0;

// Vertex / index scratch for the SDL_RenderGeometry flush below.
// One rect = 4 vertices + 6 indices (two triangles). At MAX_BATCH_SIZE
// = 1024 that's ~98 KB of vertices + 24 KB of indices in BSS - cheap
// and avoids any per-frame allocation.
static SDL_Vertex g_batchVerts[MAX_BATCH_SIZE * 4];
static int        g_batchIndices[MAX_BATCH_SIZE * 6];

// Helper function to flush the batch.
//
// The previous implementation did one SDL_SetRenderDrawColor +
// SDL_RenderFillRect per rect: 2*N SDL Render API calls, and on the
// D3D11 / Vulkan backends each call carries pipeline-state plumbing
// (constant buffer update, draw call, possible state-cache miss). For
// a typical UI scene that built up hundreds of "FillRect" requests per
// frame and turned into the dominant CPU-side render cost.
//
// SDL_RenderGeometry takes a vertex/index mesh and a single texture,
// and emits ONE GPU draw call regardless of how many primitives are
// in the mesh. By packing every batched rect into one big mesh we go
// from O(N) state changes + draw calls to O(1) per flush, with no
// behavioural difference - rectangles still draw in submission order,
// per-rect color is preserved via per-vertex SDL_FColor, and the
// renderer's active blend mode keeps applying as before.

#include "window_draw.inc"


// Best-effort WSLg detection on Linux. Reads /proc/version and looks
// for "microsoft" / "WSL" — both appear in the kernel string of any
// Microsoft-shipped WSL kernel. On native Linux returns 0.
//
// We only call this once (from OptimizeSDLForHighPerformance) so the
// per-startup file read is fine. Caching it as a static helps if any
// later code wants to branch on it without re-reading the file.
#if defined(__linux__)
static int DetectWSLg(void) {
    static int cached = -1;
    if (cached >= 0) return cached;
    cached = 0;
    FILE* f = fopen("/proc/version", "r");
    if (f) {
        char buf[512];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = '\0';
        fclose(f);
        // /proc/version on WSL2 contains "Microsoft" (Win11 builds) or
        // "microsoft-standard-WSL2". Match case-insensitively.
        for (size_t i = 0; i + 7 < n; i++) {
            if ((buf[i] == 'M' || buf[i] == 'm') &&
                (buf[i+1] == 'i' || buf[i+1] == 'I') &&
                (buf[i+2] == 'c' || buf[i+2] == 'C') &&
                (buf[i+3] == 'r' || buf[i+3] == 'R') &&
                (buf[i+4] == 'o' || buf[i+4] == 'O') &&
                (buf[i+5] == 's' || buf[i+5] == 'S')) {
                cached = 1;
                break;
            }
        }
    }
    return cached;
}
#endif

// Hints / config applied AFTER the renderer is created. SDL_HINT_RENDER_DRIVER
// has no effect at this point (it only affects renderer creation), so it
// was removed. VSync is controlled per-renderer via SDL_SetRenderVSync
// (the SDL_GL_* equivalents only work with the OpenGL backend, so they
// were a no-op when the user picked D3D12).
void OptimizeSDLForHighPerformance(SDL_Renderer* renderer) {
    // 3 = SDL3's "geometry" line method — emits indexed triangles for
    // lines, which integrates with the same batching path as
    // SDL_RenderGeometry. The default ("polyline") issues per-segment
    // draw calls and breaks batching every time a line is rendered.
    SDL_SetHint(SDL_HINT_RENDER_LINE_METHOD, "3");

    // Disable Vulkan validation layers if anything in the runtime is
    // trying to enable them. They add 20-40 ns per call which shows up
    // when we issue hundreds of draws per frame. Production binaries
    // should never load the validation layer anyway, but this is belt
    // + suspenders for dev builds where VK_LOADER_DEBUG / similar
    // tooling could enable them implicitly.
    SDL_SetHint("SDL_RENDER_VULKAN_DEBUG", "0");

    // Allow the renderer to skip writing to the depth buffer (we don't
    // use depth) and to skip the screensaver inhibitor (UI apps already
    // have their own focus handling). Cheap, no behaviour change.
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");

    // Don't auto-capture the mouse on button down — saves a couple of
    // syscalls per click and avoids an OS-level grab we never wanted.
    SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0");

    // VSync OFF by default - frame pacing is owned by UIApp_Run
    // (UIApp_SetTargetFPS controls the cap). Users that prefer matching
    // the monitor refresh rate can call SDL_SetRenderVSync directly.
    if (renderer) {
        SDL_SetRenderVSync(renderer, 0);
    }

    // Renderer diagnostic. Logs the active driver, vsync state, and —
    // on Vulkan specifically — the actual swapchain image count the
    // platform handed out. This last number matters for the "GPU is
    // idle but FPS is low" case under WSLg / DXVK: SDL requests
    // `surfaceCapabilities.minImageCount + 2` images and gets capped
    // at `maxImageCount`. If the host returns a swapchain of only 2
    // images, the CPU blocks on the present-fence every 2 frames
    // (vkAcquireNextImage waits for the compositor to release the
    // older image), which serialises render + present and leaves the
    // GPU at ~20-30% utilisation while throughput tanks. Reading this
    // log first is the fastest way to diagnose that case.
    if (renderer) {
        const char* drv = SDL_GetRendererName(renderer);
        SDL_PropertiesID props = SDL_GetRendererProperties(renderer);
        int vsync = 0;
        SDL_GetRenderVSync(renderer, &vsync);
        int swapImages = 0;
        if (drv && SDL_strcasecmp(drv, "vulkan") == 0) {
            swapImages = (int)SDL_GetNumberProperty(props,
                SDL_PROP_RENDERER_VULKAN_SWAPCHAIN_IMAGE_COUNT_NUMBER, 0);
        }
        if (swapImages > 0) {
            UI_INFO(UI_CAT_RENDER,
                    "Renderer: %s | VSync: %d | Vulkan swapchain images: %d",
                    drv ? drv : "(unknown)", vsync, swapImages);
        } else {
            UI_INFO(UI_CAT_RENDER,
                    "Renderer: %s | VSync: %d",
                    drv ? drv : "(unknown)", vsync);
        }
    }

#if defined(__linux__)
    // WSLg diagnostic. Going from a 1700+ FPS Windows native baseline
    // to a few hundred under WSLg windowed, and a few dozen at
    // a maximised window, is structural overhead of:
    //   App  -> DXVK -> Vulkan -> WSLg compositor -> RDP -> Windows DWM
    // The present-fence wait at the end of each frame stalls the CPU
    // until WSLg releases the previous swapchain image, which can take
    // 15-25 ms at 1080p depending on the compositor's RDP throughput.
    // Combined with a small swapchain image count (cap above), the GPU
    // ends up doing 5 ms of work then waiting 17 ms for the fence to
    // signal — exactly the "GPU at 25%, low FPS" symptom.
    if (DetectWSLg()) {
        const char* drv = renderer ? SDL_GetRendererName(renderer) : NULL;
        UI_INFO(UI_CAT_RENDER,
                "WSLg detected (kernel reports Microsoft). Renderer: %s. "
                "Expect a structural FPS cap that scales inversely with "
                "window pixel count: a maximised window will be much slower "
                "than a small one. The render itself is fast; the cap is "
                "the WSLg compositor's per-frame present latency.",
                drv ? drv : "(unknown)");
    }
#endif
}

// Note: the older `UICleanupAll` was removed - it was never called by
// anything and duplicated state that UIWindow_Destroy already cleans up.
// Public callers should use UIApp_TrimCaches when they want to free
// the renderer caches mid-flight.


#include "window_render.inc"


// Main rendering function - optimized for high performance
int UIWindow_Render(UIWindow* window) {
    if (!window || !window->sdlRenderer) return -1;
    
    // Query the live window size every frame. The previous 500ms
    // cache produced a visible black bar on the right edge during
    // resize whenever an AA mode that uses the offscreen target
    // (SSAA / FXAA / TAA) was active: the cached rw/rh kept the old
    // value, the offscreen texture was sized to that, and the blit
    // destination rect under-filled the new window backbuffer until
    // the cache refreshed. SDL_GetWindowSize just reads SDL's
    // already-cached value — there is no per-frame cost worth
    // tolerating that artifact for.
    int rw = 0, rh = 0;
    SDL_GetWindowSize(window->sdlWindow, &rw, &rh);

    // Keep the renderer's logical presentation locked to the live window
    // size every frame. On rotation (iOS) the resize event can settle a
    // frame or two after ApplyResize ran with stale dimensions, leaving the
    // logical presentation out of sync — which letterboxed/clipped the
    // whole scene (the "container breaks on rotate-back" bug). Re-applying
    // here is idempotent and self-heals regardless of resize-event timing.
    if (rw > 0 && rh > 0) {
        SDL_SetRenderLogicalPresentation(window->sdlRenderer, rw, rh,
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
    }

    // Initialize TTF once
    static int ttfInited = 0;
    if (!ttfInited) {
        if (TTF_Init() != 1) {
            UI_ERROR(UI_CAT_FONT, "TTF_Init error: %s", SDL_GetError());
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

    // Full-frame AA pipeline. We render to an offscreen target only
    // when the active mode actually needs it (SSAA, FXAA or TAA);
    // otherwise we draw straight to the window.
    int aaScale = 1;
    if      (g_aaMode == 2) aaScale = 2; // SSAA_2X
    else if (g_aaMode == 3) aaScale = 4; // SSAA_4X
    const int needsOffscreen = (g_aaMode >= 2);

    if (needsOffscreen) {
        const int tw = rw * aaScale;
        const int th = rh * aaScale;
        if (!g_smoothTexture || g_smoothW != tw || g_smoothH != th) {
            if (g_smoothTexture) SDL_DestroyTexture(g_smoothTexture);
            // RGBA32 = R,G,B,A in memory on any endian. Matches what
            // FxaaPass reads/writes via SDL_UpdateTexture - without
            // this the byte order disagrees with the texture's
            // declared format and we get channel-swapped + ghosted output.
            g_smoothTexture = SDL_CreateTexture(window->sdlRenderer,
                SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, tw, th);
            if (!g_smoothTexture) {
                UI_ERROR(UI_CAT_RENDER, "failed to create AA target texture: %s", SDL_GetError());
                return -1;
            }
            g_smoothW = tw;
            g_smoothH = th;
            SDL_SetTextureScaleMode(g_smoothTexture, SDL_SCALEMODE_LINEAR);
            g_taaHistoryReady = 0; // size changed - invalidate history
        }
        SDL_SetRenderTarget(window->sdlRenderer, g_smoothTexture);
        SDL_SetRenderScale(window->sdlRenderer, (float)aaScale, (float)aaScale);
    } else {
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
            RenderSingleWidget(window, window->children->children[i]);
        }

        FlushRenderBatch(window->sdlRenderer);
    }


    // Post-process pass (when the active AA mode requires one).
    if (needsOffscreen) {
        if (g_aaMode == 4) {
            // FXAA: edge-detect + blur on the offscreen pixels.
            FxaaPass(window->sdlRenderer, g_smoothTexture, g_smoothW, g_smoothH);
        } else if (g_aaMode == 5) {
            // TAA: blend with history then push result back into the target.
            TaaPass(window->sdlRenderer, g_smoothTexture, g_smoothW, g_smoothH);
        }
        // SSAA modes (2 / 3) need no extra pass - the bilinear
        // downscale on the final blit performs the resolve.

        // Blit the final image to the window. Force blend mode NONE
        // so the texture's alpha doesn't blend with whatever is in the
        // window's backbuffer (TaaPass leaves the texture in BLEND mode
        // and a stray partial alpha would let the previous frame bleed
        // through, creating a ghost trail).
        SDL_SetRenderTarget(window->sdlRenderer, NULL);
        SDL_SetRenderScale(window->sdlRenderer, 1.0f, 1.0f);

        // Clear the backbuffer before the blit. Without this, a freshly
        // resized backbuffer (which can happen mid-frame during the
        // Windows live-resize loop) can show black/uninitialised
        // pixels on the right/bottom edge. SSAA blits a 1:1 logical
        // pixel-for-pixel copy that fully covers the new backbuffer, so
        // it wasn't visible there — but FXAA/TAA spend 4-8 ms in CPU
        // post-process between the render and the blit, plenty of time
        // for the freshly enlarged backbuffer to be presented partly
        // empty. A single full-window clear (~0.05 ms on the GPU) costs
        // nothing and guarantees a clean canvas.
        SDL_SetRenderDrawColor(window->sdlRenderer,
            (Uint8)window->backgroundColor.r,
            (Uint8)window->backgroundColor.g,
            (Uint8)window->backgroundColor.b,
            255);
        SDL_RenderClear(window->sdlRenderer);

        SDL_SetTextureScaleMode(g_smoothTexture, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(g_smoothTexture, SDL_BLENDMODE_NONE);

        g_tempRect1.x = 0;
        g_tempRect1.y = 0;
        g_tempRect1.w = (float)rw;
        g_tempRect1.h = (float)rh;
        SDL_RenderTexture(window->sdlRenderer, g_smoothTexture, NULL, &g_tempRect1);
    }

    // Debug overlay draws on top of everything else, before present. In
    // release builds (MOCIDA_DEBUG_ENABLED == 0) this is compiled away to
    // an immediate return inside UIDebugOverlay_Draw. When debug is on
    // but no flag is set, it is a single int test.
    UIDebugOverlay_Draw(window);

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
            UI_ERROR(UI_CAT_WINDOW, "failed to reallocate UI properties");
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
UIWindow* UIWindow_GetActive(void) {
    return g_activeWindow;
}

void UIWindow_SetActive(UIWindow* window) {
    g_activeWindow = window;
}

// Safe-area insets of the active window. Lives here (not screen.c) because
// SDL_GetWindowSafeArea needs the SDL_Window, which only the active
// UIWindow holds. On desktop the safe area == the whole window, so all
// insets come out 0; on iOS the top reflects the notch / status bar.
UIScreenInsets UIScreen_GetSafeArea(void) {
    UIScreenInsets ins = { 0, 0, 0, 0 };
    if (!g_activeWindow || !g_activeWindow->sdlWindow) return ins;
    int ww = 0, wh = 0;
    SDL_GetWindowSize(g_activeWindow->sdlWindow, &ww, &wh);
    SDL_Rect r;
    if (SDL_GetWindowSafeArea(g_activeWindow->sdlWindow, &r) && r.w > 0 && r.h > 0) {
        ins.left   = r.x;
        ins.top    = r.y;
        ins.right  = ww - (r.x + r.w);
        ins.bottom = wh - (r.y + r.h);
        if (ins.left   < 0) ins.left   = 0;
        if (ins.top    < 0) ins.top    = 0;
        if (ins.right  < 0) ins.right  = 0;
        if (ins.bottom < 0) ins.bottom = 0;
    }
    return ins;
}

UIWindow* UIWindow_Create(const char* title, int width, int height) {
    // Initialize SDL with options optimized for performance
    if (SDL_Init(SDL_INIT_VIDEO) != 1) {
        UI_ERROR(UI_CAT_WINDOW, "SDL_Init error: %s", SDL_GetError());
        return NULL;
    }

    // ----- Hardware MSAA (only takes effect on the OpenGL backend) -----
    // These hints must be set BEFORE the window is created.
    // D3D11/12/Vulkan/Metal silently ignore them, but the CPU analytic
    // coverage AA already gives consistent results on every backend, so
    // this is a bonus for OpenGL users.
    if (g_aaSamplesPerSide > 1 && !g_aaHintsApplied) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        // Hardware MSAA typically supports 2, 4, 8, 16. We saturate at
        // 16 and rely on the driver to pick the nearest valid value below.
        int hwSamples = g_aaSamplesPerSide * g_aaSamplesPerSide;
        if (hwSamples > 16) hwSamples = 16;
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, hwSamples);
        g_aaHintsApplied = 1;
    }

    // Configure OpenGL attributes for better performance before creating window
    // Note: In SDL3, some attributes might need to be updated
    if (SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) != 1) {
        UI_WARN(UI_CAT_RENDER, "failed to set SDL_GL_DOUBLEBUFFER: %s", SDL_GetError());
        // Continue anyway - not critical
    }
    
    // More conservative settings to avoid compatibility issues
    if (SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16) != 1) {
        UI_WARN(UI_CAT_RENDER, "failed to set SDL_GL_DEPTH_SIZE: %s", SDL_GetError());
    }
    
    if (SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0) != 1) {
        UI_WARN(UI_CAT_RENDER, "failed to set SDL_GL_STENCIL_SIZE: %s", SDL_GetError());
    }
    
    // Use more compatible OpenGL version settings - SDL3 might have different requirements
    if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) != 1) {
        UI_WARN(UI_CAT_RENDER, "failed to set GL_CONTEXT_MAJOR_VERSION: %s", SDL_GetError());
    }
    
    if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3) != 1) {
        UI_WARN(UI_CAT_RENDER, "failed to set GL_CONTEXT_MINOR_VERSION: %s", SDL_GetError());
    }
    
    if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) != 1) {
        UI_WARN(UI_CAT_RENDER, "failed to set GL_CONTEXT_PROFILE_MASK: %s", SDL_GetError());
    }
    
    // VSync is configured later, via SDL_SetRenderVSync, inside
    // OptimizeSDLForHighPerformance - that path works with any backend
    // (D3D11/12, Vulkan, Metal, etc), not only OpenGL.

    // Allocate window structure
    UIWindow* window = (UIWindow*)malloc(sizeof(UIWindow));
    if (window == NULL) {
        UI_ERROR(UI_CAT_WINDOW, "out of memory allocating UIWindow");
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
        UI_ERROR(UI_CAT_WINDOW, "out of memory allocating UI properties");
        free(window);
        return NULL;
    }
    
    unsigned int* max_events = malloc(sizeof(unsigned int));
    if (!max_events) {
        UI_ERROR(UI_CAT_WINDOW, "out of memory allocating max_events");
        free(window->__ui_props.props);
        free(window);
        return NULL;
    }
    *max_events = 10;
    UIWindow_SetProperty(window, UI_PROP_MAX_EVENTS, max_events);

    window->events = (UIEventCallbackData**)calloc(*max_events, sizeof(UIEventCallbackData*));
    if (!window->events) {
        UI_ERROR(UI_CAT_WINDOW, "out of memory allocating events array");
        free(max_events);
        free(window->__ui_props.props);
        free(window);
        return NULL;
    }

    // Create window with simpler flags to avoid compatibility issues
    uint32_t window_flags = SDL_WINDOW_RESIZABLE;

    // Per-pixel-alpha window for OS backdrops (Mica/Acrylic). Must be a creation
    // flag — SDL wires up the composition swapchain at window-create time. The
    // app still clears opaque by default, so a transparent window looks identical
    // until a backdrop zeroes the clear alpha (UIWindow_SetBackdrop). Honored by
    // the D3D11 renderer (DirectComposition); a no-op on Vulkan/GL swapchains.
    if (g_wantTransparent) {
        window_flags |= SDL_WINDOW_TRANSPARENT;
    }

    // Client-side decorations: drop the native title bar / frame but stay
    // resizable. The app paints its own title bar and supplies a hit-test
    // (SDL_SetWindowHitTest, wired in app.c) so the OS still handles dragging,
    // double-click-maximize, Aero-snap and edge-resize on Windows.
    //
    // macOS is the exception: a fully BORDERLESS NSWindow loses native rounded
    // corners, the drop shadow, the resize behaviour AND the traffic-light
    // buttons. There the custom titlebar is done the Cocoa way — keep a normal
    // titled+resizable window and make the titlebar transparent / full-size
    // content (UIWindow_ApplyNativeDecorations → titlebar_cocoa.mm) so our
    // content fills the bar while the OS keeps drawing the rounding/shadow and
    // the traffic-lights. So: borderless on Windows/Linux only.
    if (g_customTitlebar) {
#if !defined(__APPLE__)
        window_flags |= SDL_WINDOW_BORDERLESS;
#endif
    }
#if defined(MOCIDA_IOS)
    // iOS windows already cover the screen. We deliberately do NOT request
    // HIGH_PIXEL_DENSITY (the renderer would otherwise draw at pixel size
    // into a point-sized corner) nor SDL_WINDOW_FULLSCREEN — SDL hides the
    // system status bar (clock / battery) for fullscreen windows, and we
    // want it visible with our content laid out below it via the safe area.
#endif

    // Linux/WSLg hints — must be set BEFORE SDL_CreateWindow so the
    // window subsystem picks them up at creation time. They are no-ops
    // on Windows/macOS so we don't ifdef them out, but the comment
    // explains why each matters on Linux (WSLg specifically):
    //
    //   X11_NET_WM_BYPASS_COMPOSITOR=1 — asks Mutter / weston-wsl to
    //   stop compositing our window. SDL3's default is already "1",
    //   but inside WSLg some shells (older builds) flip the default;
    //   explicit pins it. Without it, fullscreen drops to ~40 FPS
    //   because the compositor frame-paces our swaps to its own clock.
    //
    //   WAYLAND_PREFER_LIBDECOR=0 — when running on the Wayland
    //   backend (WSLg uses Wayland on Win11), prefer xdg-shell
    //   decorations over libdecor. xdg-shell has lower per-present
    //   latency on WSLg's Mutter; libdecor adds a client-side
    //   draw pass we don't need.
    //
    //   VIDEO_FORCE_EGL=1 — under Wayland the EGL path uses native
    //   client-side buffers; the GLX path falls back to XWayland
    //   round-trips. EGL is the strictly faster path on WSLg.
#if defined(__linux__)
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "1",
                            SDL_HINT_DEFAULT);
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_WAYLAND_PREFER_LIBDECOR, "0",
                            SDL_HINT_DEFAULT);
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_FORCE_EGL, "1",
                            SDL_HINT_DEFAULT);
#endif

    // Get available display mode
    SDL_DisplayID displayID = SDL_GetPrimaryDisplay();
    if (displayID == 0) {
        UI_ERROR(UI_CAT_WINDOW, "failed to get primary display: %s", SDL_GetError());
        // Continue anyway, using default settings
    } else {
        // We successfully got the display, we can query more information if needed
        UI_DEBUG(UI_CAT_WINDOW, "using primary display: %u", displayID);
    }
    
    SDL_Window* sdlWindow = SDL_CreateWindow(title, width, height, window_flags);
    if (!sdlWindow) {
        UI_ERROR(UI_CAT_WINDOW, "SDL_CreateWindow error: %s", SDL_GetError());
        free(window->events);
        free(window->__ui_props.props);
        free(window);
        return NULL;
    }

#if defined(MOCIDA_IOS)
    // The requested size is meaningless on iOS — the OS sized the window to
    // the device screen. Read the real logical size back and adopt it so
    // layout (and UIScreen) reflect the actual device, not the desktop
    // default the app asked for.
    {
        int rw = 0, rh = 0;
        SDL_GetWindowSize(sdlWindow, &rw, &rh);
        if (rw > 0 && rh > 0) {
            width  = rw;
            height = rh;
            window->width  = rw;
            window->height = rh;
        }
    }
#endif

    // Dump the list of renderers SDL3 knows about at runtime. A renderer
    // missing here = either disabled at compile time or its runtime
    // dependency (libGL.so, vulkan loader, ...) failed to load. Helpful
    // when SDL_CreateRenderer below errors with "X not available".
    {
        int n = SDL_GetNumRenderDrivers();
        char buf[256] = {0};
        size_t off = 0;
        for (int i = 0; i < n && off < sizeof(buf) - 1; i++) {
            const char* nm = SDL_GetRenderDriver(i);
            int w = snprintf(buf + off, sizeof(buf) - off,
                             "%s%s", off ? ", " : "", nm ? nm : "?");
            if (w < 0) break;
            off += (size_t)w;
        }
        UI_INFO(UI_CAT_RENDER, "SDL render drivers available (%d): %s", n, buf);
    }

    // Platform-specific default renderer preference (only applies when
    // the user did NOT call UIApp_SetRenderDriver before this point,
    // which is what UIApp_Create does — by the time we get here, the
    // hint env may or may not have been set).
    //
    //   Linux:   prefer Vulkan first, opengl second. SDL's default
    //            order on Linux already favours Vulkan, but on WSLg
    //            it's worth being explicit since the wsl pulled-in
    //            opengl loader sometimes races ahead.
    //   Windows: leave SDL's default (direct3d11 → direct3d12 →
    //            opengl → vulkan). D3D11 has the lowest CPU-side
    //            overhead per draw on Windows and is what every
    //            existing measurement assumes.
    //   macOS:   Metal is the only sensible choice; SDL picks it
    //            first anyway.
    //
    // Honour an existing hint (set by UIApp_SetRenderDriver or the
    // SDL_RENDER_DRIVER env var) — SDL_SetHintWithPriority at
    // SDL_HINT_DEFAULT means "set only if not already set by the
    // user or env".
#if defined(__linux__)
    SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "vulkan,opengl,software",
                            SDL_HINT_DEFAULT);
#endif

#if defined(__APPLE__)
    // macOS render-creation hints. Both MUST be set before SDL_CreateRenderer
    // (they only influence renderer creation), so they live here rather than
    // in OptimizeSDLForHighPerformance which runs afterwards. DEFAULT priority
    // = "set only if the user/env didn't already pick", so UIApp_SetRenderDriver
    // and SDL_RENDER_DRIVER still win.
    //
    //   RENDER_DRIVER "metal,opengl,software" — SDL already picks Metal first
    //   on macOS, but being explicit pins the fallback chain: Metal is the
    //   only HW path that matters (D3D/Vulkan don't exist here, MoltenVK would
    //   add a translation layer), opengl is the deprecated-but-present
    //   fallback, software last. No "gpu" entry: SDL's GPU renderer on macOS
    //   sits on top of Metal anyway, so it only adds an indirection for a 2D
    //   UI workload.
    SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "metal,opengl,software",
                            SDL_HINT_DEFAULT);

    //   METAL_PREFER_LOW_POWER_DEVICE "1" — on dual-GPU Intel MacBooks
    //   (integrated Intel + discrete AMD) this keeps us on the integrated GPU.
    //   For a 2D UI the integrated GPU is more than enough, and forcing it
    //   avoids the discrete-GPU power-up: a multi-hundred-ms GPU-switch hitch
    //   the first time a Metal device is requested, plus a constant battery
    //   drain for the rest of the session. On Apple Silicon there is a single
    //   unified GPU so the hint is a harmless no-op. Users that genuinely want
    //   the discrete GPU can override via the env var (DEFAULT priority).
    SDL_SetHintWithPriority(SDL_HINT_RENDER_METAL_PREFER_LOW_POWER_DEVICE, "1",
                            SDL_HINT_DEFAULT);
#endif

    // A transparent window composites its D3D11 swap chain into a
    // Windows.UI.Composition visual tree (real acrylic backdrop). The compositor
    // runs on its own thread and shares the swap chain's device, so that device
    // must be multithread-safe — SDL defaults to a single-threaded D3D11 device,
    // which makes CreateCompositionSurfaceForSwapChain fail with
    // DXGI_ERROR_UNSUPPORTED. Request a thread-safe device for transparent windows.
    if (g_wantTransparent) {
        SDL_SetHintWithPriority(SDL_HINT_RENDER_DIRECT3D_THREADSAFE, "1",
                                SDL_HINT_OVERRIDE);
    }

    // In SDL3, CreateRenderer has different parameters
    SDL_Renderer* sdlRenderer = SDL_CreateRenderer(sdlWindow, NULL);
    if (!sdlRenderer) {
        UI_ERROR(UI_CAT_RENDER, "SDL_CreateRenderer error: %s", SDL_GetError());
        SDL_DestroyWindow(sdlWindow);
        free(window->events);
        free(window->__ui_props.props);
        free(window);
        return NULL;
    }
    // Log the actual backend SDL picked so we can tell software fallback
    // apart from real HW acceleration (specially under WSLg / remote
    // X). The SDL_HINT_RENDER_DRIVER env var influences this choice;
    // passing NULL above means "first available in SDL's order".
    {
        const char* drvName = SDL_GetRendererName(sdlRenderer);
        UI_INFO(UI_CAT_RENDER, "SDL renderer: %s", drvName ? drvName : "(unknown)");
    }

    window->title = _strdup(title);
    if (!window->title) {
        UI_ERROR(UI_CAT_WINDOW, "failed to duplicate window title");
        SDL_DestroyRenderer(sdlRenderer);
        SDL_DestroyWindow(sdlWindow);
        free(window->events);
        free(window->__ui_props.props);
        free(window);
        return NULL;
    }
    
    window->sdlWindow = sdlWindow;
    window->sdlRenderer = sdlRenderer;
    g_activeWindow = window;

    // Configure blending
    if (SDL_SetRenderDrawBlendMode(sdlRenderer, SDL_BLENDMODE_BLEND) != 1) {
        UI_WARN(UI_CAT_RENDER, "failed to set blend mode: %s", SDL_GetError());
        // Continue anyway - not critical
    }
    
    // Apply additional optimizations
    OptimizeSDLForHighPerformance(sdlRenderer);

    // Custom titlebar: restore the OS-native decorations the borderless flag
    // (Windows/Linux) would otherwise strip — Win11 rounded corners + drop
    // shadow — or, on macOS, switch the (non-borderless) window into the
    // transparent full-size-content titlebar mode. See titlebar_native.c /
    // titlebar_cocoa.mm.
    if (g_customTitlebar) {
        UIWindow_ApplyNativeDecorations(sdlWindow);
    }

    return window;
}

void UIWindow_SetBackdrop(UIWindow* window, UIBackdropMaterial material,
                          UIColor tint, float tintOpacity) {
    if (!window) return;

    if (material == UI_BACKDROP_AUTO) {
        material = UIBackdrop_ResolveAuto();
    }

    window->backdrop            = material;
    window->backdropTint        = tint;
    window->backdropTintOpacity = tintOpacity;

    if (material == UI_BACKDROP_NONE) {
        // Disable: hand a NONE to the platform layer (it restores the opaque
        // window) and forget the native flag so we clear opaque again. Restore
        // the clear's opacity (it was zeroed when a native backdrop turned on).
        if (window->sdlWindow) {
            UIBackdrop_Apply(window->sdlWindow, UI_BACKDROP_NONE, tint, tintOpacity);
        }
        window->backdropNative = 0;
        window->backgroundColor.a = 1.0f;
        return;
    }

    int native = 0;
    if (window->sdlWindow) {
        native = UIBackdrop_Apply(window->sdlWindow, material, tint, tintOpacity);
    }
    window->backdropNative = native;

    // A native backdrop (acrylic/mica) composites behind the window, so the
    // per-frame clear is made transparent: wherever the app leaves alpha 0 (the
    // window clear, or a widget drawn with a translucent fill) the OS-blurred
    // backdrop shows through. Opaque widgets still cover it. On the SDL backends
    // that DO composite swapchain alpha (Vulkan, and D3D11 flip-model on Win11)
    // this yields a true acrylic region; on backends that don't, the area reads
    // as the backdrop tint.
    if (native) {
        window->backgroundColor.a = 0.0f;
        UI_INFO(UI_CAT_WINDOW, "window backdrop enabled (material=%d, native)", (int)material);
    } else {
        UI_INFO(UI_CAT_WINDOW,
                "window backdrop requested (material=%d) but no native compositor "
                "effect is available; using in-app fallback", (int)material);
    }
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
    if (g_activeWindow == window) g_activeWindow = NULL;

    free(window->title);
    CleanupCircleCache();
    CleanupShadowCache();
    CleanupTaaHistory();
    free(window);
}
