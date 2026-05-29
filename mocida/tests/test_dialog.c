// test_dialog.c
//
// Click a button to open a modal dialog. The dialog has its own title,
// body text and two buttons (OK / Cancel). Backdrop click dismisses it
// because dismissOnBackdrop defaults to 1.

#include <uikit/app.h>
#include <stdio.h>

static UIDialog* g_dlg = NULL;

static void OnOpen(UIButton* b, void* ud) {
    (void)b; (void)ud;
    if (g_dlg) UIDialog_Show(g_dlg);
}
static void OnCancel(UIButton* b, void* ud) {
    (void)b; (void)ud;
    if (g_dlg) UIDialog_Hide(g_dlg);
    printf("[dialog] cancel\n");
}
static void OnOk(UIButton* b, void* ud) {
    (void)b; (void)ud;
    if (g_dlg) UIDialog_Hide(g_dlg);
    printf("[dialog] confirmed\n");
}
static void OnDismiss(UIDialog* d, void* ud) {
    (void)d; (void)ud;
    printf("[dialog] dismissed via backdrop\n");
}

int main(void) {
    const int WIN_W = 640, WIN_H = 460;
    UIApp* app = UIApp_Create("dialog", WIN_W, WIN_H);
    if (!app) return 1;
    UISearchFonts();
    UIApp_SetWindowIcon(app, "assets/logo.svg");

    UIChildren* children = UIChildren_Create(8);

    // Some background content so the dialog feels modal.
    UIRectangle* bg = UIRectangle_Create();
    UIRectangle_SetColor(bg, UI_COLOR_WHITE);
    UIRectangle_SetRadius(bg, 12.0f);
    UIRectangle_SetShadow(bg, UI_SHADOW_DEFAULT);
    UIWidget* bgW = widgcs(bg, 540.0f, 320.0f);
    UIWidget_SetPosition(bgW, 50.0f, 50.0f);
    UIChildren_Add(children, bgW);

    UIButton* openBtn = UIButton_Create("Open dialog", 16.0f);
    UIButton_SetFontFamily(openBtn, UIGetFont("Arial"));
    UIButton_SetRadius(openBtn, 8.0f);
    UIButton_OnClick(openBtn, OnOpen, NULL);
    UIWidget* openBtnW = widgcs(openBtn, 200.0f, 48.0f);
    UIWidget_SetPosition(openBtnW, 80.0f, 100.0f);
    UIChildren_Add(children, openBtnW);

    // Dialog at full window bounds (z-index high so it's drawn last).
    UIDialog* dlg = UIDialog_Create(400.0f, 220.0f);
    UIDialog_OnDismiss(dlg, OnDismiss, NULL);
    g_dlg = dlg;

    // Dialog content (positions are relative to the card top-left).
    UIText* dlgTitle = UIText_Create("Are you sure?", 22.0f);
    UIText_SetFontFamily(dlgTitle, UIGetFont("Arial"));
    UIText_SetColor(dlgTitle, (UIColor){ 15, 23, 42, 1.0f });
    UIWidget* dlgTitleW = widgc(dlgTitle);
    UIWidget_SetPosition(dlgTitleW, 24.0f, 24.0f);
    UIDialog_AddContent(dlg, dlgTitleW);

    UIText* dlgBody = UIText_Create("This action cannot be undone.", 15.0f);
    UIText_SetFontFamily(dlgBody, UIGetFont("Arial"));
    UIText_SetColor(dlgBody, (UIColor){ 71, 85, 105, 1.0f });
    UIWidget* dlgBodyW = widgc(dlgBody);
    UIWidget_SetPosition(dlgBodyW, 24.0f, 70.0f);
    UIDialog_AddContent(dlg, dlgBodyW);

    UIButton* cancelBtn = UIButton_Create("Cancel", 14.0f);
    UIButton_SetFontFamily(cancelBtn, UIGetFont("Arial"));
    UIButton_SetRadius(cancelBtn, 6.0f);
    UIButton_SetColors(cancelBtn, (UIColor){ 226, 232, 240, 1.0f }, (UIColor){ 15, 23, 42, 1.0f });
    UIButton_OnClick(cancelBtn, OnCancel, NULL);
    UIWidget* cancelBtnW = widgcs(cancelBtn, 110.0f, 38.0f);
    UIWidget_SetPosition(cancelBtnW, 150.0f, 160.0f);
    UIDialog_AddContent(dlg, cancelBtnW);

    UIButton* okBtn = UIButton_Create("Confirm", 14.0f);
    UIButton_SetFontFamily(okBtn, UIGetFont("Arial"));
    UIButton_SetRadius(okBtn, 6.0f);
    UIButton_SetColors(okBtn, (UIColor){ 239, 68, 68, 1.0f }, UI_COLOR_WHITE);
    UIButton_OnClick(okBtn, OnOk, NULL);
    UIWidget* okBtnW = widgcs(okBtn, 110.0f, 38.0f);
    UIWidget_SetPosition(okBtnW, 270.0f, 160.0f);
    UIDialog_AddContent(dlg, okBtnW);

    UIWidget* dlgW = widgcs(dlg, (float)WIN_W, (float)WIN_H);
    UIWidget_SetPosition(dlgW, 0.0f, 0.0f);
    UIWidget_SetZIndex(dlgW, 9999);
    UIChildren_Add(children, dlgW);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, (UIColor){ 226, 232, 240, 1.0f });
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
