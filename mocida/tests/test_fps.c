// test_fps.c
//
// Verifies the configurable FPS cap.
// The label in the window automatically cycles through 30 / 60 / 120 /
// UNLIMITED (one change every ~3 seconds).
//
// What to verify:
//   - In 30 mode: "Measured" hovers near 30.
//   - In 60 mode: near 60.
//   - In 120 mode: near 120 (capped by the monitor if the driver
//     forces VSync - it isn't here).
//   - In UNLIMITED mode: jumps to hundreds or thousands of FPS,
//     showing the unlock works.

#include <uikit/app.h>
#include <stdio.h>

static UIApp* g_app = NULL;
static int    g_modeIdx = 1; // starts at 60

static const int   g_fpsModes[]  = { 30, 60, 120, UI_FPS_UNLIMITED };
static const char* g_fpsLabels[] = { "30", "60", "120", "UNLIMITED"  };
#define MODE_COUNT (int)(sizeof(g_fpsModes) / sizeof(g_fpsModes[0]))

// Fired once per second by UIWindow_Render (FPS computation).
static void OnFps(UIEventData data) {
    static int secondsAtMode = 0;
    secondsAtMode++;

    UIWidget* label_widget = (UIWidget*)UIChildren_GetById(data.children, "fps_label");
    if (label_widget) {
        UIText* t = (UIText*)label_widget->data;
        if (t) {
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "Target: %s   Measured: %.0f FPS",
                     g_fpsLabels[g_modeIdx], data.framerate.fps);
            UIText_SetText(t, buf);
        }
    }

    if (secondsAtMode >= 3) {
        secondsAtMode = 0;
        g_modeIdx = (g_modeIdx + 1) % MODE_COUNT;
        UIApp_SetTargetFPS(g_app, g_fpsModes[g_modeIdx]);
    }
}

int main(void) {
    UIApp* app = UIApp_Create("Mocida - FPS lock", 760, 200);
    if (!app) return 1;
    g_app = app;

    UISearchFonts();

    UIText* label = UIText_Create("Target: 60   Measured: ...", 28);
    UIText_SetFontFamily(label, UIGetFont("Arial"));
    UIText_SetColor(label, UI_COLOR_BLACK);

    UIWidget* lw = widgc(label);
    UIWidget_SetId(lw, "fps_label");
    UIWidget_SetPosition(lw, 20.0f, 80.0f);

    UIChildren* children = UIChildren_Create(1);
    UIChildren_Add(children, lw);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, UI_COLOR_WHITE);
    UIApp_SetEventCallback(app, UI_EVENT_FRAMERATE_CHANGED, OnFps);
    UIApp_SetTargetFPS(app, g_fpsModes[g_modeIdx]);

    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
