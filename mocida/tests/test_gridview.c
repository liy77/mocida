// test_gridview.c
//
// Demonstrates UIGrid, UIScroll, UIListView and UIGridView together.
//   - Left half:  a UIListView with 30 coloured rows. Scroll with the
//                 mouse wheel. Hold shift to scroll horizontally
//                 (a no-op here since the list is vertical only).
//   - Right half: a UIGridView (4 columns) with 24 cards.
//
// Both views clip their content via the UIScroll viewport, so cards
// vanish into the edges as you scroll - no overdraw outside the
// viewport bounds.

#include <uikit/app.h>
#include <stdio.h>

static UIWidget* make_list_row(int index) {
    UIRectangle* r = UIRectangle_Create();
    // Alternate between two tones to make scrolling visible.
    if (index % 2 == 0) {
        UIRectangle_SetColor(r, (UIColor){ 241, 245, 249, 1.0f });
    } else {
        UIRectangle_SetColor(r, (UIColor){ 226, 232, 240, 1.0f });
    }
    UIRectangle_SetRadius(r, 6.0f);
    return widgc(r);
}

static UIWidget* make_grid_card(int index) {
    UIRectangle* r = UIRectangle_Create();
    // Cycle through a small palette so the grid is visually busy.
    const UIColor palette[] = {
        (UIColor){ 59, 130, 246, 1.0f },
        (UIColor){ 34, 197, 94, 1.0f },
        (UIColor){ 239, 68, 68, 1.0f },
        (UIColor){ 168, 85, 247, 1.0f },
        (UIColor){ 251, 191, 36, 1.0f },
        (UIColor){ 14, 165, 233, 1.0f }
    };
    UIRectangle_SetColor(r, palette[index % 6]);
    UIRectangle_SetRadius(r, 12.0f);
    UIRectangle_SetShadow(r, (UIShadow){
        .offsetX = 0.0f, .offsetY = 6.0f,
        .blur = 14.0f, .spread = -2.0f,
        .color = { 0, 0, 0, 0.25f }
    });
    return widgc(r);
}

int main(void) {
    UIApp* app = UIApp_Create("Mocida - GridView & ListView", 920, 600);
    if (!app) return 1;
    UISearchFonts();
    UIApp_SetWindowIcon(app, "assets/logo.svg");

    UIChildren* children = UIChildren_Create(4);

    // --- Title ---
    UIText* title = UIText_Create("ListView (left)   |   GridView (right) - use the mouse wheel", 18.0f);
    UIText_SetFontFamily(title, UIGetFont("Arial"));
    UIText_SetColor(title, (UIColor){ 51, 65, 85, 1.0f });
    UIWidget* titleW = widgc(title);
    UIWidget_SetPosition(titleW, 24.0f, 18.0f);
    UIChildren_Add(children, titleW);

    // --- ListView (1-column scrollable list) ---
    UIScroll* list = UIListView_Create(56.0f);
    UIScroll_SetWheelSpeed(list, 70.0f);
    for (int i = 0; i < 30; i++) {
        UIListView_AddItem(list, make_list_row(i));
    }
    UIWidget* listW = widgcs(list, 380.0f, 510.0f);
    UIWidget_SetPosition(listW, 24.0f, 60.0f);
    UIChildren_Add(children, listW);

    // --- GridView (4-column scrollable grid) ---
    UIScroll* grid = UIGridView_Create(/*columns=*/4, /*cellW=*/100.0f, /*cellH=*/100.0f);
    UIScroll_SetWheelSpeed(grid, 90.0f);
    for (int i = 0; i < 24; i++) {
        UIGridView_AddItem(grid, make_grid_card(i));
    }
    UIWidget* gridW = widgcs(grid, 480.0f, 510.0f);
    UIWidget_SetPosition(gridW, 416.0f, 60.0f);
    UIChildren_Add(children, gridW);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, (UIColor){ 248, 250, 252, 1.0f });
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
