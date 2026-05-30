// Mocida - demo
//
// Single-file showcase that exercises the most important components
// of the library in a tidy layout:
//
//   - UIRectangle + drop shadow (cards)
//   - UIText with rounded background (FPS / target labels)
//   - UIButton with onClick callbacks (toggles AA + theme)
//   - UIMouseArea + dragTarget (drag the blue square anywhere)
//   - UIApp_SetTargetFPS (locked to 60 with sub-millisecond pacing)
//   - UIApp_Destroy at exit so no leaks survive process teardown
//
// The previous demo named its FPS label "Frame time" (incorrect: the
// value is FPS in Hz, not milliseconds) and didn't call UIApp_Destroy
// at the end. Both issues are fixed here.

#include <uikit/app.h>
#include <stdio.h>

#ifdef MOCIDA_IOS
// On iOS the app is launched by SDL's UIKit app delegate
// (UIApplicationMain). Including SDL_main.h renames our main() to
// SDL_main and pulls in the bootstrap the delegate calls.
#include <SDL3/SDL_main.h>
#endif

typedef struct {
    UIWidget* fpsLabel;      // text "FPS: ..."
    UIWidget* targetLabel;   // text "Target: 60 FPS"
    UIWidget* aaLabel;       // text "AA: COVERAGE"

    // Widgets that participate in the resize layout. Tracked so the
    // resize callback can reposition / resize them when the window
    // dimensions change.
    UIWidget* panel;         // white background panel
    UIWidget* titleLabel;
    UIWidget* footerLabel;
    UIWidget* cards[4];      // blue / green / purple / orange (orange = draggable visual)
    UIWidget* dragHit;       // mouse-area on top of cards[3]
    UIWidget* fpsBtn;
    UIWidget* aaBtn;
    UIWidget* trimBtn;

    // Set to 1 the first time the user drags the orange card. Once
    // that happens, OnResize stops auto-positioning the orange tile
    // back into the grid — the user's drag should stick.
    int       orangeDragged;

    UIApp*    app;
    int       aaCycleIndex;
    int       fpsCycleIndex;
} DemoState;

static DemoState g_state;

// --------------------------------------------------------------------
// Event handlers
// --------------------------------------------------------------------

// UI_EVENT_FRAMERATE_CHANGED fires once per second. Update the label
// in-place; the lazy text texture is invalidated by UIText_SetText.
static void OnFpsTick(UIEventData data) {
    if (!g_state.fpsLabel || !g_state.fpsLabel->data) return;
    UIText* t = (UIText*)g_state.fpsLabel->data;

    char buf[32];
    snprintf(buf, sizeof(buf), "FPS: %.0f", data.framerate.fps);
    UIText_SetText(t, buf);
}

static const int   kFpsTargets[]      = { 30, 60, 120, UI_FPS_UNLIMITED };
static const char* kFpsTargetLabels[] = { "30", "60", "120", "UNLIMITED" };
#define FPS_CYCLE_COUNT (int)(sizeof(kFpsTargets) / sizeof(kFpsTargets[0]))

static void OnToggleFps(UIButton* btn, void* ud) {
    (void)ud;
    g_state.fpsCycleIndex = (g_state.fpsCycleIndex + 1) % FPS_CYCLE_COUNT;
    UIApp_SetTargetFPS(g_state.app, kFpsTargets[g_state.fpsCycleIndex]);

    char buf[64];
    snprintf(buf, sizeof(buf), "Target: %s FPS", kFpsTargetLabels[g_state.fpsCycleIndex]);
    if (g_state.targetLabel && g_state.targetLabel->data) {
        UIText_SetText((UIText*)g_state.targetLabel->data, buf);
    }
    // Echo back through the button text so the click feedback is obvious.
    char btnBuf[64];
    snprintf(btnBuf, sizeof(btnBuf), "FPS Target: %s", kFpsTargetLabels[g_state.fpsCycleIndex]);
    UIButton_SetText(btn, btnBuf);
    UIText_DestroyTexture(btn->label);
}

static const UIAAMode kAaModes[]      = { UI_AA_COVERAGE, UI_AA_SSAA_2X, UI_AA_FXAA, UI_AA_TAA };
static const char*    kAaModeLabels[] = { "COVERAGE",     "SSAA 2x",     "FXAA",     "TAA"     };
#define AA_CYCLE_COUNT (int)(sizeof(kAaModes) / sizeof(kAaModes[0]))

static void OnToggleAa(UIButton* btn, void* ud) {
    (void)ud;
    g_state.aaCycleIndex = (g_state.aaCycleIndex + 1) % AA_CYCLE_COUNT;
    UIApp_SetAAMode(g_state.app, kAaModes[g_state.aaCycleIndex]);

    char buf[64];
    snprintf(buf, sizeof(buf), "AA: %s", kAaModeLabels[g_state.aaCycleIndex]);
    if (g_state.aaLabel && g_state.aaLabel->data) {
        UIText_SetText((UIText*)g_state.aaLabel->data, buf);
    }
    char btnBuf[64];
    snprintf(btnBuf, sizeof(btnBuf), "AA Mode: %s", kAaModeLabels[g_state.aaCycleIndex]);
    UIButton_SetText(btn, btnBuf);
    UIText_DestroyTexture(btn->label);
}

static void OnTrimMemory(UIButton* btn, void* ud) {
    (void)btn; (void)ud;
    UIApp_TrimCaches(g_state.app);
    printf("[demo] caches trimmed - GPU/CPU memory released\n");
}

// Latch the "user moved the orange card" flag so future resizes keep
// it where they dropped it instead of snapping back into the grid.
static void OnOrangeDragStart(UIMouseArea* area, UIMouseEvent ev, void* ud) {
    (void)area; (void)ev; (void)ud;
    g_state.orangeDragged = 1;
}

// Fluid resize: keep the white panel inset by 24px on every side, fan
// out the 4 cards in equal columns across the panel, line the buttons
// up under them, and dock the title / FPS labels to the top-left and
// the footer hint to the bottom-left. Called by UIApp_OnResize.
// Set a label/button font size from its widget (responsive text).
static void set_label_font(UIWidget* w, float sz) {
    if (w && w->data) UIText_SetFontSize((UIText*)w->data, sz);
}
static void set_button_font(UIWidget* w, float sz) {
    if (w && w->data) UIButton_SetFontSize((UIButton*)w->data, sz);
}

static void OnResize(int win_w, int win_h, void* userdata) {
    (void)userdata;
    if (win_w <= 0 || win_h <= 0) return;

    // Proportional layout: size everything RELATIVE to the real window so
    // it fills the screen on mobile (not a shrunk-down desktop layout).
    // `compact` switches to a phone-friendly arrangement (stacked header,
    // tighter padding) while keeping readable, fixed font sizes.
    // Compact (phone) layout when either dimension is small — covers both
    // portrait phones and landscape phones (short height).
    const int   compact = (win_w < 700) || (win_h < 600);
    const float pad     = compact ? 14.0f : 24.0f;
    // Keep clear of the notch / Dynamic Island / home indicator: inset the
    // panel by the device safe area so no content hides behind a cutout.
    const UIScreenInsets safe = UIScreen_GetSafeArea();
    // The safe-area top already clears the notch, so add only a small extra
    // margin there (a full `pad` on top of it looked too gappy on a phone).
    const float topGap  = (safe.top > 0) ? 4.0f : pad;
    const float panelX  = pad + (float)safe.left;
    const float panelY  = topGap + (float)safe.top;
    const float panelW  = (float)win_w - panelX - pad - (float)safe.right;
    const float panelH  = (float)win_h - panelY - pad - (float)safe.bottom;
    if (panelW <= 0.0f || panelH <= 0.0f) return;

    // Readable fixed font sizes (slightly smaller on compact, never shrunk
    // to unreadable).
    set_label_font(g_state.titleLabel,  compact ? 22.0f : 28.0f);
    set_label_font(g_state.fpsLabel,    compact ? 15.0f : 18.0f);
    set_label_font(g_state.targetLabel, compact ? 15.0f : 18.0f);
    set_label_font(g_state.aaLabel,     compact ? 15.0f : 18.0f);
    set_label_font(g_state.footerLabel, compact ? 12.0f : 14.0f);
    set_button_font(g_state.fpsBtn,  compact ? 15.0f : 18.0f);
    set_button_font(g_state.aaBtn,   compact ? 15.0f : 18.0f);
    set_button_font(g_state.trimBtn, compact ? 15.0f : 18.0f);

    // White background card
    if (g_state.panel) {
        UIWidget_SetPosition(g_state.panel, panelX, panelY);
        UIWidget_SetSize    (g_state.panel, panelW, panelH);
    }

    const float inset = compact ? 14.0f : 24.0f;
    const float hx    = panelX + inset;

    // Header. On compact, stack the three stat labels vertically (they
    // don't fit side by side on a phone); on desktop keep them in a row.
    if (g_state.titleLabel) UIWidget_SetPosition(g_state.titleLabel, hx, panelY + (compact ? 12.0f : 20.0f));
    const float statY = panelY + (compact ? 44.0f : 66.0f);
    float headerBottom;
    if (compact) {
        if (g_state.fpsLabel)    UIWidget_SetPosition(g_state.fpsLabel,    hx, statY);
        if (g_state.targetLabel) UIWidget_SetPosition(g_state.targetLabel, hx, statY + 22.0f);
        if (g_state.aaLabel)     UIWidget_SetPosition(g_state.aaLabel,     hx, statY + 44.0f);
        headerBottom = statY + 66.0f;
    } else {
        if (g_state.fpsLabel)    UIWidget_SetPosition(g_state.fpsLabel,    hx, statY);
        if (g_state.targetLabel) UIWidget_SetPosition(g_state.targetLabel, hx + 132.0f, statY);
        if (g_state.aaLabel)     UIWidget_SetPosition(g_state.aaLabel,     hx + 300.0f, statY);
        headerBottom = statY + 34.0f;
    }

    // Cards: a row that FILLS the panel width — four equal columns. Height
    // tracks width (3:2-ish), capped so it never dominates a tall phone.
    const float gap         = compact ? 10.0f : 16.0f;
    const float cardsRowY   = headerBottom + (compact ? 12.0f : 24.0f);
    float       cardW       = (panelW - 2.0f * inset - 3.0f * gap) / 4.0f;
    if (cardW < 1.0f) cardW = 1.0f;
    float       cardsRowH   = cardW * 0.66f;
    if (cardsRowH > 180.0f) cardsRowH = 180.0f;
    const float fixedW[4]   = { cardW, cardW, cardW, cardW };
    float       cursorX     = hx;
    for (int i = 0; i < 4; i++) {
        const int isOrange = (i == 3);

        if (g_state.cards[i]) {
            if (isOrange && g_state.orangeDragged) {
                // Keep the user's drop position, only clamp into the
                // new viewport so it isn't stranded off-screen when the
                // window shrinks.
                const float curW = g_state.cards[i]->width  ? *g_state.cards[i]->width  : fixedW[i];
                const float curH = g_state.cards[i]->height ? *g_state.cards[i]->height : cardsRowH;
                float cx = g_state.cards[i]->x;
                float cy = g_state.cards[i]->y;
                if (cx + curW > (float)win_w) cx = (float)win_w - curW;
                if (cy + curH > (float)win_h) cy = (float)win_h - curH;
                if (cx < 0.0f) cx = 0.0f;
                if (cy < 0.0f) cy = 0.0f;
                UIWidget_SetPosition(g_state.cards[i], cx, cy);
            } else {
                UIWidget_SetPosition(g_state.cards[i], cursorX, cardsRowY);
                UIWidget_SetSize    (g_state.cards[i], fixedW[i], cardsRowH);
            }
        }

        if (isOrange && g_state.dragHit) {
            if (g_state.orangeDragged && g_state.cards[3]) {
                UIWidget_SetPosition(g_state.dragHit,
                    g_state.cards[3]->x, g_state.cards[3]->y);
                UIWidget_SetSize(g_state.dragHit, fixedW[3], cardsRowH);
            } else {
                UIWidget_SetPosition(g_state.dragHit, cursorX, cardsRowY);
                UIWidget_SetSize    (g_state.dragHit, fixedW[i], cardsRowH);
            }
            UIMouseArea* area = (UIMouseArea*)g_state.dragHit->data;
            if (area) UIMouseArea_SetDragBounds(area, 0.0f, 0.0f, (float)win_w, (float)win_h);
        }

        cursorX += fixedW[i] + gap;
    }

    // Buttons. On compact (phone) the labels ("AA Mode: COVERAGE", …) don't
    // fit three-across, and buttons can't wrap text — so STACK them
    // full-width, one per row. On desktop keep the three-column row.
    const float btnRowY = cardsRowY + cardsRowH + (compact ? 18.0f : 50.0f);
    const float btnH    = compact ? 46.0f : 52.0f;
    const float fullW   = panelW - 2.0f * inset;
    if (compact) {
        if (g_state.fpsBtn) {
            UIWidget_SetPosition(g_state.fpsBtn, hx, btnRowY);
            UIWidget_SetSize    (g_state.fpsBtn, fullW, btnH);
        }
        if (g_state.aaBtn) {
            UIWidget_SetPosition(g_state.aaBtn, hx, btnRowY + (btnH + gap));
            UIWidget_SetSize    (g_state.aaBtn, fullW, btnH);
        }
        if (g_state.trimBtn) {
            UIWidget_SetPosition(g_state.trimBtn, hx, btnRowY + 2.0f * (btnH + gap));
            UIWidget_SetSize    (g_state.trimBtn, fullW, btnH);
        }
    } else {
        const float btnW = (fullW - 2.0f * gap) / 3.0f;
        if (g_state.fpsBtn) {
            UIWidget_SetPosition(g_state.fpsBtn, hx, btnRowY);
            UIWidget_SetSize    (g_state.fpsBtn, btnW, btnH);
        }
        if (g_state.aaBtn) {
            UIWidget_SetPosition(g_state.aaBtn, hx + btnW + gap, btnRowY);
            UIWidget_SetSize    (g_state.aaBtn, btnW, btnH);
        }
        if (g_state.trimBtn) {
            UIWidget_SetPosition(g_state.trimBtn, hx + 2.0f * (btnW + gap), btnRowY);
            UIWidget_SetSize    (g_state.trimBtn, btnW, btnH);
        }
    }

    // Footer: wrap the hint to the panel width (it's a full sentence) and
    // anchor it near the bottom, leaving room for the wrapped second line.
    if (g_state.footerLabel) {
        if (g_state.footerLabel->data)
            UIText_SetWrapWidth((UIText*)g_state.footerLabel->data, (int)fullW);
        UIWidget_SetPosition(g_state.footerLabel, hx,
                             panelY + panelH - (compact ? 56.0f : 40.0f));
    }
}

// --------------------------------------------------------------------
// Small builder helpers to keep main() readable
// --------------------------------------------------------------------

static UIWidget* BuildLabel(UIChildren* children, const char* text,
                            float fontSize, UIColor color,
                            float x, float y) {
    UIText* t = UIText_Create((char*)text, fontSize);
    UIText_SetFontFamily(t, UIGetFont("Arial"));
    UIText_SetColor(t, color);
    UIWidget* w = widgc(t);
    UIWidget_SetPosition(w, x, y);
    UIChildren_Add(children, w);
    return w;
}

static UIWidget* BuildCard(UIChildren* children,
                           float x, float y, float w, float h,
                           UIColor color, float radius) {
    UIRectangle* r = UIRectangle_Create();
    UIRectangle_SetColor(r, color);
    UIRectangle_SetRadius(r, radius);
    UIRectangle_SetShadow(r, (UIShadow){
        .offsetX = 0.0f, .offsetY = 8.0f,
        .blur    = 18.0f, .spread = -2.0f,
        .color   = { 0, 0, 0, 0.18f }
    });
    UIWidget* widget = widgcs(r, w, h);
    UIWidget_SetPosition(widget, x, y);
    UIChildren_Add(children, widget);
    return widget;
}

// Helper that pairs a visible card with a UIMouseArea sized to match,
// using dragTarget so both move together when dragged. Returns the
// mouse-area widget so callers can track it for resize updates.
static UIWidget* BuildDraggable(UIChildren* children,
                                float x, float y, float w, float h,
                                UIColor color, float radius,
                                float boundsW, float boundsH,
                                UIWidget** outCard) {
    UIWidget* card = BuildCard(children, x, y, w, h, color, radius);
    UIWidget_SetZIndex(card, 5);

    UIMouseArea* area = UIMouseArea_Create();
    UIMouseArea_SetDraggable(area, 1);
    UIMouseArea_SetDragTarget(area, card);
    UIMouseArea_SetDragBounds(area, 0.0f, 0.0f, boundsW, boundsH);

    UIWidget* hit = widgcs(area, w, h);
    UIWidget_SetPosition(hit, x, y);
    UIWidget_SetZIndex(hit, 100); // above the card so it captures
    UIChildren_Add(children, hit);
    if (outCard) *outCard = card;
    return hit;
}

// --------------------------------------------------------------------
// main
// --------------------------------------------------------------------

int main(int argc, char** argv) {
    (void)argc; (void)argv;   // SDL_main signature (also matches winmain shim)
    const int WIN_W = 1024;
    const int WIN_H = 640;

    UIApp* app = UIApp_Create("Mocida demo", WIN_W, WIN_H);
    if (!app) return 1;

    g_state.app = app;
    g_state.aaCycleIndex  = 0;
    g_state.fpsCycleIndex = 1; // start at 60

    // Lock to 60 FPS - PreciseDelay gives sub-ms pacing accuracy.
    UIApp_SetTargetFPS(app, 60);
    UIApp_SetRenderQuality(app, UI_QUALITY_HIGH);
    UIApp_SetAAMode(app, UI_AA_COVERAGE);

    UIApp_SetWindowIcon(app, "assets/logo.svg");
    UISearchFonts();

    UIChildren* children = UIChildren_Create(16);

    // Background panel
    UIRectangle* panel = UIRectangle_Create();
    UIRectangle_SetColor(panel, UI_COLOR_WHITE);
    UIRectangle_SetRadius(panel, 14.0f);
    UIWidget* panelW = widgcs(panel, WIN_W - 48.0f, WIN_H - 48.0f);
    UIWidget_SetPosition(panelW, 24.0f, 24.0f);
    UIChildren_Add(children, panelW);
    g_state.panel = panelW;

    // Header (title + FPS line)
    g_state.titleLabel = BuildLabel(children, "Mocida - demo", 28.0f,
                                    (UIColor){ 15, 23, 42, 1.0f }, 48.0f, 44.0f);

    g_state.fpsLabel = BuildLabel(children, "FPS: 60", 18.0f,
                                   (UIColor){ 71, 85, 105, 1.0f },
                                   48.0f, 90.0f);

    g_state.targetLabel = BuildLabel(children, "Target: 60 FPS", 18.0f,
                                      (UIColor){ 71, 85, 105, 1.0f },
                                      180.0f, 90.0f);

    g_state.aaLabel = BuildLabel(children, "AA: COVERAGE", 18.0f,
                                  (UIColor){ 71, 85, 105, 1.0f },
                                  360.0f, 90.0f);

    // The "Target:" and "AA:" labels show variable-length strings
    // (e.g. "Target: UNLIMITED FPS" is much wider than "Target: 60 FPS").
    // Give each label a fixed slot width and turn on UI_WRAP_FIT so the
    // text scales DOWN inside the slot instead of overflowing into the
    // next label. wrapToBounds(1) tells FIT to read the slot width
    // from the widget itself rather than from wrapWidth (px).
    UIText* tTarget = (UIText*)g_state.targetLabel->data;
    UIText_SetWrapToBounds(tTarget, 1);
    UIText_SetWrapMode    (tTarget, UI_WRAP_FIT);
    UIWidget_SetSize(g_state.targetLabel, 170.0f, 28.0f);

    UIText* tAa = (UIText*)g_state.aaLabel->data;
    UIText_SetWrapToBounds(tAa, 1);
    UIText_SetWrapMode    (tAa, UI_WRAP_FIT);
    UIWidget_SetSize(g_state.aaLabel, 160.0f, 28.0f);

    // Four demo cards (the last one is also the drag target).
    g_state.cards[0] = BuildCard(children,  48.0f, 160.0f, 240.0f, 160.0f,
              (UIColor){ 59, 130, 246, 1.0f }, 14.0f);
    g_state.cards[1] = BuildCard(children, 312.0f, 160.0f, 240.0f, 160.0f,
              (UIColor){ 34, 197, 94, 1.0f }, 14.0f);
    g_state.cards[2] = BuildCard(children, 576.0f, 160.0f, 240.0f, 160.0f,
              (UIColor){ 168, 85, 247, 1.0f }, 14.0f);

    // Draggable orange card on the right - the mouse-area hit widget
    // and its visual sit at the same position. Track both for resize.
    UIWidget* orangeCard = NULL;
    g_state.dragHit = BuildDraggable(children, 840.0f, 160.0f, 140.0f, 160.0f,
                                     (UIColor){ 251, 146, 60, 1.0f }, 14.0f,
                                     (float)WIN_W, (float)WIN_H,
                                     &orangeCard);
    g_state.cards[3] = orangeCard;

    // Latch the "user-dragged" flag so subsequent resizes don't snap
    // the orange card back to the grid layout.
    if (g_state.dragHit && g_state.dragHit->data) {
        UIMouseArea_OnDragStart((UIMouseArea*)g_state.dragHit->data,
                                OnOrangeDragStart, NULL);
    }

    // Action buttons (FPS / AA / Trim)
    UIButton* fpsBtn = UIButton_Create("FPS Target: 60", 18.0f);
    UIButton_SetFontFamily(fpsBtn, UIGetFont("Arial"));
    UIButton_SetRadius(fpsBtn, 8.0f);
    UIButton_SetColors(fpsBtn, (UIColor){ 59, 130, 246, 1.0f }, UI_COLOR_WHITE);
    UIButton_SetShadow (fpsBtn, UI_SHADOW_DEFAULT);
    UIButton_OnClick   (fpsBtn, OnToggleFps, NULL);
    UIWidget* fpsBtnW = widgcs(fpsBtn, 220.0f, 52.0f);
    UIWidget_SetPosition(fpsBtnW, 48.0f, 380.0f);
    UIChildren_Add(children, fpsBtnW);
    g_state.fpsBtn = fpsBtnW;

    UIButton* aaBtn = UIButton_Create("AA Mode: COVERAGE", 18.0f);
    UIButton_SetFontFamily(aaBtn, UIGetFont("Arial"));
    UIButton_SetRadius(aaBtn, 8.0f);
    UIButton_SetColors(aaBtn, (UIColor){ 34, 197, 94, 1.0f }, UI_COLOR_WHITE);
    UIButton_SetShadow (aaBtn, UI_SHADOW_DEFAULT);
    UIButton_OnClick   (aaBtn, OnToggleAa, NULL);
    UIWidget* aaBtnW = widgcs(aaBtn, 240.0f, 52.0f);
    UIWidget_SetPosition(aaBtnW, 288.0f, 380.0f);
    UIChildren_Add(children, aaBtnW);
    g_state.aaBtn = aaBtnW;

    UIButton* trimBtn = UIButton_Create("Trim caches", 18.0f);
    UIButton_SetFontFamily(trimBtn, UIGetFont("Arial"));
    UIButton_SetRadius(trimBtn, 8.0f);
    UIButton_SetColors(trimBtn, (UIColor){ 168, 85, 247, 1.0f }, UI_COLOR_WHITE);
    UIButton_SetShadow (trimBtn, UI_SHADOW_DEFAULT);
    UIButton_OnClick   (trimBtn, OnTrimMemory, NULL);
    UIWidget* trimBtnW = widgcs(trimBtn, 180.0f, 52.0f);
    UIWidget_SetPosition(trimBtnW, 548.0f, 380.0f);
    UIChildren_Add(children, trimBtnW);
    g_state.trimBtn = trimBtnW;

    // Footer hint
    g_state.footerLabel = BuildLabel(children,
               "Drag the orange card. Click the buttons to toggle FPS/AA/trim caches.",
               14.0f, (UIColor){ 100, 116, 139, 1.0f },
               48.0f, WIN_H - 64.0f);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, (UIColor){ 226, 232, 240, 1.0f });
    UIApp_SetEventCallback (app, UI_EVENT_FRAMERATE_CHANGED, OnFpsTick);
    UIApp_OnResize        (app, OnResize, NULL);

    // Run the resize hook once so the initial layout uses the same
    // fluid logic as later resizes (avoids divergence between the
    // hand-coded `BuildCard` positions above and the OnResize math).
    // Use the REAL window size (on iOS the OS sized it to the device
    // screen, not the requested WIN_W/WIN_H desktop default).
    OnResize(UIApp_GetWidth(app), UIApp_GetHeight(app), NULL);

    // The debug overlay is opt-in. Enable it here so the demo shows it
    // off; toggle live with F9 (bounds) · F10 (stats HUD) · F8 (heatmap)
    // · F12 (all). Start with the stats/FPS HUD visible.
    UIDebugOverlay_SetEnabled(1);
    UIDebugOverlay_SetFlags(UI_OVERLAY_STATS);

    UIApp_ShowWindow(app);
    UIApp_Run(app);

    // Clean teardown - the old demo skipped this, leaving the entire
    // app tree leaked at exit. UIApp_Destroy walks children with the
    // proper widget destructors (now that UIChildren_Remove/Clear
    // route through UIWidget_Destroy).
    UIApp_Destroy(app);
    return 0;
}
