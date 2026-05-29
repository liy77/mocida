// test_drag.c
//
// Demonstrates UIMouseArea with drag support.
//
//   - Three coloured cards. Press and hold the left mouse button on a
//     card and move - the card follows the cursor.
//   - The card is clamped to the visible window area (see
//     UIMouseArea_SetDragBounds).
//   - The big purple "tray" at the bottom is a UIMouseArea with no
//     drag target: it just logs press/release/hover transitions to
//     stdout so you can see the event flow.
//
// Architecture per draggable card:
//
//   visible widget : UIRectangle that draws the card (background +
//                    shadow). Position is updated by the mouse area.
//   mouse area     : UIMouseArea sized exactly like the card, placed
//                    on top with a higher z-index. dragTarget points
//                    at the visible widget so both move together.
//
// Important: the area's widget MUST have a higher z-index than the
// visual widget so it sits on top and captures clicks.

#include <uikit/app.h>
#include <stdio.h>

static void OnTrayDown(UIMouseArea* a, UIMouseEvent ev, void* ud) {
    (void)a; (void)ud;
    printf("[tray] press at (%.0f, %.0f)\n", ev.x, ev.y);
}
static void OnTrayUp(UIMouseArea* a, UIMouseEvent ev, void* ud) {
    (void)a; (void)ud;
    printf("[tray] release at (%.0f, %.0f)\n", ev.x, ev.y);
}
static void OnTrayEnter(UIMouseArea* a, UIMouseEvent ev, void* ud) {
    (void)a; (void)ev; (void)ud;
    printf("[tray] hover ENTER\n");
}
static void OnTrayExit(UIMouseArea* a, UIMouseEvent ev, void* ud) {
    (void)a; (void)ev; (void)ud;
    printf("[tray] hover EXIT\n");
}

static void OnDragStart(UIMouseArea* a, UIMouseEvent ev, void* ud) {
    (void)a; (void)ev;
    const char* name = (const char*)ud;
    printf("[%s] DRAG START\n", name);
}
static void OnDragEnd(UIMouseArea* a, UIMouseEvent ev, void* ud) {
    (void)a; (void)ev;
    const char* name = (const char*)ud;
    printf("[%s] DRAG END\n", name);
}

// Creates a card: visible widget + matching mouse area pinned on top.
static void make_draggable_card(UIChildren* children,
                                float x, float y, float w, float h,
                                UIColor color,
                                float winW, float winH,
                                const char* name) {
    // Visible card (the thing the user sees moving).
    UIRectangle* rect = UIRectangle_Create();
    UIRectangle_SetColor(rect, color);
    UIRectangle_SetRadius(rect, 14.0f);
    UIRectangle_SetShadow(rect, (UIShadow){
        .offsetX = 0.0f, .offsetY = 10.0f,
        .blur = 22.0f, .spread = -4.0f,
        .color = { color.r, color.g, color.b, 0.55f }
    });
    UIWidget* cardW = widgcs(rect, w, h);
    UIWidget_SetPosition(cardW, x, y);
    UIWidget_SetZIndex(cardW, 1);
    UIChildren_Add(children, cardW);

    // Invisible mouse area covering the card.
    UIMouseArea* area = UIMouseArea_Create();
    UIMouseArea_SetDraggable(area, 1);
    UIMouseArea_SetDragTarget(area, cardW);
    // Keep both card and area inside the window.
    UIMouseArea_SetDragBounds(area, 0.0f, 0.0f, winW, winH);
    UIMouseArea_OnDragStart(area, OnDragStart, (void*)name);
    UIMouseArea_OnDragEnd  (area, OnDragEnd,   (void*)name);

    UIWidget* areaW = widgcs(area, w, h);
    UIWidget_SetPosition(areaW, x, y);
    UIWidget_SetZIndex(areaW, 100); // above any visual widget
    UIChildren_Add(children, areaW);
}

int main(void) {
    const float WIN_W = 820.0f;
    const float WIN_H = 560.0f;

    UIApp* app = UIApp_Create("Mocida - drag", (int)WIN_W, (int)WIN_H);
    if (!app) return 1;

    UISearchFonts();
    UIApp_SetWindowIcon(app, "assets/logo.svg");

    UIChildren* children = UIChildren_Create(16);

    // Hint label
    UIText* hint = UIText_Create("Drag the cards. The tray at the bottom logs events.", 16.0f);
    UIText_SetFontFamily(hint, UIGetFont("Arial"));
    UIText_SetColor(hint, (UIColor){ 71, 85, 105, 1.0f });
    UIWidget* hintW = widgc(hint);
    UIWidget_SetPosition(hintW, 24.0f, 16.0f);
    UIChildren_Add(children, hintW);

    // Three draggable cards
    make_draggable_card(children,  60.0f,  80.0f, 160.0f, 160.0f,
                       (UIColor){ 59, 130, 246, 1.0f }, WIN_W, WIN_H, "blue");
    make_draggable_card(children, 260.0f,  80.0f, 160.0f, 160.0f,
                       (UIColor){ 34, 197, 94, 1.0f }, WIN_W, WIN_H, "green");
    make_draggable_card(children, 460.0f,  80.0f, 160.0f, 160.0f,
                       (UIColor){ 239, 68, 68, 1.0f }, WIN_W, WIN_H, "red");

    // Bottom "tray": a UIMouseArea that just logs hover / press / release.
    UIRectangle* tray = UIRectangle_Create();
    UIRectangle_SetColor(tray, (UIColor){ 168, 85, 247, 0.18f });
    UIRectangle_SetRadius(tray, 10.0f);
    UIRectangle_SetBorderWidth(tray, 1.0f);
    UIRectangle_SetBorderColor(tray, (UIColor){ 168, 85, 247, 0.5f });
    UIWidget* trayVis = widgcs(tray, WIN_W - 48.0f, 120.0f);
    UIWidget_SetPosition(trayVis, 24.0f, WIN_H - 144.0f);
    UIWidget_SetZIndex(trayVis, 1);
    UIChildren_Add(children, trayVis);

    UIMouseArea* trayArea = UIMouseArea_Create();
    UIMouseArea_SetDraggable(trayArea, 0); // hover/click only
    UIMouseArea_OnMouseDown (trayArea, OnTrayDown,  NULL);
    UIMouseArea_OnMouseUp   (trayArea, OnTrayUp,    NULL);
    UIMouseArea_OnHoverEnter(trayArea, OnTrayEnter, NULL);
    UIMouseArea_OnHoverExit (trayArea, OnTrayExit,  NULL);
    UIWidget* trayHit = widgcs(trayArea, WIN_W - 48.0f, 120.0f);
    UIWidget_SetPosition(trayHit, 24.0f, WIN_H - 144.0f);
    UIWidget_SetZIndex(trayHit, 50);
    UIChildren_Add(children, trayHit);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, (UIColor){ 248, 250, 252, 1.0f });
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
