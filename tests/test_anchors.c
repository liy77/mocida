// test_anchors.c
//
// Visual demo of the Mocida alignment / anchor system. Layout:
//
//   - A central "parent" card (slate background, rounded). Its size
//     follows the window via the resize callback, so anchored children
//     reposition live as you drag the window edge.
//
//   - 9 small coloured chips, each anchored to a different combination
//     of UI_ALIGN_V_{TOP,CENTER,BOTTOM} x UI_ALIGN_H_{LEFT,CENTER,RIGHT}
//     relative to the parent card. Their labels say which corner they
//     pin to.
//
//   - 2 chips anchored to OTHER chips (not the parent), to exercise the
//     "anchor to sibling" use case: a yellow "follower" pinned to the
//     right edge of the central blue chip, and a pink one pinned below
//     the bottom-right chip.
//
//   - The window background, the parent card and labels all update
//     automatically when the window resizes — proof that the alignment
//     pass relays out the full subtree.
//
// What to verify:
//   - Resize the window. Every chip stays glued to its anchor point.
//   - The "FOLLOWER" chip follows the centred blue chip horizontally
//     and stays vertically centred next to it.
//   - The pink "TAIL" chip stays anchored under the bottom-right chip.
//   - Closing via the X button exits cleanly.
//
// What this demo does NOT do:
//   - It doesn't use UIStack / UIGrid containers (those have their own
//     tests). Pure raw anchors only.

#include <uikit/app.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------
// State
// ---------------------------------------------------------------------

typedef struct {
    UIApp*   app;
    UIWidget* card;       // central anchor host
    UIChildren* children; // window children
    float padding;        // window-edge breathing room for `card`
} Demo;

static Demo g_demo = {0};

// ---------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------

static UIWidget* make_chip(UIChildren* parent,
                           UIWidget* anchorTarget,
                           uint8_t valign, uint8_t halign,
                           UIColor fill, const char* label,
                           float w, float h,
                           float marginL, float marginT,
                           float marginR, float marginB) {
    UIRectangle* bg = UIRectangle_Create();
    UIRectangle_SetColor(bg, fill);
    UIRectangle_SetRadius(bg, 10.0f);
    UIRectangle_SetBorderWidth(bg, 0.0f);
    UIRectangle_SetMargins(bg, marginL, marginT, marginR, marginB);

    UIWidget* chip = widgcs(bg, w, h);
    UIWidget_SetAlignment(chip, UIAlignment_Create(
        (UIAlign){ .value = valign, .target_widget = anchorTarget },
        (UIAlign){ .value = halign, .target_widget = anchorTarget }
    ));
    UIChildren_Add(parent, chip);

    if (label && *label) {
        UIText* t = UIText_Create((char*)label, 12.0f);
        UIText_SetFontFamily(t, UIGetFont("Arial"));
        UIText_SetColor(t, UI_COLOR_WHITE);
        UIText_SetAlignment(t, UI_TEXT_HALIGN_CENTER, UI_TEXT_VALIGN_CENTER);
        UIWidget* label_w = widgcs(t, w, h);
        UIWidget_SetAlignment(label_w, UIAlignment_Create(
            (UIAlign){ .value = UI_ALIGN_V_CENTER, .target_widget = chip },
            (UIAlign){ .value = UI_ALIGN_H_CENTER, .target_widget = chip }
        ));
        UIChildren_Add(parent, label_w);
    }
    return chip;
}

// Called on every window resize. Resizes the parent card to match the
// window minus padding, then re-runs the alignment pass so all
// anchored children find their new spots.
static void on_resize(int win_w, int win_h, void* userdata) {
    (void)userdata;
    if (!g_demo.card || !g_demo.app || !g_demo.app->mainWidget) return;

    const float card_w = (float)win_w  - 2.0f * g_demo.padding;
    const float card_h = (float)win_h  - 2.0f * g_demo.padding;
    if (card_w <= 0.0f || card_h <= 0.0f) return;

    UIWidget_SetSize(g_demo.card, card_w, card_h);
    UIWidget_SetPosition(g_demo.card, g_demo.padding, g_demo.padding);

    // Cascading relayout: every alignment target with the new card size
    // recomputes its position.
    UIChildren_Relayout(g_demo.children);
}

// ---------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------

int main(void) {
    const int START_W = 900;
    const int START_H = 600;

    UIApp* app = UIApp_Create("Mocida - anchors demo", START_W, START_H);
    if (!app) return 1;
    UISearchFonts();

    UIApp_SetBackgroundColor(app, (UIColor){ 15, 23, 42, 1.0f });
    // Chips have a hard-coded size, so the layout assumes enough card
    // height for three rows + title. Lock in a minimum viewport so the
    // user can't shrink the window into a TOP/CENTER/BOTTOM overlap.
    UIApp_SetMinSize(app, 640, 420);

    UIChildren* children = UIChildren_Create(64);

    // Parent "card" — the anchor host. Initial size matches the window
    // minus a generous padding so it's clearly inset.
    const float padding = 40.0f;
    UIRectangle* cardBg = UIRectangle_Create();
    UIRectangle_SetColor(cardBg, (UIColor){ 30, 41, 59, 1.0f });
    UIRectangle_SetRadius(cardBg, 14.0f);
    UIRectangle_SetBorderColor(cardBg, (UIColor){ 71, 85, 105, 1.0f });
    UIRectangle_SetBorderWidth(cardBg, 1.0f);
    UIWidget* card = widgcs(cardBg, START_W - 2 * padding, START_H - 2 * padding);
    UIWidget_SetPosition(card, padding, padding);
    // Card clips its anchored children: anything that hangs off the
    // edge (e.g. the TAIL pinned below the bottom-right chip) is cut
    // at the card border instead of bleeding into the window
    // background. Demonstrates the UIWidget_SetClipChildren feature.
    UIWidget_SetClipChildren(card, 1);
    UIChildren_Add(children, card);

    // Sub-title on top of the card (anchored to the card itself).
    UIText* title = UIText_Create("Anchored to the slate card. Resize the window.", 14.0f);
    UIText_SetFontFamily(title, UIGetFont("Arial"));
    UIText_SetColor(title, (UIColor){ 148, 163, 184, 1.0f });
    // Centre the text WITHIN its widget bounds. Without this the
    // widget itself is centred on the card but the glyphs left-align
    // inside it, drifting the visible text away from the card centre.
    UIText_SetAlignment(title, UI_TEXT_HALIGN_CENTER, UI_TEXT_VALIGN_CENTER);
    UIWidget* title_w = widgcs(title, 480.0f, 24.0f);
    UIWidget_SetAlignment(title_w, UIAlignment_Create(
        (UIAlign){ .value = UI_ALIGN_V_TOP,    .target_widget = card },
        (UIAlign){ .value = UI_ALIGN_H_CENTER, .target_widget = card }
    ));
    // Push the title down a bit so it doesn't kiss the card border.
    {
        UIRectangle* bg = (UIRectangle*)title->background;
        UIRectangle_SetMargins(bg, 0.0f, 12.0f, 0.0f, 0.0f);
        // re-align after margin update
        UIAlignment_Align(title_w);
    }
    UIChildren_Add(children, title_w);

    // 9-way anchor matrix.
    const UIColor blue   = {  59, 130, 246, 1.0f };
    const UIColor green  = {  34, 197,  94, 1.0f };
    const UIColor purple = { 168,  85, 247, 1.0f };
    const UIColor orange = { 249, 115,  22, 1.0f };
    const UIColor cyan   = {  14, 165, 233, 1.0f };
    const UIColor rose   = { 244,  63,  94, 1.0f };
    const UIColor amber  = { 245, 158,  11, 1.0f };
    const UIColor teal   = {  20, 184, 166, 1.0f };
    const UIColor indigo = {  99, 102, 241, 1.0f };

    const float CHIP_W = 120.0f;
    const float CHIP_H = 56.0f;
    const float INSET  = 16.0f; // distance from the card edge

    make_chip(children, card, UI_ALIGN_V_TOP,    UI_ALIGN_H_LEFT,
              blue,   "TOP / LEFT",   CHIP_W, CHIP_H,
              INSET, 56.0f, 0.0f, 0.0f);     // top inset 56 to clear the title
    make_chip(children, card, UI_ALIGN_V_TOP,    UI_ALIGN_H_CENTER,
              green,  "TOP / CENTER", CHIP_W, CHIP_H,
              0.0f,  56.0f, 0.0f, 0.0f);
    make_chip(children, card, UI_ALIGN_V_TOP,    UI_ALIGN_H_RIGHT,
              purple, "TOP / RIGHT",  CHIP_W, CHIP_H,
              0.0f,  56.0f, INSET, 0.0f);

    UIWidget* centerLeft   = make_chip(children, card,
        UI_ALIGN_V_CENTER, UI_ALIGN_H_LEFT,
        cyan,   "CENTER / LEFT",  CHIP_W, CHIP_H,
        INSET, 0.0f, 0.0f, 0.0f);
    UIWidget* centerCentre = make_chip(children, card,
        UI_ALIGN_V_CENTER, UI_ALIGN_H_CENTER,
        rose,   "CENTER / CENTER", CHIP_W, CHIP_H,
        0.0f, 0.0f, 0.0f, 0.0f);
    UIWidget* centerRight  = make_chip(children, card,
        UI_ALIGN_V_CENTER, UI_ALIGN_H_RIGHT,
        teal,   "CENTER / RIGHT", CHIP_W, CHIP_H,
        0.0f, 0.0f, INSET, 0.0f);
    (void)centerLeft;
    (void)centerRight;

    UIWidget* botLeft  = make_chip(children, card,
        UI_ALIGN_V_BOTTOM, UI_ALIGN_H_LEFT,
        orange, "BOTTOM / LEFT",  CHIP_W, CHIP_H,
        INSET, 0.0f, 0.0f, INSET);
    UIWidget* botCenter = make_chip(children, card,
        UI_ALIGN_V_BOTTOM, UI_ALIGN_H_CENTER,
        amber, "BOTTOM / CENTER", CHIP_W, CHIP_H,
        0.0f, 0.0f, 0.0f, INSET);
    UIWidget* botRight = make_chip(children, card,
        UI_ALIGN_V_BOTTOM, UI_ALIGN_H_RIGHT,
        indigo, "BOTTOM / RIGHT", CHIP_W, CHIP_H,
        0.0f, 0.0f, INSET, INSET);
    (void)botLeft;
    (void)botCenter;

    // --- Sibling anchoring -----------------------------------------
    // Yellow "FOLLOWER" pinned to the right edge of the centred rose
    // chip — vertically centred next to it. Demonstrates anchoring to
    // a non-parent widget.
    UIColor yellow = { 250, 204, 21, 1.0f };
    make_chip(children, centerCentre,
              UI_ALIGN_V_CENTER, UI_ALIGN_H_RIGHT,
              yellow, "FOLLOWER",
              90.0f, 38.0f,
              0.0f, 0.0f, -110.0f, 0.0f); // negative right-margin floats it outside the rose chip's right edge

    // Pink "TAIL" anchored to the bottom-right chip. Vertical = TOP
    // with a NEGATIVE top margin lifts the tail above the chip's top
    // edge, so it sits like a small label floating just above the
    // BOTTOM/RIGHT chip. With clipChildren on the card it would also
    // be clipped at the card border if pushed further out.
    UIColor pink = { 236, 72, 153, 1.0f };
    make_chip(children, botRight,
              UI_ALIGN_V_TOP, UI_ALIGN_H_CENTER,
              pink, "TAIL",
              80.0f, 28.0f,
              0.0f, -36.0f, 0.0f, 0.0f); // -36 = lift it 36px above botRight's top

    UIApp_SetChildren(app, children);
    UIApp_ShowWindow(app);
    UIApp_SetTargetFPS(app, 0); // unlimited so resize feels live

    g_demo.app = app;
    g_demo.card = card;
    g_demo.children = children;
    g_demo.padding = padding;
    UIApp_OnResize(app, on_resize, NULL);

    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
