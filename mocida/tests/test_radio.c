// test_radio.c
//
// Exercises UIRadioButton with two independent groups.
//
// What to verify:
//   - Within each group, clicking one radio selects it AND deselects
//     the others in the same group. Only one can be active per group.
//   - The two groups are independent: selecting in group A does not
//     touch the selection of group B.
//   - The inner dot grows from the centre over the configured animMs.
//   - The "Theme" group uses customised colours, border and dot scale.

#include <uikit/app.h>
#include <stdio.h>

static int g_size_group;   // any non-NULL address works as a group ID
static int g_theme_group;

static void on_size(UIRadioButton* r, int selected, void* ud) {
    (void)r;
    if (selected) printf("[size] -> %s\n", (const char*)ud);
}

static void on_theme(UIRadioButton* r, int selected, void* ud) {
    (void)r;
    if (selected) printf("[theme] -> %s\n", (const char*)ud);
}

static void add_label(UIChildren* children, const char* msg,
                      float x, float y, UIColor color) {
    UIText* t = UIText_Create((char*)msg, 14.0f);
    UIText_SetFontFamily(t, UIGetFont("Arial"));
    UIText_SetColor(t, color);
    UIWidget* w = widgc(t);
    UIWidget_SetPosition(w, x, y);
    UIChildren_Add(children, w);
}

static void add_header(UIChildren* children, const char* msg,
                       float x, float y) {
    UIText* t = UIText_Create((char*)msg, 18.0f);
    UIText_SetFontFamily(t, UIGetFont("Arial"));
    UIText_SetColor(t, (UIColor){ 15, 23, 42, 1.0f });
    UIText_SetFontStyle(t, Bold);
    UIWidget* w = widgc(t);
    UIWidget_SetPosition(w, x, y);
    UIChildren_Add(children, w);
}

static UIWidget* make_radio(UIChildren* children, void* group,
                            int initiallySelected, float x, float y,
                            float size, UIRadioCallback cb,
                            const char* userdata) {
    UIRadioButton* r = UIRadio_Create(group, initiallySelected);
    UIRadio_OnChange(r, cb, (void*)userdata);
    UIWidget* w = widgcs(r, size, size);
    UIWidget_SetPosition(w, x, y);
    UIChildren_Add(children, w);
    return w;
}

int main(void) {
    UIApp* app = UIApp_Create("Mocida - radio button", 520, 460);
    if (!app) return 1;
    UISearchFonts();

    UIChildren* children = UIChildren_Create(40);

    // ---- Group 1: T-shirt size (default look) --------------------
    add_header(children, "Size", 30.0f, 24.0f);

    const float baseY = 60.0f;
    const float lineH = 44.0f;
    const float radSize = 26.0f;

    make_radio(children, &g_size_group, 1, 30.0f, baseY + 0 * lineH,
               radSize, on_size, "Small");
    add_label(children, "Small",
              30.0f + radSize + 12.0f, baseY + 0 * lineH + 6.0f,
              (UIColor){ 51, 65, 85, 1.0f });

    make_radio(children, &g_size_group, 0, 30.0f, baseY + 1 * lineH,
               radSize, on_size, "Medium");
    add_label(children, "Medium",
              30.0f + radSize + 12.0f, baseY + 1 * lineH + 6.0f,
              (UIColor){ 51, 65, 85, 1.0f });

    make_radio(children, &g_size_group, 0, 30.0f, baseY + 2 * lineH,
               radSize, on_size, "Large");
    add_label(children, "Large",
              30.0f + radSize + 12.0f, baseY + 2 * lineH + 6.0f,
              (UIColor){ 51, 65, 85, 1.0f });

    make_radio(children, &g_size_group, 0, 30.0f, baseY + 3 * lineH,
               radSize, on_size, "Extra Large");
    add_label(children, "Extra Large",
              30.0f + radSize + 12.0f, baseY + 3 * lineH + 6.0f,
              (UIColor){ 51, 65, 85, 1.0f });

    // ---- Group 2: Theme picker (customised look) ------------------
    add_header(children, "Theme", 280.0f, 24.0f);

    const UIColor lightBox  = (UIColor){ 255, 251, 235, 1.0f };
    const UIColor amberDot  = (UIColor){ 234, 88, 12, 1.0f };
    const UIColor amberRing = (UIColor){ 234, 88, 12, 1.0f };

    UIRadioButton* t1 = UIRadio_Create(&g_theme_group, 1);
    UIRadio_SetColors  (t1, lightBox, amberDot);
    UIRadio_SetBorder  (t1, amberRing, 2.0f);
    UIRadio_SetDotScale(t1, 0.42f);
    UIRadio_SetAnimMs  (t1, 240);
    UIRadio_OnChange   (t1, on_theme, (void*)"Amber");
    UIWidget* t1W = widgcs(t1, 32.0f, 32.0f);
    UIWidget_SetPosition(t1W, 280.0f, baseY + 0 * lineH - 3.0f);
    UIChildren_Add(children, t1W);
    add_label(children, "Amber",
              280.0f + 44.0f, baseY + 0 * lineH + 6.0f,
              (UIColor){ 51, 65, 85, 1.0f });

    UIRadioButton* t2 = UIRadio_Create(&g_theme_group, 0);
    UIRadio_SetColors  (t2, (UIColor){ 240, 253, 250, 1.0f },
                            (UIColor){ 13, 148, 136, 1.0f });
    UIRadio_SetBorder  (t2, (UIColor){ 13, 148, 136, 1.0f }, 2.0f);
    UIRadio_SetDotScale(t2, 0.58f);
    UIRadio_SetAnimMs  (t2, 100);
    UIRadio_OnChange   (t2, on_theme, (void*)"Teal");
    UIWidget* t2W = widgcs(t2, 32.0f, 32.0f);
    UIWidget_SetPosition(t2W, 280.0f, baseY + 1 * lineH - 3.0f);
    UIChildren_Add(children, t2W);
    add_label(children, "Teal",
              280.0f + 44.0f, baseY + 1 * lineH + 6.0f,
              (UIColor){ 51, 65, 85, 1.0f });

    UIRadioButton* t3 = UIRadio_Create(&g_theme_group, 0);
    UIRadio_SetColors  (t3, (UIColor){ 245, 243, 255, 1.0f },
                            (UIColor){ 124, 58, 237, 1.0f });
    UIRadio_SetBorder  (t3, (UIColor){ 124, 58, 237, 1.0f }, 2.0f);
    UIRadio_SetDotScale(t3, 0.50f);
    UIRadio_SetAnimMs  (t3, 0); // instant
    UIRadio_OnChange   (t3, on_theme, (void*)"Violet (instant)");
    UIWidget* t3W = widgcs(t3, 32.0f, 32.0f);
    UIWidget_SetPosition(t3W, 280.0f, baseY + 2 * lineH - 3.0f);
    UIChildren_Add(children, t3W);
    add_label(children, "Violet (instant)",
              280.0f + 44.0f, baseY + 2 * lineH + 6.0f,
              (UIColor){ 51, 65, 85, 1.0f });

    add_label(children, "Each column is its own group.", 30.0f, 280.0f,
              (UIColor){ 100, 116, 139, 1.0f });
    add_label(children, "Click between them - selections don't bleed across.",
              30.0f, 300.0f, (UIColor){ 100, 116, 139, 1.0f });

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, UI_COLOR_WHITE);
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
