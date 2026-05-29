// test_button.c
//
// Demonstrates the UIButton component:
//   - Three buttons with different colors (primary / success / danger).
//   - The 4th starts DISABLED and is re-enabled by clicking the primary.
//   - The counter on the primary's label increments on every click.
//   - Showcases automatic hover/pressed transitions via the internal
//     state machine.
//
// How to interact:
//   - Move the mouse over the buttons -> they lighten (HOVER).
//   - Press -> they darken and the label drops 1px (PRESSED).
//   - Release inside the button -> fires onClick.
//   - Release outside -> click is cancelled (proper button state machine).

#include <uikit/app.h>
#include <stdio.h>

static int g_counter = 0;

// Forward declarations so handlers can reference each other.
static void OnPrimaryClick(UIButton* b, void* userdata);
static void OnSuccessClick(UIButton* b, void* userdata);
static void OnDangerClick(UIButton* b, void* userdata);

static void OnPrimaryClick(UIButton* b, void* userdata) {
    UIButton* disabledOne = (UIButton*)userdata;
    g_counter++;

    char buf[64];
    snprintf(buf, sizeof(buf), "Clicks: %d", g_counter);
    UIButton_SetText(b, buf);
    // Force the label texture to regenerate with the new text.
    UIText_DestroyTexture(b->label);

    // Re-enable the fourth button on the first click.
    if (disabledOne) {
        UIButton_SetEnabled(disabledOne, 1);
    }
}

static void OnSuccessClick(UIButton* b, void* userdata) {
    (void)b; (void)userdata;
    printf("[success] OK clicked\n");
}

static void OnDangerClick(UIButton* b, void* userdata) {
    UIApp* app = (UIApp*)userdata;
    (void)b;
    printf("[danger] closing the app\n");
    if (app && app->window) app->window->visible = 0;
}

int main(void) {
    UIApp* app = UIApp_Create("Mocida - buttons", 720, 320);
    if (!app) return 1;
    UISearchFonts();

    UIChildren* children = UIChildren_Create(8);

    // ----- Button 1: primary -----
    UIButton* primary = UIButton_Create("Clicks: 0", 22.0f);
    UIButton_SetFontFamily(primary, UIGetFont("Arial"));
    UIButton_SetRadius(primary, 8.0f);
    UIWidget* w1 = widgcs(primary, 200.0f, 56.0f);
    UIWidget_SetPosition(w1, 30.0f, 60.0f);
    UIChildren_Add(children, w1);

    // ----- Button 2: success (green) -----
    UIButton* success = UIButton_Create("Confirm", 22.0f);
    UIButton_SetFontFamily(success, UIGetFont("Arial"));
    UIButton_SetRadius(success, 8.0f);
    UIButton_SetColors(success, (UIColor){34, 197, 94, 1.0f}, UI_COLOR_WHITE);
    UIButton_OnClick(success, OnSuccessClick, NULL);
    UIWidget* w2 = widgcs(success, 200.0f, 56.0f);
    UIWidget_SetPosition(w2, 260.0f, 60.0f);
    UIChildren_Add(children, w2);

    // ----- Button 3: danger (red) - closes the app -----
    UIButton* danger = UIButton_Create("Close", 22.0f);
    UIButton_SetFontFamily(danger, UIGetFont("Arial"));
    UIButton_SetRadius(danger, 8.0f);
    UIButton_SetColors(danger, (UIColor){239, 68, 68, 1.0f}, UI_COLOR_WHITE);
    UIButton_OnClick(danger, OnDangerClick, app);
    UIWidget* w3 = widgcs(danger, 200.0f, 56.0f);
    UIWidget_SetPosition(w3, 490.0f, 60.0f);
    UIChildren_Add(children, w3);

    // ----- Button 4: starts DISABLED, the primary unlocks it -----
    UIButton* secret = UIButton_Create("Unlocked by primary", 18.0f);
    UIButton_SetFontFamily(secret, UIGetFont("Arial"));
    UIButton_SetRadius(secret, 8.0f);
    UIButton_SetColors(secret, (UIColor){168, 85, 247, 1.0f}, UI_COLOR_WHITE);
    UIButton_SetEnabled(secret, 0);
    UIButton_OnClick(secret, OnSuccessClick, NULL);
    UIWidget* w4 = widgcs(secret, 430.0f, 56.0f);
    UIWidget_SetPosition(w4, 30.0f, 160.0f);
    UIChildren_Add(children, w4);

    // The primary's onClick references 'secret' so it can re-enable it.
    UIButton_OnClick(primary, OnPrimaryClick, secret);

    // Helper hint at the bottom of the window.
    UIText* hint = UIText_Create("Move the mouse, click, hold and drag to see the states.", 14.0f);
    UIText_SetFontFamily(hint, UIGetFont("Arial"));
    UIText_SetColor(hint, (UIColor){100, 116, 139, 1.0f});
    UIWidget* hw = widgc(hint);
    UIWidget_SetPosition(hw, 30.0f, 250.0f);
    UIChildren_Add(children, hw);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, (UIColor){248, 250, 252, 1.0f});
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
