// examples/hello.c
//
// The smallest useful Mocida app: a greeting label and a button that
// counts taps. Cross-platform (desktop + iOS) and responsive — the layout
// is computed against the real window size and kept clear of the device
// safe area (notch / Dynamic Island).
//
//   Desktop:  python build.py            (then run build/<plat>/debug/hello)
//   iOS:      see build.py --ios

#include <uikit/app.h>
#include <stdio.h>

static UIApp*    g_app   = NULL;
static UIWidget* g_label = NULL;
static UIWidget* g_btnW  = NULL;
static int       g_taps  = 0;

static void on_tap(UIButton* b, void* ud) {
    (void)b; (void)ud;
    char buf[64];
    g_taps++;
    snprintf(buf, sizeof(buf), "Tapped %d time%s", g_taps, g_taps == 1 ? "" : "s");
    if (g_label && g_label->data) UIText_SetText((UIText*)g_label->data, buf);
}

static void on_resize(int w, int h, void* ud) {
    (void)h; (void)ud;
    if (w <= 0) return;
    UIScreenInsets safe = UIScreen_GetSafeArea();
    const float pad  = 24.0f + (float)safe.left;
    const float top  = 40.0f + (float)safe.top;
    const float fw   = (float)w - pad - (24.0f + (float)safe.right);
    if (g_label) UIWidget_SetPosition(g_label, pad, top);
    if (g_btnW) {
        UIWidget_SetSize(g_btnW, fw > 80.0f ? fw : 80.0f, 50.0f);
        UIWidget_SetPosition(g_btnW, pad, top + 44.0f);
    }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    g_app = UIApp_Create("Mocida Hello", 480, 320);
    if (!g_app) return 1;
    UISearchFonts();

    UIChildren* children = UIChildren_Create(4);

    UIText* t = UIText_Create("Hello from Mocida!", 22.0f);
    UIText_SetFontFamily(t, UIGetFont("Arial"));
    UIText_SetColor(t, (UIColor){ 30, 41, 59, 1.0f });
    g_label = widgc(t);
    UIChildren_Add(children, g_label);

    UIButton* btn = UIButton_Create("Tap me", 18.0f);
    UIButton_SetFontFamily(btn, UIGetFont("Arial"));
    UIButton_SetRadius(btn, 10.0f);
    UIButton_SetColors(btn, (UIColor){ 79, 70, 229, 1.0f }, UI_COLOR_WHITE);
    UIButton_OnClick(btn, on_tap, NULL);
    g_btnW = widgcs(btn, 200.0f, 50.0f);
    UIChildren_Add(children, g_btnW);

    UIApp_SetChildren(g_app, children);
    UIApp_SetBackgroundColor(g_app, (UIColor){ 226, 232, 240, 1.0f });
    UIApp_OnResize(g_app, on_resize, NULL);
    on_resize(UIApp_GetWidth(g_app), UIApp_GetHeight(g_app), NULL);

    UIApp_ShowWindow(g_app);
    UIApp_Run(g_app);
    UIApp_Destroy(g_app);
    return 0;
}
