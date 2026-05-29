/*
 * Mocida debug overlay — see overlay.h.
 *
 * Draws after the regular frame in UIWindow_Render. Uses SDL_RenderRect
 * for borders and SDL_RenderDebugText for the HUD; both ship with SDL3
 * core so no font/asset dependency.
 */

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include <uikit/overlay.h>
#include <uikit/window.h>
#include <uikit/widget.h>
#include <uikit/walker.h>
#include <uikit/profile.h>
#include <uikit/debug.h>

#if defined(MOCIDA_USE_MIMALLOC)
#  include <mimalloc.h>
#endif

static unsigned g_overlayFlags = 0;
static int      g_overlayEnabled = 0;   /* opt-in: off until the app enables it */

void UIDebugOverlay_SetEnabled(int enabled) {
    g_overlayEnabled = enabled ? 1 : 0;
    UI_INFO(UI_CAT_RENDER, "overlay %s", g_overlayEnabled ? "enabled" : "disabled");
}

int UIDebugOverlay_IsEnabled(void) {
    return g_overlayEnabled;
}

void UIDebugOverlay_SetFlags(unsigned flags) {
    g_overlayFlags = flags;
    UI_INFO(UI_CAT_RENDER, "overlay flags = 0x%X", flags);
}

unsigned UIDebugOverlay_GetFlags(void) {
    return g_overlayFlags;
}

void UIDebugOverlay_ToggleFlag(UIOverlayFlag flag) {
    g_overlayFlags ^= (unsigned)flag;
    UI_INFO(UI_CAT_RENDER, "overlay flag 0x%X -> %s",
            (unsigned)flag, (g_overlayFlags & flag) ? "ON" : "OFF");
}

int UIDebugOverlay_HandleScancode(int scancode) {
    /* Hotkeys only respond while the overlay is enabled; otherwise the
     * key passes through untouched to the focused widget. */
    if (!g_overlayEnabled) return 0;
    switch (scancode) {
        case SDL_SCANCODE_F9:  UIDebugOverlay_ToggleFlag(UI_OVERLAY_BOUNDS);  return 1;
        case SDL_SCANCODE_F10: UIDebugOverlay_ToggleFlag(UI_OVERLAY_STATS);   return 1;
        case SDL_SCANCODE_F8:  UIDebugOverlay_ToggleFlag(UI_OVERLAY_HEATMAP); return 1;
        case SDL_SCANCODE_F12:
            if (g_overlayFlags) UIDebugOverlay_SetFlags(0);
            else UIDebugOverlay_SetFlags(UI_OVERLAY_BOUNDS | UI_OVERLAY_STATS);
            return 1;
        default: return 0;
    }
}

/* ---- helpers ---- */

static void color_for_depth(int depth, Uint8* r, Uint8* g, Uint8* b) {
    /* Cycle through a palette so nested widgets are visually distinct. */
    static const Uint8 palette[][3] = {
        { 70, 200, 255}, /* cyan      */
        {255, 180,  60}, /* amber     */
        {180, 255,  80}, /* lime      */
        {255,  90, 200}, /* magenta   */
        { 90, 255, 150}, /* mint      */
        {255, 120, 120}, /* salmon    */
        {180, 140, 255}, /* lavender  */
        {255, 240,  80}  /* yellow    */
    };
    const int n = (int)(sizeof(palette) / sizeof(palette[0]));
    int idx = ((depth % n) + n) % n;
    *r = palette[idx][0]; *g = palette[idx][1]; *b = palette[idx][2];
}

static void draw_widget_rect(SDL_Renderer* r, UIWidget* w, int depth, unsigned flags) {
    if (!w || !w->visible || !w->width || !w->height) return;
    const float ww = *w->width, hh = *w->height;
    SDL_FRect rect = { w->x, w->y, ww, hh };

    if (flags & UI_OVERLAY_HEATMAP) {
        /* Tint by depth: more nested = warmer/redder. */
        Uint8 cr = (Uint8)SDL_min(255, 40 + depth * 30);
        Uint8 cg = (Uint8)SDL_max(40, 200 - depth * 30);
        Uint8 cb = 60;
        SDL_SetRenderDrawColor(r, cr, cg, cb, 60);
        SDL_RenderFillRect(r, &rect);
    }

    if (flags & UI_OVERLAY_BOUNDS) {
        Uint8 br, bg, bb;
        color_for_depth(depth, &br, &bg, &bb);
        SDL_SetRenderDrawColor(r, br, bg, bb, 220);
        SDL_RenderRect(r, &rect);
    }

    if ((flags & UI_OVERLAY_IDS) && w->id) {
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        /* SDL_RenderDebugText uses a built-in 8x8 monospace font. */
        SDL_RenderDebugText(r, rect.x + 2, rect.y + 2, w->id);
    }
}

/** Context threaded through UIChildren_Walk during the debug overlay pass. */
typedef struct {
    SDL_Renderer* r;       /**< Renderer the overlay is drawn into. */
    unsigned      flags;   /**< Bitmask of UI_OVERLAY_* flags. */
} OverlayCtx;

static UIWalkResult overlay_visit(UIWidget* w, int depth, void* user) {
    OverlayCtx* c = (OverlayCtx*)user;
    draw_widget_rect(c->r, w, depth, c->flags);
    return UI_WALK_CONTINUE;
}

static void draw_stats_hud(SDL_Renderer* r, UIWindow* w) {
    UIFrameStats s;
    UIProfile_GetFrameStats(&s);

    size_t memCurMB = 0, memPeakMB = 0;
#if defined(MOCIDA_USE_MIMALLOC)
    size_t elapsed = 0, user = 0, sys = 0, cur = 0, peak = 0;
    size_t pf = 0, pr = 0, pc = 0;
    mi_process_info(&elapsed, &user, &sys, &cur, &peak, &pf, &pr, &pc);
    memCurMB  = cur  / (1024 * 1024);
    memPeakMB = peak / (1024 * 1024);
#else
    (void)memCurMB; (void)memPeakMB;
#endif

    /* Background panel */
    int rw = w->width, rh = w->height;
    (void)rh;
    SDL_FRect panel = { 8.0f, 8.0f, 220.0f, 110.0f };
    SDL_SetRenderDrawColor(r, 0, 0, 0, 180);
    SDL_RenderFillRect(r, &panel);
    SDL_SetRenderDrawColor(r, 80, 200, 255, 220);
    SDL_RenderRect(r, &panel);

    char buf[160];
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    float ty = panel.y + 6.0f;
    const float lh = 12.0f;

    snprintf(buf, sizeof(buf), "frame %.2f ms  (%.1f fps)", s.frameTimeMs, s.fpsSmoothed);
    SDL_RenderDebugText(r, panel.x + 6, ty, buf); ty += lh;

    snprintf(buf, sizeof(buf), "event  %.2f ms", s.eventTimeMs);
    SDL_RenderDebugText(r, panel.x + 6, ty, buf); ty += lh;

    snprintf(buf, sizeof(buf), "layout %.2f ms", s.layoutTimeMs);
    SDL_RenderDebugText(r, panel.x + 6, ty, buf); ty += lh;

    snprintf(buf, sizeof(buf), "render %.2f ms", s.renderTimeMs);
    SDL_RenderDebugText(r, panel.x + 6, ty, buf); ty += lh;

    snprintf(buf, sizeof(buf), "frames %u", (unsigned)s.frameCount);
    SDL_RenderDebugText(r, panel.x + 6, ty, buf); ty += lh;

#if defined(MOCIDA_USE_MIMALLOC)
    snprintf(buf, sizeof(buf), "mem %zu MB (peak %zu)", memCurMB, memPeakMB);
    SDL_RenderDebugText(r, panel.x + 6, ty, buf); ty += lh;
#endif

    snprintf(buf, sizeof(buf), "win %dx%d", rw, w->height);
    SDL_RenderDebugText(r, panel.x + 6, ty, buf); ty += lh;

    /* Hint line at the bottom of the screen */
    SDL_SetRenderDrawColor(r, 180, 180, 180, 200);
    SDL_RenderDebugText(r, 8, (float)w->height - 16,
                        "F9 bounds  F10 stats  F8 heatmap  F12 toggle-all");
}

void UIDebugOverlay_Draw(UIWindow* window) {
    if (!g_overlayEnabled) return;
    if (!window || !window->sdlRenderer) return;

    const unsigned flags = g_overlayFlags;
    if (flags == 0) return;

    SDL_Renderer* r = window->sdlRenderer;

    /* Make sure we draw at 1:1 in the window, regardless of whatever
     * scale the AA pipeline left set. */
    SDL_SetRenderTarget(r, NULL);
    SDL_SetRenderScale(r, 1.0f, 1.0f);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    if (flags & (UI_OVERLAY_BOUNDS | UI_OVERLAY_HEATMAP | UI_OVERLAY_IDS)) {
        OverlayCtx ctx = { r, flags };
        UIChildren_WalkTree(window->children, 0, overlay_visit, &ctx);
    }
    if (flags & UI_OVERLAY_STATS) {
        draw_stats_hud(r, window);
    }
}
