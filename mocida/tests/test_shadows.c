// test_shadows.c
//
// Demonstrates:
//   1. UIApp_SetWindowIcon: loads assets/logo.svg as the window icon
//      (visible in the title bar and the taskbar).
//   2. Drop shadows on UIRectangle and UIButton via UIShadow + a
//      smoothstep SDF falloff.
//
// What to look for:
//   - The app icon in the top-left corner of the window.
//   - Each card has a shadow with distinct offset/blur/spread values to
//     showcase the three parameters.
//   - The button keeps its shadow across every state (HOVER/PRESSED).
//   - Shadow edges should be smooth, with no banding or aliasing.

#include <uikit/app.h>
#include <stdio.h>

static void OnNothing(UIButton* b, void* userdata) { (void)b; (void)userdata; }

static UIWidget* make_card(UIChildren* children,
                           float x, float y, float w, float h,
                           UIColor color, float radius,
                           UIShadow shadow) {
    UIRectangle* r = UIRectangle_Create();
    UIRectangle_SetColor(r, color);
    UIRectangle_SetRadius(r, radius);
    UIRectangle_SetShadow(r, shadow);

    UIWidget* widget = widgcs(r, w, h);
    UIWidget_SetPosition(widget, x, y);
    UIChildren_Add(children, widget);
    return widget;
}

int main(void) {
    UIApp* app = UIApp_Create("Mocida - shadows & icon", 820, 540);
    if (!app) return 1;

    // ----- Window icon -----
    // Resolution is handled by UIAsset_LoadSurface (tries CWD, then
    // the exe's directory). PNG/JPG/BMP/SVG accepted (SDL_image + plutosvg).
    UIApp_SetWindowIcon(app, "assets/logo.svg");

    UISearchFonts();
    UIChildren* children = UIChildren_Create(16);

    // Card 1: soft default shadow (material design)
    make_card(children, 40, 60, 200, 130,
              UI_COLOR_WHITE, 12.0f,
              UI_SHADOW_DEFAULT);

    // Card 2: more diffuse, shifted down
    make_card(children, 280, 60, 200, 130,
              UI_COLOR_WHITE, 12.0f,
              (UIShadow){
                  .offsetX = 0.0f, .offsetY = 12.0f,
                  .blur = 24.0f, .spread = 0.0f,
                  .color = { 0, 0, 0, 0.35f }
              });

    // Card 3: colored shadow (blue glow)
    make_card(children, 520, 60, 200, 130,
              UI_COLOR_WHITE, 12.0f,
              (UIShadow){
                  .offsetX = 0.0f, .offsetY = 0.0f,
                  .blur = 30.0f, .spread = 4.0f,
                  .color = { 59, 130, 246, 0.6f }
              });

    // Card 4 (row 2): negative SPREAD (shadow smaller than the shape)
    make_card(children, 40, 230, 200, 130,
              (UIColor){ 248, 250, 252, 1.0f }, 12.0f,
              (UIShadow){
                  .offsetX = 0.0f, .offsetY = 8.0f,
                  .blur = 16.0f, .spread = -8.0f,
                  .color = { 0, 0, 0, 0.5f }
              });

    // Card 5: hard shadow (blur = 0) to see the exact shape
    make_card(children, 280, 230, 200, 130,
              (UIColor){ 254, 240, 138, 1.0f }, 12.0f,
              (UIShadow){
                  .offsetX = 6.0f, .offsetY = 6.0f,
                  .blur = 0.0f, .spread = 0.0f,
                  .color = { 0, 0, 0, 0.6f }
              });

    // Card 6: "pill" with an elevated shadow
    UIRectangle* pill = UIRectangle_Create();
    UIRectangle_SetColor(pill, (UIColor){ 168, 85, 247, 1.0f });
    UIRectangle_SetRadius(pill, 65.0f);
    UIRectangle_SetShadow(pill, (UIShadow){
        .offsetX = 0.0f, .offsetY = 16.0f,
        .blur = 28.0f, .spread = -4.0f,
        .color = { 168, 85, 247, 0.45f }  // shadow tinted with the shape's color
    });
    UIWidget* pillW = widgcs(pill, 200.0f, 130.0f);
    UIWidget_SetPosition(pillW, 520.0f, 230.0f);
    UIChildren_Add(children, pillW);

    // Button with shadow (verifies that buttons inherit shadow from background)
    UIButton* btn = UIButton_Create("Confirm with style", 22.0f);
    UIButton_SetFontFamily(btn, UIGetFont("Arial"));
    UIButton_SetRadius(btn, 10.0f);
    UIButton_SetColors(btn, (UIColor){ 34, 197, 94, 1.0f }, UI_COLOR_WHITE);
    UIButton_SetShadow(btn, (UIShadow){
        .offsetX = 0.0f, .offsetY = 8.0f,
        .blur = 18.0f, .spread = -2.0f,
        .color = { 34, 197, 94, 0.55f }
    });
    UIButton_OnClick(btn, OnNothing, NULL);
    UIWidget* btnW = widgcs(btn, 280.0f, 60.0f);
    UIWidget_SetPosition(btnW, 280.0f, 420.0f);
    UIChildren_Add(children, btnW);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, (UIColor){ 226, 232, 240, 1.0f });
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
