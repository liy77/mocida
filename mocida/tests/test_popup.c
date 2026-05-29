// test_popup.c
//
// Demonstrates UITooltip, UIMenu and UIDropdown together. Hover the
// blue button for ~400ms to see its tooltip. Click the dropdown to
// pick a value. Right-clicking (or left-clicking) the "Show menu"
// button opens a vertical menu at the cursor.

#include <uikit/app.h>
#include <stdio.h>

static UIMenu* g_menu = NULL;
static UIWidget* g_status = NULL;

static void OnMenuItem(UIMenu* m, int idx, const char* label, void* ud) {
    (void)m; (void)ud;
    if (g_status && g_status->data) {
        char buf[120];
        snprintf(buf, sizeof(buf), "menu: picked '%s' (idx %d)", label, idx);
        UIText_SetText((UIText*)g_status->data, buf);
    }
    printf("[menu] %s\n", label);
}

static void OnShowMenu(UIButton* b, void* ud) {
    (void)b; (void)ud;
    if (!g_menu) return;
    float mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);
    UIMenu_ShowAt(g_menu, mx, my);
}

static void OnDropdownChange(UIDropdown* d, int idx, const char* label, void* ud) {
    (void)d; (void)ud;
    if (g_status && g_status->data) {
        char buf[120];
        snprintf(buf, sizeof(buf), "dropdown: picked '%s' (idx %d)", label, idx);
        UIText_SetText((UIText*)g_status->data, buf);
    }
    printf("[dropdown] %s\n", label);
}

int main(void) {
    UIApp* app = UIApp_Create("popups", 640, 420);
    if (!app) return 1;
    UISearchFonts();
    UIApp_SetWindowIcon(app, "assets/logo.svg");

    UIChildren* children = UIChildren_Create(16);

    // Anchor button (target for the tooltip).
    UIButton* anchor = UIButton_Create("Hover me", 16.0f);
    UIButton_SetFontFamily(anchor, UIGetFont("Arial"));
    UIButton_SetRadius(anchor, 8.0f);
    UIWidget* anchorW = widgcs(anchor, 180.0f, 44.0f);
    UIWidget_SetPosition(anchorW, 30.0f, 50.0f);
    UIChildren_Add(children, anchorW);

    // Dropdown
    UIDropdown* dd = UIDropdown_Create();
    UIDropdown_SetFont(dd, UIGetFont("Arial"), 14.0f);
    UIDropdown_OnChange(dd, OnDropdownChange, NULL);
    UIDropdown_AddOption(dd, "Light");
    UIDropdown_AddOption(dd, "Dark");
    UIDropdown_AddOption(dd, "Solarized");
    UIDropdown_AddOption(dd, "Dracula");
    UIWidget* ddW = widgcs(dd, 200.0f, 40.0f);
    UIWidget_SetPosition(ddW, 230.0f, 50.0f);
    UIWidget_SetZIndex(ddW, 5);
    UIChildren_Add(children, ddW);

    // Show-menu button
    UIButton* mbtn = UIButton_Create("Show menu", 14.0f);
    UIButton_SetFontFamily(mbtn, UIGetFont("Arial"));
    UIButton_SetRadius(mbtn, 8.0f);
    UIButton_OnClick(mbtn, OnShowMenu, NULL);
    UIWidget* mbtnW = widgcs(mbtn, 160.0f, 40.0f);
    UIWidget_SetPosition(mbtnW, 450.0f, 50.0f);
    UIChildren_Add(children, mbtnW);

    // Tooltip
    UITooltip* tt = UITooltip_Create(anchorW,
        "This is a UITooltip. Move your cursor away to hide it.", 12.0f);
    UITooltip_SetFontFamily(tt, UIGetFont("Arial"));
    UITooltip_SetDelay(tt, 350);
    UIWidget* ttW = widgc(tt);
    UIWidget_SetZIndex(ttW, 9999); // drawn on top
    UIChildren_Add(children, ttW);

    // Menu
    UIMenu* menu = UIMenu_Create(32.0f, 200.0f);
    UIMenu_SetFont(menu, UIGetFont("Arial"), 14.0f);
    UIMenu_OnItem(menu, OnMenuItem, NULL);
    UIMenu_AddItem(menu, "Open file...");
    UIMenu_AddItem(menu, "Save");
    UIMenu_AddItem(menu, "Save as...");
    UIMenu_AddItem(menu, "Close window");
    g_menu = menu;
    UIWidget* menuW = widgc(menu);
    UIWidget_SetZIndex(menuW, 9998); // drawn on top of regular widgets
    UIChildren_Add(children, menuW);

    // Status line
    UIText* st = UIText_Create("(awaiting interaction)", 14.0f);
    UIText_SetFontFamily(st, UIGetFont("Arial"));
    UIText_SetColor(st, (UIColor){ 71, 85, 105, 1.0f });
    g_status = widgc(st);
    UIWidget_SetPosition(g_status, 30.0f, 200.0f);
    UIChildren_Add(children, g_status);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, (UIColor){ 248, 250, 252, 1.0f });
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
