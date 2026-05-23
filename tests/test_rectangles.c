// test_rectangles.c
//
// Renders several rectangles with combinations of color, radius and border.
// What to verify:
//   - Sharp corners when radius = 0.
//   - Rounded corners without distortion (the old "squashed circle"
//     bug is fixed - each corner now draws a proper quadrant).
//   - Borders are concentric and have the correct thickness.

#include <uikit/app.h>

static void add_rect(UIChildren* children,
                     float x, float y, float w, float h,
                     UIColor color, float radius,
                     float borderW, UIColor borderColor) {
    UIRectangle* r = UIRectangle_Create();
    UIRectangle_SetColor(r, color);
    UIRectangle_SetRadius(r, radius);
    if (borderW > 0.0f) {
        UIRectangle_SetBorderWidth(r, borderW);
        UIRectangle_SetBorderColor(r, borderColor);
    }
    UIWidget* widget = widgcs(r, w, h);
    UIWidget_SetPosition(widget, x, y);
    UIChildren_Add(children, widget);
}

int main(void) {
    UIApp* app = UIApp_Create("Mocida - rectangles", 800, 500);
    if (!app) return 1;

    UIChildren* children = UIChildren_Create(8);

    // Row 1: varying radius
    add_rect(children, 30,  30, 150, 100, UI_COLOR_RED,    0,  0, UI_COLOR_BLACK);
    add_rect(children, 210, 30, 150, 100, UI_COLOR_GREEN, 10,  0, UI_COLOR_BLACK);
    add_rect(children, 390, 30, 150, 100, UI_COLOR_BLUE,  25,  0, UI_COLOR_BLACK);
    add_rect(children, 570, 30, 150, 100, UI_COLOR_PURPLE, 50, 0, UI_COLOR_BLACK);

    // Row 2: border + pill + square
    add_rect(children, 30,  180, 150, 100, UI_COLOR_YELLOW, 15,  5, UI_COLOR_BLACK);
    add_rect(children, 210, 180, 200, 100, UI_COLOR_TEAL,   50,  0, UI_COLOR_BLACK);
    add_rect(children, 440, 180, 100, 100, UI_COLOR_ORANGE,  8,  0, UI_COLOR_BLACK);
    add_rect(children, 570, 180, 150, 100, UI_COLOR_GRAY,   20,  3, UI_COLOR_NAVY);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, UI_COLOR_WHITE);
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
