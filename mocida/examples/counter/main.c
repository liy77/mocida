// examples/counter.c
//
// A counter with - / + buttons. Demonstrates simple state, a responsive
// layout that fills the window width, and safe-area-aware top spacing so
// it looks right on a phone with a notch and on a desktop window alike.

#include <uikit/app.h>
#include <stdio.h>

static UIApp*    g_app   = NULL;
static UIWidget* g_value = NULL;   // the number label
static UIWidget* g_minus = NULL;
static UIWidget* g_plus  = NULL;
static int       g_count = 0;

static void refresh(void) {
    if (g_value && g_value->data) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", g_count);
        UIText_SetText((UIText*)g_value->data, buf);
    }
}

static void on_minus(UIButton* b, void* ud) { (void)b; (void)ud; g_count--; refresh(); }
static void on_plus (UIButton* b, void* ud) { (void)b; (void)ud; g_count++; refresh(); }

static void on_resize(int w, int h, void* ud) {
    (void)ud;
    if (w <= 0 || h <= 0) return;
    UIScreenInsets safe = UIScreen_GetSafeArea();
    const float padL = 20.0f + (float)safe.left;
    const float padR = 20.0f + (float)safe.right;
    const float top  = 30.0f + (float)safe.top;
    const float W    = (float)w - padL - padR;
    const float gap  = 16.0f;

    // Big value label centered-ish near the top.
    if (g_value) UIWidget_SetPosition(g_value, padL, top);

    // Two equal buttons filling the width on a row below.
    const float bw = (W - gap) * 0.5f;
    const float by = top + 80.0f;
    const float bh = 56.0f;
    if (g_minus) { UIWidget_SetSize(g_minus, bw, bh); UIWidget_SetPosition(g_minus, padL, by); }
    if (g_plus)  { UIWidget_SetSize(g_plus,  bw, bh); UIWidget_SetPosition(g_plus,  padL + bw + gap, by); }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    g_app = UIApp_Create("Mocida Counter", 420, 280);
    if (!g_app) return 1;
    UISearchFonts();

    UIChildren* children = UIChildren_Create(4);

    UIText* v = UIText_Create("0", 48.0f);
    UIText_SetFontFamily(v, UIGetFont("Arial"));
    UIText_SetColor(v, (UIColor){ 15, 23, 42, 1.0f });
    g_value = widgc(v);
    UIChildren_Add(children, g_value);

    UIButton* minus = UIButton_Create("-", 28.0f);
    UIButton_SetFontFamily(minus, UIGetFont("Arial"));
    UIButton_SetRadius(minus, 12.0f);
    UIButton_SetColors(minus, (UIColor){ 239, 68, 68, 1.0f }, UI_COLOR_WHITE);
    UIButton_OnClick(minus, on_minus, NULL);
    g_minus = widgcs(minus, 100.0f, 56.0f);
    UIChildren_Add(children, g_minus);

    UIButton* plus = UIButton_Create("+", 28.0f);
    UIButton_SetFontFamily(plus, UIGetFont("Arial"));
    UIButton_SetRadius(plus, 12.0f);
    UIButton_SetColors(plus, (UIColor){ 34, 197, 94, 1.0f }, UI_COLOR_WHITE);
    UIButton_OnClick(plus, on_plus, NULL);
    g_plus = widgcs(plus, 100.0f, 56.0f);
    UIChildren_Add(children, g_plus);

    UIApp_SetChildren(g_app, children);
    UIApp_SetBackgroundColor(g_app, (UIColor){ 241, 245, 249, 1.0f });
    UIApp_OnResize(g_app, on_resize, NULL);
    on_resize(UIApp_GetWidth(g_app), UIApp_GetHeight(g_app), NULL);

    UIApp_ShowWindow(g_app);
    UIApp_Run(g_app);
    UIApp_Destroy(g_app);
    return 0;
}
