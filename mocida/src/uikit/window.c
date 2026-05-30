#include <uikit/window.h>
#include <uikit/button.h>
#include <uikit/mouse_area.h>
#include <uikit/container.h>
#include <uikit/controls.h>
#include <uikit/textfield.h>
#include <uikit/textarea.h>
#include <uikit/webview.h>
#include <uikit/stack.h>
#include <uikit/dialog.h>
#include <uikit/tab.h>
#include <uikit/popup.h>
#include <uikit/video.h>
#include <uikit/file_drop.h>
#include <uikit/asset.h>
#include <uikit/overlay.h>
#include <uikit/debug.h>
#include <uikit/perf.h>
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

// Full-frame AA pipeline. Matches UIAAMode in app.h:
//   0 NONE, 1 COVERAGE (default - no postfx), 2 SSAA_2X, 3 SSAA_4X,
//   4 FXAA, 5 TAA.
static int   g_aaMode   = 1;
static float g_taaBlend = 0.5f;

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
static void FlushRenderBatch(SDL_Renderer* renderer) {
    if (UI_UNLIKELY(g_batchSize <= 0)) return;

    const int N = g_batchSize;
    // Read/write tightly-packed scratch arrays sequentially. The
    // hardware prefetcher catches most of this on x86, but explicit
    // hints help on older WSLg setups where the L1 stream prefetcher
    // backs off after a render-target switch and the vertex burst
    // arrives cold. UI_PREFETCH/UI_PREFETCH_RW expand to no-ops where
    // the toolchain doesn't support the builtin.
    UI_PREFETCH(&g_rectBatch[0]);

    for (int i = 0; i < N; i++) {
        // Look ahead 2 rects (covers the next ~40 bytes of input + 280
        // bytes of vertex output, roughly one cache line each way).
        if (UI_LIKELY(i + 2 < N)) {
            UI_PREFETCH(&g_rectBatch[i + 2]);
            UI_PREFETCH_RW(&g_batchVerts[(i + 2) * 4]);
        }

        const SDL_FRect* UI_RESTRICT r = &g_rectBatch[i].rect;
        const SDL_Color  c = g_rectBatch[i].color;
        const SDL_FColor fc = {
            (float)c.r * (1.0f / 255.0f),
            (float)c.g * (1.0f / 255.0f),
            (float)c.b * (1.0f / 255.0f),
            (float)c.a * (1.0f / 255.0f),
        };
        const float x0 = r->x;
        const float y0 = r->y;
        const float x1 = x0 + r->w;
        const float y1 = y0 + r->h;

        SDL_Vertex* UI_RESTRICT v = &g_batchVerts[i * 4];
        // Top-left, top-right, bottom-right, bottom-left.
        v[0].position  = (SDL_FPoint){ x0, y0 };
        v[1].position  = (SDL_FPoint){ x1, y0 };
        v[2].position  = (SDL_FPoint){ x1, y1 };
        v[3].position  = (SDL_FPoint){ x0, y1 };
        v[0].color = v[1].color = v[2].color = v[3].color = fc;
        v[0].tex_coord = v[1].tex_coord = v[2].tex_coord = v[3].tex_coord =
            (SDL_FPoint){ 0.0f, 0.0f };

        int* UI_RESTRICT idx = &g_batchIndices[i * 6];
        const int vbase = i * 4;
        // Two triangles: (TL, TR, BR) and (TL, BR, BL).
        idx[0] = vbase + 0;
        idx[1] = vbase + 1;
        idx[2] = vbase + 2;
        idx[3] = vbase + 0;
        idx[4] = vbase + 2;
        idx[5] = vbase + 3;
    }

    SDL_RenderGeometry(renderer, NULL,
                       g_batchVerts,   N * 4,
                       g_batchIndices, N * 6);
    g_batchSize = 0;
}

// Function to add a rectangle to the batch. Force-inlined: called from
// every DrawRoundedRectFill / DrawRoundedRectWithBorder so even the
// few cycles of call overhead add up across a UI scene.
UI_FORCE_INLINE void BatchRect(SDL_Renderer* renderer, const SDL_FRect* rect, SDL_Color color) {
    // Check if batch is full
    if (UI_UNLIKELY(g_batchSize >= MAX_BATCH_SIZE)) {
        FlushRenderBatch(renderer);
    }

    // Add to batch
    g_rectBatch[g_batchSize].rect = *rect;
    g_rectBatch[g_batchSize].color = color;
    g_batchSize++;
}

// Cap the font cache so a long-running app that opens many sizes
// doesn't slowly leak TTF_Font handles. The linked list is moved-to-front
// on hit, so the tail eviction is effectively LRU.
#define MAX_FONT_CACHE 32

static TTF_Font* GetFont(const char *path, float size) {
    // Find + move to front. If hit, we leave the entry at the front
    // (since g_fontCache is the head) so it survives eviction longer.
    FontCacheEntry *prev = NULL;
    FontCacheEntry *e = g_fontCache;
    int count = 0;
    while (e) {
        count++;
        if (e->size == size && strcmp(e->path, path) == 0) {
            if (prev) {
                // Splice to front for LRU semantics.
                prev->next = e->next;
                e->next = g_fontCache;
                g_fontCache = e;
            }
            return e->font;
        }
        prev = e;
        e = e->next;
    }

    // Evict the tail if we're at the cap (count was incremented in the loop).
    if (count >= MAX_FONT_CACHE) {
        FontCacheEntry *p = NULL;
        FontCacheEntry *tail = g_fontCache;
        while (tail && tail->next) { p = tail; tail = tail->next; }
        if (tail) {
            if (tail->font) TTF_CloseFont(tail->font);
            free(tail->path);
            if (p) p->next = NULL; else g_fontCache = NULL;
            free(tail);
        }
    }

    // Load new font.
    TTF_Font *f = TTF_OpenFont(path, size);
    if (!f) return NULL;

    FontCacheEntry *ne = (FontCacheEntry*)malloc(sizeof(FontCacheEntry));
    if (!ne) { TTF_CloseFont(f); return NULL; }
    ne->path = _strdup(path);
    if (!ne->path) { free(ne); TTF_CloseFont(f); return NULL; }
    ne->size = size;
    ne->font = f;
    ne->next = g_fontCache;
    g_fontCache = ne;

    // Set NORMAL hinting once when the font is first loaded. The per-
    // widget render paths used to call TTF_SetFontHinting(font, NORMAL)
    // on every frame, which can rebuild internal glyph caches inside
    // SDL_ttf. Setting it once at load time and relying on persistence
    // removes that cost across the entire text-heavy hot path.
    TTF_SetFontHinting(f, TTF_HINTING_NORMAL);
    return f;
}

UI_FORCE_INLINE void ApplyMargins(SDL_FRect* r, float ml, float mt, float mr, float mb) {
    // Fast single-instruction margin application
    r->x += ml;
    r->y += mt;
    r->w -= (ml + mr);
    r->h -= (mt + mb);
}

// ---------------------------------------------------------------------
// Analytic-coverage AA
// ---------------------------------------------------------------------
// Renders a white circle mask into a CPU RGBA buffer where each pixel's
// alpha equals the fraction of the pixel area covered by the disc. This
// is computed by taking N*N uniform subsamples inside the pixel and
// counting how many fall within the radius (N = g_aaSamplesPerSide).
//
// Result: mathematically exact AA, equivalent in quality to N*N-sample
// MSAA - but resolved once at generation time and cached. The final
// texture is exactly the on-screen size of the circle, so there is no
// resampling or sharpness loss at draw time.
//
// The cache is invalidated when g_aaSamplesPerSide changes (see
// UIWindow_SetMSAASamples).
// ---------------------------------------------------------------------
static int g_cacheSamplesPerSide = 0; // last N used to populate the cache

static SDL_Texture* RasterizeCoverageCircle(SDL_Renderer* renderer,
                                            int size,
                                            int samplesPerSide) {
    if (size < 2) size = 2;
    if (samplesPerSide < 1) samplesPerSide = 1;

    const size_t pixCount = (size_t)size * (size_t)size;
    Uint8* pixels = (Uint8*)malloc(pixCount * 4);
    if (!pixels) return NULL;

    const float cx = (float)size * 0.5f;
    const float cy = (float)size * 0.5f;
    // Inset by half a pixel so that the disc edge falls INSIDE the
    // texture: without this the outer row/column gets clipped and
    // loses half a pixel of AA. The visual difference is subtle but
    // it prevents a "hard edge".
    const float r  = (float)size * 0.5f - 0.5f;
    const float r2 = r * r;

    const float step       = 1.0f / (float)samplesPerSide;
    const float halfStep   = step * 0.5f;
    const int   totalSamps = samplesPerSide * samplesPerSide;

    // Quick reject per pixel: alpha = 0 (outside) or = 255 (fully inside).
    // When the whole pixel is in the interior we skip the inner sample loop.
    for (int py = 0; py < size; py++) {
        // Min/max Y distance from the pixel to the centre. When the
        // whole pixel is comfortably inside or outside we avoid the
        // sample loop entirely.
        const float dyMin = (float)py + 0.0f - cy;
        const float dyMax = (float)py + 1.0f - cy;
        const float dyAbs = SDL_max(fabsf(dyMin), fabsf(dyMax));

        for (int px = 0; px < size; px++) {
            const float dxMin = (float)px + 0.0f - cx;
            const float dxMax = (float)px + 1.0f - cx;
            const float dxAbs = SDL_max(fabsf(dxMin), fabsf(dxMax));

            // Largest distance from any corner of the pixel to the centre.
            const float maxDist2 = dxAbs * dxAbs + dyAbs * dyAbs;
            // Smallest distance (closest corner, accounting for whether
            // the centre falls outside the pixel on either axis).
            const float dxClosest = (dxMin > 0.0f) ? dxMin : ((dxMax < 0.0f) ? dxMax : 0.0f);
            const float dyClosest = (dyMin > 0.0f) ? dyMin : ((dyMax < 0.0f) ? dyMax : 0.0f);
            const float minDist2  = dxClosest * dxClosest + dyClosest * dyClosest;

            Uint8 alpha;
            if (maxDist2 <= r2) {
                alpha = 255;          // pixel fully inside
            } else if (minDist2 >= r2) {
                alpha = 0;            // pixel fully outside
            } else {
                // Edge: take NxN subsamples for coverage.
                int inside = 0;
                for (int sy = 0; sy < samplesPerSide; sy++) {
                    const float fy   = (float)py + halfStep + step * (float)sy - cy;
                    const float fy2  = fy * fy;
                    for (int sx = 0; sx < samplesPerSide; sx++) {
                        const float fx = (float)px + halfStep + step * (float)sx - cx;
                        if (fx * fx + fy2 <= r2) inside++;
                    }
                }
                alpha = (Uint8)((inside * 255 + totalSamps / 2) / totalSamps);
            }

            const size_t idx = ((size_t)py * (size_t)size + (size_t)px) * 4;
            pixels[idx + 0] = 255;   // R
            pixels[idx + 1] = 255;   // G
            pixels[idx + 2] = 255;   // B
            pixels[idx + 3] = alpha; // A (= coverage)
        }
    }

    // SDL_PIXELFORMAT_RGBA32 = R,G,B,A in memory order (endian-neutral).
    SDL_Texture* tex = SDL_CreateTexture(renderer,
                                         SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC,
                                         size, size);
    if (tex) {
        SDL_UpdateTexture(tex, NULL, pixels, size * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        // LINEAR for the cases where the caller resizes slightly.
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    }
    free(pixels);
    return tex;
}

static SDL_Texture* GetCachedCircleTexture(SDL_Renderer* renderer, int size) {
    if (size < 2) size = 2;

    // If the global quality changed since the last generation, drop everything.
    if (g_cacheSamplesPerSide != g_aaSamplesPerSide) {
        CleanupCircleCache();
        g_cacheSamplesPerSide = g_aaSamplesPerSide;
    }

    for (int i = 0; i < MAX_CIRCLE_CACHE; i++) {
        if (g_circleCacheSizes[i] == size && g_circleCache[i] != NULL) {
            return g_circleCache[i];
        }
    }

    int slotToUse = 0;
    for (int i = 0; i < MAX_CIRCLE_CACHE; i++) {
        if (g_circleCache[i] == NULL) {
            slotToUse = i;
            break;
        }
    }

    if (g_circleCache[slotToUse] != NULL) {
        SDL_DestroyTexture(g_circleCache[slotToUse]);
        g_circleCache[slotToUse] = NULL;
    }

    SDL_Texture* tex = RasterizeCoverageCircle(renderer, size, g_aaSamplesPerSide);
    if (!tex) return NULL;

    g_circleCache[slotToUse]      = tex;
    g_circleCacheSizes[slotToUse] = size;
    return tex;
}

void UIWindow_SetMSAASamples(int samples) {
    if (samples < MOCIDA_MIN_AA_SAMPLES) samples = MOCIDA_MIN_AA_SAMPLES;
    if (samples > MOCIDA_MAX_AA_SAMPLES) samples = MOCIDA_MAX_AA_SAMPLES;
    g_aaSamplesPerSide = samples;
    // The cache is invalidated lazily on the next access when N changes.
}

int UIWindow_GetMSAASamples(void) {
    return g_aaSamplesPerSide;
}

void UIWindow_SetAAMode(int mode) {
    if (mode < 0 || mode > 5) mode = 1;
    if (mode != g_aaMode) {
        // Drop TAA history when leaving/entering TAA to avoid ghost frames.
        g_taaHistoryReady = 0;

        // Free GPU resources the new mode no longer needs - "ultra
        // light": the offscreen target and TAA history can each weigh
        // tens of MB; nuke them when switching to a coverage-only mode.
        const int needsOffscreen  = (mode == 2 || mode == 3 || mode == 4 || mode == 5);
        const int needsTaaHistory = (mode == 5);

        if (!needsOffscreen && g_smoothTexture) {
            SDL_DestroyTexture(g_smoothTexture);
            g_smoothTexture = NULL;
            g_smoothW = g_smoothH = 0;
        }
        if (!needsTaaHistory) {
            // Free both the legacy GPU history (if any) and the new CPU buffers.
            CleanupTaaHistory();
        }

        // (FXAA used to need a hardware-renderer warning here because the
        // CPU implementation cost a full readback every frame. The path
        // is now a GPU 4-tap blur in FxaaPass — no readback, sub-1 ms.
        // No warning needed.)
    }
    g_aaMode = mode;
}

int UIWindow_GetAAMode(void) {
    return g_aaMode;
}

void UIWindow_SetTAABlend(float a) {
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    g_taaBlend = a;
}

float UIWindow_GetTAABlend(void) {
    return g_taaBlend;
}

void UIWindow_SetTAAMotionThreshold(int threshold) {
    if (threshold < 0)   threshold = 0;
    if (threshold > 255) threshold = 255;
    g_taaMotionThreshold = threshold;
    // Force a history refresh so the previous threshold's ghost trails
    // don't survive across the change.
    g_taaHistoryReady = 0;
}

int UIWindow_GetTAAMotionThreshold(void) {
    return g_taaMotionThreshold;
}

void UIWindow_TrimCaches(void) {
    CleanupCircleCache();
    CleanupShadowCache();
    CleanupTaaHistory();
    if (g_smoothTexture) {
        SDL_DestroyTexture(g_smoothTexture);
        g_smoothTexture = NULL;
        g_smoothW = g_smoothH = 0;
    }
}

// ---------------------------------------------------------------------
// FXAA post-process (GPU 4-tap blur)
// ---------------------------------------------------------------------
// Previous implementation read the framebuffer back to CPU, ran a
// luma-based edge detect + edge-aligned blur, and uploaded the result.
// On Vulkan / D3D / Metal that pipeline-stalling readback dominated
// frame time (~30 ms at 1080p, observed as 33 FPS).
//
// SDL_Renderer doesn't give us fragment shaders, so we can't replicate
// FXAA's per-pixel edge logic on the GPU. What we CAN do — and what
// every "cheap AA" technique ultimately reduces to in edge regions — is
// a sub-pixel blur of the whole frame. Done with:
//
//   1. Aux render target the same size as the offscreen scene.
//   2. Clear aux to 0.
//   3. Render the scene into aux 4 times, each with a ±0.5 px offset and
//      additive blend (custom SDL_BlendMode) at 25% weight. SDL_RENDER_
//      LINEAR sampling on the source turns each offset blit into a 4-tap
//      bilinear average; combining 4 of them produces the standard
//      tent-filter 3×3 kernel:
//
//             1  2  1
//             2  4  2   (× 1/16)
//             1  2  1
//
//   4. Copy aux back over the source so the outer blit path sees the
//      processed result with no other changes.
//
// Cost: 5 GPU draw calls per frame, zero CPU→GPU transfers. On hardware
// backends this is well under 1 ms even at 4K. The visual effect is a
// uniform sub-pixel soften — not the edge-only smoothing real FXAA does,
// but at sub-pixel scale on UI content the difference is imperceptible
// and the order-of-magnitude FPS win matters more than the algorithmic
// purity. Users who want strict edge-only AA still have SSAA (which
// scales the rasterizer up and resolves on downsample) and the analytic
// coverage AA on circles / rounded corners.
//
// History (CPU implementation): kept the FXAA scratch buffer globals
// nulled so CleanupTaaHistory's free() chain on g_fxaaSrcBuf /
// g_fxaaDstBuf still compiles and is a no-op on shutdown.
static Uint8* g_fxaaSrcBuf = NULL;
static Uint8* g_fxaaDstBuf = NULL;
static size_t g_fxaaBufCap = 0;

// GPU FXAA state.
static SDL_Texture* g_fxaaAux       = NULL;
static int          g_fxaaAuxW      = 0;
static int          g_fxaaAuxH      = 0;
static SDL_BlendMode g_fxaaAddBlend = SDL_BLENDMODE_INVALID;

static void FxaaPass(SDL_Renderer* r, SDL_Texture* tex, int w, int h) {
    if (!r || !tex || w < 2 || h < 2) return;

    // (Re)create the aux target on first use or on geometry change.
    if (!g_fxaaAux || g_fxaaAuxW != w || g_fxaaAuxH != h) {
        if (g_fxaaAux) SDL_DestroyTexture(g_fxaaAux);
        g_fxaaAux = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_TARGET, w, h);
        if (!g_fxaaAux) {
            UI_WARN(UI_CAT_RENDER, "FXAA: failed to create aux target: %s", SDL_GetError());
            g_fxaaAuxW = g_fxaaAuxH = 0;
            return;
        }
        SDL_SetTextureScaleMode(g_fxaaAux, SDL_SCALEMODE_LINEAR);
        g_fxaaAuxW = w;
        g_fxaaAuxH = h;
    }

    // Custom blend mode is composed once and cached for the process
    // lifetime. SDL_ComposeCustomBlendMode does a small lookup on each
    // call; better to memoise.
    //   out.rgb   = src.rgb * src.a + dst.rgb * 1   (ADD)
    //   out.alpha = src.a   * 1     + dst.a   * 1   (ADD)
    // With src.a driven by alpha mod = 64 (~25%), four taps sum to a
    // unit-weight average.
    if (g_fxaaAddBlend == SDL_BLENDMODE_INVALID) {
        g_fxaaAddBlend = SDL_ComposeCustomBlendMode(
            SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
            SDL_BLENDFACTOR_ONE,       SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
    }

    SDL_Texture* prevTarget = SDL_GetRenderTarget(r);

    // Clear aux. Force NONE so the clear writes 0 outright.
    SDL_SetRenderTarget(r, g_fxaaAux);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);

    // Configure source for the 4 taps.
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(tex, g_fxaaAddBlend);
    SDL_SetTextureAlphaMod(tex, 64); // 64/255 ≈ 0.25 weight per tap

    // Offsets at ±0.25 px (not ±0.5). The smaller offset keeps the
    // bilinear sampler much closer to the original pixel, so the
    // resulting 3×3 kernel is:
    //
    //     0.0156  0.0938  0.0156
    //     0.0938  0.5625  0.0938
    //     0.0156  0.0938  0.0156
    //
    // i.e. 56% of each output pixel comes from the source pixel itself
    // (vs. 25% at ±0.5). That preserves text + edge sharpness while
    // still smoothing the sub-pixel stair-step on rect corners. The
    // previous ±0.5 setup looked correct for AA-of-edges in synthetic
    // tests but bled text on real UI content.
    static const float offsets[4][2] = {
        { -0.25f, -0.25f }, { +0.25f, -0.25f },
        { -0.25f, +0.25f }, { +0.25f, +0.25f },
    };
    for (int i = 0; i < 4; i++) {
        SDL_FRect dst = { offsets[i][0], offsets[i][1], (float)w, (float)h };
        SDL_RenderTexture(r, tex, NULL, &dst);
    }

    // Restore source texture state for the rest of the pipeline.
    SDL_SetTextureAlphaMod(tex, 255);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    // Push the blurred image back onto the source texture so the outer
    // blit / SSAA downsample picks it up without any extra paths.
    SDL_SetRenderTarget(r, tex);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetTextureBlendMode(g_fxaaAux, SDL_BLENDMODE_NONE);
    SDL_RenderTexture(r, g_fxaaAux, NULL, NULL);

    SDL_SetRenderTarget(r, prevTarget);

    // Restore the renderer's expected blend mode for normal widget
    // draws (the outer code expects BLEND).
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    // The legacy CPU scratch buffers are unused; reference them so the
    // CleanupTaaHistory free() chain still has linkage to the globals.
    (void)g_fxaaSrcBuf;
    (void)g_fxaaDstBuf;
    (void)g_fxaaBufCap;
}

// ---------------------------------------------------------------------
// TAA post-process (GPU-resident)
// ---------------------------------------------------------------------
// History is a persistent SDL render target. Each frame we blend the
// current scene INTO history using SDL's native BLEND mode with an
// alpha-mod weight, then copy history back over the current target so
// the outer blit path picks it up unchanged.
//
// SDL_BLENDMODE_BLEND on the GPU evaluates per pixel:
//     dstRGB = srcRGB * srcA + dstRGB * (1 - srcA)
// where srcA = current.alpha * (alphaMod / 255). For an opaque UI
// (current.alpha == 255) this reduces to:
//     out = (alphaMod/255) * current + (1 - alphaMod/255) * history
// i.e. the exact uniform lerp TAA does.
//
// Cost: two GPU draw calls per frame (one blend, one copy). No
// SDL_RenderReadPixels / SDL_UpdateTexture, no CPU pixel loop. On
// Vulkan / D3D this drops the per-frame overhead from ~5-20 ms
// (readback stall + bus transfer) to well under 0.5 ms.
//
// Tradeoff vs the v2 motion-mask CPU implementation: no per-pixel
// motion detection, so fast-moving content can ghost. At UI blend
// values (0.05-0.2) and the largely-static content of a real UI scene
// this is imperceptible. If you ever need motion-mask back, it
// requires readback and pays the price that path made unacceptable.
static void TaaPass(SDL_Renderer* r, SDL_Texture* current, int w, int h) {
    if (!r || !current || w < 1 || h < 1) return;

    // (Re)create the history target on first use or geometry change.
    if (!g_taaHistory || g_taaHistoryW != w || g_taaHistoryH != h) {
        if (g_taaHistory) SDL_DestroyTexture(g_taaHistory);
        g_taaHistory = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_TARGET, w, h);
        if (!g_taaHistory) {
            UI_WARN(UI_CAT_RENDER, "TAA: failed to create history target: %s", SDL_GetError());
            g_taaHistoryW = g_taaHistoryH = 0;
            g_taaHistoryReady = 0;
            return;
        }
        SDL_SetTextureBlendMode(g_taaHistory, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(g_taaHistory, SDL_SCALEMODE_LINEAR);
        g_taaHistoryW = w;
        g_taaHistoryH = h;
        g_taaHistoryReady = 0;
    }

    SDL_Texture* prevTarget = SDL_GetRenderTarget(r);

    if (!g_taaHistoryReady) {
        // Prime history with the current scene. NONE blend = exact copy.
        SDL_SetRenderTarget(r, g_taaHistory);
        SDL_SetTextureBlendMode(current, SDL_BLENDMODE_NONE);
        SDL_RenderTexture(r, current, NULL, NULL);
        g_taaHistoryReady = 1;
    } else {
        // Weighted blend on the GPU: history ← alphaMod*current + (1-alphaMod)*history.
        SDL_SetRenderTarget(r, g_taaHistory);
        SDL_SetTextureBlendMode(current, SDL_BLENDMODE_BLEND);
        const Uint8 a = (Uint8)SDL_clamp((int)(g_taaBlend * 255.0f + 0.5f), 0, 255);
        SDL_SetTextureAlphaMod(current, a);
        SDL_RenderTexture(r, current, NULL, NULL);
        SDL_SetTextureAlphaMod(current, 255);
    }

    // Push resolved history back onto `current` so the outer blit /
    // SSAA downsample path picks it up unchanged.
    SDL_SetRenderTarget(r, current);
    SDL_SetTextureBlendMode(g_taaHistory, SDL_BLENDMODE_NONE);
    SDL_RenderTexture(r, g_taaHistory, NULL, NULL);

    SDL_SetRenderTarget(r, prevTarget);

    // Quiet the threshold-was-unused warning; the field is kept for
    // API compatibility (UIApp_SetTAAMotionThreshold still accepts it
    // but it has no effect on the GPU path).
    (void)g_taaMotionThreshold;
}

static void CleanupTaaHistory(void) {
    // GPU history target (TAA).
    if (g_taaHistory) {
        SDL_DestroyTexture(g_taaHistory);
        g_taaHistory = NULL;
        g_taaHistoryW = g_taaHistoryH = 0;
    }
    // Legacy CPU buffers - free if a prior path ever allocated them.
    free(g_taaHistoryCpu);
    free(g_taaScratchCpu);
    g_taaHistoryCpu = NULL;
    g_taaScratchCpu = NULL;
    g_taaCpuW = g_taaCpuH = 0;
    g_taaHistoryReady = 0;

    // GPU aux target (FXAA blur).
    if (g_fxaaAux) {
        SDL_DestroyTexture(g_fxaaAux);
        g_fxaaAux = NULL;
        g_fxaaAuxW = g_fxaaAuxH = 0;
    }

    // Legacy FXAA scratch buffers (CPU path).
    free(g_fxaaSrcBuf);
    free(g_fxaaDstBuf);
    g_fxaaSrcBuf = NULL;
    g_fxaaDstBuf = NULL;
    g_fxaaBufCap = 0;
}

// ---------------------------------------------------------------------
// Drop shadow
// ---------------------------------------------------------------------
// Computes the shadow mask (alpha per pixel) using the SDF (signed
// distance function) of a rounded rectangle:
//
//   shape:  rect (-W/2..W/2) x (-H/2..H/2), corner radius R
//   sdf(p): signed distance from p to the shape surface
//           (<= 0 inside, > 0 outside)
//
// The resulting alpha is:
//   - 1.0           when sdf <= 0      (inside the shape)
//   - 0.0           when sdf >= blur
//   - smoothstep    over the [0, blur] interval (soft falloff)
//
// smoothstep is cubic (3t^2 - 2t^3); it approximates a Gaussian
// falloff well enough for UI shadows without any blur passes.
//
// Result: a cached white texture whose alpha == coverage. The shadow
// color is applied via SDL_SetTextureColorMod at draw time, so the
// same texture serves any colour.

static float Smoothstep01(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static SDL_Texture* RasterizeShadowMask(SDL_Renderer* renderer,
                                        int shapeW, int shapeH,
                                        float radius, float blur, float spread,
                                        int* outPadding) {
    if (shapeW < 1) shapeW = 1;
    if (shapeH < 1) shapeH = 1;
    if (blur < 0.0f) blur = 0.0f;

    // Apply spread to the shape before rasterization.
    const float effW = (float)shapeW + 2.0f * spread;
    const float effH = (float)shapeH + 2.0f * spread;
    const float effR = SDL_max(0.0f, radius + spread);

    if (effW <= 0.0f || effH <= 0.0f) {
        if (outPadding) *outPadding = 0;
        return NULL;
    }

    // Padding around the shape so that the blur halo is not clipped.
    // +2 as a safety margin to avoid any visible hard edge.
    const int pad = (int)ceilf(blur) + 2;
    const int texW = (int)ceilf(effW) + 2 * pad;
    const int texH = (int)ceilf(effH) + 2 * pad;

    if (outPadding) *outPadding = pad;

    Uint8* pixels = (Uint8*)malloc((size_t)texW * (size_t)texH * 4);
    if (!pixels) return NULL;

    // Shape: rectangle centred in the texture.
    const float cx = (float)texW * 0.5f;
    const float cy = (float)texH * 0.5f;
    const float halfW_minusR = effW * 0.5f - effR;
    const float halfH_minusR = effH * 0.5f - effR;
    // Saturate when effR is larger than half the shape.
    const float r2EdgeW = SDL_max(0.0f, halfW_minusR);
    const float r2EdgeH = SDL_max(0.0f, halfH_minusR);
    // When the shape is smaller than 2*R we clamp r to half the shorter side.
    const float clampedR = SDL_min(effR, SDL_min(effW, effH) * 0.5f);

    // Precomputed to avoid a divide-by-zero when blur == 0.
    const float invBlur = (blur > 0.0f) ? (1.0f / blur) : 0.0f;

    for (int py = 0; py < texH; py++) {
        const float dy = fabsf((float)py + 0.5f - cy);
        for (int px = 0; px < texW; px++) {
            const float dx = fabsf((float)px + 0.5f - cx);

            // Distance to the "skeleton" rectangle (the rect inset by R).
            const float gx = SDL_max(dx - r2EdgeW, 0.0f);
            const float gy = SDL_max(dy - r2EdgeH, 0.0f);

            // SDF of the rounded rect.
            const float sdf = sqrtf(gx * gx + gy * gy) - clampedR;

            float a;
            if (sdf <= 0.0f) {
                a = 1.0f;
            } else if (blur <= 0.0f || sdf >= blur) {
                a = 0.0f;
            } else {
                a = 1.0f - Smoothstep01(sdf * invBlur);
            }

            const Uint8 alpha = (Uint8)(a * 255.0f + 0.5f);
            const size_t idx = ((size_t)py * (size_t)texW + (size_t)px) * 4;
            pixels[idx + 0] = 255;
            pixels[idx + 1] = 255;
            pixels[idx + 2] = 255;
            pixels[idx + 3] = alpha;
        }
    }

    SDL_Texture* tex = SDL_CreateTexture(renderer,
                                         SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC,
                                         texW, texH);
    if (tex) {
        SDL_UpdateTexture(tex, NULL, pixels, texW * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    }
    free(pixels);
    return tex;
}

static SDL_Texture* GetCachedShadow(SDL_Renderer* renderer,
                                    int w, int h, int r, int blur, int spread,
                                    int* outPadding) {
    // Lookup
    for (int i = 0; i < MAX_SHADOW_CACHE; i++) {
        ShadowCacheEntry* e = &g_shadowCache[i];
        if (e->tex && e->w == w && e->h == h && e->radius == r &&
            e->blur == blur && e->spread == spread) {
            e->lastUsed = ++g_shadowCacheTick;
            if (outPadding) *outPadding = e->padding;
            return e->tex;
        }
    }

    // Empty slot, or LRU eviction.
    int slot = 0;
    Uint64 oldest = (Uint64)-1;
    for (int i = 0; i < MAX_SHADOW_CACHE; i++) {
        ShadowCacheEntry* e = &g_shadowCache[i];
        if (!e->tex) { slot = i; break; }
        if (e->lastUsed < oldest) { oldest = e->lastUsed; slot = i; }
    }

    if (g_shadowCache[slot].tex) {
        SDL_DestroyTexture(g_shadowCache[slot].tex);
        g_shadowCache[slot].tex = NULL;
    }

    int pad = 0;
    SDL_Texture* tex = RasterizeShadowMask(renderer, w, h,
                                           (float)r, (float)blur, (float)spread,
                                           &pad);
    if (!tex) return NULL;

    g_shadowCache[slot] = (ShadowCacheEntry){
        .w = w, .h = h, .radius = r, .blur = blur, .spread = spread,
        .tex = tex, .padding = pad, .lastUsed = ++g_shadowCacheTick
    };
    if (outPadding) *outPadding = pad;
    return tex;
}

static void CleanupShadowCache(void) {
    for (int i = 0; i < MAX_SHADOW_CACHE; i++) {
        if (g_shadowCache[i].tex) {
            SDL_DestroyTexture(g_shadowCache[i].tex);
            g_shadowCache[i].tex = NULL;
        }
    }
}

// Draws the shadow for the given shape into the renderer. 'rect' is the
// shape's bounding box in screen coords (without any shadow offset).
// 'radius' is the shape's border radius. Call this BEFORE drawing the shape.
static void DrawDropShadow(SDL_Renderer* renderer, SDL_FRect rect,
                           float radius, UIShadow shadow) {
    if (!renderer || rect.w <= 0.0f || rect.h <= 0.0f) return;
    if (shadow.color.a <= 0.0f) return;
    if (shadow.blur <= 0.0f && shadow.offsetX == 0.0f && shadow.offsetY == 0.0f &&
        shadow.spread == 0.0f) {
        return; // "invisible" shadow
    }

    // Before touching textures, make sure any pending rectangle batch
    // has been flushed (otherwise the paint order would be wrong).
    FlushRenderBatch(renderer);

    int pad = 0;
    SDL_Texture* mask = GetCachedShadow(renderer,
                                        (int)ceilf(rect.w), (int)ceilf(rect.h),
                                        (int)ceilf(radius),
                                        (int)ceilf(shadow.blur),
                                        (int)ceilf(shadow.spread),
                                        &pad);
    if (!mask) return;

    SDL_SetTextureColorMod(mask,
                           (Uint8)shadow.color.r,
                           (Uint8)shadow.color.g,
                           (Uint8)shadow.color.b);
    SDL_SetTextureAlphaMod(mask,
                           (Uint8)SDL_clamp((int)(shadow.color.a * 255.0f), 0, 255));

    float texW = 0.0f, texH = 0.0f;
    SDL_GetTextureSize(mask, &texW, &texH);

    // The texture covers (rect.w + 2*pad) x (rect.h + 2*pad), with the
    // shape centred. We place the texture corner at (rect.x - pad +
    // offsetX, rect.y - pad + offsetY), also accounting for spread.
    const float dx = rect.x + shadow.offsetX - (float)pad;
    const float dy = rect.y + shadow.offsetY - (float)pad;

    SDL_FRect dst = { dx, dy, texW, texH };
    SDL_RenderTexture(renderer, mask, NULL, &dst);

    // Reset the mods so they don't bleed into the next use of this texture.
    SDL_SetTextureColorMod(mask, 255, 255, 255);
    SDL_SetTextureAlphaMod(mask, 255);
}

// Draws a filled rectangle with rounded corners:
//   1. 5 axis-aligned rectangles covering the "inner" area (everything
//      except the 4 r x r corner squares).
//   2. 4 quadrants of an AA coverage circle texture placed in the corners.
//
// The unified flow works for any radius (including r ~ min(w,h)/2, i.e.
// a pill / perfect circle). For radius 0 it collapses to a single
// RenderFillRect.
// --------------------------------------------------------------------
// CPU-rasterised rotated rounded rectangle with analytic coverage AA.
//
// Why this exists: rendering the rect upright into an offscreen target
// and then SDL_RenderTextureRotated'ing it produces visible diagonal
// stair-stepping on the rounded corners — bilinear filtering can't
// reconstruct sharp edges that fall on sub-pixel positions. Bumping
// the supersample factor helps a little but the fundamental issue is
// the sampling, not the source resolution.
//
// This function computes coverage directly in SCREEN space. For each
// output pixel it inverse-rotates the pixel centre back into the
// rect's local frame, evaluates the rounded-rect signed distance, and
// derives ~1px feathered alpha from it. The result is uploaded as a
// flat ARGB texture and blit 1:1 — no rotation at sample time, so
// every angle looks identical in quality.
//
// Cost: ~50 ns/pixel inside the rotated bounding box. A 40×40 rect
// at ~7° rotation works out to ~56×56 = ~3 K pixels = ~150 µs per
// frame, per rect. Comfortable for the typical handful of rotated
// rects an app has on screen.
// --------------------------------------------------------------------
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void DrawRotatedRoundedRectFill(SDL_Renderer* rend,
                                SDL_FRect rect,
                                float angleDeg,
                                float radius,
                                UIColor color) {
    if (!rend || rect.w <= 0.0f || rect.h <= 0.0f) return;
    if (radius < 0.0f) radius = 0.0f;
    float halfW = rect.w * 0.5f;
    float halfH = rect.h * 0.5f;
    if (radius > halfW) radius = halfW;
    if (radius > halfH) radius = halfH;

    const float cx = rect.x + halfW;
    const float cy = rect.y + halfH;

    // Inverse rotation matrix (screen → local). Pre-compute once.
    const float aRad = -angleDeg * (float)(M_PI / 180.0);
    const float cosA = cosf(aRad);
    const float sinA = sinf(aRad);

    // Half extents of the inner (corner-radius-shrunk) rect — the SDF
    // for a rounded rect is the SDF of this smaller rect minus radius.
    const float hw = halfW - radius;
    const float hh = halfH - radius;

    // Axis-aligned screen bounding box of the rotated rect, padded by
    // 1 px on every side to leave room for the AA falloff.
    const float ang = angleDeg * (float)(M_PI / 180.0);
    const float absC = fabsf(cosf(ang));
    const float absS = fabsf(sinf(ang));
    const float bboxHW = halfW * absC + halfH * absS + 1.0f;
    const float bboxHH = halfW * absS + halfH * absC + 1.0f;

    const int x0 = (int)floorf(cx - bboxHW) - 1;
    const int y0 = (int)floorf(cy - bboxHH) - 1;
    const int x1 = (int)ceilf (cx + bboxHW) + 1;
    const int y1 = (int)ceilf (cy + bboxHH) + 1;
    const int W = x1 - x0 + 1;
    const int H = y1 - y0 + 1;
    if (W <= 0 || H <= 0) return;

    Uint32* pixels = (Uint32*)malloc((size_t)W * (size_t)H * sizeof(Uint32));
    if (!pixels) return;

    const Uint8 fR = (Uint8)color.r;
    const Uint8 fG = (Uint8)color.g;
    const Uint8 fB = (Uint8)color.b;
    const float fA = color.a;

    for (int py = 0; py < H; py++) {
        // Distance from the rect's centre, fixed for this row.
        const float dy0 = (float)(y0 + py) + 0.5f - cy;
        Uint32* row = pixels + (size_t)py * (size_t)W;
        for (int px = 0; px < W; px++) {
            const float dx = (float)(x0 + px) + 0.5f - cx;

            // Inverse rotation → local coords of the unrotated rect.
            const float lx = dx * cosA - dy0 * sinA;
            const float ly = dx * sinA + dy0 * cosA;

            // Signed distance to the rounded rectangle:
            //   sdf = length(max(|p| - inner, 0)) + min(max(qx,qy),0) - r
            const float qx = fabsf(lx) - hw;
            const float qy = fabsf(ly) - hh;
            const float ax = qx > 0.0f ? qx : 0.0f;
            const float ay = qy > 0.0f ? qy : 0.0f;
            const float outDist = sqrtf(ax * ax + ay * ay);
            const float qmax    = qx > qy ? qx : qy;
            const float inDist  = qmax < 0.0f ? qmax : 0.0f;
            const float sdf     = outDist + inDist - radius;

            // ~1 px feather: sdf <= -0.5 = fully inside (cov=1),
            // sdf >= 0.5 = fully outside (cov=0), linear in between.
            float cov;
            if      (sdf <= -0.5f) cov = 1.0f;
            else if (sdf >=  0.5f) cov = 0.0f;
            else                   cov = 0.5f - sdf;

            const Uint8 a = (Uint8)(fA * 255.0f * cov + 0.5f);
            row[px] = ((Uint32)a << 24) |
                      ((Uint32)fR << 16) |
                      ((Uint32)fG <<  8) |
                      ((Uint32)fB);
        }
    }

    SDL_Texture* tex = SDL_CreateTexture(rend, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STATIC, W, H);
    if (tex) {
        SDL_UpdateTexture(tex, NULL, pixels, W * (int)sizeof(Uint32));
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
        SDL_FRect dst = { (float)x0, (float)y0, (float)W, (float)H };
        SDL_RenderTexture(rend, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
    }
    free(pixels);
}

void DrawRoundedRectFill(SDL_Renderer* rend, SDL_FRect rect, UIColor color, float radius) {
    if (!rend || rect.w <= 0.0f || rect.h <= 0.0f) return;

    const SDL_Color sdlColor = {
        (Uint8)color.r, (Uint8)color.g, (Uint8)color.b,
        (Uint8)SDL_clamp((int)(color.a * 255.0f), 0, 255)
    };

    // No radius: plain rectangle (batched).
    if (radius <= 0.5f) {
        BatchRect(rend, &rect, sdlColor);
        return;
    }

    // Clamp the radius to half of the shortest side (pill / circle case).
    const float r = SDL_min(radius, SDL_min(rect.w, rect.h) * 0.5f);

    // (1) Five rects covering everything except the 4 r x r corner squares.
    const SDL_FRect parts[5] = {
        { rect.x + r,           rect.y,                  rect.w - 2.0f * r, r                  }, // top
        { rect.x + r,           rect.y + rect.h - r,     rect.w - 2.0f * r, r                  }, // bottom
        { rect.x,               rect.y + r,              r,                 rect.h - 2.0f * r  }, // left
        { rect.x + rect.w - r,  rect.y + r,              r,                 rect.h - 2.0f * r  }, // right
        { rect.x + r,           rect.y + r,              rect.w - 2.0f * r, rect.h - 2.0f * r  }  // centre
    };

    for (int i = 0; i < 5; i++) {
        if (parts[i].w > 0.0f && parts[i].h > 0.0f) {
            BatchRect(rend, &parts[i], sdlColor);
        }
    }
    FlushRenderBatch(rend);

    // (2) AA quadrants in the 4 corners — packed into a single
    // SDL_RenderGeometry call. The cached circle texture is sampled in
    // its four quadrants (u/v in [0,0.5] / [0.5,1]) and the fill colour
    // is carried per-vertex, which replaces the SetTextureColorMod /
    // SetTextureAlphaMod state changes the previous version needed.
    //
    // Why this matters: each SDL_RenderTexture call carries pipeline
    // plumbing on the D3D11 / Vulkan backends. Going from 4 textured
    // draws + 2 mod state updates per rounded rect to a single
    // RenderGeometry call removes the dominant per-widget overhead on
    // a UI scene full of buttons/cards/dropdowns.
    const int cornerSize = (int)ceilf(r * 2.0f);
    SDL_Texture* corner = GetCachedCircleTexture(rend, cornerSize);
    if (!corner) return;

    // DrawCircle elsewhere applies SDL_SetTextureColorMod on the same
    // cached texture and never resets it. Reset to identity here so the
    // per-vertex colour in the mesh is the final output colour, not
    // (per-vertex × lingering mod).
    SDL_SetTextureColorMod(corner, 255, 255, 255);
    SDL_SetTextureAlphaMod(corner, 255);

    const SDL_FColor fc = {
        sdlColor.r * (1.0f / 255.0f),
        sdlColor.g * (1.0f / 255.0f),
        sdlColor.b * (1.0f / 255.0f),
        sdlColor.a * (1.0f / 255.0f),
    };

    // 4 destination quads, one per corner of the rect.
    const float x0 = rect.x;
    const float x1 = rect.x + rect.w - r;
    const float y0 = rect.y;
    const float y1 = rect.y + rect.h - r;

    SDL_Vertex   v[16];
    int          idx[24];

    // (quadrant tex_coord, destination quad)
    //  i  | u0   u1   v0   v1  | dst_x  dst_y
    //  0  | 0.0  0.5  0.0  0.5 |  x0     y0     (top-left)
    //  1  | 0.5  1.0  0.0  0.5 |  x1     y0     (top-right)
    //  2  | 0.0  0.5  0.5  1.0 |  x0     y1     (bottom-left)
    //  3  | 0.5  1.0  0.5  1.0 |  x1     y1     (bottom-right)
    static const float uvs[4][4] = {
        { 0.0f, 0.5f, 0.0f, 0.5f },
        { 0.5f, 1.0f, 0.0f, 0.5f },
        { 0.0f, 0.5f, 0.5f, 1.0f },
        { 0.5f, 1.0f, 0.5f, 1.0f },
    };
    const float dx[4] = { x0, x1, x0, x1 };
    const float dy[4] = { y0, y0, y1, y1 };

    for (int i = 0; i < 4; i++) {
        const float u0 = uvs[i][0], u1 = uvs[i][1];
        const float v0 = uvs[i][2], v1 = uvs[i][3];
        const float dxi = dx[i], dyi = dy[i];
        const int b = i * 4;

        v[b + 0].position  = (SDL_FPoint){ dxi,      dyi      };
        v[b + 1].position  = (SDL_FPoint){ dxi + r,  dyi      };
        v[b + 2].position  = (SDL_FPoint){ dxi + r,  dyi + r  };
        v[b + 3].position  = (SDL_FPoint){ dxi,      dyi + r  };
        v[b + 0].color = v[b + 1].color = v[b + 2].color = v[b + 3].color = fc;
        v[b + 0].tex_coord = (SDL_FPoint){ u0, v0 };
        v[b + 1].tex_coord = (SDL_FPoint){ u1, v0 };
        v[b + 2].tex_coord = (SDL_FPoint){ u1, v1 };
        v[b + 3].tex_coord = (SDL_FPoint){ u0, v1 };

        const int j = i * 6;
        idx[j + 0] = b + 0; idx[j + 1] = b + 1; idx[j + 2] = b + 2;
        idx[j + 3] = b + 0; idx[j + 4] = b + 2; idx[j + 5] = b + 3;
    }

    SDL_RenderGeometry(rend, corner, v, 16, idx, 24);
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

// Helper: makes sure a UIWidget has float* width/height initialised to
// the given default values. Used by container layouts so children get
// usable bounds without forcing the caller to size every item.
static void EnsureWidgetSize(UIWidget* w, float defaultW, float defaultH) {
    if (!w) return;
    if (!w->width) {
        w->width = (float*)malloc(sizeof(float));
        if (w->width) *w->width = defaultW;
    }
    if (!w->height) {
        w->height = (float*)malloc(sizeof(float));
        if (w->height) *w->height = defaultH;
    }
}

// Forward declaration so containers can recurse into themselves.
static void RenderSingleWidget(UIWindow* window, UIWidget* el);
static void RenderSingleWidget_Inner(UIWindow* window, UIWidget* el);

// Lays a UIGrid's items out relative to the grid widget's (x, y) and
// recursively renders them via RenderSingleWidget. Children are sized
// to (grid->cellW, grid->cellH) when they have no explicit size.
static void RenderGrid(UIWindow* window, UIWidget* el, UIGrid* g) {
    if (!g->items) return;
    UIChildren_SortByZ(g->items);

    const int cols = g->columns > 0 ? g->columns : 1;
    const float stepX = g->cellW + g->gapX;
    const float stepY = g->cellH + g->gapY;
    const float originX = el->x + g->paddingLeft;
    const float originY = el->y + g->paddingTop;

    for (int i = 0; i < g->items->count; i++) {
        UIWidget* item = g->items->children[i];
        if (!item) continue;
        const int col = i % cols;
        const int row = i / cols;
        EnsureWidgetSize(item, g->cellW, g->cellH);
        item->x = originX + col * stepX;
        item->y = originY + row * stepY;
        RenderSingleWidget(window, item);
    }
}

// Renders a UIScroll: optional background, clip rect over the viewport,
// then renders content offset by (-scrollX, -scrollY). The content's
// position is mutated so nested grids see their absolute on-screen
// coords.
static void RenderScroll(UIWindow* window, UIWidget* el, UIScroll* s) {
    if (!el->width || !el->height) return;
    const SDL_FRect viewport = { el->x, el->y, *el->width, *el->height };
    if (viewport.w <= 0 || viewport.h <= 0) return;

    // Measure content the first time (or after invalidation).
    if (s->contentW == 0.0f || s->contentH == 0.0f) {
        if (s->content && s->content->data) {
            UIWidgetBase* cb = (UIWidgetBase*)s->content->data;
            if (!strcmp(cb->__widget_type, UI_WIDGET_GRID)) {
                UIGrid* g = (UIGrid*)cb;
                // ListView-style: cellW <= 0 stretches to viewport width.
                if (g->cellW <= 0.0f) {
                    g->cellW = viewport.w - g->paddingLeft - g->paddingRight;
                }
                UIGrid_GetContentSize(g, &s->contentW, &s->contentH);
            } else if (s->content->width && s->content->height) {
                s->contentW = *s->content->width;
                s->contentH = *s->content->height;
            }
        }
    }

    // Clamp scroll offsets.
    float maxX = s->contentW - viewport.w; if (maxX < 0.0f) maxX = 0.0f;
    float maxY = s->contentH - viewport.h; if (maxY < 0.0f) maxY = 0.0f;
    if (s->scrollX < 0.0f) s->scrollX = 0.0f;
    if (s->scrollY < 0.0f) s->scrollY = 0.0f;
    if (s->scrollX > maxX) s->scrollX = maxX;
    if (s->scrollY > maxY) s->scrollY = maxY;
    if (!s->allowHorizontal) s->scrollX = 0.0f;
    if (!s->allowVertical)   s->scrollY = 0.0f;

    // Optional background.
    if (s->background) {
        UIRectangle* bg = (UIRectangle*)s->background;
        DrawRoundedRectWithBorder(window->sdlRenderer, viewport,
                                  bg->color, bg->radius,
                                  (int)bg->borderWidth, bg->borderColor);
    }

    // Flush pending batches before changing the clip rect - otherwise
    // they may be rendered against the wrong scissor.
    FlushRenderBatch(window->sdlRenderer);

    SDL_Rect clip = {
        (int)viewport.x, (int)viewport.y,
        (int)ceilf(viewport.w), (int)ceilf(viewport.h)
    };
    SDL_SetRenderClipRect(window->sdlRenderer, &clip);

    if (s->content) {
        s->content->x = viewport.x - s->scrollX;
        s->content->y = viewport.y - s->scrollY;
        EnsureWidgetSize(s->content, s->contentW, s->contentH);
        RenderSingleWidget(window, s->content);
    }

    FlushRenderBatch(window->sdlRenderer);
    SDL_SetRenderClipRect(window->sdlRenderer, NULL);
}

// Bounds-check that culls widgets entirely outside the current render
// target. The numbers we render with always have width/height set
// (containers ensure that via EnsureWidgetSize), so this is cheap.
static int WidgetCulled(UIWindow* window, UIWidget* el) {
    if (!el->width || !el->height) return 0;
    const float w = *el->width;
    const float h = *el->height;
    if (w <= 0 || h <= 0) return 1;
    int rw = 0, rh = 0;
    SDL_GetWindowSize(window->sdlWindow, &rw, &rh);
    if (el->x + w < 0 || el->y + h < 0 ||
        el->x > (float)rw || el->y > (float)rh) {
        return 1;
    }
    return 0;
}

// Wrapper that honours UIWidget_SetClipChildren: when the widget being
// rendered is anchored (directly or transitively) to an ancestor that
// has clipChildren = 1, we set the SDL render clip rect to that ancestor's
// bounds for the duration of the inner render. The previous clip is
// saved on a small local stack and restored after, so nested clips
// compose with stack discipline.
static void RenderSingleWidget(UIWindow* window, UIWidget* el) {
    if (!el || !window || !window->sdlRenderer) {
        RenderSingleWidget_Inner(window, el);
        return;
    }

    // Walk the alignment chain looking for the nearest ancestor with
    // clipChildren enabled. The chain is "widget -> H-target -> its
    // H-target -> ..."; we use H over V because in practice both
    // resolve to the same parent in this codebase, and inconsistencies
    // would just degrade to no-clip rather than mis-clip.
    UIWidget* clipAnc = NULL;
    if (el->alignment) {
        UIWidget* cur = UIAlignment_GetHTarget((UIAlignment*)el->alignment);
        // Bound the walk so a pathological cycle can't lock the renderer.
        for (int hops = 0; cur != NULL && hops < 32; hops++) {
            if (cur->clipChildren && cur->width && cur->height) {
                clipAnc = cur;
                break;
            }
            cur = cur->alignment
                ? UIAlignment_GetHTarget((UIAlignment*)cur->alignment)
                : NULL;
        }
    }

    if (!clipAnc) {
        RenderSingleWidget_Inner(window, el);
        return;
    }

    // Save the current clip so we can restore it after — supports
    // nesting (a clipping child of a clipping parent).
    const bool hadClip = SDL_RenderClipEnabled(window->sdlRenderer);
    SDL_Rect prevClip = {0, 0, 0, 0};
    if (hadClip) SDL_GetRenderClipRect(window->sdlRenderer, &prevClip);

    const SDL_Rect cr = {
        (int)clipAnc->x,
        (int)clipAnc->y,
        (int)*clipAnc->width,
        (int)*clipAnc->height
    };
    SDL_SetRenderClipRect(window->sdlRenderer, &cr);

    RenderSingleWidget_Inner(window, el);

    if (hadClip) SDL_SetRenderClipRect(window->sdlRenderer, &prevClip);
    else         SDL_SetRenderClipRect(window->sdlRenderer, NULL);
}

static void RenderSingleWidget_Inner(UIWindow* window, UIWidget* el) {
    if (UI_UNLIKELY(!el || !el->visible || !el->data)) return;
    // Skip widgets fully outside the window. Big win for scrolls /
    // lists with off-screen content; cheap when the widget has bounds.
    if (UI_UNLIKELY(WidgetCulled(window, el))) return;

    if (el->alignment) UIAlignment_Align(el);
    UIWidgetBase* base = (UIWidgetBase*)el->data;

    // Opacity: skip fully transparent widgets to save GPU work, otherwise
    // propagate the value to per-shape alpha. Stored on UIWidget;
    // defaults to 1.0 from UIWidget_Create.
    float op = el->opacity;
    if (op <= 0.0f) return;
    if (op > 1.0f) op = 1.0f;

    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_GRID)) {
        RenderGrid(window, el, (UIGrid*)base);
        return;
    }
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_SCROLL)) {
        RenderScroll(window, el, (UIScroll*)base);
        return;
    }

    // ----- UIStack (lays children sequentially along one axis) -----
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_STACK)) {
        UIStack* s = (UIStack*)base;
        if (!s->items) return;
        UIChildren_SortByZ(s->items);
        float cursorX = el->x + s->paddingLeft;
        float cursorY = el->y + s->paddingTop;
        for (int i = 0; i < s->items->count; i++) {
            UIWidget* item = s->items->children[i];
            if (!item || !item->visible) continue;
            const float iw = item->width  ? *item->width  : 0.0f;
            const float ih = item->height ? *item->height : 0.0f;
            item->x = cursorX;
            item->y = cursorY;
            RenderSingleWidget(window, item);
            if (s->orientation == UI_STACK_HORIZONTAL) cursorX += iw + s->spacing;
            else                                       cursorY += ih + s->spacing;
        }
        return;
    }

    // ----- UIDialog (backdrop + centred card with children) --------
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_DIALOG)) {
        UIDialog* d = (UIDialog*)base;
        if (!d->visible || !el->width || !el->height) return;

        SDL_FRect full = { el->x, el->y, *el->width, *el->height };
        UIColor backdrop = d->backdropColor; backdrop.a *= op;
        DrawRoundedRectFill(window->sdlRenderer, full, backdrop, 0.0f);

        const float cx = el->x + (*el->width  - d->cardW) * 0.5f;
        const float cy = el->y + (*el->height - d->cardH) * 0.5f;
        SDL_FRect card = { cx, cy, d->cardW, d->cardH };

        UIColor cc = d->cardColor; cc.a *= op;
        // Drop a shadow under the card so it pops off the backdrop.
        UIShadow dropSh = {
            .offsetX = 0.0f, .offsetY = 14.0f,
            .blur    = 32.0f, .spread = -6.0f,
            .color   = { 0, 0, 0, 0.4f * op }
        };
        DrawDropShadow(window->sdlRenderer, card, d->radius, dropSh);
        DrawRoundedRectWithBorder(window->sdlRenderer, card, cc, d->radius, 0,
                                  (UIColor){0,0,0,0.0f});

        // Content children: positioned relative to the card top-left.
        if (d->content) {
            for (int i = 0; i < d->content->count; i++) {
                UIWidget* c = d->content->children[i];
                if (!c) continue;
                // Store absolute coords during render so mouse hit-tests
                // and event handlers see them correctly.
                const float storedX = c->x;
                const float storedY = c->y;
                c->x = cx + storedX;
                c->y = cy + storedY;
                RenderSingleWidget(window, c);
                c->x = storedX;
                c->y = storedY;
            }
        }
        return;
    }

    // ----- UITabView -----------------------------------------------
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_TABVIEW)) {
        UITabView* tv = (UITabView*)base;
        if (!el->width || !el->height || tv->tabCount <= 0) return;

        const float W = *el->width;
        const float H = *el->height;
        const float headerH = tv->tabHeight;

        // Body panel underneath.
        SDL_FRect panel = { el->x, el->y + headerH, W, H - headerH };
        UIColor panelC = tv->panelBg; panelC.a *= op;
        DrawRoundedRectFill(window->sdlRenderer, panel, panelC, tv->radius);

        // Header row.
        const float tabW = W / (float)tv->tabCount;
        TTF_Font* font = (tv->fontFamily && tv->fontSize > 0.0f)
            ? GetFont(tv->fontFamily, tv->fontSize) : NULL;
        // Cached fonts are shared across all widgets — reset the style
        // bits so a Bold/Italic UIText rendered earlier doesn't leak
        // into these labels (TabView has no fontStyle field).
        if (font) TTF_SetFontStyle(font, TTF_STYLE_NORMAL);

        for (int i = 0; i < tv->tabCount; i++) {
            SDL_FRect h = { el->x + i * tabW, el->y, tabW, headerH };
            const int active = (i == tv->activeIndex);
            UIColor bg = active ? tv->tabBgActive : tv->tabBg; bg.a *= op;
            DrawRoundedRectFill(window->sdlRenderer, h, bg, tv->radius);

            if (font && tv->titles[i]) {
                const UIColor txt = active ? tv->tabTextActive : tv->tabText;
                SDL_Color sc = {(Uint8)txt.r,(Uint8)txt.g,(Uint8)txt.b,
                                (Uint8)SDL_clamp((int)(txt.a*op*255.0f),0,255)};
                const size_t len = strlen(tv->titles[i]);
                SDL_Surface* surf = TTF_RenderText_Blended(font, tv->titles[i], len, sc);
                if (surf) {
                    SDL_Texture* tex = SDL_CreateTextureFromSurface(window->sdlRenderer, surf);
                    if (tex) {
                        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
                        float tw = 0, th = 0;
                        SDL_GetTextureSize(tex, &tw, &th);
                        SDL_FRect dst = { h.x + (tabW - tw) * 0.5f,
                                          h.y + (headerH - th) * 0.5f,
                                          tw, th };
                        SDL_RenderTexture(window->sdlRenderer, tex, NULL, &dst);
                        SDL_DestroyTexture(tex);
                    }
                    SDL_DestroySurface(surf);
                }
            }
        }

        // Active panel content.
        if (tv->activeIndex >= 0 && tv->activeIndex < tv->panels->count) {
            UIWidget* p = tv->panels->children[tv->activeIndex];
            if (p) {
                const float storedX = p->x;
                const float storedY = p->y;
                p->x = el->x + storedX;
                p->y = el->y + headerH + storedY;
                // If the panel has no explicit size, give it the body
                // area so its own renderer / children know the bounds.
                EnsureWidgetSize(p, W, H - headerH);
                RenderSingleWidget(window, p);
                p->x = storedX;
                p->y = storedY;
            }
        }
        return;
    }

    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_RECTANGLE)) {
        UIRectangle *rect = (UIRectangle*)base;
        if (!el->width || !el->height) return;
        g_tempRect1.x = el->x;
        g_tempRect1.y = el->y;
        g_tempRect1.w = *el->width;
        g_tempRect1.h = *el->height;
        // Rectangle margins are now consumed by the alignment pass as
        // pure outer offsets (see UIAlignment_Align). Re-applying them
        // here would double-offset the geometry and — when margin >=
        // widget dimension — collapse the render to zero, which is
        // exactly what made anchored chips with a top margin invisible.
        // Apply opacity to the fill / border colours by scaling their alpha.
        UIColor fillC   = rect->color;       fillC.a   *= op;
        UIColor borderC = rect->borderColor; borderC.a *= op;
        UIShadow sh     = rect->shadow;      sh.color.a *= op;

        if (rect->hasShadow) {
            DrawDropShadow(window->sdlRenderer, g_tempRect1, rect->radius, sh);
        }
        if (g_tempRect1.w > 0 && g_tempRect1.h > 0) {
            // Rotated path: render the rect's shape into an offscreen
            // target at 4× resolution, then RenderTextureRotated it onto
            // the final destination. The 4× supersample + the linear
            // downsample during the rotated blit gives ~16:1 effective
            // AA per output pixel, enough to kill the diagonal stair-
            // step ("serrilhado") that even 2× still showed on the
            // rounded corners at the small rotation angles the logo
            // uses. Shadows are drawn upright by the call above — the
            // rect rotates over them.
            if (el->rotation != 0.0f) {
                // Rasterise the rotated rect with analytic per-pixel
                // coverage AA at the screen resolution. This avoids
                // the bilinear-sampling stair-step on the rounded
                // corners that texture rotation produces at small
                // angles. Border in rotation mode isn't supported yet
                // — fall back to the unrotated path if one is set.
                if (rect->borderWidth <= 0.0f) {
                    DrawRotatedRoundedRectFill(window->sdlRenderer,
                                               g_tempRect1,
                                               el->rotation,
                                               rect->radius,
                                               fillC);
                    return;
                }
                // Borderless fallback: draw upright if the caller
                // really did want a border + rotation.
            }
            if (fabsf(g_tempRect1.w - g_tempRect1.h) < 0.5f &&
                fabsf(rect->radius - g_tempRect1.w/2) < 0.5f) {
                float centerX = g_tempRect1.x + g_tempRect1.w/2;
                float centerY = g_tempRect1.y + g_tempRect1.h/2;
                float radius  = g_tempRect1.w/2;
                if (rect->borderWidth > 0) {
                    DrawCircle(window->sdlRenderer, centerX, centerY, radius, borderC);
                    radius -= rect->borderWidth;
                }
                if (radius > 0) {
                    DrawCircle(window->sdlRenderer, centerX, centerY, radius, fillC);
                }
            } else {
                DrawRoundedRectWithBorder(window->sdlRenderer, g_tempRect1,
                                          fillC, rect->radius,
                                          (int)rect->borderWidth, borderC);
            }
        }
        return;
    }

    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_TEXT)) {
        UIText *twid = (UIText*)base;
        if (!twid->text || !twid->fontFamily || !*twid->text ||
            !twid->textLength || twid->fontSize <= 0.0f) return;

        // Resolve the effective wrap width. When wrapToBounds is set we
        // follow the widget's explicit width (minus padding); otherwise
        // we use the fixed wrapWidth.
        int effWrapW = twid->wrapWidth;
        if (twid->wrapToBounds && el->width && *el->width > 0.0f) {
            int contentW = (int)(*el->width - twid->paddingLeft - twid->paddingRight);
            if (contentW > 0) effWrapW = contentW;
        }
        // Pick a wrap mode. UI_WRAP_NONE with a positive wrapWidth keeps
        // the historical "wrap by word at fixed pixel width" behaviour.
        UIWrapMode effMode = twid->wrapMode;
        if (effMode == UI_WRAP_NONE && effWrapW > 0) effMode = UI_WRAP_WORD;

        // -------- Selectable path --------
        // Lays text out per visual line (so a hit-test can map a click
        // back to a byte offset) and draws a selection highlight on
        // top. FIT does not compose with selection (scaling would skew
        // mouse picking) and is treated as NONE here.
        if (twid->selectable) {
            UIWrapMode selMode = (effMode == UI_WRAP_FIT) ? UI_WRAP_NONE : effMode;
            int selWrapW = (selMode == UI_WRAP_NONE) ? -1 : effWrapW;
            if (selWrapW < 1) selWrapW = -1;

            TTF_Font *font = GetFont(twid->fontFamily, twid->fontSize);
            if (!font) return;
            // Hinting is set once at font load (GetFont); no need to
            // touch it per-render. Setting it again would force TTF to
            // rebuild internal glyph caches on a hit.
            TTF_SetFontStyle(font, (TTF_FontStyleFlags)twid->fontStyle);

            const int needRebuild =
                twid->__cachedTextLen  != twid->textLength ||
                twid->__cachedWrapMode != (int)selMode     ||
                twid->__cachedWrapW    != selWrapW;

            if (needRebuild) {
                if (twid->__lineTextures) {
                    for (int i = 0; i < twid->__linesLen; i++) {
                        if (twid->__lineTextures[i]) SDL_DestroyTexture(twid->__lineTextures[i]);
                    }
                }
                if (twid->__lineCharOffsets) {
                    for (int i = 0; i < twid->__linesLen; i++) {
                        free(twid->__lineCharOffsets[i]);
                    }
                }
                free(twid->__lineTextures);       twid->__lineTextures = NULL;
                free(twid->__lineStarts);         twid->__lineStarts = NULL;
                free(twid->__lineLengths);        twid->__lineLengths = NULL;
                free(twid->__lineCharOffsets);    twid->__lineCharOffsets = NULL;
                free(twid->__lineCharOffsetsLen); twid->__lineCharOffsetsLen = NULL;
                free(twid->__lineIsSoft);         twid->__lineIsSoft = NULL;
                twid->__linesLen = 0;

                int cap = 1;
                for (int i = 0; i < twid->textLength; i++) if (twid->text[i] == '\n') cap++;
                if (selWrapW > 0) cap = cap * 2 + 4;
                if (cap < 1) cap = 1;

                twid->__linesCap            = cap;
                twid->__lineTextures        = (SDL_Texture**)calloc(cap, sizeof(SDL_Texture*));
                twid->__lineStarts          = (int*)calloc(cap, sizeof(int));
                twid->__lineLengths         = (int*)calloc(cap, sizeof(int));
                twid->__lineCharOffsets     = (int**)calloc(cap, sizeof(int*));
                twid->__lineCharOffsetsLen  = (int*)calloc(cap, sizeof(int));
                twid->__lineIsSoft          = (int*)calloc(cap, sizeof(int));

                const UIColor c = twid->color;
                const SDL_Color sc = { (Uint8)c.r, (Uint8)c.g, (Uint8)c.b,
                                       (Uint8)SDL_clamp((int)(c.a * 255.0f), 0, 255) };

                int logicalStart = 0;
                for (int i = 0; i <= twid->textLength; i++) {
                    if (i < twid->textLength && twid->text[i] != '\n') continue;
                    const int lineEnd = i;
                    int segStart    = logicalStart;
                    int firstInLine = 1;

                    do {
                        int segEnd;
                        if (selWrapW <= 0 || lineEnd - segStart == 0) {
                            segEnd = lineEnd;
                        } else {
                            int w = 0, h = 0;
                            TTF_GetStringSize(font, twid->text + segStart,
                                              (size_t)(lineEnd - segStart), &w, &h);
                            if (w <= selWrapW) {
                                segEnd = lineEnd;
                            } else {
                                int lo = 1, hi = lineEnd - segStart, fit = 1;
                                while (lo <= hi) {
                                    int mid = (lo + hi) / 2;
                                    int ww = 0, hh = 0;
                                    TTF_GetStringSize(font, twid->text + segStart,
                                                      (size_t)mid, &ww, &hh);
                                    if (ww <= selWrapW) { fit = mid; lo = mid + 1; }
                                    else                 { hi = mid - 1; }
                                }
                                int cut = fit;
                                if (selMode == UI_WRAP_WORD) {
                                    int spAfter = -1;
                                    for (int k = cut; k > 0; k--) {
                                        char ch = twid->text[segStart + k - 1];
                                        if (ch == ' ' || ch == '\t') { spAfter = k; break; }
                                    }
                                    if (spAfter > 0) cut = spAfter;
                                }
                                if (cut <= 0) cut = 1;
                                segEnd = segStart + cut;
                            }
                        }

                        if (twid->__linesLen >= twid->__linesCap) {
                            int newCap = twid->__linesCap * 2;
                            if (newCap < twid->__linesLen + 1) newCap = twid->__linesLen + 8;
                            twid->__lineTextures        = (SDL_Texture**)realloc(twid->__lineTextures,        newCap * sizeof(SDL_Texture*));
                            twid->__lineStarts          = (int*)         realloc(twid->__lineStarts,          newCap * sizeof(int));
                            twid->__lineLengths         = (int*)         realloc(twid->__lineLengths,         newCap * sizeof(int));
                            twid->__lineCharOffsets     = (int**)        realloc(twid->__lineCharOffsets,     newCap * sizeof(int*));
                            twid->__lineCharOffsetsLen  = (int*)         realloc(twid->__lineCharOffsetsLen,  newCap * sizeof(int));
                            twid->__lineIsSoft          = (int*)         realloc(twid->__lineIsSoft,          newCap * sizeof(int));
                            for (int z = twid->__linesCap; z < newCap; z++) {
                                twid->__lineTextures[z]       = NULL;
                                twid->__lineStarts[z]         = 0;
                                twid->__lineLengths[z]        = 0;
                                twid->__lineCharOffsets[z]    = NULL;
                                twid->__lineCharOffsetsLen[z] = 0;
                                twid->__lineIsSoft[z]         = 0;
                            }
                            twid->__linesCap = newCap;
                        }

                        const int li     = twid->__linesLen;
                        const int segLen = segEnd - segStart;
                        twid->__lineStarts[li]  = segStart;
                        twid->__lineLengths[li] = segLen;
                        twid->__lineIsSoft[li]  = firstInLine ? 0 : 1;

                        if (segLen > 0) {
                            SDL_Surface* surf = TTF_RenderText_Blended(
                                font, twid->text + segStart, (size_t)segLen, sc);
                            if (surf) {
                                twid->__lineTextures[li] = SDL_CreateTextureFromSurface(
                                    window->sdlRenderer, surf);
                                SDL_DestroySurface(surf);
                                if (twid->__lineTextures[li]) {
                                    SDL_SetTextureScaleMode(twid->__lineTextures[li], SDL_SCALEMODE_LINEAR);
                                }
                            }
                        } else {
                            twid->__lineTextures[li] = NULL;
                        }

                        const int need = segLen + 1;
                        twid->__lineCharOffsetsLen[li] = need;
                        twid->__lineCharOffsets[li] = (int*)calloc(need, sizeof(int));
                        if (twid->__lineCharOffsets[li]) {
                            twid->__lineCharOffsets[li][0] = 0;
                            for (int k = 1; k < need; k++) {
                                int w = 0, h = 0;
                                TTF_GetStringSize(font, twid->text + segStart, (size_t)k, &w, &h);
                                twid->__lineCharOffsets[li][k] = w;
                            }
                        }

                        twid->__linesLen++;
                        firstInLine = 0;
                        segStart = segEnd;
                    } while (segStart < lineEnd);

                    logicalStart = lineEnd + 1;
                }

                twid->__cachedTextLen  = twid->textLength;
                twid->__cachedWrapMode = (int)selMode;
                twid->__cachedWrapW    = selWrapW;
            }

            // Compute the bounding box of the laid-out lines.
            const float lineH = twid->fontSize * 1.25f;
            float maxLineW = 0.0f;
            for (int li = 0; li < twid->__linesLen; li++) {
                if (!twid->__lineTextures[li]) continue;
                float lw = 0, lh = 0;
                SDL_GetTextureSize(twid->__lineTextures[li], &lw, &lh);
                if (lw > maxLineW) maxLineW = lw;
            }
            const float blockW = (selWrapW > 0) ? (float)selWrapW : maxLineW;
            const float blockH = lineH * (twid->__linesLen > 0 ? twid->__linesLen : 1);

            const float pw = blockW + twid->paddingLeft + twid->paddingRight;
            const float ph = blockH + twid->paddingTop  + twid->paddingBottom;

            float alignOffsetX = 0.0f;
            if (el->width && *el->width > pw) {
                if (twid->hAlign == UI_TEXT_HALIGN_CENTER) alignOffsetX = (*el->width - pw) * 0.5f;
                else if (twid->hAlign == UI_TEXT_HALIGN_RIGHT) alignOffsetX = (*el->width - pw);
            }
            float alignOffsetY = 0.0f;
            if (el->height && *el->height > ph) {
                if (twid->vAlign == UI_TEXT_VALIGN_CENTER) alignOffsetY = (*el->height - ph) * 0.5f;
                else if (twid->vAlign == UI_TEXT_VALIGN_BOTTOM) alignOffsetY = (*el->height - ph);
            }

            g_tempRect1.x = el->x + alignOffsetX;
            g_tempRect1.y = el->y + alignOffsetY;
            g_tempRect1.w = pw;
            g_tempRect1.h = ph;

            if (twid->background && twid->background->hasShadow) {
                UIShadow sh = twid->background->shadow; sh.color.a *= op;
                DrawDropShadow(window->sdlRenderer, g_tempRect1,
                               twid->background->radius, sh);
            }
            if (twid->background) {
                UIColor bgC     = twid->background->color;       bgC.a *= op;
                UIColor borderC = twid->background->borderColor; borderC.a *= op;
                DrawRoundedRectWithBorder(window->sdlRenderer, g_tempRect1,
                                          bgC, twid->background->radius,
                                          (int)twid->background->borderWidth,
                                          borderC);
            }

            const float originX = g_tempRect1.x + twid->paddingLeft;
            const float originY = g_tempRect1.y + twid->paddingTop;

            // Selection highlight (one rect per affected line).
            if (twid->selAnchor >= 0 && twid->selAnchor != twid->selCaret &&
                twid->__linesLen > 0) {
                int s = twid->selAnchor, e = twid->selCaret;
                if (s > e) { int t = s; s = e; e = t; }
                if (s < 0) s = 0;
                if (e > twid->textLength) e = twid->textLength;

                int sLine = 0, eLine = 0;
                { int lo = 0, hi = twid->__linesLen - 1;
                  while (lo < hi) {
                    int mid = (lo + hi + 1) / 2;
                    if (twid->__lineStarts[mid] <= s) lo = mid; else hi = mid - 1;
                  } sLine = lo; }
                { int lo = 0, hi = twid->__linesLen - 1;
                  while (lo < hi) {
                    int mid = (lo + hi + 1) / 2;
                    if (twid->__lineStarts[mid] <= e) lo = mid; else hi = mid - 1;
                  } eLine = lo; }

                UIColor selC = twid->selectionColor; selC.a *= op;
                for (int li = sLine; li <= eLine; li++) {
                    if (!twid->__lineCharOffsets[li]) continue;
                    int colStart = (li == sLine) ? s - twid->__lineStarts[li] : 0;
                    int colEnd   = (li == eLine) ? e - twid->__lineStarts[li]
                                                 : twid->__lineLengths[li];
                    if (colStart < 0) colStart = 0;
                    if (colEnd > twid->__lineLengths[li]) colEnd = twid->__lineLengths[li];
                    const float x1 = (float)twid->__lineCharOffsets[li][colStart];
                    const float x2 = (float)twid->__lineCharOffsets[li][colEnd];
                    const int isHardBreak = (li < eLine) &&
                        (li + 1 >= twid->__linesLen || !twid->__lineIsSoft[li + 1]);
                    const float tail = isHardBreak ? twid->fontSize * 0.4f : 0.0f;
                    SDL_FRect r = {
                        originX + x1,
                        originY + li * lineH,
                        (x2 - x1) + tail,
                        lineH
                    };
                    DrawRoundedRectFill(window->sdlRenderer, r, selC, 2.0f);
                }
            }

            // Draw the line glyph textures.
            for (int li = 0; li < twid->__linesLen; li++) {
                if (!twid->__lineTextures[li]) continue;
                float tw, th;
                SDL_GetTextureSize(twid->__lineTextures[li], &tw, &th);
                SDL_FRect dst = {
                    originX,
                    originY + li * lineH,
                    tw, th
                };
                SDL_SetTextureAlphaMod(twid->__lineTextures[li], (Uint8)(op * 255.0f + 0.5f));
                SDL_RenderTexture(window->sdlRenderer, twid->__lineTextures[li], NULL, &dst);
                SDL_SetTextureAlphaMod(twid->__lineTextures[li], 255);
            }
            return;
        }

        // Rebuild the cached texture when the width that drives wrap
        // changes (e.g. parent resized and we're following bounds). FIT
        // keeps the natural-size texture - scaling happens at draw time.
        if (twid->__SDL_textTexture && effMode != UI_WRAP_NONE &&
            effMode != UI_WRAP_FIT && effWrapW > 0) {
            float curW = 0.0f, curH = 0.0f;
            SDL_GetTextureSize(twid->__SDL_textTexture, &curW, &curH);
            if ((int)curW > effWrapW + 1) {
                SDL_DestroyTexture(twid->__SDL_textTexture);
                twid->__SDL_textTexture = NULL;
            }
        }

        if (!twid->__SDL_textTexture) {
            TTF_Font *font = GetFont(twid->fontFamily, twid->fontSize);
            if (font) {
                // Hinting set once at font load (GetFont).
                TTF_SetFontStyle(font, (TTF_FontStyleFlags)twid->fontStyle);
                SDL_Color sc = {(Uint8)twid->color.r,
                                (Uint8)twid->color.g,
                                (Uint8)twid->color.b,
                                (Uint8)SDL_clamp((int)(twid->color.a * 255), 0, 255)};
                SDL_Surface *surf = NULL;
                if (effMode == UI_WRAP_WORD && effWrapW > 0) {
                    surf = TTF_RenderText_Blended_Wrapped(font, twid->text,
                                                          twid->textLength, sc, effWrapW);
                } else if (effMode == UI_WRAP_FIT) {
                    // FIT renders single-line at natural size; the draw
                    // step below shrinks the dest rect to fit effWrapW.
                    surf = TTF_RenderText_Blended(font, twid->text,
                                                  twid->textLength, sc);
                } else if (effMode == UI_WRAP_CHAR && effWrapW > 0) {
                    // SDL_ttf doesn't ship a char-wrap mode; pre-insert
                    // '\n' between characters that would overflow and let
                    // the wrapped renderer respect them.
                    char* prepared = (char*)malloc((size_t)twid->textLength * 2 + 1);
                    if (prepared) {
                        int outLen = 0, lineStart = 0;
                        for (int i = 0; i < twid->textLength; i++) {
                            char ch = twid->text[i];
                            if (ch == '\n') {
                                prepared[outLen++] = '\n';
                                lineStart = outLen;
                                continue;
                            }
                            prepared[outLen] = ch;
                            int ww = 0, hh = 0;
                            TTF_GetStringSize(font, prepared + lineStart,
                                              (size_t)(outLen - lineStart + 1), &ww, &hh);
                            if (ww > effWrapW && outLen > lineStart) {
                                prepared[outLen++] = '\n';
                                lineStart = outLen;
                                prepared[outLen] = ch;
                            }
                            outLen++;
                        }
                        prepared[outLen] = '\0';
                        surf = TTF_RenderText_Blended_Wrapped(font, prepared,
                                                              (size_t)outLen, sc, 0x7FFFFFFF);
                        free(prepared);
                    }
                } else {
                    surf = TTF_RenderText_Blended(font, twid->text,
                                                  twid->textLength, sc);
                }
                if (surf) {
                    twid->__SDL_textTexture = SDL_CreateTextureFromSurface(
                        window->sdlRenderer, surf);
                    SDL_DestroySurface(surf);
                }
            }
        }
        if (!twid->__SDL_textTexture) return;

        SDL_SetTextureScaleMode(twid->__SDL_textTexture, SDL_SCALEMODE_LINEAR);
        float txw, txh;
        SDL_GetTextureSize(twid->__SDL_textTexture, &txw, &txh);

        // FIT: shrink the glyph block uniformly so it fits the
        // available width on a single line. Texture is rendered at the
        // natural size and the destination rectangle does the scaling.
        if (effMode == UI_WRAP_FIT && effWrapW > 0 && txw > (float)effWrapW) {
            const float s = (float)effWrapW / txw;
            txw *= s;
            txh *= s;
        }

        float pw = txw + twid->paddingLeft + twid->paddingRight;
        float ph = txh + twid->paddingTop + twid->paddingBottom;

        // Honor horizontal/vertical alignment when the widget has an
        // explicit size larger than the natural glyph block.
        float alignOffsetX = 0.0f;
        if (el->width && *el->width > pw) {
            switch (twid->hAlign) {
                case UI_TEXT_HALIGN_CENTER:
                    alignOffsetX = (*el->width - pw) * 0.5f;
                    break;
                case UI_TEXT_HALIGN_RIGHT:
                    alignOffsetX = (*el->width - pw);
                    break;
                case UI_TEXT_HALIGN_LEFT:
                default:
                    alignOffsetX = 0.0f;
                    break;
            }
        }

        float alignOffsetY = 0.0f;
        if (el->height && *el->height > ph) {
            switch (twid->vAlign) {
                case UI_TEXT_VALIGN_CENTER:
                    alignOffsetY = (*el->height - ph) * 0.5f;
                    break;
                case UI_TEXT_VALIGN_BOTTOM:
                    alignOffsetY = (*el->height - ph);
                    break;
                case UI_TEXT_VALIGN_TOP:
                default:
                    alignOffsetY = 0.0f;
                    break;
            }
        }

        g_tempRect1.x = el->x + alignOffsetX;
        g_tempRect1.y = el->y + alignOffsetY;
        g_tempRect1.w = pw;
        g_tempRect1.h = ph;

        // Opacity applied to background fill + border + shadow.
        if (twid->background && twid->background->hasShadow) {
            UIShadow sh = twid->background->shadow; sh.color.a *= op;
            DrawDropShadow(window->sdlRenderer, g_tempRect1,
                           twid->background->radius, sh);
        }

        if (twid->background) {
            UIColor bgC     = twid->background->color;       bgC.a *= op;
            UIColor borderC = twid->background->borderColor; borderC.a *= op;
            if (fabsf(twid->background->radius - g_tempRect1.w/2) < 0.5f &&
                fabsf(g_tempRect1.w - g_tempRect1.h) < 0.5f) {
                float centerX = g_tempRect1.x + g_tempRect1.w/2;
                float centerY = g_tempRect1.y + g_tempRect1.h/2;
                DrawCircle(window->sdlRenderer, centerX, centerY,
                           g_tempRect1.w/2, bgC);
            } else {
                DrawRoundedRectWithBorder(window->sdlRenderer, g_tempRect1,
                                          bgC, twid->background->radius,
                                          (int)twid->background->borderWidth,
                                          borderC);
            }
        }

        // Apply opacity to the glyph texture via alpha mod.
        SDL_SetTextureAlphaMod(twid->__SDL_textTexture, (Uint8)(op * 255.0f + 0.5f));

        g_tempRect2.x = g_tempRect1.x + twid->paddingLeft;
        g_tempRect2.y = g_tempRect1.y + twid->paddingTop;
        g_tempRect2.w = txw;
        g_tempRect2.h = txh;

        if (el->rotation != 0.0f) {
            SDL_FPoint pivot = { g_tempRect2.w * 0.5f, g_tempRect2.h * 0.5f };
            SDL_RenderTextureRotated(window->sdlRenderer, twid->__SDL_textTexture,
                                     NULL, &g_tempRect2,
                                     (double)el->rotation, &pivot, SDL_FLIP_NONE);
        } else {
            SDL_RenderTexture(window->sdlRenderer, twid->__SDL_textTexture,
                              NULL, &g_tempRect2);
        }
        // Reset alpha mod so subsequent users of the same texture aren't
        // affected by our opacity.
        SDL_SetTextureAlphaMod(twid->__SDL_textTexture, 255);
        return;
    }

    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_BUTTON)) {
        UIButton* btn = (UIButton*)base;
        if (!el->width || !el->height) return;

        g_tempRect1.x = el->x;
        g_tempRect1.y = el->y;
        g_tempRect1.w = *el->width;
        g_tempRect1.h = *el->height;
        ApplyMargins(&g_tempRect1, btn->marginLeft, btn->marginTop,
                                   btn->marginRight, btn->marginBottom);
        if (g_tempRect1.w <= 0 || g_tempRect1.h <= 0) return;

        const UIButtonStyle* style = &btn->styles[btn->state];
        // Apply widget opacity to fill / border / shadow / label.
        UIColor bgC     = style->background; bgC.a *= op;
        UIColor bdrC    = style->border;     bdrC.a *= op;
        UIColor textC   = style->text;       textC.a *= op;

        if (btn->background->hasShadow) {
            UIShadow sh = btn->background->shadow; sh.color.a *= op;
            DrawDropShadow(window->sdlRenderer, g_tempRect1,
                           btn->background->radius, sh);
        }
        DrawRoundedRectWithBorder(window->sdlRenderer, g_tempRect1,
                                  bgC, btn->background->radius,
                                  (int)btn->background->borderWidth, bdrC);

        if (btn->label && btn->label->text && *btn->label->text &&
            btn->label->fontFamily && btn->label->fontSize > 0.0f) {
            if (!btn->label->__SDL_textTexture) {
                TTF_Font* font = GetFont(btn->label->fontFamily, btn->label->fontSize);
                if (font) {
                    // Hinting set once at font load (GetFont).
                    TTF_SetFontStyle(font, (TTF_FontStyleFlags)btn->label->fontStyle);
                    const SDL_Color white = {255, 255, 255, 255};
                    SDL_Surface* surf = TTF_RenderText_Blended(
                        font, btn->label->text, btn->label->textLength, white);
                    if (surf) {
                        btn->label->__SDL_textTexture =
                            SDL_CreateTextureFromSurface(window->sdlRenderer, surf);
                        SDL_DestroySurface(surf);
                    }
                }
            }
            if (btn->label->__SDL_textTexture) {
                SDL_SetTextureScaleMode(btn->label->__SDL_textTexture, SDL_SCALEMODE_LINEAR);
                SDL_SetTextureColorMod(btn->label->__SDL_textTexture,
                                       (Uint8)textC.r,
                                       (Uint8)textC.g,
                                       (Uint8)textC.b);
                SDL_SetTextureAlphaMod(btn->label->__SDL_textTexture,
                                       (Uint8)SDL_clamp((int)(textC.a * 255.0f), 0, 255));
                float txw = 0.0f, txh = 0.0f;
                SDL_GetTextureSize(btn->label->__SDL_textTexture, &txw, &txh);
                const float pressOffset = (btn->state == UI_BUTTON_STATE_PRESSED) ? 1.0f : 0.0f;

                // Optical centring: SDL_ttf surfaces are ascent + descent
                // tall regardless of which glyphs are in them, so a
                // string like "Browse..." (no descenders) leaves an
                // empty band at the bottom of the bbox. Geometric centre
                // of the bbox therefore sits below the visual centre of
                // the ink — the text reads as drifting low. We pull it
                // back up by |descent|/2 to align the optical centre
                // with the button's vertical middle.
                float opticalShift = 0.0f;
                {
                    TTF_Font* metricFont = GetFont(btn->label->fontFamily,
                                                   btn->label->fontSize);
                    if (metricFont) {
                        int descent = TTF_GetFontDescent(metricFont); // negative
                        if (descent < 0) opticalShift = (float)(-descent) * 0.5f;
                    }
                }

                // Snap to the integer pixel grid. Centring an odd-width
                // glyph block in an even-width button lands the text on
                // a half-pixel, and LINEAR sampling then softens every
                // glyph. floor(... + 0.5) rounds to nearest int so the
                // blit is pixel-perfect regardless of widths.
                g_tempRect2.x = floorf(g_tempRect1.x + (g_tempRect1.w - txw) * 0.5f + 0.5f);
                g_tempRect2.y = floorf(g_tempRect1.y + (g_tempRect1.h - txh) * 0.5f
                                       - opticalShift + pressOffset + 0.5f);
                g_tempRect2.w = txw;
                g_tempRect2.h = txh;
                if (el->rotation != 0.0f) {
                    SDL_FPoint pivot = { txw * 0.5f, txh * 0.5f };
                    SDL_RenderTextureRotated(window->sdlRenderer, btn->label->__SDL_textTexture,
                                             NULL, &g_tempRect2,
                                             (double)el->rotation, &pivot, SDL_FLIP_NONE);
                } else {
                    SDL_RenderTexture(window->sdlRenderer, btn->label->__SDL_textTexture,
                                      NULL, &g_tempRect2);
                }
            }
        }
        return;
    }

    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_IMAGE)) {
        UIImage *img = (UIImage*)base;
        if (!img->source || !el->width || !el->height) return;
        if (!img->__SDL_texture && img->loadState != IMAGE_LOAD_FAILURE) {
            img->__SDL_texture = UIAsset_LoadTexture(window->sdlRenderer, img->source);
            if (img->__SDL_texture) {
                img->loadState = IMAGE_LOAD_SUCCESS;
                SDL_SetTextureScaleMode(img->__SDL_texture, SDL_SCALEMODE_LINEAR);
            } else {
                img->loadState = IMAGE_LOAD_FAILURE;
            }
        }
        if (!img->__SDL_texture) return;

        SDL_FRect bounds = { el->x, el->y, *el->width, *el->height };
        ApplyMargins(&bounds, img->marginLeft, img->marginTop,
                              img->marginRight, img->marginBottom);
        if (bounds.w <= 0 || bounds.h <= 0) return;

        // Tint colour combined with widget opacity. When the tint alpha
        // is 0 we treat it as "no tint" but still apply opacity.
        if (img->tintColor.a > 0.0f) {
            SDL_SetTextureColorMod(img->__SDL_texture,
                                   (Uint8)img->tintColor.r,
                                   (Uint8)img->tintColor.g,
                                   (Uint8)img->tintColor.b);
            const float a = img->tintColor.a * op;
            SDL_SetTextureAlphaMod(img->__SDL_texture,
                                   (Uint8)SDL_clamp((int)(a * 255.0f), 0, 255));
        } else {
            SDL_SetTextureColorMod(img->__SDL_texture, 255, 255, 255);
            SDL_SetTextureAlphaMod(img->__SDL_texture,
                                   (Uint8)SDL_clamp((int)(op * 255.0f), 0, 255));
        }

        float texW = 0.0f, texH = 0.0f;
        SDL_GetTextureSize(img->__SDL_texture, &texW, &texH);
        if (texW <= 0 || texH <= 0) return;

        SDL_FRect dst = bounds;
        int useClipForOverflow = 0;
        switch (img->fillMode) {
            case FILL_STRETCH:
            case FILL_SCALE:    dst = bounds; break;
            case FILL_NONE:
                dst.x = bounds.x; dst.y = bounds.y; dst.w = texW; dst.h = texH;
                useClipForOverflow = 1; break;
            case FILL_CENTER:
                dst.w = texW; dst.h = texH;
                dst.x = bounds.x + (bounds.w - texW) * 0.5f;
                dst.y = bounds.y + (bounds.h - texH) * 0.5f;
                useClipForOverflow = 1; break;
            case FILL_FIT: {
                const float s = SDL_min(bounds.w / texW, bounds.h / texH);
                dst.w = texW * s; dst.h = texH * s;
                dst.x = bounds.x + (bounds.w - dst.w) * 0.5f;
                dst.y = bounds.y + (bounds.h - dst.h) * 0.5f;
                break;
            }
            case FILL_FIT_WIDTH: {
                const float s = bounds.w / texW;
                dst.w = bounds.w; dst.h = texH * s;
                dst.x = bounds.x;
                dst.y = bounds.y + (bounds.h - dst.h) * 0.5f;
                break;
            }
            case FILL_FIT_HEIGHT: {
                const float s = bounds.h / texH;
                dst.w = texW * s; dst.h = bounds.h;
                dst.x = bounds.x + (bounds.w - dst.w) * 0.5f;
                dst.y = bounds.y;
                break;
            }
            case FILL_COVER: {
                const float s = SDL_max(bounds.w / texW, bounds.h / texH);
                dst.w = texW * s; dst.h = texH * s;
                dst.x = bounds.x + (bounds.w - dst.w) * 0.5f;
                dst.y = bounds.y + (bounds.h - dst.h) * 0.5f;
                useClipForOverflow = 1; break;
            }
            case FILL_TILE: {
                SDL_Rect clip2 = { (int)bounds.x, (int)bounds.y,
                                   (int)ceilf(bounds.w), (int)ceilf(bounds.h) };
                SDL_SetRenderClipRect(window->sdlRenderer, &clip2);
                for (float y = bounds.y; y < bounds.y + bounds.h; y += texH) {
                    for (float x = bounds.x; x < bounds.x + bounds.w; x += texW) {
                        SDL_FRect tile = { x, y, texW, texH };
                        SDL_RenderTexture(window->sdlRenderer,
                                          img->__SDL_texture, NULL, &tile);
                    }
                }
                SDL_SetRenderClipRect(window->sdlRenderer, NULL);
                return;
            }
            default: dst = bounds; break;
        }
        const int wantRound = (img->radius > 0.0f);
        const int wantBorder = (img->borderWidth > 0.0f);

        // Porter-Duff DST_IN: dst' = dst * src_alpha (both color + alpha).
        // Used as an alpha-mask blend on the rounded-shape texture so
        // pixels outside the rounded shape get zeroed out.
        const SDL_BlendMode kDstIn = SDL_ComposeCustomBlendMode(
            SDL_BLENDFACTOR_ZERO,            SDL_BLENDFACTOR_SRC_ALPHA,
            SDL_BLENDOPERATION_ADD,
            SDL_BLENDFACTOR_ZERO,            SDL_BLENDFACTOR_SRC_ALPHA,
            SDL_BLENDOPERATION_ADD);

        // DST_OUT: dst' = dst * (1 - src_alpha). Punches a hole in the
        // outer ring so the border becomes hollow.
        const SDL_BlendMode kDstOut = SDL_ComposeCustomBlendMode(
            SDL_BLENDFACTOR_ZERO,            SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            SDL_BLENDOPERATION_ADD,
            SDL_BLENDFACTOR_ZERO,            SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            SDL_BLENDOPERATION_ADD);

        const int tw = (int)ceilf(bounds.w);
        const int th = (int)ceilf(bounds.h);

        // Helper closure unavailable in C; we just keep two scratch
        // textures alive for the duration of this widget. Both are
        // freed before we return.
        SDL_Texture* scratch = (wantRound && tw > 0 && th > 0)
            ? SDL_CreateTexture(window->sdlRenderer,
                SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, tw, th)
            : NULL;

        if (scratch) {
            SDL_SetTextureBlendMode(scratch, SDL_BLENDMODE_BLEND);
            SDL_Texture* prevTarget = SDL_GetRenderTarget(window->sdlRenderer);

            // (1) Image -> scratch.
            SDL_SetRenderTarget(window->sdlRenderer, scratch);
            SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(window->sdlRenderer, 0, 0, 0, 0);
            SDL_RenderClear(window->sdlRenderer);
            SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_BLEND);

            SDL_FRect localDst = { dst.x - bounds.x, dst.y - bounds.y, dst.w, dst.h };
            if (useClipForOverflow) {
                SDL_Rect clipLocal = { 0, 0, tw, th };
                SDL_SetRenderClipRect(window->sdlRenderer, &clipLocal);
            }
            if (el->rotation != 0.0f) {
                SDL_FPoint pivot = { localDst.w * 0.5f, localDst.h * 0.5f };
                SDL_RenderTextureRotated(window->sdlRenderer, img->__SDL_texture,
                                         NULL, &localDst,
                                         (double)el->rotation, &pivot, SDL_FLIP_NONE);
            } else {
                SDL_RenderTexture(window->sdlRenderer, img->__SDL_texture, NULL, &localDst);
            }
            if (useClipForOverflow) {
                SDL_SetRenderClipRect(window->sdlRenderer, NULL);
            }

            // (2) Rounded mask -> a separate texture, drawn with normal
            // blending so DrawRoundedRectFill's internal AA corner
            // textures composite correctly.
            SDL_Texture* mask = SDL_CreateTexture(window->sdlRenderer,
                SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, tw, th);
            if (mask) {
                SDL_SetTextureBlendMode(mask, SDL_BLENDMODE_BLEND);
                SDL_SetRenderTarget(window->sdlRenderer, mask);
                SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(window->sdlRenderer, 0, 0, 0, 0);
                SDL_RenderClear(window->sdlRenderer);
                SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_BLEND);

                SDL_FRect maskRect = { 0, 0, bounds.w, bounds.h };
                const UIColor white = { 255, 255, 255, 1.0f };
                DrawRoundedRectFill(window->sdlRenderer, maskRect, white, img->radius);

                // (3) Apply mask to scratch via DST_IN. SetTextureBlend
                // controls how the *texture* blends onto the render
                // target, so SDL's batched texture path honours it.
                SDL_SetRenderTarget(window->sdlRenderer, scratch);
                SDL_SetTextureBlendMode(mask, kDstIn);
                SDL_FRect full = { 0, 0, bounds.w, bounds.h };
                SDL_RenderTexture(window->sdlRenderer, mask, NULL, &full);

                SDL_DestroyTexture(mask);
            }

            // (4) Border ring on top of the masked image (still in scratch).
            if (wantBorder) {
                const float bw = img->borderWidth;
                SDL_Texture* ring = SDL_CreateTexture(window->sdlRenderer,
                    SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, tw, th);
                if (ring) {
                    SDL_SetTextureBlendMode(ring, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderTarget(window->sdlRenderer, ring);
                    SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_NONE);
                    SDL_SetRenderDrawColor(window->sdlRenderer, 0, 0, 0, 0);
                    SDL_RenderClear(window->sdlRenderer);
                    SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_BLEND);

                    // Outer rounded shape filled with border colour.
                    UIColor bc = { 0, 0, 0, op };
                    SDL_FRect outer = { 0, 0, bounds.w, bounds.h };
                    DrawRoundedRectFill(window->sdlRenderer, outer, bc, img->radius);

                    // Inner hole.
                    if (outer.w - 2 * bw > 0 && outer.h - 2 * bw > 0) {
                        SDL_Texture* innerMask = SDL_CreateTexture(window->sdlRenderer,
                            SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, tw, th);
                        if (innerMask) {
                            SDL_SetTextureBlendMode(innerMask, SDL_BLENDMODE_BLEND);
                            SDL_SetRenderTarget(window->sdlRenderer, innerMask);
                            SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_NONE);
                            SDL_SetRenderDrawColor(window->sdlRenderer, 0, 0, 0, 0);
                            SDL_RenderClear(window->sdlRenderer);
                            SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_BLEND);

                            SDL_FRect inner = {
                                bw, bw, outer.w - 2 * bw, outer.h - 2 * bw
                            };
                            const float innerR = SDL_max(0.0f, img->radius - bw);
                            const UIColor whiteOpaque = { 255, 255, 255, 1.0f };
                            DrawRoundedRectFill(window->sdlRenderer, inner, whiteOpaque, innerR);

                            SDL_SetRenderTarget(window->sdlRenderer, ring);
                            SDL_SetTextureBlendMode(innerMask, kDstOut);
                            SDL_FRect rectFull = { 0, 0, bounds.w, bounds.h };
                            SDL_RenderTexture(window->sdlRenderer, innerMask, NULL, &rectFull);
                            SDL_DestroyTexture(innerMask);
                        }
                    }

                    // Composite the ring on top of scratch.
                    SDL_SetRenderTarget(window->sdlRenderer, scratch);
                    SDL_SetTextureBlendMode(ring, SDL_BLENDMODE_BLEND);
                    SDL_FRect ringRect = { 0, 0, bounds.w, bounds.h };
                    SDL_RenderTexture(window->sdlRenderer, ring, NULL, &ringRect);
                    SDL_DestroyTexture(ring);
                }
            }

            // (5) Blit final scratch to the screen.
            SDL_SetRenderTarget(window->sdlRenderer, prevTarget);
            SDL_RenderTexture(window->sdlRenderer, scratch, NULL, &bounds);
            SDL_DestroyTexture(scratch);
        } else {
            // No rounded clip needed. Render image directly.
            if (useClipForOverflow) {
                SDL_Rect clip2 = { (int)bounds.x, (int)bounds.y,
                                   (int)ceilf(bounds.w), (int)ceilf(bounds.h) };
                SDL_SetRenderClipRect(window->sdlRenderer, &clip2);
            }
            if (el->rotation != 0.0f) {
                SDL_FPoint pivot = { dst.w * 0.5f, dst.h * 0.5f };
                SDL_RenderTextureRotated(window->sdlRenderer, img->__SDL_texture,
                                         NULL, &dst,
                                         (double)el->rotation, &pivot, SDL_FLIP_NONE);
            } else {
                SDL_RenderTexture(window->sdlRenderer, img->__SDL_texture, NULL, &dst);
            }
            if (useClipForOverflow) {
                SDL_SetRenderClipRect(window->sdlRenderer, NULL);
            }

            // Square (non-rounded) border drawn as four edge strips.
            if (wantBorder) {
                const float bw = img->borderWidth;
                const SDL_Color bc = {
                    0, 0, 0, (Uint8)SDL_clamp((int)(op * 255.0f), 0, 255)
                };
                SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(window->sdlRenderer,
                                       bc.r, bc.g, bc.b, bc.a);
                SDL_FRect edges[4] = {
                    { bounds.x, bounds.y, bounds.w, bw },
                    { bounds.x, bounds.y + bounds.h - bw, bounds.w, bw },
                    { bounds.x, bounds.y + bw, bw, bounds.h - 2 * bw },
                    { bounds.x + bounds.w - bw, bounds.y + bw, bw, bounds.h - 2 * bw }
                };
                for (int i = 0; i < 4; i++) {
                    if (edges[i].w > 0 && edges[i].h > 0) {
                        SDL_RenderFillRect(window->sdlRenderer, &edges[i]);
                    }
                }
            }
        }
        return;
    }

    // ----- UIVideo -------------------------------------------------
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_VIDEO)) {
        UIVideo* v = (UIVideo*)base;
        if (!el->width || !el->height) return;

        // Decode any frames that are due for the current clock + push
        // queued audio.
        UIVideo_Tick(v, window->sdlRenderer);
        SDL_Texture* tex = UIVideo_GetTexture(v);
        if (!tex) return;

        const SDL_FRect bounds = { el->x, el->y, *el->width, *el->height };
        if (bounds.w <= 0 || bounds.h <= 0) return;

        const int videoW = UIVideo_GetWidth(v);
        const int videoH = UIVideo_GetHeight(v);
        if (videoW <= 0 || videoH <= 0) return;

        // Compute destination rect from fillMode (mirror of UIImage).
        SDL_FRect dst = bounds;
        int needClip = 0;
        UIFillMode fm = FILL_FIT;
        // Inspect the widget's internal fillMode via UIVideo getter would
        // be cleaner; for now we always letterbox (FILL_FIT) so the
        // aspect ratio is preserved. UIVideo_SetFillMode is a no-op for
        // render until we expose a getter.
        (void)fm;
        const float texW = (float)videoW;
        const float texH = (float)videoH;
        const float s = SDL_min(bounds.w / texW, bounds.h / texH);
        dst.w = texW * s;
        dst.h = texH * s;
        dst.x = bounds.x + (bounds.w - dst.w) * 0.5f;
        dst.y = bounds.y + (bounds.h - dst.h) * 0.5f;
        needClip = (dst.x < bounds.x || dst.y < bounds.y ||
                    dst.x + dst.w > bounds.x + bounds.w ||
                    dst.y + dst.h > bounds.y + bounds.h);

        if (needClip) {
            SDL_Rect clip = {
                (int)bounds.x, (int)bounds.y,
                (int)ceilf(bounds.w), (int)ceilf(bounds.h)
            };
            SDL_SetRenderClipRect(window->sdlRenderer, &clip);
        }
        SDL_SetTextureAlphaMod(tex, (Uint8)(op * 255.0f + 0.5f));
        SDL_RenderTexture(window->sdlRenderer, tex, NULL, &dst);
        SDL_SetTextureAlphaMod(tex, 255);
        if (needClip) {
            SDL_SetRenderClipRect(window->sdlRenderer, NULL);
        }
        return;
    }

    // ----- UIWebView -----------------------------------------------
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_WEBVIEW)) {
#ifdef _WIN32
        UIWebView* wv = (UIWebView*)base;
        if (!el->width || !el->height) return;
        if (!el->visible) return;

        // Pull the SDL window's HWND for parenting.
        HWND hwnd = NULL;
        if (window->sdlWindow) {
            SDL_PropertiesID props = SDL_GetWindowProperties(window->sdlWindow);
            hwnd = (HWND)SDL_GetPointerProperty(
                props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
        }

        // Walk siblings: any visible widget with a higher z that
        // overlaps the webview's bounds should be punched out of its
        // host HWND so it stays visible (the WebView2 child paints on
        // top of the SDL surface otherwise).
        //
        // We also pull the widget's corner radius (UIRectangle / UIButton)
        // so the punch matches the rendered shape - a rectangular punch
        // around a rounded card would leave the 4 corner triangles of
        // the bounding box showing the SDL background, visible as a
        // faint frame around the card.
        RECT clipBuf[32];
        int  clipRadii[32];
        int  clipCount = 0;
        const float wx = el->x, wy = el->y;
        const float ww = *el->width, wh = *el->height;
        if (window->children) {
            for (int i = 0; i < window->children->count; i++) {
                UIWidget* sib = window->children->children[i];
                if (!sib || sib == el || !sib->visible) continue;
                if (sib->z <= el->z) continue;
                if (!sib->width || !sib->height) continue;
                const float sx = sib->x;
                const float sy = sib->y;
                const float sw = *sib->width;
                const float sh = *sib->height;
                // Rect intersection test.
                if (sx + sw <= wx || sy + sh <= wy ||
                    sx >= wx + ww || sy >= wy + wh) continue;
                if (clipCount < (int)(sizeof(clipBuf)/sizeof(clipBuf[0]))) {
                    clipBuf[clipCount].left   = (LONG)sx;
                    clipBuf[clipCount].top    = (LONG)sy;
                    clipBuf[clipCount].right  = (LONG)(sx + sw);
                    clipBuf[clipCount].bottom = (LONG)(sy + sh);

                    // Best-effort radius probe by widget type. Text /
                    // mouse-areas / file-drops stay rectangular (0).
                    int radius = 0;
                    UIWidgetBase* sb = (UIWidgetBase*)sib->data;
                    if (sb && sb->__widget_type) {
                        if (!strcmp(sb->__widget_type, UI_WIDGET_RECTANGLE)) {
                            radius = (int)((UIRectangle*)sb)->radius;
                        } else if (!strcmp(sb->__widget_type, UI_WIDGET_BUTTON)) {
                            UIButton* btn = (UIButton*)sb;
                            if (btn->background) radius = (int)btn->background->radius;
                        }
                    }
                    clipRadii[clipCount] = radius;
                    clipCount++;
                }
            }
        }

        // Paint the configured border on the SDL surface BEFORE the
        // host HWND eats the area. UIWebView_RendererTick insets the
        // host by borderWidth, leaving the border ring visible.
        extern void UIWebView_GetVisuals(const UIWebView*, float*, float*,
                                         UIColor*, int*);
        float wvRadius = 0.0f, wvBorderW = 0.0f;
        UIColor wvBorderColor = { 0, 0, 0, 0.0f };
        int wvHasBorder = 0;
        UIWebView_GetVisuals(wv, &wvRadius, &wvBorderW, &wvBorderColor, &wvHasBorder);
        if (wvHasBorder && wvBorderW > 0.0f && window->sdlRenderer) {
            // Paint a hollow ring: outer rounded rect in border colour
            // followed by inner rounded rect in the window's clear
            // colour. We can't fill the whole bounding box with border
            // colour because the host HWND will be inset to expose the
            // ring - but higher-z widgets sitting on top of the
            // webview (overlays with their own rounded shape) punch
            // ROUNDED holes in the host HWND, and inside those holes
            // the SDL surface shows through. A solid border-colour
            // fill would leak through the overlay's bounding-box
            // corners (the triangles outside the rounded card body).
            SDL_FRect outer = { wx, wy, ww, wh };
            DrawRoundedRectFill(window->sdlRenderer, outer, wvBorderColor, wvRadius);
            SDL_FRect inner = {
                wx + wvBorderW, wy + wvBorderW,
                ww - 2.0f * wvBorderW, wh - 2.0f * wvBorderW
            };
            if (inner.w > 0.0f && inner.h > 0.0f) {
                float innerR = wvRadius - wvBorderW;
                if (innerR < 0.0f) innerR = 0.0f;
                DrawRoundedRectFill(window->sdlRenderer, inner,
                                    window->backgroundColor, innerR);
            }
            FlushRenderBatch(window->sdlRenderer);
        }

        extern void UIWebView_RendererTick(UIWebView*, HWND, int, int, int, int,
                                           const RECT*, const int*, int);
        UIWebView_RendererTick(wv, hwnd,
                               (int)wx, (int)wy, (int)ww, (int)wh,
                               clipBuf, clipRadii, clipCount);
#elif defined(__linux__) && defined(MOCIDA_HAS_WEBKITGTK)
        // Linux WebKitGTK path: paint optional border on the SDL surface
        // (same shape the Windows path uses), then ask the webview for
        // its latest snapshot texture and composite it inside the inset
        // area. The MVP doesn't yet round the corners — the snapshot is
        // drawn as a straight rectangle inside the border.
        UIWebView* wv = (UIWebView*)base;
        if (!el->width || !el->height || !el->visible) return;
        const float wx = el->x, wy = el->y;
        const float ww = *el->width, wh = *el->height;

        extern void UIWebView_GetVisuals(const UIWebView*, float*, float*,
                                         UIColor*, int*);
        extern SDL_Texture* UIWebView_GetSnapshotTexture_Linux(UIWebView*, SDL_Renderer*, int, int);

        float wvRadius = 0.0f, wvBorderW = 0.0f;
        UIColor wvBorderColor = { 0, 0, 0, 0.0f };
        int wvHasBorder = 0;
        UIWebView_GetVisuals(wv, &wvRadius, &wvBorderW, &wvBorderColor, &wvHasBorder);

        SDL_FRect dst = { wx, wy, ww, wh };
        if (wvHasBorder && wvBorderW > 0.0f) {
            UIColor bc = wvBorderColor; bc.a *= op;
            SDL_FRect outer = { wx, wy, ww, wh };
            DrawRoundedRectFill(window->sdlRenderer, outer, bc, wvRadius);
            FlushRenderBatch(window->sdlRenderer);
            dst.x += wvBorderW;
            dst.y += wvBorderW;
            dst.w -= 2.0f * wvBorderW;
            dst.h -= 2.0f * wvBorderW;
        }
        if (dst.w > 0.0f && dst.h > 0.0f) {
            SDL_Texture* wvTex = UIWebView_GetSnapshotTexture_Linux(
                wv, window->sdlRenderer, (int)dst.w, (int)dst.h);
            if (wvTex) {
                SDL_SetTextureBlendMode(wvTex, SDL_BLENDMODE_BLEND);

                // Inner corner radius: when bordered, nest inside the ring
                // exactly like the border path's inner rect; otherwise use
                // the full radius.
                float innerR = wvRadius;
                if (wvHasBorder && wvBorderW > 0.0f) {
                    innerR = wvRadius - wvBorderW;
                    if (innerR < 0.0f) innerR = 0.0f;
                }

                const int rtw = (int)ceilf(dst.w);
                const int rth = (int)ceilf(dst.h);

                // Round only when there is a real radius; otherwise keep the
                // straight, allocation-free fast path.
                SDL_Texture* scratch = (innerR > 0.5f && rtw > 0 && rth > 0)
                    ? SDL_CreateTexture(window->sdlRenderer,
                        SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, rtw, rth)
                    : NULL;

                if (scratch) {
                    // Porter-Duff DST_IN: dst' = dst * src_alpha. Same blend
                    // the UIImage rounded path uses.
                    const SDL_BlendMode kDstInWv = SDL_ComposeCustomBlendMode(
                        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA,
                        SDL_BLENDOPERATION_ADD,
                        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA,
                        SDL_BLENDOPERATION_ADD);

                    SDL_SetTextureBlendMode(scratch, SDL_BLENDMODE_BLEND);
                    SDL_Texture* prevTarget = SDL_GetRenderTarget(window->sdlRenderer);

                    // (1) Snapshot -> scratch at local origin.
                    SDL_SetRenderTarget(window->sdlRenderer, scratch);
                    SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_NONE);
                    SDL_SetRenderDrawColor(window->sdlRenderer, 0, 0, 0, 0);
                    SDL_RenderClear(window->sdlRenderer);
                    SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_BLEND);
                    SDL_FRect localDst = { 0.0f, 0.0f, dst.w, dst.h };
                    SDL_RenderTexture(window->sdlRenderer, wvTex, NULL, &localDst);

                    // (2) White rounded mask -> its own target.
                    SDL_Texture* mask = SDL_CreateTexture(window->sdlRenderer,
                        SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, rtw, rth);
                    if (mask) {
                        SDL_SetTextureBlendMode(mask, SDL_BLENDMODE_BLEND);
                        SDL_SetRenderTarget(window->sdlRenderer, mask);
                        SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_NONE);
                        SDL_SetRenderDrawColor(window->sdlRenderer, 0, 0, 0, 0);
                        SDL_RenderClear(window->sdlRenderer);
                        SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_BLEND);

                        SDL_FRect maskRect = { 0.0f, 0.0f, dst.w, dst.h };
                        const UIColor white = { 255, 255, 255, 1.0f };
                        DrawRoundedRectFill(window->sdlRenderer, maskRect, white, innerR);

                        // (3) DST_IN: clip scratch to the rounded shape.
                        SDL_SetRenderTarget(window->sdlRenderer, scratch);
                        SDL_SetTextureBlendMode(mask, kDstInWv);
                        SDL_RenderTexture(window->sdlRenderer, mask, NULL, &maskRect);
                        SDL_DestroyTexture(mask);
                    }

                    // (4) Blit rounded scratch to screen with opacity.
                    SDL_SetRenderTarget(window->sdlRenderer, prevTarget);
                    SDL_SetTextureBlendMode(scratch, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureAlphaMod(scratch, (Uint8)(op * 255.0f + 0.5f));
                    SDL_RenderTexture(window->sdlRenderer, scratch, NULL, &dst);
                    SDL_SetTextureAlphaMod(scratch, 255);
                    SDL_DestroyTexture(scratch);
                } else {
                    // No radius (or alloc failed): straight blit, unchanged.
                    SDL_SetTextureAlphaMod(wvTex, (Uint8)(op * 255.0f + 0.5f));
                    SDL_RenderTexture(window->sdlRenderer, wvTex, NULL, &dst);
                    SDL_SetTextureAlphaMod(wvTex, 255);
                }
            }
        }
#elif defined(__APPLE__) && defined(MOCIDA_HAS_WKWEBVIEW)
        // macOS WKWebView path: the webview is a native NSView subview of
        // the SDL window's NSWindow.contentView. It paints itself (and
        // handles its own rounded corners / border via its CALayer), so
        // there is no SDL-side compositing here — we only forward the
        // current bounds + visibility to the Cocoa backend, which
        // positions the subview (flipping Y into Cocoa's bottom-left
        // space). Unlike the early-return paths above we ALSO tick when
        // hidden so the backend can hide the subview.
        UIWebView* wv = (UIWebView*)base;
        if (!el->width || !el->height) return;

        void* nsWindow = NULL;
        if (window->sdlWindow) {
            SDL_PropertiesID props = SDL_GetWindowProperties(window->sdlWindow);
            nsWindow = SDL_GetPointerProperty(
                props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
        }

        extern void UIWebView_RendererTick_Mac(UIWebView*, void*,
                                               int, int, int, int, int);
        UIWebView_RendererTick_Mac(wv, nsWindow,
                                   (int)el->x, (int)el->y,
                                   (int)*el->width, (int)*el->height,
                                   el->visible ? 1 : 0);
#else
        // ---- No webview backend compiled in ----------------------------
        // Reached when the build has no UIWebView implementation for this
        // platform — most commonly Linux without WebKitGTK (the
        // libwebkit2gtk-4.x dev package was absent at configure time, so
        // MOCIDA_HAS_WEBKITGTK is undefined and UIWebView_Create returned a
        // no-op stub). Rather than leave a silently-blank rectangle that
        // looks like a broken page, paint a clear error placeholder so the
        // missing dependency is obvious on screen.
        if (!el->width || !el->height || !el->visible) return;
        {
            const float wx = el->x, wy = el->y;
            const float ww = *el->width, wh = *el->height;
            SDL_FRect box = { wx, wy, ww, wh };

            // Muted slate card so the light message text reads against it.
            UIColor bg = { 30, 41, 59, 1.0f }; bg.a *= op;   // slate-800
            DrawRoundedRectFill(window->sdlRenderer, box, bg, 8.0f);

            const char* msg =
#if defined(__linux__)
                "WebView unavailable\n\n"
                "WebKitGTK was not found at build time.\n"
                "Install the library and rebuild:\n"
                "sudo apt install libwebkit2gtk-4.1-dev";
#else
                "WebView unavailable: no backend for this platform.";
#endif

            // Resolve a usable font path directly so the placeholder works
            // even if UISearchFonts() was never called by the host app.
            const char* fpath = NULL;
#if defined(__APPLE__)
            // /Library/Fonts/Arial.ttf does not exist on modern macOS; Arial
            // ships in /System/Library/Fonts/Supplemental, Helvetica.ttc is
            // always present. Probe candidates so the placeholder still draws.
            static const char* kFontCands[] = {
                "/System/Library/Fonts/Supplemental/Arial.ttf",
                "/Library/Fonts/Arial Unicode.ttf",
                "/System/Library/Fonts/Helvetica.ttc",
                "/System/Library/Fonts/SFNS.ttf",
                NULL
            };
            for (int i = 0; kFontCands[i]; i++) {
                FILE* tf = fopen(kFontCands[i], "rb");
                if (tf) { fclose(tf); fpath = kFontCands[i]; break; }
            }
#else
            static const char* kFontCands[] = {
                "/usr/share/fonts/dejavu/DejaVuSans.ttf",
                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
                NULL
            };
            for (int i = 0; kFontCands[i]; i++) {
                FILE* tf = fopen(kFontCands[i], "rb");
                if (tf) { fclose(tf); fpath = kFontCands[i]; break; }
            }
#endif
            TTF_Font* font = fpath ? GetFont(fpath, 15.0f) : NULL;
            if (font) {
                SDL_Color sc = { 226, 232, 240,
                                 (Uint8)SDL_clamp((int)(op * 255.0f), 0, 255) };
                int wrapW = (int)(ww - 48.0f);
                if (wrapW < 1) wrapW = 1;
                SDL_Surface* surf = TTF_RenderText_Blended_Wrapped(
                    font, msg, strlen(msg), sc, wrapW);
                if (surf) {
                    SDL_Texture* tex =
                        SDL_CreateTextureFromSurface(window->sdlRenderer, surf);
                    if (tex) {
                        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
                        float tw = 0, th = 0;
                        SDL_GetTextureSize(tex, &tw, &th);
                        SDL_FRect tdst = { wx + (ww - tw) * 0.5f,
                                           wy + (wh - th) * 0.5f, tw, th };
                        SDL_RenderTexture(window->sdlRenderer, tex, NULL, &tdst);
                        SDL_DestroyTexture(tex);
                    }
                    SDL_DestroySurface(surf);
                }
            }
        }
#endif
        return;
    }

    // ----- UIFileDrop ----------------------------------------------
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_FILE_DROP)) {
        UIFileDrop* fd = (UIFileDrop*)base;
        if (!el->width || !el->height) return;
        SDL_FRect bounds = { el->x, el->y, *el->width, *el->height };

        const UIColor bgRaw  = fd->dragOver ? fd->activeBgColor     : fd->bgColor;
        const UIColor brRaw  = fd->dragOver ? fd->activeBorderColor : fd->borderColor;
        UIColor bg = bgRaw; bg.a *= op;
        UIColor br = brRaw; br.a *= op;
        DrawRoundedRectWithBorder(window->sdlRenderer, bounds, bg, fd->radius,
                                  (int)fd->borderWidth, br);

        // Centred prompt text.
        if (fd->prompt && *fd->prompt && fd->fontFamily) {
            TTF_Font* font = GetFont(fd->fontFamily, fd->fontSize);
            if (font) {
                TTF_SetFontStyle(font, TTF_STYLE_NORMAL);
                UIColor tc = fd->textColor; tc.a *= op;
                SDL_Color sc = {(Uint8)tc.r,(Uint8)tc.g,(Uint8)tc.b,
                                (Uint8)SDL_clamp((int)(tc.a*255.0f),0,255)};
                const size_t len = strlen(fd->prompt);
                SDL_Surface* surf = TTF_RenderText_Blended(font, fd->prompt, len, sc);
                if (surf) {
                    SDL_Texture* tex = SDL_CreateTextureFromSurface(window->sdlRenderer, surf);
                    if (tex) {
                        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
                        float tw=0, th=0;
                        SDL_GetTextureSize(tex, &tw, &th);
                        SDL_FRect dst = { bounds.x + (bounds.w - tw) * 0.5f,
                                          bounds.y + (bounds.h - th) * 0.5f,
                                          tw, th };
                        SDL_RenderTexture(window->sdlRenderer, tex, NULL, &dst);
                        SDL_DestroyTexture(tex);
                    }
                    SDL_DestroySurface(surf);
                }
            }
        }
        return;
    }

    // ----- UICheckbox ----------------------------------------------
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_CHECKBOX)) {
        UICheckbox* c = (UICheckbox*)base;
        if (!el->width || !el->height) return;
        SDL_FRect bounds = { el->x, el->y, *el->width, *el->height };

        UIColor boxC    = c->boxColor;    boxC.a    *= op;
        UIColor borderC = c->borderColor; borderC.a *= op;
        UIColor checkC  = c->checkColor;  checkC.a  *= op;

        // Box
        DrawRoundedRectWithBorder(window->sdlRenderer, bounds,
                                  boxC, c->radius,
                                  (int)c->borderWidth, borderC);

        // Ease _phase toward the checked target.
        const float target = c->checked ? 1.0f : 0.0f;
        if (c->animMs > 0) {
            const float k = 1.0f - powf(0.3f, 1.0f / (float)c->animMs);
            c->_phase += (target - c->_phase) * k * 16.0f;
            if ((target == 1.0f && c->_phase > 0.999f) ||
                (target == 0.0f && c->_phase < 0.001f)) {
                c->_phase = target;
            }
        } else {
            c->_phase = target;
        }

        // Draw the check mark as two line segments forming a "✓". The
        // stroke length is revealed proportionally to _phase, so it
        // looks like the symbol is being drawn from start to end (and
        // erased back when unchecking).
        if (c->_phase > 0.001f) {
            const float ph = c->_phase;
            // Three control points in normalised coordinates within
            // the checkbox bounds. Tuned to feel balanced regardless
            // of box size.
            const float P1x = 0.22f, P1y = 0.52f;  // upper-left start
            const float P2x = 0.43f, P2y = 0.73f;  // lower-middle corner
            const float P3x = 0.80f, P3y = 0.30f;  // upper-right end

            const float ax = bounds.x + bounds.w * P1x;
            const float ay = bounds.y + bounds.h * P1y;
            const float bx = bounds.x + bounds.w * P2x;
            const float by = bounds.y + bounds.h * P2y;
            const float cx = bounds.x + bounds.w * P3x;
            const float cy = bounds.y + bounds.h * P3y;

            const float L1 = sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
            const float L2 = sqrtf((cx - bx) * (cx - bx) + (cy - by) * (cy - by));
            const float total = L1 + L2;
            const float visible = total * ph;

            // Stroke thickness scales with box size so the mark stays
            // readable at any scale.
            const float minDim = SDL_min(bounds.w, bounds.h);
            const float thick = SDL_max(1.2f, minDim * 0.12f);
            const float radius = thick * 0.5f;
            const float step = SDL_max(0.5f, radius * 0.6f);

            // Helper to walk a segment and stamp circles at `step`
            // intervals up to `endDist`. Returns the distance covered.
            // Inlined as two loops to keep the code C and dependency-free.
            float drawn = 0.0f;

            // Segment 1: A -> B
            {
                const float maxD = SDL_min(visible, L1);
                if (maxD > 0.0f && L1 > 0.0f) {
                    const float dx = (bx - ax) / L1;
                    const float dy = (by - ay) / L1;
                    for (float d = 0.0f; d <= maxD; d += step) {
                        const float px = ax + dx * d;
                        const float py = ay + dy * d;
                        DrawCircle(window->sdlRenderer, px, py, radius, checkC);
                    }
                    // Cap at the end of the visible portion so the
                    // stroke doesn't have a gap.
                    const float ex = ax + dx * maxD;
                    const float ey = ay + dy * maxD;
                    DrawCircle(window->sdlRenderer, ex, ey, radius, checkC);
                    drawn = maxD;
                }
            }

            // Segment 2: B -> C, only after segment 1 is fully drawn.
            if (visible > L1 && L2 > 0.0f) {
                const float remaining = SDL_min(visible - L1, L2);
                const float dx = (cx - bx) / L2;
                const float dy = (cy - by) / L2;
                for (float d = 0.0f; d <= remaining; d += step) {
                    const float px = bx + dx * d;
                    const float py = by + dy * d;
                    DrawCircle(window->sdlRenderer, px, py, radius, checkC);
                }
                const float ex = bx + dx * remaining;
                const float ey = by + dy * remaining;
                DrawCircle(window->sdlRenderer, ex, ey, radius, checkC);
                (void)drawn;
            }
        }
        return;
    }

    // ----- UISlider ------------------------------------------------
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_SLIDER)) {
        UISlider* s = (UISlider*)base;
        if (!el->width || !el->height) return;

        const float W = *el->width;
        const float H = *el->height;
        const float trackY = el->y + (H - s->trackHeight) * 0.5f;
        SDL_FRect track = { el->x, trackY, W, s->trackHeight };

        UIColor trackC = s->trackColor; trackC.a *= op;
        UIColor fillC  = s->fillColor;  fillC.a  *= op;
        UIColor knobC  = s->knobColor;  knobC.a  *= op;

        DrawRoundedRectFill(window->sdlRenderer, track, trackC, s->trackHeight * 0.5f);

        const float range = (s->maxValue - s->minValue);
        const float ratio = (range > 0.0f) ? (s->value - s->minValue) / range : 0.0f;
        SDL_FRect filled = { el->x, trackY, W * ratio, s->trackHeight };
        if (filled.w > 0.0f) {
            DrawRoundedRectFill(window->sdlRenderer, filled, fillC, s->trackHeight * 0.5f);
        }

        // Knob: filled circle centred at the value position.
        const float knobX = el->x + W * ratio;
        const float knobY = el->y + H * 0.5f;
        DrawCircle(window->sdlRenderer, knobX, knobY, s->knobRadius, knobC);
        return;
    }

    // ----- UIProgressBar -------------------------------------------
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_PROGRESS_BAR)) {
        UIProgressBar* p = (UIProgressBar*)base;
        if (!el->width || !el->height) return;
        SDL_FRect bounds = { el->x, el->y, *el->width, *el->height };

        UIColor trackC = p->trackColor; trackC.a *= op;
        UIColor fillC  = p->fillColor;  fillC.a  *= op;

        DrawRoundedRectFill(window->sdlRenderer, bounds, trackC, p->radius);

        if (p->indeterminate) {
            // Advance phase based on wall-clock so the strip travels
            // independently of frame timing.
            const float t = (float)((SDL_GetTicks() % 1500ULL)) / 1500.0f;
            const float segW = bounds.w * 0.3f;
            const float x    = bounds.x + (bounds.w + segW) * t - segW;
            const float clampedX = (x < bounds.x) ? bounds.x : x;
            const float clampedW = (x + segW > bounds.x + bounds.w)
                                   ? (bounds.x + bounds.w - clampedX)
                                   : (segW - (clampedX - x));
            if (clampedW > 0.0f) {
                SDL_FRect bar = { clampedX, bounds.y, clampedW, bounds.h };
                DrawRoundedRectFill(window->sdlRenderer, bar, fillC, p->radius);
            }
            (void)p->_phase;
        } else {
            float v = p->value;
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            SDL_FRect bar = { bounds.x, bounds.y, bounds.w * v, bounds.h };
            if (bar.w > 0.0f) {
                DrawRoundedRectFill(window->sdlRenderer, bar, fillC, p->radius);
            }
        }
        return;
    }

    // ----- UITextField ---------------------------------------------
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_TEXTFIELD)) {
        UITextField* tf = (UITextField*)base;
        if (!el->width || !el->height) return;
        SDL_FRect bounds = { el->x, el->y, *el->width, *el->height };

        UIColor bg     = tf->bgColor;       bg.a     *= op;
        UIColor border = (tf->focused ? tf->borderColorFocused : tf->borderColor);
        border.a *= op;

        DrawRoundedRectWithBorder(window->sdlRenderer, bounds,
                                  bg, tf->radius,
                                  (int)tf->borderWidth, border);

        // Decide which string to render and which colour.
        const int   showPlaceholder = (tf->textLen == 0 && tf->placeholder);
        const char* renderStr = showPlaceholder ? tf->placeholder : tf->text;
        int         renderLen = showPlaceholder
                              ? (int)strlen(tf->placeholder)
                              : tf->textLen;

        // Password mask: replace each glyph with a bullet (U+2022 in UTF-8
        // is e2 80 a2). For simplicity we use ASCII '*' here.
        char  passBuf[256];
        int   passUsed = 0;
        if (!showPlaceholder && tf->passwordMask && renderLen > 0) {
            int n = renderLen;
            if (n > (int)sizeof(passBuf) - 1) n = sizeof(passBuf) - 1;
            for (int i = 0; i < n; i++) passBuf[i] = '*';
            passBuf[n] = '\0';
            renderStr = passBuf;
            renderLen = n;
            passUsed  = 1;
        }
        (void)passUsed;

        TTF_Font* font = (tf->fontFamily && tf->fontSize > 0.0f)
            ? GetFont(tf->fontFamily, tf->fontSize) : NULL;
        if (font) {
            TTF_SetFontStyle(font, (TTF_FontStyleFlags)tf->fontStyle);
        }

        // Lazily regenerate the text texture when the content changed.
        if (font && (!tf->__SDL_textTexture ||
                     tf->__cachedTextLen != renderLen)) {
            if (tf->__SDL_textTexture) {
                SDL_DestroyTexture(tf->__SDL_textTexture);
                tf->__SDL_textTexture = NULL;
            }
            const UIColor c = showPlaceholder ? tf->placeholderColor : tf->textColor;
            SDL_Color sc = { (Uint8)c.r, (Uint8)c.g, (Uint8)c.b,
                             (Uint8)SDL_clamp((int)(c.a * 255.0f), 0, 255) };
            if (renderLen > 0) {
                SDL_Surface* surf = TTF_RenderText_Blended(font, renderStr,
                                                          (size_t)renderLen, sc);
                if (surf) {
                    tf->__SDL_textTexture = SDL_CreateTextureFromSurface(
                        window->sdlRenderer, surf);
                    SDL_DestroySurface(surf);
                    if (tf->__SDL_textTexture) {
                        SDL_SetTextureScaleMode(tf->__SDL_textTexture, SDL_SCALEMODE_LINEAR);
                    }
                }
            }
            tf->__cachedTextLen = renderLen;
        }

        // Rebuild __charOffsets so the dispatcher can map mouse x to a
        // caret position. We track offsets for the *actual* edited text
        // (tf->text), not for the placeholder. When the placeholder is
        // visible there is nothing to click into anyway.
        if (font && !showPlaceholder) {
            const int need = tf->textLen + 1;
            if (tf->__charOffsetsLen != need) {
                int* p = (int*)realloc(tf->__charOffsets, (size_t)need * sizeof(int));
                if (p) {
                    tf->__charOffsets    = p;
                    tf->__charOffsetsLen = need;
                }
            }
            if (tf->__charOffsets && tf->__charOffsetsLen == need) {
                const char* measureStr = renderStr; // matches glyph rendering
                tf->__charOffsets[0] = 0;
                for (int i = 1; i <= tf->textLen; i++) {
                    int w = 0, h = 0;
                    TTF_GetStringSize(font, measureStr, (size_t)i, &w, &h);
                    tf->__charOffsets[i] = w;
                }
            }
        } else if (showPlaceholder) {
            // Discard stale offsets so a click on an empty field can't
            // land the caret past 0. The dispatcher already early-outs
            // when textLen <= 0.
            tf->__charOffsetsLen = 0;
        }

        // Clip drawing of glyphs + caret to the inside of the field.
        SDL_Rect clip = {
            (int)(bounds.x + tf->paddingLeft),
            (int)(bounds.y),
            (int)(bounds.w - (tf->paddingLeft + tf->paddingRight)),
            (int)(bounds.h)
        };
        SDL_SetRenderClipRect(window->sdlRenderer, &clip);

        // Selection highlight - drawn behind the glyphs so the text
        // colour stays untouched.
        if (!showPlaceholder && tf->selAnchor >= 0 &&
            tf->selAnchor != tf->caretPos &&
            tf->__charOffsets && tf->__charOffsetsLen == tf->textLen + 1) {
            int s = tf->selAnchor, e = tf->caretPos;
            if (s > e) { int t = s; s = e; e = t; }
            if (s < 0) s = 0;
            if (e > tf->textLen) e = tf->textLen;
            const float sx = (float)tf->__charOffsets[s];
            const float ex = (float)tf->__charOffsets[e];
            const float caretH = tf->fontSize * 1.1f;
            SDL_FRect selRect = {
                bounds.x + tf->paddingLeft + sx,
                bounds.y + (bounds.h - caretH) * 0.5f,
                ex - sx,
                caretH
            };
            UIColor sc2 = tf->selectionColor; sc2.a *= op;
            DrawRoundedRectFill(window->sdlRenderer, selRect, sc2, 2.0f);
        }

        if (tf->__SDL_textTexture && renderLen > 0) {
            float tw = 0.0f, th = 0.0f;
            SDL_GetTextureSize(tf->__SDL_textTexture, &tw, &th);
            const float tx = bounds.x + tf->paddingLeft;
            const float ty = bounds.y + (bounds.h - th) * 0.5f;
            SDL_FRect dst = { tx, ty, tw, th };

            /* Placeholder pulse: only when the field is empty, unfocused,
             * and SetPlaceholderAnimated(true) was called. Sine on a
             * 2.5 s period, gentle band 55% .. 100%. */
            float alpha = op;
            if (showPlaceholder && tf->placeholderAnimated && !tf->focused) {
                const double t = (double)SDL_GetTicks() * 0.001;
                const double s = (SDL_sin(t * 2.5132741) + 1.0) * 0.5; /* 2pi/2.5 */
                alpha = op * (float)(0.55 + 0.45 * s);
            }
            SDL_SetTextureAlphaMod(tf->__SDL_textTexture, (Uint8)(alpha * 255.0f + 0.5f));
            SDL_RenderTexture(window->sdlRenderer, tf->__SDL_textTexture, NULL, &dst);
            SDL_SetTextureAlphaMod(tf->__SDL_textTexture, 255);
        }

        // Caret. Position derives from substring width up to the caret.
        if (tf->focused && font) {
            /* Caret blink: cadence configurable via SetCaretBlinkRate.
             * blinkMs <= 0 keeps it solid (useful for screenshots). */
            const int blinkMs = tf->caretBlinkMs;
            int visible;
            if (blinkMs <= 0) {
                visible = 1;
            } else {
                const Uint64 t = SDL_GetTicks();
                visible = (int)(((t / (Uint64)blinkMs) & 1ULL) == 0);
            }
            if (visible) {
                int caretBytes = tf->caretPos;
                if (caretBytes > tf->textLen) caretBytes = tf->textLen;

                int w = 0, h = 0;
                const char* measureStr = renderStr;
                int          measureLen = caretBytes;
                if (tf->passwordMask) {
                    // For password fields, measure '*' repeated to caretBytes.
                    measureStr = passBuf;
                    if (measureLen > (int)sizeof(passBuf) - 1)
                        measureLen = sizeof(passBuf) - 1;
                }
                if (measureLen > 0) {
                    TTF_GetStringSize(font, measureStr, (size_t)measureLen, &w, &h);
                }
                const float caretX = bounds.x + tf->paddingLeft + (float)w;
                const float caretH = tf->fontSize * 1.1f;
                const float caretY = bounds.y + (bounds.h - caretH) * 0.5f;
                UIColor cc = tf->caretColor; cc.a *= op;
                SDL_FRect caretRect = { caretX, caretY, 1.5f, caretH };
                DrawRoundedRectFill(window->sdlRenderer, caretRect, cc, 0.0f);
            }
        }

        SDL_SetRenderClipRect(window->sdlRenderer, NULL);
        return;
    }

    // ----- UITextArea ----------------------------------------------
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_TEXTAREA)) {
        UITextArea* ta = (UITextArea*)base;
        if (!el->width || !el->height) return;
        SDL_FRect bounds = { el->x, el->y, *el->width, *el->height };

        UIColor bg2     = ta->bgColor;     bg2.a     *= op;
        UIColor border2 = ta->focused ? ta->borderColorFocused : ta->borderColor;
        border2.a *= op;
        DrawRoundedRectWithBorder(window->sdlRenderer, bounds,
                                  bg2, ta->radius,
                                  (int)ta->borderWidth, border2);

        TTF_Font* font = (ta->fontFamily && ta->fontSize > 0.0f)
            ? GetFont(ta->fontFamily, ta->fontSize) : NULL;
        if (!font) return;
        TTF_SetFontStyle(font, (TTF_FontStyleFlags)ta->fontStyle);

        const int showPlaceholder = (ta->textLen == 0 && ta->placeholder);
        const float lineH = ta->fontSize * ta->lineSpacing;

        // Available pixel width inside the padding box. When wrap is
        // enabled and this value changes (the user resized the widget),
        // we have to rebuild the line cache.
        int wrapW = -1;
        if (ta->wrapMode != UI_WRAP_NONE) {
            wrapW = (int)(bounds.w - ta->paddingLeft - ta->paddingRight);
            if (wrapW < 1) wrapW = -1;
        }

        const int needRebuild =
            ta->__cachedTextLen  != ta->textLen ||
            ta->__cachedWrapMode != (int)ta->wrapMode ||
            ta->__cachedWrapW    != wrapW;

        // Rebuild the per-line cache when the text/wrap config changed.
        // Each visual line gets its own glyph texture + char-offset array.
        if (needRebuild) {
            // Reuse InvalidateLineCache: it frees and zeroes.
            if (ta->lineTextures) {
                for (int i = 0; i < ta->linesLen; i++) {
                    if (ta->lineTextures[i]) SDL_DestroyTexture(ta->lineTextures[i]);
                }
            }
            if (ta->lineCharOffsets) {
                for (int i = 0; i < ta->linesLen; i++) {
                    free(ta->lineCharOffsets[i]);
                }
            }
            free(ta->lineTextures);       ta->lineTextures = NULL;
            free(ta->lineStarts);         ta->lineStarts = NULL;
            free(ta->lineLengths);        ta->lineLengths = NULL;
            free(ta->lineCharOffsets);    ta->lineCharOffsets = NULL;
            free(ta->lineCharOffsetsLen); ta->lineCharOffsetsLen = NULL;
            free(ta->lineIsSoft);         ta->lineIsSoft = NULL;
            ta->linesLen = 0;

            // Initial capacity: hard line count, with extra room when
            // wrap is on (each logical line may produce several visual
            // segments). The arrays grow on demand inside the loop.
            int cap = 1;
            for (int i = 0; i < ta->textLen; i++) if (ta->text[i] == '\n') cap++;
            if (wrapW > 0) { cap = cap * 2 + 4; }
            if (cap < 1) cap = 1;

            ta->linesCap            = cap;
            ta->lineTextures        = (SDL_Texture**)calloc(cap, sizeof(SDL_Texture*));
            ta->lineStarts          = (int*)calloc(cap, sizeof(int));
            ta->lineLengths         = (int*)calloc(cap, sizeof(int));
            ta->lineCharOffsets     = (int**)calloc(cap, sizeof(int*));
            ta->lineCharOffsetsLen  = (int*)calloc(cap, sizeof(int));
            ta->lineIsSoft          = (int*)calloc(cap, sizeof(int));

            const UIColor c = ta->textColor;
            const SDL_Color sc = { (Uint8)c.r, (Uint8)c.g, (Uint8)c.b,
                                   (Uint8)SDL_clamp((int)(c.a * 255.0f), 0, 255) };

            // Iterate over logical lines (separated by '\n'). Inside
            // each, either emit a single segment (wrap off) or break
            // it into visual segments that fit in wrapW.
            int logicalStart = 0;
            for (int i = 0; i <= ta->textLen; i++) {
                if (i < ta->textLen && ta->text[i] != '\n') continue;
                const int lineEnd = i;

                int segStart    = logicalStart;
                int firstInLine = 1;

                do {
                    int segEnd;
                    if (wrapW <= 0 || lineEnd - segStart == 0) {
                        segEnd = lineEnd;
                    } else {
                        // Does the whole remainder fit on one line?
                        int w = 0, h = 0;
                        TTF_GetStringSize(font, ta->text + segStart,
                                          (size_t)(lineEnd - segStart), &w, &h);
                        if (w <= wrapW) {
                            segEnd = lineEnd;
                        } else {
                            // Binary-search the longest prefix that fits.
                            int lo = 1, hi = lineEnd - segStart, fit = 1;
                            while (lo <= hi) {
                                int mid = (lo + hi) / 2;
                                int ww = 0, hh = 0;
                                TTF_GetStringSize(font, ta->text + segStart,
                                                  (size_t)mid, &ww, &hh);
                                if (ww <= wrapW) { fit = mid; lo = mid + 1; }
                                else              { hi = mid - 1; }
                            }
                            int cut = fit;
                            if (ta->wrapMode == UI_WRAP_WORD) {
                                // Back up to just after the last
                                // whitespace inside the fitting prefix
                                // so trailing spaces stay with this
                                // line. Falls back to char-wrap when
                                // no whitespace was found (a single
                                // very long word).
                                int spAfter = -1;
                                for (int k = cut; k > 0; k--) {
                                    char ch = ta->text[segStart + k - 1];
                                    if (ch == ' ' || ch == '\t') { spAfter = k; break; }
                                }
                                if (spAfter > 0) cut = spAfter;
                            }
                            if (cut <= 0) cut = 1;
                            segEnd = segStart + cut;
                        }
                    }

                    // Grow the arrays if we ran out of capacity.
                    if (ta->linesLen >= ta->linesCap) {
                        int newCap = ta->linesCap * 2;
                        if (newCap < ta->linesLen + 1) newCap = ta->linesLen + 8;
                        ta->lineTextures        = (SDL_Texture**)realloc(ta->lineTextures,        newCap * sizeof(SDL_Texture*));
                        ta->lineStarts          = (int*)         realloc(ta->lineStarts,          newCap * sizeof(int));
                        ta->lineLengths         = (int*)         realloc(ta->lineLengths,         newCap * sizeof(int));
                        ta->lineCharOffsets     = (int**)        realloc(ta->lineCharOffsets,     newCap * sizeof(int*));
                        ta->lineCharOffsetsLen  = (int*)         realloc(ta->lineCharOffsetsLen,  newCap * sizeof(int));
                        ta->lineIsSoft          = (int*)         realloc(ta->lineIsSoft,          newCap * sizeof(int));
                        for (int z = ta->linesCap; z < newCap; z++) {
                            ta->lineTextures[z]       = NULL;
                            ta->lineStarts[z]         = 0;
                            ta->lineLengths[z]        = 0;
                            ta->lineCharOffsets[z]    = NULL;
                            ta->lineCharOffsetsLen[z] = 0;
                            ta->lineIsSoft[z]         = 0;
                        }
                        ta->linesCap = newCap;
                    }

                    const int li     = ta->linesLen;
                    const int segLen = segEnd - segStart;
                    ta->lineStarts[li]  = segStart;
                    ta->lineLengths[li] = segLen;
                    ta->lineIsSoft[li]  = firstInLine ? 0 : 1;

                    if (segLen > 0) {
                        SDL_Surface* surf = TTF_RenderText_Blended(
                            font, ta->text + segStart, (size_t)segLen, sc);
                        if (surf) {
                            ta->lineTextures[li] = SDL_CreateTextureFromSurface(
                                window->sdlRenderer, surf);
                            SDL_DestroySurface(surf);
                            if (ta->lineTextures[li]) {
                                SDL_SetTextureScaleMode(ta->lineTextures[li], SDL_SCALEMODE_LINEAR);
                            }
                        }
                    } else {
                        ta->lineTextures[li] = NULL;
                    }

                    const int need = segLen + 1;
                    ta->lineCharOffsetsLen[li] = need;
                    ta->lineCharOffsets[li] = (int*)calloc(need, sizeof(int));
                    if (ta->lineCharOffsets[li]) {
                        ta->lineCharOffsets[li][0] = 0;
                        for (int k = 1; k < need; k++) {
                            int w = 0, h = 0;
                            TTF_GetStringSize(font, ta->text + segStart, (size_t)k, &w, &h);
                            ta->lineCharOffsets[li][k] = w;
                        }
                    }

                    ta->linesLen++;
                    firstInLine = 0;
                    segStart = segEnd;
                } while (segStart < lineEnd);

                logicalStart = lineEnd + 1; // skip the '\n'
            }

            ta->__cachedTextLen  = ta->textLen;
            ta->__cachedWrapMode = (int)ta->wrapMode;
            ta->__cachedWrapW    = wrapW;
        }

        // Auto-scroll so the caret stays inside the viewport.
        {
            int pos = ta->caretPos;
            if (pos < 0) pos = 0;
            if (pos > ta->textLen) pos = ta->textLen;
            int lo = 0, hi = ta->linesLen - 1;
            while (lo < hi) {
                int mid = (lo + hi + 1) / 2;
                if (ta->lineStarts[mid] <= pos) lo = mid; else hi = mid - 1;
            }
            const int caretLine = lo;

            const float viewH = bounds.h - (ta->paddingTop + ta->paddingBottom);
            const float caretY = caretLine * lineH;
            if (caretY < ta->scrollY) ta->scrollY = caretY;
            if (caretY + lineH > ta->scrollY + viewH) {
                ta->scrollY = caretY + lineH - viewH;
            }
            if (ta->scrollY < 0.0f) ta->scrollY = 0.0f;
        }

        // Clip the inside of the field so glyphs / caret never bleed
        // over the padding/border.
        SDL_Rect clipR = {
            (int)(bounds.x + ta->paddingLeft),
            (int)(bounds.y + ta->paddingTop),
            (int)(bounds.w - (ta->paddingLeft + ta->paddingRight)),
            (int)(bounds.h - (ta->paddingTop + ta->paddingBottom))
        };
        SDL_SetRenderClipRect(window->sdlRenderer, &clipR);

        // Selection highlight - emit one rect per affected line.
        if (!showPlaceholder && ta->selAnchor >= 0 &&
            ta->selAnchor != ta->caretPos && ta->linesLen > 0) {
            int s, e;
            // (inline SelectionRange)
            s = ta->selAnchor; e = ta->caretPos;
            if (s > e) { int t = s; s = e; e = t; }
            if (s < 0) s = 0;
            if (e > ta->textLen) e = ta->textLen;

            int sLine = 0, eLine = 0;
            { int lo = 0, hi = ta->linesLen - 1;
              while (lo < hi) {
                int mid = (lo + hi + 1) / 2;
                if (ta->lineStarts[mid] <= s) lo = mid; else hi = mid - 1;
              } sLine = lo; }
            { int lo = 0, hi = ta->linesLen - 1;
              while (lo < hi) {
                int mid = (lo + hi + 1) / 2;
                if (ta->lineStarts[mid] <= e) lo = mid; else hi = mid - 1;
              } eLine = lo; }

            UIColor selC = ta->selectionColor; selC.a *= op;
            for (int li = sLine; li <= eLine; li++) {
                if (!ta->lineCharOffsets[li]) continue;
                int colStart = (li == sLine) ? s - ta->lineStarts[li] : 0;
                int colEnd   = (li == eLine) ? e - ta->lineStarts[li]
                                             : ta->lineLengths[li];
                if (colStart < 0) colStart = 0;
                if (colEnd > ta->lineLengths[li]) colEnd = ta->lineLengths[li];
                const float x1 = (float)ta->lineCharOffsets[li][colStart];
                const float x2 = (float)ta->lineCharOffsets[li][colEnd];
                // If selection wraps across a hard newline at end of
                // line, add a small visual tail so the highlight reads.
                // Soft-wrapped breaks (visual wrap, same logical line)
                // skip the tail since there's no real '\n' to mark.
                const int isHardBreak = (li < eLine) &&
                    (li + 1 >= ta->linesLen || !ta->lineIsSoft[li + 1]);
                const float tail = isHardBreak ? ta->fontSize * 0.4f : 0.0f;
                SDL_FRect r = {
                    bounds.x + ta->paddingLeft + x1,
                    bounds.y + ta->paddingTop + li * lineH - ta->scrollY,
                    (x2 - x1) + tail,
                    lineH
                };
                DrawRoundedRectFill(window->sdlRenderer, r, selC, 2.0f);
            }
        }

        // Glyphs.
        if (showPlaceholder) {
            const UIColor c = ta->placeholderColor;
            SDL_Color sc = { (Uint8)c.r, (Uint8)c.g, (Uint8)c.b,
                             (Uint8)SDL_clamp((int)(c.a * 255.0f), 0, 255) };
            const int phLen = (int)strlen(ta->placeholder);
            SDL_Surface* surf = TTF_RenderText_Blended(font, ta->placeholder,
                                                      (size_t)phLen, sc);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(window->sdlRenderer, surf);
                SDL_DestroySurface(surf);
                if (tex) {
                    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
                    float tw, th;
                    SDL_GetTextureSize(tex, &tw, &th);
                    SDL_FRect dst = {
                        bounds.x + ta->paddingLeft,
                        bounds.y + ta->paddingTop,
                        tw, th
                    };
                    /* Same pulse logic as the textfield: when the area
                     * is empty + unfocused + SetPlaceholderAnimated(1),
                     * breathe the alpha on a slow sine. */
                    float alpha = op;
                    if (ta->placeholderAnimated && !ta->focused) {
                        const double tt = (double)SDL_GetTicks() * 0.001;
                        const double s = (SDL_sin(tt * 2.5132741) + 1.0) * 0.5;
                        alpha = op * (float)(0.55 + 0.45 * s);
                    }
                    SDL_SetTextureAlphaMod(tex, (Uint8)(alpha * 255.0f + 0.5f));
                    SDL_RenderTexture(window->sdlRenderer, tex, NULL, &dst);
                    SDL_DestroyTexture(tex);
                }
            }
        } else {
            for (int li = 0; li < ta->linesLen; li++) {
                if (!ta->lineTextures[li]) continue;
                float tw, th;
                SDL_GetTextureSize(ta->lineTextures[li], &tw, &th);
                SDL_FRect dst = {
                    bounds.x + ta->paddingLeft,
                    bounds.y + ta->paddingTop + li * lineH - ta->scrollY,
                    tw, th
                };
                SDL_SetTextureAlphaMod(ta->lineTextures[li], (Uint8)(op * 255.0f + 0.5f));
                SDL_RenderTexture(window->sdlRenderer, ta->lineTextures[li], NULL, &dst);
                SDL_SetTextureAlphaMod(ta->lineTextures[li], 255);
            }
        }

        // Caret. Half-period configurable via UITextArea_SetCaretBlinkRate;
        // 0 keeps it solid.
        if (ta->focused && ta->linesLen > 0) {
            const int blinkMs = ta->caretBlinkMs;
            int visible;
            if (blinkMs <= 0) {
                visible = 1;
            } else {
                const Uint64 t = SDL_GetTicks();
                visible = (int)(((t / (Uint64)blinkMs) & 1ULL) == 0);
            }
            if (visible) {
                int caretLine = 0;
                int pos = ta->caretPos;
                if (pos < 0) pos = 0;
                if (pos > ta->textLen) pos = ta->textLen;
                int lo = 0, hi = ta->linesLen - 1;
                while (lo < hi) {
                    int mid = (lo + hi + 1) / 2;
                    if (ta->lineStarts[mid] <= pos) lo = mid; else hi = mid - 1;
                }
                caretLine = lo;
                const int col = pos - ta->lineStarts[caretLine];
                float cx = 0.0f;
                if (ta->lineCharOffsets[caretLine] &&
                    col < ta->lineCharOffsetsLen[caretLine]) {
                    cx = (float)ta->lineCharOffsets[caretLine][col];
                }
                const float caretH = ta->fontSize * 1.1f;
                UIColor cc = ta->caretColor; cc.a *= op;
                SDL_FRect caretRect = {
                    bounds.x + ta->paddingLeft + cx,
                    bounds.y + ta->paddingTop + caretLine * lineH - ta->scrollY
                        + (lineH - caretH) * 0.5f,
                    1.5f, caretH
                };
                DrawRoundedRectFill(window->sdlRenderer, caretRect, cc, 0.0f);
            }
        }

        SDL_SetRenderClipRect(window->sdlRenderer, NULL);
        return;
    }

    // ----- UISpinner -----------------------------------------------
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_SPINNER)) {
        UISpinner* sp = (UISpinner*)base;
        if (!el->width || !el->height) return;

        UIColor c = sp->color; c.a *= op;

        // Drive rotation from wall-clock with the user-configurable
        // speed (rad/s). Default = 2*pi (one revolution per second).
        const float speed = (sp->speed != 0.0f) ? sp->speed : 6.28318530718f;
        const float phase = (float)(SDL_GetTicks() * 0.001) * speed;
        sp->_phase = phase;

        const float cx = el->x + *el->width * 0.5f;
        const float cy = el->y + *el->height * 0.5f;
        const float r  = sp->radius;

        // 12 dots positioned around the ring with descending alpha.
        const int N = 12;
        for (int i = 0; i < N; i++) {
            const float a = phase + i * (6.28318530718f / N);
            const float dotX = cx + cosf(a) * r;
            const float dotY = cy + sinf(a) * r;
            const float fade = (float)i / (float)N;
            UIColor dc = c;
            dc.a = c.a * fade;
            DrawCircle(window->sdlRenderer, dotX, dotY, sp->thickness, dc);
        }
        return;
    }

    // ----- UIRadioButton -------------------------------------------
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_RADIO)) {
        UIRadioButton* r = (UIRadioButton*)base;
        if (!el->width || !el->height) return;

        const float w = *el->width;
        const float h = *el->height;
        const float cx = el->x + w * 0.5f;
        const float cy = el->y + h * 0.5f;
        const float outerR = SDL_min(w, h) * 0.5f;

        UIColor boxC    = r->boxColor;    boxC.a    *= op;
        UIColor borderC = r->borderColor; borderC.a *= op;
        UIColor dotC    = r->dotColor;    dotC.a    *= op;

        // Outer disc: border ring first, then inner fill.
        if (r->borderWidth > 0.0f) {
            DrawCircle(window->sdlRenderer, cx, cy, outerR, borderC);
            const float innerR = SDL_max(0.0f, outerR - r->borderWidth);
            if (innerR > 0.0f) {
                DrawCircle(window->sdlRenderer, cx, cy, innerR, boxC);
            }
        } else {
            DrawCircle(window->sdlRenderer, cx, cy, outerR, boxC);
        }

        // Ease _phase toward the selected target.
        const float target = r->selected ? 1.0f : 0.0f;
        if (r->animMs > 0) {
            const float k = 1.0f - powf(0.3f, 1.0f / (float)r->animMs);
            r->_phase += (target - r->_phase) * k * 16.0f;
            if ((target == 1.0f && r->_phase > 0.999f) ||
                (target == 0.0f && r->_phase < 0.001f)) {
                r->_phase = target;
            }
        } else {
            r->_phase = target;
        }

        // Inner dot grows from the centre with the phase. Alpha also
        // fades so the appearance/disappearance feels softer.
        if (r->_phase > 0.001f) {
            const float dotMaxR = outerR * r->dotScale;
            const float dotR = dotMaxR * r->_phase;
            UIColor dc = dotC;
            dc.a *= r->_phase;
            DrawCircle(window->sdlRenderer, cx, cy, dotR, dc);
        }
        return;
    }

    // ----- UISwitch ------------------------------------------------
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_SWITCH)) {
        UISwitch* sw = (UISwitch*)base;
        if (!el->width || !el->height) return;

        const float w = *el->width;
        const float h = *el->height;

        // Animate _phase toward the on/off target.
        const float target = sw->on ? 1.0f : 0.0f;
        if (sw->animMs > 0) {
            // Frame-rate independent ease: move ~70% of the remaining
            // distance every animMs (close enough for the eye).
            const float k = 1.0f - powf(0.3f, 1.0f / (float)sw->animMs);
            sw->_phase += (target - sw->_phase) * k * 16.0f; // assume ~16ms/frame
            if ((target == 1.0f && sw->_phase > 0.999f) ||
                (target == 0.0f && sw->_phase < 0.001f)) {
                sw->_phase = target;
            }
        } else {
            sw->_phase = target;
        }

        // Track (pill background) blends from off to on colour.
        UIColor tc;
        tc.r = sw->offColor.r + (sw->onColor.r - sw->offColor.r) * sw->_phase;
        tc.g = sw->offColor.g + (sw->onColor.g - sw->offColor.g) * sw->_phase;
        tc.b = sw->offColor.b + (sw->onColor.b - sw->offColor.b) * sw->_phase;
        tc.a = (sw->offColor.a + (sw->onColor.a - sw->offColor.a) * sw->_phase) * op;

        SDL_FRect track = { el->x, el->y, w, h };
        const float trackRadius = h * 0.5f;
        UIColor bc = sw->borderColor; bc.a *= op;
        DrawRoundedRectWithBorder(window->sdlRenderer, track,
                                  tc, trackRadius,
                                  (int)sw->borderWidth, bc);

        // Knob slides between the off/on positions.
        const float m = sw->knobMargin;
        const float knobR = SDL_max(0.0f, h * 0.5f - m);
        const float minX = el->x + m + knobR;
        const float maxX = el->x + w - m - knobR;
        const float knobX = minX + (maxX - minX) * sw->_phase;
        const float knobY = el->y + h * 0.5f;
        UIColor kc = sw->knobColor; kc.a *= op;
        DrawCircle(window->sdlRenderer, knobX, knobY, knobR, kc);
        return;
    }

    // ----- UITooltip -----------------------------------------------
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_TOOLTIP)) {
        UITooltip* tt = (UITooltip*)base;
        if (!tt->_visible || !tt->text || !*tt->text) return;

        TTF_Font* font = (tt->fontFamily && tt->fontSize > 0.0f)
            ? GetFont(tt->fontFamily, tt->fontSize) : NULL;
        if (!font) return;
        TTF_SetFontStyle(font, TTF_STYLE_NORMAL);

        const int textLen = (int)strlen(tt->text);
        if (!tt->__SDL_textTexture || tt->__cachedTextLen != textLen) {
            if (tt->__SDL_textTexture) {
                SDL_DestroyTexture(tt->__SDL_textTexture);
                tt->__SDL_textTexture = NULL;
            }
            SDL_Color sc = {(Uint8)tt->textColor.r,(Uint8)tt->textColor.g,
                            (Uint8)tt->textColor.b,255};
            SDL_Surface* surf = TTF_RenderText_Blended(font, tt->text, (size_t)textLen, sc);
            if (surf) {
                tt->__SDL_textTexture = SDL_CreateTextureFromSurface(
                    window->sdlRenderer, surf);
                SDL_DestroySurface(surf);
                if (tt->__SDL_textTexture) {
                    SDL_SetTextureScaleMode(tt->__SDL_textTexture, SDL_SCALEMODE_LINEAR);
                }
            }
            tt->__cachedTextLen = textLen;
        }
        if (!tt->__SDL_textTexture) return;

        float tw = 0.0f, th = 0.0f;
        SDL_GetTextureSize(tt->__SDL_textTexture, &tw, &th);
        const float bw = tw + tt->paddingX * 2.0f;
        const float bh = th + tt->paddingY * 2.0f;
        // Position next to the cursor with a small offset; clamp to window.
        int winW = 0, winH = 0;
        SDL_GetWindowSize(window->sdlWindow, &winW, &winH);
        float bx = tt->_cursorX + 14.0f;
        float by = tt->_cursorY + 14.0f;
        if (bx + bw > winW) bx = winW - bw - 4.0f;
        if (by + bh > winH) by = winH - bh - 4.0f;

        SDL_FRect bg = { bx, by, bw, bh };
        UIColor bgC = tt->bgColor; bgC.a *= op;
        DrawDropShadow(window->sdlRenderer, bg, tt->radius, (UIShadow){
            .offsetX=0, .offsetY=2, .blur=10, .spread=0,
            .color={0,0,0,0.35f * op}
        });
        DrawRoundedRectFill(window->sdlRenderer, bg, bgC, tt->radius);

        SDL_FRect dst = { bx + tt->paddingX, by + tt->paddingY, tw, th };
        SDL_SetTextureAlphaMod(tt->__SDL_textTexture, (Uint8)(op * 255.0f));
        SDL_RenderTexture(window->sdlRenderer, tt->__SDL_textTexture, NULL, &dst);
        SDL_SetTextureAlphaMod(tt->__SDL_textTexture, 255);
        return;
    }

    // ----- UIMenu --------------------------------------------------
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_MENU)) {
        UIMenu* m = (UIMenu*)base;
        if (!m->visible || m->itemCount <= 0) return;

        const float w = (m->itemWidth > 0.0f) ? m->itemWidth : 180.0f;
        const float h = m->paddingY * 2.0f + m->itemCount * m->itemHeight;
        SDL_FRect rect = { m->anchorX, m->anchorY, w, h };

        DrawDropShadow(window->sdlRenderer, rect, m->radius, (UIShadow){
            .offsetX=0, .offsetY=8, .blur=20, .spread=-2,
            .color={0,0,0,0.25f * op}
        });
        UIColor bg = m->bgColor;     bg.a *= op;
        UIColor br = m->borderColor; br.a *= op;
        DrawRoundedRectWithBorder(window->sdlRenderer, rect, bg, m->radius,
                                  (int)m->borderWidth, br);

        TTF_Font* font = (m->fontFamily && m->fontSize > 0.0f)
            ? GetFont(m->fontFamily, m->fontSize) : NULL;
        if (font) TTF_SetFontStyle(font, TTF_STYLE_NORMAL);

        for (int i = 0; i < m->itemCount; i++) {
            SDL_FRect itemR = {
                rect.x + 4.0f,
                rect.y + m->paddingY + i * m->itemHeight,
                rect.w - 8.0f,
                m->itemHeight
            };
            if (i == m->hoverIndex) {
                UIColor hc = m->itemHoverColor; hc.a *= op;
                DrawRoundedRectFill(window->sdlRenderer, itemR, hc, 4.0f);
            }
            if (font && m->labels[i]) {
                UIColor tc = m->textColor; tc.a *= op;
                SDL_Color sc = {(Uint8)tc.r,(Uint8)tc.g,(Uint8)tc.b,255};
                const size_t len = strlen(m->labels[i]);
                SDL_Surface* surf = TTF_RenderText_Blended(font, m->labels[i], len, sc);
                if (surf) {
                    SDL_Texture* tex = SDL_CreateTextureFromSurface(window->sdlRenderer, surf);
                    if (tex) {
                        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
                        float tw = 0, th = 0;
                        SDL_GetTextureSize(tex, &tw, &th);
                        SDL_FRect dst = { itemR.x + m->paddingX,
                                          itemR.y + (m->itemHeight - th) * 0.5f,
                                          tw, th };
                        SDL_RenderTexture(window->sdlRenderer, tex, NULL, &dst);
                        SDL_DestroyTexture(tex);
                    }
                    SDL_DestroySurface(surf);
                }
            }
        }
        return;
    }

    // ----- UIDropdown ---------------------------------------------
    if (UIWidget_TypeIs(base->__widget_type, UI_WIDGET_DROPDOWN)) {
        UIDropdown* d = (UIDropdown*)base;
        if (!el->width || !el->height) return;
        SDL_FRect btn = { el->x, el->y, *el->width, *el->height };

        UIColor bg = d->bgColor;     bg.a *= op;
        UIColor br = d->borderColor; br.a *= op;
        UIColor tc = d->textColor;   tc.a *= op;
        DrawRoundedRectWithBorder(window->sdlRenderer, btn, bg, d->radius,
                                  (int)d->borderWidth, br);

        TTF_Font* font = (d->fontFamily && d->fontSize > 0.0f)
            ? GetFont(d->fontFamily, d->fontSize) : NULL;
        if (font) TTF_SetFontStyle(font, TTF_STYLE_NORMAL);

        const char* label = (d->selectedIndex >= 0 && d->selectedIndex < d->itemCount)
            ? d->labels[d->selectedIndex] : "(select)";
        if (font && label) {
            SDL_Color sc = {(Uint8)tc.r,(Uint8)tc.g,(Uint8)tc.b,255};
            const size_t len = strlen(label);
            SDL_Surface* surf = TTF_RenderText_Blended(font, label, len, sc);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(window->sdlRenderer, surf);
                if (tex) {
                    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
                    float tw = 0, th = 0;
                    SDL_GetTextureSize(tex, &tw, &th);
                    SDL_FRect dst = { btn.x + d->paddingX,
                                      btn.y + (btn.h - th) * 0.5f,
                                      tw, th };
                    SDL_RenderTexture(window->sdlRenderer, tex, NULL, &dst);
                    SDL_DestroyTexture(tex);
                }
                SDL_DestroySurface(surf);
            }
        }

        // Chevron marker.
        const float chevSize = 6.0f;
        const float cxv = btn.x + btn.w - d->paddingX - chevSize;
        const float cyv = btn.y + btn.h * 0.5f - chevSize * 0.3f;
        SDL_FRect chev = { cxv, cyv, chevSize, chevSize };
        DrawRoundedRectFill(window->sdlRenderer, chev, tc, 1.0f);

        // Popup list when open.
        if (d->open && d->itemCount > 0) {
            const float popY = btn.y + btn.h + 4.0f;
            const float popH = d->itemCount * d->popupItemHeight;
            SDL_FRect popR = { btn.x, popY, btn.w, popH };
            DrawDropShadow(window->sdlRenderer, popR, d->radius, (UIShadow){
                .offsetX=0, .offsetY=8, .blur=18, .spread=-2,
                .color={0,0,0,0.25f * op}
            });
            DrawRoundedRectWithBorder(window->sdlRenderer, popR, bg, d->radius,
                                      (int)d->borderWidth, br);

            for (int i = 0; i < d->itemCount; i++) {
                SDL_FRect rowR = { popR.x + 2.0f,
                                   popR.y + i * d->popupItemHeight,
                                   popR.w - 4.0f, d->popupItemHeight };
                if (i == d->hoverIndex) {
                    UIColor hc = d->itemHoverColor; hc.a *= op;
                    DrawRoundedRectFill(window->sdlRenderer, rowR, hc, 4.0f);
                }
                if (font && d->labels[i]) {
                    SDL_Color sc = {(Uint8)tc.r,(Uint8)tc.g,(Uint8)tc.b,255};
                    const size_t len = strlen(d->labels[i]);
                    SDL_Surface* surf = TTF_RenderText_Blended(font, d->labels[i], len, sc);
                    if (surf) {
                        SDL_Texture* tex = SDL_CreateTextureFromSurface(window->sdlRenderer, surf);
                        if (tex) {
                            SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
                            float tw = 0, th = 0;
                            SDL_GetTextureSize(tex, &tw, &th);
                            SDL_FRect dst = { rowR.x + d->paddingX,
                                              rowR.y + (d->popupItemHeight - th) * 0.5f,
                                              tw, th };
                            SDL_RenderTexture(window->sdlRenderer, tex, NULL, &dst);
                            SDL_DestroyTexture(tex);
                        }
                        SDL_DestroySurface(surf);
                    }
                }
            }
        }
        return;
    }
}

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
    if (g_activeWindow == window) g_activeWindow = NULL;

    free(window->title);
    CleanupCircleCache();
    CleanupShadowCache();
    CleanupTaaHistory();
    free(window);
}
