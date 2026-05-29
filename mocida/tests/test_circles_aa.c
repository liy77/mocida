// test_circles_aa.c
//
// "Circles" (rectangles with radius = w/2) at several sizes.
// What to verify:
//   - The edges must be SMOOTH, free of pixel staircasing.
//   - This works because GetCachedCircleTexture rasterizes each circle
//     via analytic coverage in CPU (see src/uikit/window.c).
//   - If you spot any staircase artefact on the edges, something has
//     regressed.

#include <uikit/app.h>

int main(void) {
    UIApp* app = UIApp_Create("Mocida - circle AA", 980, 220);
    if (!app) return 1;

    const float sizes[] = { 20.0f, 40.0f, 60.0f, 80.0f, 100.0f, 120.0f, 140.0f, 160.0f };
    const int   N       = (int)(sizeof(sizes) / sizeof(sizes[0]));
    const UIColor colors[] = {
        UI_COLOR_RED, UI_COLOR_GREEN, UI_COLOR_BLUE, UI_COLOR_PURPLE,
        UI_COLOR_TEAL, UI_COLOR_ORANGE, UI_COLOR_NAVY, UI_COLOR_BLACK
    };

    UIChildren* children = UIChildren_Create(N);

    float x = 20.0f;
    for (int i = 0; i < N; i++) {
        UIRectangle* r = UIRectangle_Create();
        UIRectangle_SetColor(r, colors[i]);
        UIRectangle_SetRadius(r, sizes[i] * 0.5f);

        UIWidget* w = widgcs(r, sizes[i], sizes[i]);
        UIWidget_SetPosition(w, x, 110.0f - sizes[i] * 0.5f);
        UIChildren_Add(children, w);

        x += sizes[i] + 10.0f;
    }

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, UI_COLOR_WHITE);
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
