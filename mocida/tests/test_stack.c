// test_stack.c
//
// Two stacks side by side: a vertical stack on the left containing a
// title + several buttons + a slider, and a horizontal stack on the
// right with coloured tiles. Demonstrates that UIStack positions
// children sequentially with constant spacing.

#include <uikit/app.h>

static void OnButtonClicked(UIButton* b, void* ud) {
    (void)b; (void)ud;
}

int main(void) {
    UIApp* app = UIApp_Create("stack", 720, 500);
    if (!app) return 1;
    UISearchFonts();
    UIApp_SetWindowIcon(app, "assets/logo.svg");

    UIChildren* children = UIChildren_Create(2);

    // --- Vertical stack on the left ---
    UIStack* vs = UIStack_Create(UI_STACK_VERTICAL);
    UIStack_SetSpacing(vs, 12.0f);
    UIStack_SetPadding(vs, 16.0f, 16.0f, 16.0f, 16.0f);

    UIText* title = UIText_Create("Vertical stack", 20.0f);
    UIText_SetFontFamily(title, UIGetFont("Arial"));
    UIText_SetColor(title, (UIColor){ 15, 23, 42, 1.0f });
    UIWidget* titleW = widgcs(title, 280.0f, 28.0f);
    UIStack_AddItem(vs, titleW);

    for (int i = 0; i < 3; i++) {
        UIButton* b = UIButton_Create("Click me", 16.0f);
        UIButton_SetFontFamily(b, UIGetFont("Arial"));
        UIButton_SetRadius(b, 8.0f);
        UIButton_OnClick(b, OnButtonClicked, NULL);
        UIWidget* bw = widgcs(b, 240.0f, 40.0f);
        UIStack_AddItem(vs, bw);
    }

    UISlider* s = UISlider_Create(0.0f, 100.0f, 50.0f);
    UIWidget* sw = widgcs(s, 240.0f, 30.0f);
    UIStack_AddItem(vs, sw);

    UIWidget* vsW = widgcs(vs, 320.0f, 460.0f);
    UIWidget_SetPosition(vsW, 20.0f, 20.0f);
    UIChildren_Add(children, vsW);

    // --- Horizontal stack on the right ---
    UIStack* hs = UIStack_Create(UI_STACK_HORIZONTAL);
    UIStack_SetSpacing(hs, 10.0f);
    UIStack_SetPadding(hs, 16.0f, 16.0f, 16.0f, 16.0f);

    const UIColor tints[] = {
        (UIColor){ 59, 130, 246, 1.0f },
        (UIColor){ 34, 197, 94, 1.0f },
        (UIColor){ 251, 191, 36, 1.0f },
        (UIColor){ 239, 68, 68, 1.0f },
        (UIColor){ 168, 85, 247, 1.0f },
    };
    for (int i = 0; i < 5; i++) {
        UIRectangle* r = UIRectangle_Create();
        UIRectangle_SetColor(r, tints[i]);
        UIRectangle_SetRadius(r, 10.0f);
        UIRectangle_SetShadow(r, UI_SHADOW_DEFAULT);
        UIWidget* w = widgcs(r, 50.0f, 50.0f);
        UIStack_AddItem(hs, w);
    }

    UIWidget* hsW = widgcs(hs, 360.0f, 80.0f);
    UIWidget_SetPosition(hsW, 340.0f, 20.0f);
    UIChildren_Add(children, hsW);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, (UIColor){ 226, 232, 240, 1.0f });
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
