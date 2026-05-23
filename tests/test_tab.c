// test_tab.c
//
// Three-tab view. Tab 1 has a button + slider, tab 2 has a multi-line
// wrapped paragraph (UIText_SetWrapWidth), tab 3 has a checkbox + spinner.
// Click headers to swap panels.

#include <uikit/app.h>
#include <stdio.h>

static void OnTabChanged(UITabView* tv, int idx, void* ud) {
    (void)tv; (void)ud;
    printf("[tab] now active: %d\n", idx);
}

static UIWidget* PanelTab1(void) {
    UIStack* s = UIStack_Create(UI_STACK_VERTICAL);
    UIStack_SetSpacing(s, 12.0f);
    UIStack_SetPadding(s, 20.0f, 20.0f, 20.0f, 20.0f);

    UIText* lbl = UIText_Create("Tab 1 - widgets demo", 18.0f);
    UIText_SetFontFamily(lbl, UIGetFont("Arial"));
    UIStack_AddItem(s, widgcs(lbl, 320.0f, 24.0f));

    UIButton* b = UIButton_Create("A button", 14.0f);
    UIButton_SetFontFamily(b, UIGetFont("Arial"));
    UIButton_SetRadius(b, 6.0f);
    UIStack_AddItem(s, widgcs(b, 200.0f, 38.0f));

    UISlider* sl = UISlider_Create(0.0f, 1.0f, 0.3f);
    UIStack_AddItem(s, widgcs(sl, 260.0f, 30.0f));

    return widgc(s);
}

static UIWidget* PanelTab2(void) {
    UIText* paragraph = UIText_Create(
        "This text widget wraps automatically because UIText_SetWrapWidth was called "
        "with a positive pixel width. Word wrap is delivered via SDL_ttf's "
        "TTF_RenderText_Blended_Wrapped, which respects spaces and dashes when "
        "breaking lines. The wrap width is in pixels, not characters - so the layout "
        "stays consistent regardless of font choice or zoom level.",
        15.0f);
    UIText_SetFontFamily(paragraph, UIGetFont("Arial"));
    UIText_SetColor(paragraph, (UIColor){ 30, 41, 59, 1.0f });
    UIText_SetWrapWidth(paragraph, 460);

    UIWidget* w = widgc(paragraph);
    UIWidget_SetPosition(w, 20.0f, 20.0f);
    return w;
}

static UIWidget* PanelTab3(void) {
    UIStack* s = UIStack_Create(UI_STACK_VERTICAL);
    UIStack_SetSpacing(s, 18.0f);
    UIStack_SetPadding(s, 20.0f, 20.0f, 20.0f, 20.0f);

    UICheckbox* c = UICheckbox_Create(1);
    UIStack_AddItem(s, widgcs(c, 26.0f, 26.0f));

    UISpinner* sp = UISpinner_Create(18.0f);
    UISpinner_SetColor(sp, (UIColor){ 59, 130, 246, 1.0f });
    UIStack_AddItem(s, widgcs(sp, 50.0f, 50.0f));

    UIText* lbl = UIText_Create("Checkbox + spinner", 14.0f);
    UIText_SetFontFamily(lbl, UIGetFont("Arial"));
    UIStack_AddItem(s, widgcs(lbl, 200.0f, 20.0f));

    return widgc(s);
}

int main(void) {
    UIApp* app = UIApp_Create("tabs + wrap", 600, 460);
    if (!app) return 1;
    UISearchFonts();
    UIApp_SetWindowIcon(app, "assets/logo.svg");

    UIChildren* children = UIChildren_Create(2);

    UITabView* tv = UITabView_Create(40.0f);
    UITabView_SetFont(tv, UIGetFont("Arial"), 14.0f);
    UITabView_OnChange(tv, OnTabChanged, NULL);

    UITabView_AddTab(tv, "Widgets",   PanelTab1());
    UITabView_AddTab(tv, "Wrap text", PanelTab2());
    UITabView_AddTab(tv, "Controls",  PanelTab3());

    UIWidget* tvW = widgcs(tv, 540.0f, 400.0f);
    UIWidget_SetPosition(tvW, 30.0f, 30.0f);
    UIChildren_Add(children, tvW);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, (UIColor){ 226, 232, 240, 1.0f });
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
