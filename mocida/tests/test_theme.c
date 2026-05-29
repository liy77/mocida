// test_theme.c
//
// Switches between the built-in light and dark themes on button click.
// The button changes its colours every press by reading from the
// active theme, demonstrating how UITheme can drive an entire UI's
// appearance from a single place.

#include <uikit/app.h>
#include <stdio.h>

static int    g_dark   = 0;
static UIApp* g_app    = NULL;
static UIButton* g_themeBtn = NULL;
static UIWidget* g_panel = NULL;
static UIWidget* g_titleW = NULL;

static void ApplyThemeColors(void) {
    const UITheme* t = UITheme_GetGlobal();

    if (g_app) UIApp_SetBackgroundColor(g_app, t->background);

    if (g_panel && g_panel->data) {
        UIRectangle* r = (UIRectangle*)g_panel->data;
        UIRectangle_SetColor(r, t->surface);
        UIRectangle_SetBorderColor(r, t->border);
        UIRectangle_SetBorderWidth(r, 1.0f);
    }
    if (g_titleW && g_titleW->data) {
        UIText_SetColor((UIText*)g_titleW->data, t->onSurface);
        UIText_DestroyTexture((UIText*)g_titleW->data);
    }
    if (g_themeBtn) {
        UIButton_SetColors(g_themeBtn, t->primary, t->onPrimary);
        UIText_DestroyTexture(g_themeBtn->label);
    }
}

static void OnToggleTheme(UIButton* b, void* ud) {
    (void)b; (void)ud;
    g_dark = !g_dark;

    UITheme t;
    if (g_dark) UITheme_FillDark(&t);
    else        UITheme_FillLight(&t);
    UITheme_SetGlobal(&t);

    ApplyThemeColors();
    UIButton_SetText(g_themeBtn, g_dark ? "Switch to light" : "Switch to dark");
    UIText_DestroyTexture(g_themeBtn->label);
}

int main(void) {
    UIApp* app = UIApp_Create("theme", 640, 400);
    if (!app) return 1;
    g_app = app;
    UISearchFonts();
    UIApp_SetWindowIcon(app, "assets/logo.svg");

    UIChildren* children = UIChildren_Create(8);

    // Surface panel
    UIRectangle* panel = UIRectangle_Create();
    UIRectangle_SetRadius(panel, 14.0f);
    g_panel = widgcs(panel, 480.0f, 220.0f);
    UIWidget_SetPosition(g_panel, 80.0f, 60.0f);
    UIChildren_Add(children, g_panel);

    // Title text on the surface
    UIText* title = UIText_Create("Mocida theme demo", 24.0f);
    UIText_SetFontFamily(title, UIGetFont("Arial"));
    g_titleW = widgc(title);
    UIWidget_SetPosition(g_titleW, 110.0f, 100.0f);
    UIChildren_Add(children, g_titleW);

    // Theme toggle button
    UIButton* btn = UIButton_Create("Switch to dark", 16.0f);
    UIButton_SetFontFamily(btn, UIGetFont("Arial"));
    UIButton_SetRadius(btn, 8.0f);
    UIButton_SetShadow(btn, UI_SHADOW_DEFAULT);
    UIButton_OnClick(btn, OnToggleTheme, NULL);
    g_themeBtn = btn;
    UIWidget* btnW = widgcs(btn, 220.0f, 48.0f);
    UIWidget_SetPosition(btnW, 110.0f, 200.0f);
    UIChildren_Add(children, btnW);

    UIApp_SetChildren(app, children);

    // Apply the initial theme (already initialised to light by the lib).
    ApplyThemeColors();

    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
