// window_draw.inc — #included by window.c (SAME translation unit).
//
// Low-level SDL drawing layer split out of window.c for readability:
//   * batched-rect flush + the font cache (FlushRenderBatch, GetFont)
//   * analytic-coverage circle + drop-shadow caches and rasterizers
//   * rounded-rect / circle / border fills (Draw*)
//   * the full-frame AA pipeline (MSAA settings, FXAA, TAA passes)
//
// It is NOT a standalone unit — it relies on the file-level caches / globals
// declared at the top of window.c. Kept as a `.inc` so CMake's `*.c` glob does
// not compile it twice.

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
    if (!path) return NULL;
    // Resolve a mocida:// virtual font path to its real file (pass-through
    // for ordinary paths). Cached entries below key on the resolved path.
    path = UIApp_ResolveBundle(path);
    if (!path) return NULL;
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

    // No radius: plain rectangle. Flush immediately so the fill is
    // committed to the target now — container backgrounds (Stack/Rectangle
    // with radius 0) draw their fill before their children, and a queued
    // rect left in the batch would otherwise flush at frame end and paint
    // over those children. The rounded path below flushes for the same
    // reason (see FlushRenderBatch after the 5 edge rects).
    if (radius <= 0.5f) {
        BatchRect(rend, &rect, sdlColor);
        FlushRenderBatch(rend);
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
