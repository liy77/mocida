#include <uikit/dialog.h>
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

UIDialog* UIDialog_Create(float cardW, float cardH) {
    UIDialog* d = (UIDialog*)calloc(1, sizeof(UIDialog));
    if (!d) return NULL;
    d->__widget_type = UI_WIDGET_DIALOG;
    d->cardW = cardW > 0.0f ? cardW : 360.0f;
    d->cardH = cardH > 0.0f ? cardH : 200.0f;
    d->radius = 12.0f;
    d->cardColor     = (UIColor){ 255, 255, 255, 1.0f };
    d->backdropColor = (UIColor){ 0, 0, 0, 0.45f };
    d->dismissOnBackdrop = 1;
    d->content = UIChildren_Create(8);
    if (!d->content) { free(d); return NULL; }
    return d;
}

UIDialog* UIDialog_SetCardColor    (UIDialog* d, UIColor c) { if (d) d->cardColor = c; return d; }
UIDialog* UIDialog_SetBackdropColor(UIDialog* d, UIColor c) { if (d) d->backdropColor = c; return d; }
UIDialog* UIDialog_SetRadius       (UIDialog* d, float r)   { if (d) d->radius = r; return d; }
UIDialog* UIDialog_SetDismissOnBackdrop(UIDialog* d, int y) { if (d) d->dismissOnBackdrop = y?1:0; return d; }
UIDialog* UIDialog_OnDismiss(UIDialog* d, UIDialogCallback cb, void* userdata) {
    if (!d) return d;
    d->onDismiss = cb;
    d->userdata  = userdata;
    return d;
}

int UIDialog_AddContent(UIDialog* d, UIWidget* w) {
    if (!d || !w) return 0;
    return UIChildren_Add(d->content, w);
}

UIDialog* UIDialog_Show(UIDialog* d) { if (d) d->visible = 1; return d; }
UIDialog* UIDialog_Hide(UIDialog* d) { if (d) d->visible = 0; return d; }

void UIDialog_Destroy(UIDialog* d) {
    if (!d) return;
    if (d->content) UIChildren_Destroy(d->content);
    free(d);
}

// ---------------------------------------------------------------------
// Mouse dispatch: when a dialog is visible, every click that lands on
// the backdrop (outside the card) consumes the event; if
// `dismissOnBackdrop` is set, it also hides the dialog. Clicks inside
// the card propagate to the dialog's content children, NOT to the
// underlying window children - that's enforced by the order of dispatch
// calls in app.c (dialog is dispatched first and returns "consumed").
// ---------------------------------------------------------------------
int UIDialog_HandleMouseDown(UIDialog* d, UIWidget* dialogWidget,
                             float x, float y, int button);

void UIDialog_DispatchMouseDown(UIChildren* children, float x, float y, int button) {
    if (!children || button != SDL_BUTTON_LEFT) return;
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w = children->children[i];
        if (!w || !w->data) continue;
        UIWidgetBase* b = (UIWidgetBase*)w->data;
        if (strcmp(b->__widget_type, UI_WIDGET_DIALOG) != 0) continue;

        UIDialog* d = (UIDialog*)b;
        if (!d->visible) continue;

        // Card geometry (centred on the widget's bounds, which equal the
        // window size when the dialog is added at full window size).
        if (!w->width || !w->height) continue;
        const float cx = w->x + (*w->width  - d->cardW) * 0.5f;
        const float cy = w->y + (*w->height - d->cardH) * 0.5f;
        const int insideCard =
            (x >= cx && x < cx + d->cardW &&
             y >= cy && y < cy + d->cardH);

        if (!insideCard) {
            if (d->dismissOnBackdrop) {
                d->visible = 0;
                if (d->onDismiss) d->onDismiss(d, d->userdata);
            }
        }
        return; // first visible dialog captures
    }
}
