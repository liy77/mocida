#ifndef UIKIT_DIALOG_H
#define UIKIT_DIALOG_H

#include <uikit/widget.h>
#include <uikit/children.h>
#include <uikit/color.h>

#define UI_WIDGET_DIALOG "@uikit/dialog"

typedef struct UIDialog UIDialog;
typedef void (*UIDialogCallback)(UIDialog* dlg, void* userdata);

/**
 * Modal-ish dialog: draws a translucent backdrop over the whole window
 * and a centered card with arbitrary content. Clicks on the backdrop
 * are absorbed; if dismissOnBackdrop is set, they also close the
 * dialog. The dialog widget itself should be added to the window's
 * children with a very high z-index so it draws on top.
 */
struct UIDialog {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_DIALOG). */

    int visible;               /**< 0 hides the dialog entirely (no backdrop, no card). */
    int dismissOnBackdrop;     /**< When 1, clicking outside the card closes the dialog. */

    float cardW;               /**< Card width in pixels. */
    float cardH;               /**< Card height in pixels. */
    float radius;              /**< Card corner radius (pixels). */
    UIColor cardColor;         /**< Card background fill. */
    UIColor backdropColor;     /**< Translucent overlay drawn behind the card. */

    UIChildren* content;       /**< Owned content tree positioned relative to the card's top-left. */

    UIDialogCallback onDismiss;/**< Fires when the dialog is closed (backdrop click / programmatic). */
    void* userdata;            /**< Opaque pointer forwarded to onDismiss. */
};

UIDialog* UIDialog_Create(float cardW, float cardH);
UIDialog* UIDialog_SetCardColor    (UIDialog* d, UIColor color);
UIDialog* UIDialog_SetBackdropColor(UIDialog* d, UIColor color);
UIDialog* UIDialog_SetRadius       (UIDialog* d, float r);
UIDialog* UIDialog_SetDismissOnBackdrop(UIDialog* d, int yes);
UIDialog* UIDialog_OnDismiss       (UIDialog* d, UIDialogCallback cb, void* userdata);

/** Adds a widget that lives inside the card. Position is relative to
    the card's top-left (set via UIWidget_SetPosition). */
int       UIDialog_AddContent      (UIDialog* d, UIWidget* w);

UIDialog* UIDialog_Show(UIDialog* d);
UIDialog* UIDialog_Hide(UIDialog* d);

void      UIDialog_Destroy(UIDialog* d);

/** Forwarded mouse click. Used by app.c to detect backdrop dismisses. */
void UIDialog_DispatchMouseDown(UIChildren* children, float x, float y, int button);

#endif // UIKIT_DIALOG_H
