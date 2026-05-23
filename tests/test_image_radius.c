// test_image_radius.c
//
// Exercises UIImage.radius and UIImage.borderWidth - both newly wired
// into the renderer. Each box shows the same SVG at a different radius
// or border setting so the visual result can be compared side by side.
//
// What to verify:
//   - radius=0 -> sharp rectangular crop (control).
//   - radius=20 / 40 / 80 -> visibly rounded corners with no jagged
//     edges (anti-aliased coverage from the existing AA path).
//   - The corner of the image never bleeds outside the rounded shape.
//   - With borderWidth>0 a hollow ring is drawn on top of the image.

#include <uikit/app.h>
#include <stdio.h>

#define ASSET "assets/logo.svg"

static void add_card(UIChildren* children, const char* label,
                     float radius, float borderWidth,
                     float x, float y, float w, float h) {
    UIImage* img = UIImage_Create(ASSET,
                                  /*animated*/ 0,
                                  /*nineSlice*/ 0,
                                  /*nineSliceMargins*/ NULL,
                                  FILL_COVER,
                                  UI_COLOR_TRANSPARENT);
    img->radius      = radius;
    img->borderWidth = borderWidth;

    UIWidget* widget = widgcs(img, w, h);
    UIWidget_SetPosition(widget, x, y);
    UIChildren_Add(children, widget);

    // Caption below.
    UIText* t = UIText_Create((char*)label, 14.0f);
    UIText_SetFontFamily(t, UIGetFont("Arial"));
    UIText_SetColor(t, (UIColor){ 71, 85, 105, 1.0f });
    UIWidget* tw = widgc(t);
    UIWidget_SetPosition(tw, x, y + h + 4.0f);
    UIChildren_Add(children, tw);
}

int main(void) {
    UIApp* app = UIApp_Create("Mocida - image radius", 900, 560);
    if (!app) return 1;
    UISearchFonts();

    UIChildren* children = UIChildren_Create(24);

    const float SX = 30.0f, SY = 30.0f;
    const float W = 180.0f, H = 180.0f;
    const float GX = W + 30.0f, GY = H + 40.0f;

    // Row 1: just radius, no border
    add_card(children, "radius=0 (control)",  0.0f,  0.0f,  SX + 0*GX, SY + 0*GY, W, H);
    add_card(children, "radius=20",          20.0f,  0.0f,  SX + 1*GX, SY + 0*GY, W, H);
    add_card(children, "radius=40",          40.0f,  0.0f,  SX + 2*GX, SY + 0*GY, W, H);
    add_card(children, "radius=90 (circle)", 90.0f,  0.0f,  SX + 3*GX, SY + 0*GY, W, H);

    // Row 2: radius + border
    add_card(children, "r=16, border=4",     16.0f,  4.0f,  SX + 0*GX, SY + 1*GY, W, H);
    add_card(children, "r=24, border=8",     24.0f,  8.0f,  SX + 1*GX, SY + 1*GY, W, H);
    add_card(children, "r=40, border=2",     40.0f,  2.0f,  SX + 2*GX, SY + 1*GY, W, H);
    add_card(children, "r=90, border=6",     90.0f,  6.0f,  SX + 3*GX, SY + 1*GY, W, H);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, (UIColor){ 248, 250, 252, 1.0f });
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
