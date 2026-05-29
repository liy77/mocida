// test_textarea.c
//
// Exercises UITextArea (the multi-line input cousin of UITextField).
//
// What to verify:
//   - Click + type. Enter inserts a newline (does not "submit").
//   - Up / Down move the caret between lines; column is preserved.
//   - Home / End jump to the line bounds; Ctrl+A selects everything.
//   - Click-drag, Shift+arrows, double-click (word), triple-click
//     (current line) all build a selection. Selection rendering spans
//     multiple lines correctly.
//   - Mouse wheel scrolls the content when it overflows the bounds.
//   - Ctrl+C / Ctrl+X / Ctrl+V respect the current selection.

#include <uikit/app.h>
#include <stdio.h>

static UIText* g_status = NULL;

static void on_change(UITextArea* ta, const char* text, void* ud) {
    (void)ta; (void)ud;
    if (!g_status) return;
    int lines = 1;
    for (const char* p = text; *p; p++) if (*p == '\n') lines++;
    char buf[96];
    snprintf(buf, sizeof(buf), "bytes: %d  |  lines: %d",
             (int)strlen(text), lines);
    UIText_SetText(g_status, buf);
}

int main(void) {
    UIApp* app = UIApp_Create("Mocida - textarea", 720, 520);
    if (!app) return 1;
    UISearchFonts();

    UIChildren* children = UIChildren_Create(16);

    UITextArea* ta = UITextArea_Create(
        "Hello, this is a multi-line text input.\n"
        "Press Enter to add a newline.\n"
        "Use Up / Down arrows to move between lines.\n"
        "Double-click a word, triple-click a line, Ctrl+A for all.\n"
        "Scroll with the wheel when content overflows.\n",
        16.0f);
    UITextArea_SetFontFamily(ta, UIGetFont("Arial"));
    UITextArea_SetPlaceholder(ta, "Type something multi-line...");
    UITextArea_SetPadding(ta, 12.0f, 10.0f);
    UITextArea_SetRadius(ta, 8.0f);
    UITextArea_SetLineSpacing(ta, 1.35f);
    UITextArea_OnChange(ta, on_change, NULL);

    UIWidget* taW = widgcs(ta, 660.0f, 360.0f);
    UIWidget_SetPosition(taW, 30.0f, 30.0f);
    UIChildren_Add(children, taW);

    g_status = UIText_Create("bytes: 0  |  lines: 1", 14.0f);
    UIText_SetFontFamily(g_status, UIGetFont("Arial"));
    UIText_SetColor(g_status, (UIColor){ 71, 85, 105, 1.0f });
    UIWidget* statusW = widgc(g_status);
    UIWidget_SetPosition(statusW, 30.0f, 410.0f);
    UIChildren_Add(children, statusW);

    UIText* hint = UIText_Create(
        "Enter = newline | Up/Down move lines | wheel scrolls | "
        "double = word | triple = line | Ctrl+A/C/X/V as usual",
        12.0f);
    UIText_SetFontFamily(hint, UIGetFont("Arial"));
    UIText_SetColor(hint, (UIColor){ 148, 163, 184, 1.0f });
    UIWidget* hintW = widgc(hint);
    UIWidget_SetPosition(hintW, 30.0f, 440.0f);
    UIChildren_Add(children, hintW);

    // Trigger an initial status update.
    on_change(ta, UITextArea_GetText(ta), NULL);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, UI_COLOR_WHITE);
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
