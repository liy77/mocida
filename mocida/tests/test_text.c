// test_text.c
//
// Renders text in different sizes, colors and configurations.
// What to verify:
//   - Sharp text at every size.
//   - Correct colors.
//   - The text with a yellow background should have visible padding
//     and undistorted rounded corners.

#include <uikit/app.h>

static UIWidget* make_text(const char* content, float size,
                           UIColor color, const char* font) {
    UIText* t = UIText_Create((char*)content, size);
    UIText_SetFontFamily(t, UIGetFont(font));
    UIText_SetColor(t, color);
    return widgc(t);
}

int main(void) {
    UIApp* app = UIApp_Create("Mocida - text", 700, 460);
    if (!app) return 1;

    UISearchFonts();

    UIChildren* children = UIChildren_Create(4);

    UIWidget* small  = make_text("Small: 16pt", 16, UI_COLOR_BLACK, "Arial");
    UIWidget_SetPosition(small,  20.0f, 20.0f);
    UIChildren_Add(children, small);

    UIWidget* medium = make_text("Medium: 28pt", 28, UI_COLOR_BLUE, "Arial");
    UIWidget_SetPosition(medium, 20.0f, 70.0f);
    UIChildren_Add(children, medium);

    UIWidget* large  = make_text("Large: 48pt", 48, UI_COLOR_RED, "Arial");
    UIWidget_SetPosition(large,  20.0f, 140.0f);
    UIChildren_Add(children, large);

    // Text with rounded background
    UIRectangle* bg = UIRectangle_Create();
    UIRectangle_SetColor(bg, UI_COLOR_YELLOW);
    UIRectangle_SetRadius(bg, 12.0f);
    UIRectangle_SetBorderWidth(bg, 2.0f);
    UIRectangle_SetBorderColor(bg, UI_COLOR_BLACK);

    UIText* boxedText = UIText_Create("With background + border", 28);
    UIText_SetFontFamily(boxedText, UIGetFont("Arial"));
    UIText_SetColor(boxedText, UI_COLOR_BLACK);
    UIText_SetBackground(boxedText, bg);
    UIText_SetPadding(boxedText, 15.0f, 10.0f, 15.0f, 10.0f);

    UIWidget* boxed = widgc(boxedText);
    UIWidget_SetPosition(boxed, 20.0f, 260.0f);
    UIChildren_Add(children, boxed);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, UI_COLOR_WHITE);
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
