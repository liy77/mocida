// test_doubleclick.c
//
// Exercises the new UIMouseArea hooks:
//   - onDoubleClick: a left double-click on the blue card flips its
//     visible state via the counter below.
//   - right / middle click: the orange card now receives mouse-down
//     events for *any* button. Status text shows which button fired.
//
// What to verify:
//   - Two quick left-clicks within ~500ms on the blue card increase
//     the "double-clicks: N" counter. A single click does not.
//   - Right-clicking or middle-clicking the orange card increments the
//     respective counters. (Buttons / textfields / etc still receive
//     only LEFT, which is the intended design.)

#include <uikit/app.h>
#include <stdio.h>

static UIText* g_dblLabel    = NULL;
static UIText* g_rightLabel  = NULL;
static UIText* g_middleLabel = NULL;
static int g_dblCount = 0;
static int g_rightCount = 0;
static int g_middleCount = 0;

static void update_label(UIText* t, const char* prefix, int n) {
    if (!t) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "%s: %d", prefix, n);
    UIText_SetText(t, buf);
}

static void OnDoubleClick(UIMouseArea* a, UIMouseEvent ev, void* ud) {
    (void)a; (void)ev; (void)ud;
    g_dblCount++;
    update_label(g_dblLabel, "double-clicks", g_dblCount);
    printf("[blue] double-click #%d at (%.0f, %.0f)\n",
           g_dblCount, ev.x, ev.y);
}

static void OnAnyDown(UIMouseArea* a, UIMouseEvent ev, void* ud) {
    (void)a; (void)ud;
    printf("[orange] mouse-down button=%d at (%.0f, %.0f)\n",
           ev.button, ev.x, ev.y);
    if (ev.button == 3) { // SDL_BUTTON_RIGHT
        g_rightCount++;
        update_label(g_rightLabel, "right-clicks", g_rightCount);
    } else if (ev.button == 2) { // SDL_BUTTON_MIDDLE
        g_middleCount++;
        update_label(g_middleLabel, "middle-clicks", g_middleCount);
    }
}

static UIWidget* make_card(UIChildren* children, UIColor color,
                           float x, float y, float w, float h) {
    UIRectangle* bg = UIRectangle_Create();
    UIRectangle_SetColor(bg, color);
    UIRectangle_SetRadius(bg, 10.0f);
    UIWidget* card = widgcs(bg, w, h);
    UIWidget_SetPosition(card, x, y);
    UIWidget_SetZIndex(card, 0);
    UIChildren_Add(children, card);
    return card;
}

static UIWidget* make_area(UIChildren* children, float x, float y,
                           float w, float h) {
    UIMouseArea* a = UIMouseArea_Create();
    UIWidget* widget = widgcs(a, w, h);
    UIWidget_SetPosition(widget, x, y);
    UIWidget_SetZIndex(widget, 10);
    UIChildren_Add(children, widget);
    return widget;
}

static UIText* make_label(UIChildren* children, const char* msg,
                          float x, float y) {
    UIText* t = UIText_Create((char*)msg, 16.0f);
    UIText_SetFontFamily(t, UIGetFont("Arial"));
    UIText_SetColor(t, (UIColor){ 30, 41, 59, 1.0f });
    UIWidget* w = widgc(t);
    UIWidget_SetPosition(w, x, y);
    UIChildren_Add(children, w);
    return t;
}

int main(void) {
    UIApp* app = UIApp_Create("Mocida - double-click + right/middle", 640, 400);
    if (!app) return 1;
    UISearchFonts();

    UIChildren* children = UIChildren_Create(16);

    // Blue card: double-click target.
    make_card(children, (UIColor){ 59, 130, 246, 1.0f },
              40.0f, 60.0f, 240.0f, 160.0f);
    UIWidget* blueAreaW = make_area(children, 40.0f, 60.0f, 240.0f, 160.0f);
    UIMouseArea* blueArea = (UIMouseArea*)blueAreaW->data;
    UIMouseArea_OnDoubleClick(blueArea, OnDoubleClick, NULL);

    make_label(children, "Double-click the blue card", 40.0f, 230.0f);
    g_dblLabel = make_label(children, "double-clicks: 0", 40.0f, 252.0f);

    // Orange card: right/middle target.
    make_card(children, (UIColor){ 249, 115, 22, 1.0f },
              320.0f, 60.0f, 240.0f, 160.0f);
    UIWidget* orangeAreaW = make_area(children, 320.0f, 60.0f, 240.0f, 160.0f);
    UIMouseArea* orangeArea = (UIMouseArea*)orangeAreaW->data;
    UIMouseArea_OnMouseDown(orangeArea, OnAnyDown, NULL);

    make_label(children, "Right/middle click the orange card", 320.0f, 230.0f);
    g_rightLabel  = make_label(children, "right-clicks: 0",  320.0f, 252.0f);
    g_middleLabel = make_label(children, "middle-clicks: 0", 320.0f, 274.0f);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, UI_COLOR_WHITE);
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
