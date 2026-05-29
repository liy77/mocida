// test_controls.c
//
// Showcases the new control widgets:
//   - UICheckbox (click to toggle)
//   - UISlider (drag to scrub)
//   - UIProgressBar (driven from the slider's value + an indeterminate one)
//   - UISpinner (always animating)
//   - UIAnim_To (the indeterminate bar tween scales another rectangle on click)
//   - UIApp_GetMemoryStats (printed in the title bar once per second)

#include <uikit/app.h>
#include <stdio.h>

static UIApp* g_app = NULL;
static UISlider* g_slider = NULL;
static UIProgressBar* g_pb = NULL;
static UIRectangle* g_animRect = NULL;
static UIWidget*    g_animRectW = NULL;

// Slider drives the determinate progress bar live.
static void OnSliderChange(UISlider* s, float value, void* ud) {
    (void)ud;
    if (g_pb) {
        const float t = (value - s->minValue) / (s->maxValue - s->minValue);
        UIProgressBar_SetValue(g_pb, t);
    }
}

// Checkbox toggles between indeterminate and determinate mode.
static void OnCheckboxChange(UICheckbox* c, int checked, void* ud) {
    (void)c; (void)ud;
    if (g_pb) UIProgressBar_SetIndeterminate(g_pb, checked);
}

// Once-a-second snapshot pushed to the window title.
static void OnFpsTick(UIEventData data) {
    if (!g_app) return;
    UIMemoryStats st = {0};
    UIApp_GetMemoryStats(g_app, &st);
    char title[160];
    snprintf(title, sizeof(title),
             "controls - %.0f FPS - mem live %.1f KB peak %.1f KB",
             data.framerate.fps,
             st.current / 1024.0,
             st.peak    / 1024.0);
    UIApp_SetWindowTitle(g_app, title);
}

// Click the "Animate" button to tween the green square's width.
static void OnAnimateClick(UIButton* b, void* ud) {
    (void)b; (void)ud;
    if (g_animRectW && g_animRectW->width) {
        const float current = *g_animRectW->width;
        const float target  = (current > 200.0f) ? 60.0f : 320.0f;
        UIAnim_To(g_animRectW->width, target, 500,
                  UI_EASE_OUT_BACK, NULL, NULL);
    }
}

int main(void) {
    UIApp* app = UIApp_Create("controls", 880, 560);
    if (!app) return 1;
    g_app = app;

    UISearchFonts();
    UIApp_SetWindowIcon(app, "assets/logo.svg");
    UIApp_SetRenderQuality(app, UI_QUALITY_HIGH);
    UIApp_SetEventCallback(app, UI_EVENT_FRAMERATE_CHANGED, OnFpsTick);

    UIChildren* children = UIChildren_Create(16);

    // Slider --------------------------------------------------------
    UISlider* slider = UISlider_Create(0.0f, 100.0f, 40.0f);
    UISlider_OnChange(slider, OnSliderChange, NULL);
    UIWidget* sliderW = widgcs(slider, 360.0f, 30.0f);
    UIWidget_SetPosition(sliderW, 40.0f, 60.0f);
    UIChildren_Add(children, sliderW);
    g_slider = slider;

    // Progress bar (driven by the slider) ---------------------------
    UIProgressBar* pb = UIProgressBar_Create(0.4f);
    UIWidget* pbW = widgcs(pb, 360.0f, 14.0f);
    UIWidget_SetPosition(pbW, 40.0f, 120.0f);
    UIChildren_Add(children, pbW);
    g_pb = pb;

    // Checkbox to toggle indeterminate ------------------------------
    UICheckbox* cb = UICheckbox_Create(0);
    UICheckbox_OnChange(cb, OnCheckboxChange, NULL);
    UIWidget* cbW = widgcs(cb, 28.0f, 28.0f);
    UIWidget_SetPosition(cbW, 40.0f, 170.0f);
    UIChildren_Add(children, cbW);

    UIText* cbLabel = UIText_Create("Indeterminate progress", 16.0f);
    UIText_SetFontFamily(cbLabel, UIGetFont("Arial"));
    UIText_SetColor(cbLabel, (UIColor){ 71, 85, 105, 1.0f });
    UIWidget* cbLabelW = widgc(cbLabel);
    UIWidget_SetPosition(cbLabelW, 78.0f, 175.0f);
    UIChildren_Add(children, cbLabelW);

    // Spinner -------------------------------------------------------
    UISpinner* sp = UISpinner_Create(20.0f);
    UISpinner_SetColor(sp, (UIColor){ 168, 85, 247, 1.0f });
    UIWidget* spW = widgcs(sp, 60.0f, 60.0f);
    UIWidget_SetPosition(spW, 460.0f, 50.0f);
    UIChildren_Add(children, spW);

    UIText* spLabel = UIText_Create("UISpinner (driven by SDL_GetTicks)", 14.0f);
    UIText_SetFontFamily(spLabel, UIGetFont("Arial"));
    UIText_SetColor(spLabel, (UIColor){ 100, 116, 139, 1.0f });
    UIWidget* spLabelW = widgc(spLabel);
    UIWidget_SetPosition(spLabelW, 530.0f, 70.0f);
    UIChildren_Add(children, spLabelW);

    // Animated rectangle + button -----------------------------------
    g_animRect = UIRectangle_Create();
    UIRectangle_SetColor(g_animRect, (UIColor){ 34, 197, 94, 1.0f });
    UIRectangle_SetRadius(g_animRect, 10.0f);
    g_animRectW = widgcs(g_animRect, 60.0f, 60.0f);
    UIWidget_SetPosition(g_animRectW, 460.0f, 250.0f);
    UIChildren_Add(children, g_animRectW);

    UIButton* animBtn = UIButton_Create("Tween width (back-ease)", 16.0f);
    UIButton_SetFontFamily(animBtn, UIGetFont("Arial"));
    UIButton_SetRadius(animBtn, 8.0f);
    UIButton_SetColors(animBtn, (UIColor){ 59, 130, 246, 1.0f }, UI_COLOR_WHITE);
    UIButton_OnClick(animBtn, OnAnimateClick, NULL);
    UIWidget* animBtnW = widgcs(animBtn, 260.0f, 44.0f);
    UIWidget_SetPosition(animBtnW, 460.0f, 360.0f);
    UIChildren_Add(children, animBtnW);

    // Opacity demo: same blue rect drawn twice with different alpha.
    for (int i = 0; i < 4; i++) {
        UIRectangle* r = UIRectangle_Create();
        UIRectangle_SetColor(r, (UIColor){ 59, 130, 246, 1.0f });
        UIRectangle_SetRadius(r, 10.0f);
        UIWidget* w = widgcs(r, 60.0f, 60.0f);
        UIWidget_SetPosition(w, 40.0f + i * 80.0f, 250.0f);
        w->opacity = 1.0f - i * 0.22f; // 1.0, 0.78, 0.56, 0.34
        UIChildren_Add(children, w);
    }

    UIText* hint = UIText_Create(
        "Drag the slider, toggle the checkbox, click the button to tween.",
        14.0f);
    UIText_SetFontFamily(hint, UIGetFont("Arial"));
    UIText_SetColor(hint, (UIColor){ 100, 116, 139, 1.0f });
    UIWidget* hintW = widgc(hint);
    UIWidget_SetPosition(hintW, 40.0f, 500.0f);
    UIChildren_Add(children, hintW);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, (UIColor){ 248, 250, 252, 1.0f });

    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
