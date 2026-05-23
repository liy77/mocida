// test_clipboard.c
//
// Exercises the UIClipboard wrapper. Two text fields:
//   - "Source": click to focus, type something, then click "Copy".
//   - "Dest"  : click "Paste" to replace its contents with whatever is
//              currently on the clipboard.
//
// What to verify:
//   - Pressing Copy puts the source field's contents on the system
//     clipboard (paste with Ctrl+V into any other app to confirm).
//   - Pressing Paste writes the current clipboard content into the
//     destination field.
//   - UIClipboard_HasText reflects whether the clipboard has content
//     (status text at the bottom).

#include <uikit/app.h>
#include <stdio.h>

static UITextField* g_source = NULL;
static UITextField* g_dest   = NULL;
static UIText*      g_status = NULL;

static void update_status(void) {
    if (!g_status) return;
    const int has = UIClipboard_HasText();
    char buf[64];
    snprintf(buf, sizeof(buf), "Clipboard has text: %s", has ? "yes" : "no");
    UIText_SetText(g_status, buf);
}

// Callback typedef differs between projects; this matches UIButtonCallback.
static void on_copy(UIButton* btn, void* userdata) {
    (void)btn; (void)userdata;
    if (!g_source) return;
    const char* txt = UITextField_GetText(g_source);
    UIClipboard_SetText(txt ? txt : "");
    update_status();
}

static void on_paste(UIButton* btn, void* userdata) {
    (void)btn; (void)userdata;
    if (!g_dest) return;
    char* txt = UIClipboard_GetText();
    UITextField_SetText(g_dest, txt ? txt : "");
    UIClipboard_FreeText(txt);
    update_status();
}

static UIWidget* make_field(UIChildren* children, const char* placeholder,
                            UITextField** outRef,
                            float x, float y, float w, float h) {
    UITextField* tf = UITextField_Create("", 18.0f);
    UITextField_SetPlaceholder(tf, placeholder);
    UITextField_SetFontFamily(tf, UIGetFont("Arial"));
    UIWidget* widget = widgcs(tf, w, h);
    UIWidget_SetPosition(widget, x, y);
    UIChildren_Add(children, widget);
    if (outRef) *outRef = tf;
    return widget;
}

static UIWidget* make_button(UIChildren* children, const char* label,
                             UIButtonCallback cb,
                             float x, float y, float w, float h) {
    UIButton* b = UIButton_Create(label, 16.0f);
    UIButton_SetFontFamily(b, UIGetFont("Arial"));
    UIButton_SetRadius(b, 6.0f);
    UIButton_OnClick(b, cb, NULL);
    UIWidget* widget = widgcs(b, w, h);
    UIWidget_SetPosition(widget, x, y);
    UIChildren_Add(children, widget);
    return widget;
}

int main(void) {
    UIApp* app = UIApp_Create("Mocida - clipboard", 600, 320);
    if (!app) return 1;
    UISearchFonts();

    UIChildren* children = UIChildren_Create(16);

    make_field(children, "Source: type here, then Copy", &g_source,
               30.0f, 40.0f, 360.0f, 36.0f);
    make_button(children, "Copy", on_copy,
                400.0f, 40.0f, 80.0f, 36.0f);

    make_field(children, "Dest: shows the pasted text", &g_dest,
               30.0f, 120.0f, 360.0f, 36.0f);
    make_button(children, "Paste", on_paste,
                400.0f, 120.0f, 80.0f, 36.0f);

    g_status = UIText_Create("Clipboard has text: ?", 14.0f);
    UIText_SetFontFamily(g_status, UIGetFont("Arial"));
    UIText_SetColor(g_status, (UIColor){ 71, 85, 105, 1.0f });
    UIWidget* statusW = widgc(g_status);
    UIWidget_SetPosition(statusW, 30.0f, 220.0f);
    UIChildren_Add(children, statusW);

    update_status();

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, UI_COLOR_WHITE);
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
