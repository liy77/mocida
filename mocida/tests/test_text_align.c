// test_text_align.c
//
// Exercises UIText_SetHAlign (LEFT / CENTER / RIGHT) and
// UIText_SetVAlign (TOP / CENTER / BOTTOM). Lays out a 3x3 grid of
// fixed-size slots so each H x V combo can be visually checked.
//
// What to verify:
//   - All 9 cells have the same outer rectangle (220 x 90).
//   - "LEFT  / TOP"     -> top-left of the slot
//   - "CENTER / TOP"    -> top centred horizontally
//   - "RIGHT / TOP"     -> top-right
//   - "LEFT  / CENTER"  -> vertically centred, hugs the left edge
//   - "CENTER/ CENTER"  -> dead centre
//   - "RIGHT / CENTER"  -> vertically centred, hugs the right edge
//   - "LEFT  / BOTTOM"  -> bottom-left
//   - "CENTER / BOTTOM" -> bottom centred
//   - "RIGHT / BOTTOM"  -> bottom-right
//
// Tight control (no explicit width / height larger than the glyphs)
// should still render unchanged - alignment is a no-op there.

#include <uikit/app.h>

static UIWidget* make_slot(UIChildren* children, float x, float y,
                           float w, float h) {
    UIRectangle* bg = UIRectangle_Create();
    UIRectangle_SetColor(bg, (UIColor){ 241, 245, 249, 1.0f });
    UIRectangle_SetRadius(bg, 4.0f);
    UIRectangle_SetBorderWidth(bg, 1.0f);
    UIRectangle_SetBorderColor(bg, (UIColor){ 148, 163, 184, 1.0f });
    UIWidget* slot = widgcs(bg, w, h);
    UIWidget_SetPosition(slot, x, y);
    UIChildren_Add(children, slot);
    return slot;
}

static void add_label(UIChildren* children, const char* msg,
                      UITextHAlign h, UITextVAlign v,
                      float x, float y, float w, float ht) {
    UIText* t = UIText_Create((char*)msg, 18.0f);
    UIText_SetFontFamily(t, UIGetFont("Arial"));
    UIText_SetColor(t, UI_COLOR_BLACK);
    UIText_SetAlignment(t, h, v);

    UIWidget* widget = widgcs(t, w, ht);
    UIWidget_SetPosition(widget, x, y);
    UIChildren_Add(children, widget);
}

int main(void) {
    UIApp* app = UIApp_Create("Mocida - text H + V align", 820, 460);
    if (!app) return 1;
    UISearchFonts();

    UIChildren* children = UIChildren_Create(32);

    const float W  = 240.0f;
    const float H  = 90.0f;
    const float SX = 30.0f, SY = 30.0f;
    const float GX = W + 20.0f;
    const float GY = H + 20.0f;

    const struct { const char* label; UITextHAlign h; } hKinds[3] = {
        { "LEFT",   UI_TEXT_HALIGN_LEFT   },
        { "CENTER", UI_TEXT_HALIGN_CENTER },
        { "RIGHT",  UI_TEXT_HALIGN_RIGHT  },
    };
    const struct { const char* label; UITextVAlign v; } vKinds[3] = {
        { "TOP",    UI_TEXT_VALIGN_TOP    },
        { "CENTER", UI_TEXT_VALIGN_CENTER },
        { "BOTTOM", UI_TEXT_VALIGN_BOTTOM },
    };

    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            const float x = SX + col * GX;
            const float y = SY + row * GY;
            make_slot(children, x, y, W, H);
            char buf[32];
            snprintf(buf, sizeof(buf), "%s/%s",
                     hKinds[col].label, vKinds[row].label);
            add_label(children, buf,
                      hKinds[col].h, vKinds[row].v,
                      x, y, W, H);
        }
    }

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, UI_COLOR_WHITE);
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
