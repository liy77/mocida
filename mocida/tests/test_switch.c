// test_switch.c
//
// Demonstrates the UISwitch widget and the new per-property styling
// setters for the rest of the controls family.
//
// What to verify:
//   - Each switch toggles when clicked. The knob slides between the
//     off (left) and on (right) positions over ~140ms.
//   - The default switch is green when on; the custom-colored switches
//     keep their custom palette.
//   - The customized checkbox / slider / progress bar / spinner all
//     pick up the per-property colours, radii and sizes set below.

#include <uikit/app.h>
#include <stdio.h>

static void on_switch(UISwitch* sw, int on, void* userdata) {
    const char* name = (const char*)userdata;
    printf("[switch %s] now %s\n", name, on ? "ON" : "OFF");
}

static UIWidget* add_label(UIChildren* children, const char* msg,
                           float x, float y) {
    UIText* t = UIText_Create((char*)msg, 14.0f);
    UIText_SetFontFamily(t, UIGetFont("Arial"));
    UIText_SetColor(t, (UIColor){ 51, 65, 85, 1.0f });
    UIWidget* w = widgc(t);
    UIWidget_SetPosition(w, x, y);
    UIChildren_Add(children, w);
    return w;
}

int main(void) {
    UIApp* app = UIApp_Create("Mocida - switch & custom styling", 760, 540);
    if (!app) return 1;
    UISearchFonts();

    UIChildren* children = UIChildren_Create(40);

    // ---------- Switches ------------------------------------------
    add_label(children, "Switch (default)",  30.0f, 30.0f);
    UISwitch* s1 = UISwitch_Create(0);
    UISwitch_OnChange(s1, on_switch, (void*)"default");
    UIWidget* s1W = widgcs(s1, 56.0f, 28.0f);
    UIWidget_SetPosition(s1W, 230.0f, 24.0f);
    UIChildren_Add(children, s1W);

    add_label(children, "Switch (purple/orange)", 30.0f, 70.0f);
    UISwitch* s2 = UISwitch_Create(1);
    UISwitch_SetColors(s2,
        (UIColor){ 100, 116, 139, 1.0f },   // off
        (UIColor){ 168, 85, 247, 1.0f },    // on
        (UIColor){ 254, 215, 170, 1.0f });  // knob
    UISwitch_SetAnimMs(s2, 220);
    UISwitch_OnChange(s2, on_switch, (void*)"purple");
    UIWidget* s2W = widgcs(s2, 72.0f, 36.0f);
    UIWidget_SetPosition(s2W, 230.0f, 64.0f);
    UIChildren_Add(children, s2W);

    add_label(children, "Switch (instant, no anim)", 30.0f, 116.0f);
    UISwitch* s3 = UISwitch_Create(0);
    UISwitch_SetOnColor (s3, (UIColor){ 234, 88, 12, 1.0f });
    UISwitch_SetAnimMs(s3, 0);
    UIWidget* s3W = widgcs(s3, 56.0f, 28.0f);
    UIWidget_SetPosition(s3W, 230.0f, 110.0f);
    UIChildren_Add(children, s3W);

    // ---------- Checkbox -------------------------------------------
    add_label(children, "Checkbox (per-prop colors + radius)", 30.0f, 170.0f);
    UICheckbox* cb = UICheckbox_Create(1);
    UICheckbox_SetBoxColor   (cb, (UIColor){ 254, 226, 226, 1.0f });
    UICheckbox_SetCheckColor (cb, (UIColor){ 220, 38, 38, 1.0f });
    UICheckbox_SetBorder     (cb, (UIColor){ 220, 38, 38, 1.0f }, 2.0f);
    UICheckbox_SetRadius     (cb, 12.0f);
    UIWidget* cbW = widgcs(cb, 28.0f, 28.0f);
    UIWidget_SetPosition(cbW, 320.0f, 164.0f);
    UIChildren_Add(children, cbW);

    // ---------- Slider ---------------------------------------------
    add_label(children, "Slider (thicker track + bigger knob)", 30.0f, 220.0f);
    UISlider* sl = UISlider_Create(0.0f, 100.0f, 35.0f);
    UISlider_SetTrackColor(sl, (UIColor){ 226, 232, 240, 1.0f });
    UISlider_SetFillColor (sl, (UIColor){ 16, 185, 129, 1.0f });
    UISlider_SetKnobColor (sl, (UIColor){ 6, 95, 70, 1.0f });
    UISlider_SetTrackHeight(sl, 12.0f);
    UISlider_SetKnobRadius (sl, 14.0f);
    UIWidget* slW = widgcs(sl, 300.0f, 30.0f);
    UIWidget_SetPosition(slW, 320.0f, 218.0f);
    UIChildren_Add(children, slW);

    // ---------- Progress bar ---------------------------------------
    add_label(children, "ProgressBar (radius 8, custom palette)", 30.0f, 280.0f);
    UIProgressBar* pb = UIProgressBar_Create(0.66f);
    UIProgressBar_SetTrackColor(pb, (UIColor){ 254, 242, 242, 1.0f });
    UIProgressBar_SetFillColor (pb, (UIColor){ 244, 63, 94, 1.0f });
    UIProgressBar_SetRadius    (pb, 8.0f);
    UIWidget* pbW = widgcs(pb, 300.0f, 16.0f);
    UIWidget_SetPosition(pbW, 320.0f, 280.0f);
    UIChildren_Add(children, pbW);

    // ---------- Spinner --------------------------------------------
    add_label(children, "Spinner (fast)", 30.0f, 340.0f);
    UISpinner* sp1 = UISpinner_Create(18.0f);
    UISpinner_SetColor    (sp1, (UIColor){ 59, 130, 246, 1.0f });
    UISpinner_SetThickness(sp1, 4.0f);
    UISpinner_SetSpeed    (sp1, 18.0f); // rad/s
    UIWidget* sp1W = widgcs(sp1, 50.0f, 50.0f);
    UIWidget_SetPosition(sp1W, 230.0f, 328.0f);
    UIChildren_Add(children, sp1W);

    add_label(children, "Spinner (slow + orange)", 30.0f, 400.0f);
    UISpinner* sp2 = UISpinner_Create(22.0f);
    UISpinner_SetColor    (sp2, (UIColor){ 249, 115, 22, 1.0f });
    UISpinner_SetThickness(sp2, 6.0f);
    UISpinner_SetSpeed    (sp2, 2.5f);
    UIWidget* sp2W = widgcs(sp2, 60.0f, 60.0f);
    UIWidget_SetPosition(sp2W, 230.0f, 388.0f);
    UIChildren_Add(children, sp2W);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, UI_COLOR_WHITE);
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
