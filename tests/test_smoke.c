// test_smoke.c
//
// Smoke test: opens a window and draws a blue square in the centre.
// What to verify:
//   - The window opens without crashing.
//   - Closing via the X button exits cleanly.
//   - No "Failed to..." messages on the console.

#include <uikit/app.h>

int main(void) {
    UIApp* app = UIApp_Create("Mocida - smoke", 600, 400);
    if (!app) return 1;

    UIRectangle* rect = UIRectangle_Create();
    UIRectangle_SetColor(rect, UI_COLOR_BLUE);

    UIWidget* w = widgcs(rect, 200.0f, 200.0f);
    UIWidget_SetAlignment(w, UIAlignment_Create(
        (UIAlign){ .value = UI_ALIGN_V_CENTER, .target_widget = app->mainWidget },
        (UIAlign){ .value = UI_ALIGN_H_CENTER, .target_widget = app->mainWidget }
    ));

    UIChildren* children = UIChildren_Create(1);
    UIChildren_Add(children, w);
    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, UI_COLOR_WHITE);

    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
