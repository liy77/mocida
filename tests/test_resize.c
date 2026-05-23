// test_resize.c
//
// Exercises the new window-resize layout pass:
//   - A blue pill anchored top-center.
//   - A grey pill centered.
//   - A green pill anchored bottom-right.
//
// What to verify:
//   - The window opens with three pills laid out as above.
//   - Drag the right/bottom edges to make the window bigger and
//     smaller. Each pill should move to keep its anchor relative to
//     the new window size.

#include <uikit/app.h>

static UIWidget* anchor_pill(UIChildren* children, UIWidget* root,
                             UIColor bg, float w, float h,
                             uint8_t valign, uint8_t halign) {
    UIRectangle* r = UIRectangle_Create();
    UIRectangle_SetColor(r, bg);
    UIRectangle_SetRadius(r, h * 0.5f);
    UIWidget* widget = widgcs(r, w, h);
    UIChildren_Add(children, widget);

    UIWidget_SetAlignment(widget, UIAlignment_Create(
        (UIAlign){ .value = valign, .target_widget = root },
        (UIAlign){ .value = halign, .target_widget = root }
    ));
    return widget;
}

int main(void) {
    UIApp* app = UIApp_Create("Mocida - resize relayout", 700, 500);
    if (!app) return 1;
    UISearchFonts();

    UIChildren* children = UIChildren_Create(8);

    // The resize handler in app.c keeps app->mainWidget's width/height
    // in sync with the SDL window, so anchoring against it makes the
    // pills move when the window is resized.
    UIWidget* root = app->mainWidget;

    anchor_pill(children, root, (UIColor){ 59, 130, 246, 1.0f },
                220.0f, 44.0f,
                UI_ALIGN_V_TOP,    UI_ALIGN_H_CENTER);
    anchor_pill(children, root, (UIColor){ 100, 116, 139, 1.0f },
                220.0f, 44.0f,
                UI_ALIGN_V_CENTER, UI_ALIGN_H_CENTER);
    anchor_pill(children, root, (UIColor){ 34, 197, 94, 1.0f },
                220.0f, 44.0f,
                UI_ALIGN_V_BOTTOM, UI_ALIGN_H_RIGHT);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, (UIColor){ 248, 250, 252, 1.0f });
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
