// test_textfield.c
//
// Exercises UITextField: click to focus, type, edit, paste (Ctrl+V),
// copy (Ctrl+C), submit on Enter. Three fields:
//   1. Plain text with placeholder.
//   2. Password field with masking.
//   3. Bounded field (maxLength = 12).
//
// A label below mirrors what the focused plain-text field contains, so
// you can confirm that onChange fires and the data is what you typed.

#include <uikit/app.h>
#include <stdio.h>

static UIWidget* g_echoLabel = NULL;
static UIWidget* g_submitLabel = NULL;

static void OnChange(UITextField* tf, const char* text, void* ud) {
    (void)tf; (void)ud;
    if (!g_echoLabel || !g_echoLabel->data) return;
    char buf[160];
    snprintf(buf, sizeof(buf), "value = \"%s\"", text);
    UIText_SetText((UIText*)g_echoLabel->data, buf);
}

static void OnSubmit(UITextField* tf, const char* text, void* ud) {
    (void)tf; (void)ud;
    if (!g_submitLabel || !g_submitLabel->data) return;
    char buf[200];
    snprintf(buf, sizeof(buf), "submitted: \"%s\"", text);
    UIText_SetText((UIText*)g_submitLabel->data, buf);
    printf("[textfield] submit: %s\n", text);
}

int main(void) {
    UIApp* app = UIApp_Create("textfield", 720, 480);
    if (!app) return 1;
    UISearchFonts();
    UIApp_SetWindowIcon(app, "assets/logo.svg");

    UIChildren* children = UIChildren_Create(16);

    // Plain field
    UITextField* tf1 = UITextField_Create("", 18.0f);
    UITextField_SetFontFamily(tf1, UIGetFont("Arial"));
    UITextField_SetPlaceholder(tf1, "Type something here...");
    UITextField_OnChange(tf1, OnChange, NULL);
    UITextField_OnSubmit(tf1, OnSubmit, NULL);
    UIWidget* tf1W = widgcs(tf1, 480.0f, 44.0f);
    UIWidget_SetPosition(tf1W, 30.0f, 60.0f);
    UIChildren_Add(children, tf1W);

    // Password field
    UITextField* tf2 = UITextField_Create("", 18.0f);
    UITextField_SetFontFamily(tf2, UIGetFont("Arial"));
    UITextField_SetPlaceholder(tf2, "Password");
    UITextField_SetPassword(tf2, 1);
    UIWidget* tf2W = widgcs(tf2, 480.0f, 44.0f);
    UIWidget_SetPosition(tf2W, 30.0f, 130.0f);
    UIChildren_Add(children, tf2W);

    // Limited
    UITextField* tf3 = UITextField_Create("", 18.0f);
    UITextField_SetFontFamily(tf3, UIGetFont("Arial"));
    UITextField_SetPlaceholder(tf3, "Max 12 chars");
    UITextField_SetMaxLength(tf3, 12);
    UIWidget* tf3W = widgcs(tf3, 480.0f, 44.0f);
    UIWidget_SetPosition(tf3W, 30.0f, 200.0f);
    UIChildren_Add(children, tf3W);

    // Echo labels
    UIText* echo = UIText_Create("value = \"\"", 16.0f);
    UIText_SetFontFamily(echo, UIGetFont("Arial"));
    UIText_SetColor(echo, (UIColor){ 71, 85, 105, 1.0f });
    g_echoLabel = widgc(echo);
    UIWidget_SetPosition(g_echoLabel, 30.0f, 280.0f);
    UIChildren_Add(children, g_echoLabel);

    UIText* sub = UIText_Create("submitted: (none yet)", 16.0f);
    UIText_SetFontFamily(sub, UIGetFont("Arial"));
    UIText_SetColor(sub, (UIColor){ 71, 85, 105, 1.0f });
    g_submitLabel = widgc(sub);
    UIWidget_SetPosition(g_submitLabel, 30.0f, 310.0f);
    UIChildren_Add(children, g_submitLabel);

    UIText* hint = UIText_Create(
        "Click to focus. Enter = submit. Ctrl+C/V copy/paste. Esc unfocuses.",
        13.0f);
    UIText_SetFontFamily(hint, UIGetFont("Arial"));
    UIText_SetColor(hint, (UIColor){ 100, 116, 139, 1.0f });
    UIWidget* hintW = widgc(hint);
    UIWidget_SetPosition(hintW, 30.0f, 430.0f);
    UIChildren_Add(children, hintW);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, (UIColor){ 248, 250, 252, 1.0f });
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
