// test_aa.c
//
// Cycles through the full-frame AA pipelines exposed by Mocida so the
// difference is easy to spot side-by-side. The scene mixes geometry
// likely to alias (diagonals, circles, small text on a coloured
// background) and stays mostly static, which suits FXAA / TAA well.
//
// Modes cycled:
//   1. COVERAGE  - analytic-coverage AA only (default).
//   2. SSAA_2X   - 2x supersampling on the whole frame, downscaled.
//   3. SSAA_4X   - 4x supersampling (very smooth, very expensive).
//   4. FXAA      - coverage + CPU edge-blur pass.
//   5. TAA       - coverage + temporal accumulation with the previous
//                  frame (use the moving circle to spot ghosting).
//
// One mode at a time, ~3s per mode. The label in the top-left tells
// you which one is active plus the live FPS, so you can compare cost.

#include <uikit/app.h>
#include <math.h>
#include <stdio.h>

static UIApp* g_app    = NULL;
static int    g_modeIx = 0;

static const UIAAMode g_modes[] = {
    UI_AA_COVERAGE, UI_AA_SSAA_2X, UI_AA_SSAA_4X, UI_AA_FXAA, UI_AA_TAA
};
static const char* g_modeNames[] = {
    "COVERAGE", "SSAA 2x", "SSAA 4x", "FXAA", "TAA"
};
#define MODE_COUNT (int)(sizeof(g_modes) / sizeof(g_modes[0]))

static void OnFps(UIEventData data) {
    static int secs = 0;
    secs++;

    UIWidget* w = (UIWidget*)UIChildren_GetById(data.children, "aa_label");
    if (w) {
        UIText* t = (UIText*)w->data;
        if (t) {
            char buf[160];
            snprintf(buf, sizeof(buf), "AA: %s   |   %.0f FPS",
                     g_modeNames[g_modeIx], data.framerate.fps);
            UIText_SetText(t, buf);
        }
    }

    if (secs >= 3) {
        secs = 0;
        g_modeIx = (g_modeIx + 1) % MODE_COUNT;
        UIApp_SetAAMode(g_app, g_modes[g_modeIx]);
    }
}

static UIWidget* g_circle = NULL;
static float g_t = 0.0f;

int main(void) {
    UIApp* app = UIApp_Create("Mocida - AA modes", 920, 520);
    if (!app) return 1;
    g_app = app;

    UISearchFonts();

    UIChildren* children = UIChildren_Create(16);

    // Background card with strong contrast for FXAA to chew on
    UIRectangle* card = UIRectangle_Create();
    UIRectangle_SetColor(card, (UIColor){ 17, 24, 39, 1.0f });
    UIRectangle_SetRadius(card, 18.0f);
    UIWidget* cardW = widgcs(card, 860.0f, 360.0f);
    UIWidget_SetPosition(cardW, 30.0f, 80.0f);
    UIChildren_Add(children, cardW);

    // Diagonal stripes (rectangles of varying radius) - aliasing magnets.
    for (int i = 0; i < 5; i++) {
        UIRectangle* r = UIRectangle_Create();
        UIRectangle_SetColor(r, (i % 2)
            ? (UIColor){ 251, 191, 36, 1.0f }
            : (UIColor){ 239, 68, 68, 1.0f });
        UIRectangle_SetRadius(r, 4.0f + i * 6.0f);
        UIWidget* rw = widgcs(r, 110.0f, 110.0f);
        UIWidget_SetPosition(rw, 60.0f + i * 130.0f, 110.0f + i * 12.0f);
        UIChildren_Add(children, rw);
    }

    // Moving circle - hover over to see TAA ghosting (a real artefact
    // of TAA without motion vectors).
    UIRectangle* dot = UIRectangle_Create();
    UIRectangle_SetColor(dot, (UIColor){ 34, 211, 238, 1.0f });
    UIRectangle_SetRadius(dot, 40.0f);
    UIWidget* dotW = widgcs(dot, 80.0f, 80.0f);
    UIWidget_SetPosition(dotW, 200.0f, 320.0f);
    UIChildren_Add(children, dotW);
    g_circle = dotW;

    // Label (overlay on top of card).
    UIText* label = UIText_Create("AA: COVERAGE   |   0 FPS", 22.0f);
    UIText_SetFontFamily(label, UIGetFont("Arial"));
    UIText_SetColor(label, UI_COLOR_WHITE);
    UIWidget* lw = widgc(label);
    UIWidget_SetId(lw, "aa_label");
    UIWidget_SetPosition(lw, 50.0f, 460.0f);
    UIChildren_Add(children, lw);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, (UIColor){ 226, 232, 240, 1.0f });
    UIApp_SetRenderQuality(app, UI_QUALITY_HIGH);
    UIApp_SetAAMode(app, g_modes[g_modeIx]);
    UIApp_SetTAABlend(app, 0.4f); // a bit more history for visible smoothing
    UIApp_SetEventCallback(app, UI_EVENT_FRAMERATE_CHANGED, OnFps);

    UIApp_ShowWindow(app);

    // Custom loop: animate the moving circle so TAA ghosting is obvious.
    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 frameStart = SDL_GetPerformanceCounter();
    SDL_Event e;
    while (app->window->visible) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) app->window->visible = 0;
        }

        g_t += 0.016f;
        if (g_circle) {
            g_circle->x = 440.0f + sinf(g_t * 1.5f) * 220.0f;
            g_circle->y = 320.0f + cosf(g_t * 1.2f) * 30.0f;
        }

        UIWindow_Render(app->window);

        if (app->targetFps > 0) {
            const Uint64 nsPerSec = 1000000000ULL;
            const Uint64 targetNs = nsPerSec / (Uint64)app->targetFps;
            const Uint64 frameEnd = SDL_GetPerformanceCounter();
            const Uint64 elapsedNs = (frameEnd - frameStart) * nsPerSec / freq;
            if (elapsedNs < targetNs) SDL_DelayNS(targetNs - elapsedNs);
        }
        frameStart = SDL_GetPerformanceCounter();
    }
    UIApp_Destroy(app);
    return 0;
}
