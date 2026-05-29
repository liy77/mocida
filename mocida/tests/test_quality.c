// test_quality.c
//
// Automatically cycles through the AA quality levels: LOW -> MEDIUM ->
// HIGH -> ULTRA -> repeat. Each level stays for ~3 seconds.
//
// You should see:
//   - LOW (1 SPP):    aliased edges (worst case, no AA).
//   - MEDIUM (4 SPP): smoothed but with noticeable residual aliasing.
//   - HIGH (16 SPP):  clean edges (default).
//   - ULTRA (64 SPP): practically pristine at close-up.
//
// The label shows the current level as text. The texture cache is
// invalidated on every switch to force regeneration at the new N.

#include <uikit/app.h>
#include <stdio.h>

static UIApp* g_app    = NULL;
static int    g_qIdx   = 2; // starts at HIGH (4)

static const UIRenderQuality g_levels[] = {
    UI_QUALITY_LOW, UI_QUALITY_MEDIUM, UI_QUALITY_HIGH, UI_QUALITY_ULTRA
};
static const char* g_names[] = { "LOW (1 SPP)", "MEDIUM (4 SPP)", "HIGH (16 SPP)", "ULTRA (64 SPP)" };
#define LEVEL_COUNT (int)(sizeof(g_levels) / sizeof(g_levels[0]))

static void OnFps(UIEventData data) {
    static int secs = 0;
    secs++;

    UIWidget* w = (UIWidget*)UIChildren_GetById(data.children, "quality_label");
    if (w) {
        UIText* t = (UIText*)w->data;
        if (t) {
            char buf[160];
            snprintf(buf, sizeof(buf), "Quality: %s   |   %.0f FPS",
                     g_names[g_qIdx], data.framerate.fps);
            UIText_SetText(t, buf);
        }
    }

    if (secs >= 3) {
        secs = 0;
        g_qIdx = (g_qIdx + 1) % LEVEL_COUNT;
        UIApp_SetRenderQuality(g_app, g_levels[g_qIdx]);
    }
}

static void add_circle(UIChildren* children, float x, float y, float size, UIColor color) {
    UIRectangle* r = UIRectangle_Create();
    UIRectangle_SetColor(r, color);
    UIRectangle_SetRadius(r, size * 0.5f);
    UIWidget* w = widgcs(r, size, size);
    UIWidget_SetPosition(w, x, y);
    UIChildren_Add(children, w);
}

int main(void) {
    UIApp* app = UIApp_Create("Mocida - quality cycle", 980, 360);
    if (!app) return 1;
    g_app = app;

    UISearchFonts();

    UIChildren* children = UIChildren_Create(10);

    // Row of circles in various sizes
    const float sizes[]    = { 30, 50, 75, 100, 130, 160 };
    const UIColor colors[] = {
        UI_COLOR_RED, UI_COLOR_GREEN, UI_COLOR_BLUE,
        UI_COLOR_PURPLE, UI_COLOR_ORANGE, UI_COLOR_TEAL
    };
    float cx = 30.0f;
    for (int i = 0; i < 6; i++) {
        add_circle(children, cx, 60.0f, sizes[i], colors[i]);
        cx += sizes[i] + 12.0f;
    }

    // Label
    UIText* label = UIText_Create("Quality: HIGH (16 SPP)", 26);
    UIText_SetFontFamily(label, UIGetFont("Arial"));
    UIText_SetColor(label, UI_COLOR_BLACK);
    UIWidget* lw = widgc(label);
    UIWidget_SetId(lw, "quality_label");
    UIWidget_SetPosition(lw, 30.0f, 270.0f);
    UIChildren_Add(children, lw);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, UI_COLOR_WHITE);
    UIApp_SetRenderQuality(app, g_levels[g_qIdx]);
    UIApp_SetEventCallback(app, UI_EVENT_FRAMERATE_CHANGED, OnFps);

    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
