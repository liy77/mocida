// test_checkbox_anim.c
//
// Exercises the new check-mark animation on UICheckbox.
//
// What to verify:
//   - Clicking a checkbox grows the inner shape from the centre over
//     ~animMs milliseconds (not an instant snap).
//   - Unchecking shrinks it back. Both ease toward the target so the
//     animation feels smooth on any framerate.
//   - The "Instant" checkbox (animMs=0) still snaps the way the old
//     behaviour did, so per-widget anim setting is honored.

#include <uikit/app.h>
#include <stdio.h>

static void on_change(UICheckbox* c, int checked, void* ud) {
    (void)c;
    printf("[%s] %s\n", (const char*)ud, checked ? "ON" : "OFF");
}

static void add_label(UIChildren* children, const char* msg,
                      float x, float y) {
    UIText* t = UIText_Create((char*)msg, 14.0f);
    UIText_SetFontFamily(t, UIGetFont("Arial"));
    UIText_SetColor(t, (UIColor){ 51, 65, 85, 1.0f });
    UIWidget* w = widgc(t);
    UIWidget_SetPosition(w, x, y);
    UIChildren_Add(children, w);
}

static UIWidget* add_checkbox(UIChildren* children, int initialChecked,
                              int animMs, UIColor box, UIColor check,
                              float x, float y, float size,
                              UICheckboxCallback cb, const char* userdata) {
    UICheckbox* c = UICheckbox_Create(initialChecked);
    UICheckbox_SetBoxColor  (c, box);
    UICheckbox_SetCheckColor(c, check);
    UICheckbox_SetBorder    (c, (UIColor){ 148, 163, 184, 1.0f }, 1.5f);
    UICheckbox_SetRadius    (c, size * 0.2f);
    UICheckbox_SetAnimMs    (c, animMs);
    UICheckbox_OnChange     (c, cb, (void*)userdata);
    UIWidget* w = widgcs(c, size, size);
    UIWidget_SetPosition(w, x, y);
    UIChildren_Add(children, w);
    return w;
}

int main(void) {
    UIApp* app = UIApp_Create("Mocida - checkbox animation", 540, 460);
    if (!app) return 1;
    UISearchFonts();

    UIChildren* children = UIChildren_Create(20);

    const UIColor lightBg = (UIColor){ 241, 245, 249, 1.0f };

    // Default (160ms ease) - off initially.
    add_label(children, "Default (160ms ease)", 80.0f, 50.0f);
    add_checkbox(children, 0, 160, lightBg,
                 (UIColor){ 34, 197, 94, 1.0f },
                 30.0f, 44.0f, 32.0f,
                 on_change, "default");

    // Slow ease, big square - shows the scale+fade clearly.
    add_label(children, "Slow (400ms) + bigger", 80.0f, 110.0f);
    add_checkbox(children, 0, 400, lightBg,
                 (UIColor){ 168, 85, 247, 1.0f },
                 30.0f, 104.0f, 32.0f,
                 on_change, "slow");

    // Fast ease.
    add_label(children, "Fast (60ms)", 80.0f, 170.0f);
    add_checkbox(children, 1, 60, lightBg,
                 (UIColor){ 244, 63, 94, 1.0f },
                 30.0f, 164.0f, 32.0f,
                 on_change, "fast");

    // Disabled animation - snaps like the old behaviour.
    add_label(children, "Instant (animMs = 0)", 80.0f, 230.0f);
    add_checkbox(children, 0, 0, lightBg,
                 (UIColor){ 234, 88, 12, 1.0f },
                 30.0f, 224.0f, 32.0f,
                 on_change, "instant");

    // A row of three with the same anim - useful to compare the
    // staggered visual when you click them quickly in a row.
    add_label(children, "Row (click them in sequence)", 30.0f, 300.0f);
    add_checkbox(children, 0, 240, lightBg,
                 (UIColor){ 59, 130, 246, 1.0f },
                 30.0f,  324.0f, 40.0f, on_change, "row1");
    add_checkbox(children, 0, 240, lightBg,
                 (UIColor){ 59, 130, 246, 1.0f },
                 90.0f,  324.0f, 40.0f, on_change, "row2");
    add_checkbox(children, 0, 240, lightBg,
                 (UIColor){ 59, 130, 246, 1.0f },
                 150.0f, 324.0f, 40.0f, on_change, "row3");

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, UI_COLOR_WHITE);
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
