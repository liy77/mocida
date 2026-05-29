// test_image.c
//
// Demonstrates the UIImage widget with:
//   - PNG loading (assets/sdl_logo.png).
//   - SVG loading (assets/logo.svg) via the plutosvg vendored by
//     SDL_image - smooth edges at any resolution.
//   - Each UIFillMode (STRETCH, FIT, COVER, CENTER, NONE, TILE,
//     FIT_WIDTH, FIT_HEIGHT) inside same-sized boxes for visual
//     comparison.
//   - Color tinting (tintColor) preserving the PNG's alpha channel.
//
// What to verify:
//   - Each labelled box must show the image adjusted according to its
//     FillMode.
//   - COVER fills the entire area, cropping any overflow (no bars).
//   - FIT shows the whole image, with letterboxing when needed.
//   - TILE repeats the image as a mosaic.
//   - The green-tinted image keeps the SDL logo's shape but with a
//     different color.
//   - The SVG renders with vector quality (no pixelation on edges).
//
// Asset resolution is automatic: UIAsset_LoadTexture probes the CWD,
// then the executable's directory, then its parent. The build step
// also copies assets/ next to the .exe.

#include <uikit/app.h>
#include <stdio.h>

// SDL PNG: used as a "rich" image to showcase fill modes.
#define ASSET_PNG  "assets/sdl_logo.png"
// Mocida SVG: shows that vector assets render cleanly.
#define ASSET_SVG  "assets/logo.svg"

static UIWidget* add_image(UIChildren* children,
                           const char* source,
                           UIFillMode mode,
                           UIColor tint,
                           float x, float y, float w, float h) {
    UIImage* img = UIImage_Create(source,
                                  /*animated*/ 0,
                                  /*nineSlice*/ 0,
                                  /*nineSliceMargins*/ NULL,
                                  mode,
                                  tint);
    UIWidget* widget = widgcs(img, w, h);
    UIWidget_SetPosition(widget, x, y);
    UIChildren_Add(children, widget);
    return widget;
}

static UIWidget* add_label(UIChildren* children, const char* txt, float x, float y) {
    UIText* t = UIText_Create((char*)txt, 14.0f);
    UIText_SetFontFamily(t, UIGetFont("Arial"));
    UIText_SetColor(t, (UIColor){ 71, 85, 105, 1.0f });
    UIWidget* w = widgc(t);
    UIWidget_SetPosition(w, x, y);
    UIChildren_Add(children, w);
    return w;
}

// Builds a card: light-gray box with the image inside and a caption below.
static void add_demo_box(UIChildren* children,
                         const char* label,
                         const char* source,
                         UIFillMode mode,
                         UIColor tint,
                         float x, float y, float w, float h) {
    // Background box (helps visualize the bounds).
    UIRectangle* bg = UIRectangle_Create();
    UIRectangle_SetColor(bg, (UIColor){ 241, 245, 249, 1.0f });
    UIRectangle_SetRadius(bg, 6.0f);
    UIRectangle_SetBorderWidth(bg, 1.0f);
    UIRectangle_SetBorderColor(bg, (UIColor){ 203, 213, 225, 1.0f });
    UIWidget* boxW = widgcs(bg, w, h);
    UIWidget_SetPosition(boxW, x, y);
    UIChildren_Add(children, boxW);

    // Image filling the same rectangle.
    add_image(children, source, mode, tint, x, y, w, h);

    // Caption.
    add_label(children, label, x, y + h + 4.0f);
}

int main(void) {
    UIApp* app = UIApp_Create("Mocida - images", 920, 620);
    if (!app) return 1;

    UISearchFonts();
    UIApp_SetWindowIcon(app, "assets/logo.svg");

    UIChildren* children = UIChildren_Create(32);

    const float BW = 180.0f, BH = 130.0f;
    const float SX = 30.0f, SY = 30.0f;
    const float GX = BW + 20.0f, GY = BH + 30.0f;

    // Row 1: STRETCH, FIT, COVER, CENTER (all with PNG)
    add_demo_box(children, "FILL_STRETCH (distorts)",
                 ASSET_PNG, FILL_STRETCH, UI_COLOR_TRANSPARENT,
                 SX + 0*GX, SY + 0*GY, BW, BH);

    add_demo_box(children, "FILL_FIT (letterbox)",
                 ASSET_PNG, FILL_FIT, UI_COLOR_TRANSPARENT,
                 SX + 1*GX, SY + 0*GY, BW, BH);

    add_demo_box(children, "FILL_COVER (crops)",
                 ASSET_PNG, FILL_COVER, UI_COLOR_TRANSPARENT,
                 SX + 2*GX, SY + 0*GY, BW, BH);

    add_demo_box(children, "FILL_CENTER (native, centered)",
                 ASSET_PNG, FILL_CENTER, UI_COLOR_TRANSPARENT,
                 SX + 3*GX, SY + 0*GY, BW, BH);

    // Row 2: NONE, TILE, FIT_WIDTH, FIT_HEIGHT
    add_demo_box(children, "FILL_NONE (top-left)",
                 ASSET_PNG, FILL_NONE, UI_COLOR_TRANSPARENT,
                 SX + 0*GX, SY + 1*GY, BW, BH);

    add_demo_box(children, "FILL_TILE (mosaic)",
                 ASSET_PNG, FILL_TILE, UI_COLOR_TRANSPARENT,
                 SX + 1*GX, SY + 1*GY, BW, BH);

    add_demo_box(children, "FILL_FIT_WIDTH",
                 ASSET_PNG, FILL_FIT_WIDTH, UI_COLOR_TRANSPARENT,
                 SX + 2*GX, SY + 1*GY, BW, BH);

    add_demo_box(children, "FILL_FIT_HEIGHT",
                 ASSET_PNG, FILL_FIT_HEIGHT, UI_COLOR_TRANSPARENT,
                 SX + 3*GX, SY + 1*GY, BW, BH);

    // Row 3: SVG, green tint, purple tint, with shadow
    add_demo_box(children, "SVG vector (logo.svg)",
                 ASSET_SVG, FILL_FIT, UI_COLOR_TRANSPARENT,
                 SX + 0*GX, SY + 2*GY, BW, BH);

    add_demo_box(children, "tintColor green",
                 ASSET_PNG, FILL_FIT,
                 (UIColor){ 34, 197, 94, 1.0f },
                 SX + 1*GX, SY + 2*GY, BW, BH);

    add_demo_box(children, "tintColor purple + alpha .6",
                 ASSET_PNG, FILL_FIT,
                 (UIColor){ 168, 85, 247, 0.6f },
                 SX + 2*GX, SY + 2*GY, BW, BH);

    // Highlight card with shadow, showing that UIImage coexists with
    // UIRectangle + shadow in the same layout.
    UIRectangle* shadowed = UIRectangle_Create();
    UIRectangle_SetColor(shadowed, UI_COLOR_WHITE);
    UIRectangle_SetRadius(shadowed, 10.0f);
    UIRectangle_SetShadow(shadowed, UI_SHADOW_DEFAULT);
    UIWidget* shadowW = widgcs(shadowed, BW, BH);
    UIWidget_SetPosition(shadowW, SX + 3*GX, SY + 2*GY);
    UIChildren_Add(children, shadowW);

    add_image(children, ASSET_SVG, FILL_FIT, UI_COLOR_TRANSPARENT,
              SX + 3*GX + 10.0f, SY + 2*GY + 10.0f, BW - 20.0f, BH - 20.0f);
    add_label(children, "SVG + shadowed card", SX + 3*GX, SY + 2*GY + BH + 4.0f);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, (UIColor){ 248, 250, 252, 1.0f });
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
