#include <uikit/bind.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uikit/mocida_alloc.h>

#ifdef _MSC_VER
#  define UI_STRDUP _strdup
#else
#  define UI_STRDUP strdup
#endif

/**
 * Internal binding handle. Every UIBind_* helper allocates one of
 * these, registers `apply` as a subscription callback, then runs apply
 * once eagerly so the widget reflects the current signal value.
 *
 * `target` is the widget property owner. `aux` holds any extra state
 * the binding needs (e.g. a format string copy); freed by UIBind_Destroy
 * when non-NULL.
 */
struct UIBinding {
    UISubscription* sub; /**< Signal subscription owned by this binding. */
    void* target;        /**< Widget / property this binding writes into. */
    char* aux;           /**< Optional extra state (e.g. format string copy). */
};

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static UIBinding* uibind_alloc(void* target, char* aux) {
    UIBinding* b = (UIBinding*)malloc(sizeof(UIBinding));
    if (!b) { free(aux); return NULL; }
    b->sub = NULL;
    b->target = target;
    b->aux = aux;
    return b;
}

static void uibind_finalize(UIBinding* b, UISignal* signal, UISignalCallback cb) {
    b->sub = UISignal_Subscribe(signal, cb, b);
    if (!b->sub) {
        free(b->aux);
        free(b);
        return;
    }
    /* Initial apply so the widget starts in sync with the signal. */
    cb(signal, b);
}

/* Format helper used by *ToFormat bindings. Writes into `out` (size cap)
   and returns it for convenience. Stack-allocated by callers. */
static const char* uibind_format(char* out, size_t cap,
                                 UISignal* sig, const char* fmt) {
    out[0] = '\0';
    if (!fmt) return out;
    switch (UISignal_GetType(sig)) {
        case UI_SIGNAL_INT:
            snprintf(out, cap, fmt, UISignal_GetInt(sig));
            break;
        case UI_SIGNAL_FLOAT:
            snprintf(out, cap, fmt, UISignal_GetFloat(sig));
            break;
        case UI_SIGNAL_STRING: {
            const char* s = UISignal_GetString(sig);
            snprintf(out, cap, fmt, s ? s : "");
            break;
        }
        case UI_SIGNAL_POINTER:
            snprintf(out, cap, fmt, UISignal_GetPointer(sig));
            break;
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* callbacks                                                          */
/* ------------------------------------------------------------------ */

static void uibind_text_string_cb(UISignal* s, void* ud) {
    UIBinding* b = (UIBinding*)ud;
    const char* v = UISignal_GetString(s);
    if (b->target && v) UIText_SetText((UIText*)b->target, (char*)v);
}

static void uibind_text_format_cb(UISignal* s, void* ud) {
    UIBinding* b = (UIBinding*)ud;
    if (!b->target || !b->aux) return;
    char buf[256];
    UIText_SetText((UIText*)b->target,
                   (char*)uibind_format(buf, sizeof(buf), s, b->aux));
}

static void uibind_button_format_cb(UISignal* s, void* ud) {
    UIBinding* b = (UIBinding*)ud;
    if (!b->target || !b->aux) return;
    char buf[256];
    UIButton_SetText((UIButton*)b->target,
                     uibind_format(buf, sizeof(buf), s, b->aux));
}

static void uibind_visible_cb(UISignal* s, void* ud) {
    UIBinding* b = (UIBinding*)ud;
    if (b->target) ((UIWidget*)b->target)->visible = UISignal_GetInt(s) ? 1 : 0;
}

static void uibind_opacity_cb(UISignal* s, void* ud) {
    UIBinding* b = (UIBinding*)ud;
    if (b->target) ((UIWidget*)b->target)->opacity = UISignal_GetFloat(s);
}

static void uibind_x_cb(UISignal* s, void* ud) {
    UIBinding* b = (UIBinding*)ud;
    if (b->target) ((UIWidget*)b->target)->x = UISignal_GetFloat(s);
}

static void uibind_y_cb(UISignal* s, void* ud) {
    UIBinding* b = (UIBinding*)ud;
    if (b->target) ((UIWidget*)b->target)->y = UISignal_GetFloat(s);
}

/* ------------------------------------------------------------------ */
/* public bindings                                                    */
/* ------------------------------------------------------------------ */

UIBinding* UIBind_TextToSignal(UIText* target, UISignal* signal) {
    if (!target || !signal || UISignal_GetType(signal) != UI_SIGNAL_STRING) return NULL;
    UIBinding* b = uibind_alloc(target, NULL);
    if (!b) return NULL;
    uibind_finalize(b, signal, uibind_text_string_cb);
    return b;
}

UIBinding* UIBind_TextToFormat(UIText* target, UISignal* signal, const char* fmt) {
    if (!target || !signal || !fmt) return NULL;
    UIBinding* b = uibind_alloc(target, UI_STRDUP(fmt));
    if (!b || !b->aux) { if (b) { free(b->aux); free(b); } return NULL; }
    uibind_finalize(b, signal, uibind_text_format_cb);
    return b;
}

UIBinding* UIBind_ButtonTextToFormat(UIButton* target, UISignal* signal, const char* fmt) {
    if (!target || !signal || !fmt) return NULL;
    UIBinding* b = uibind_alloc(target, UI_STRDUP(fmt));
    if (!b || !b->aux) { if (b) { free(b->aux); free(b); } return NULL; }
    uibind_finalize(b, signal, uibind_button_format_cb);
    return b;
}

UIBinding* UIBind_VisibleToSignal(UIWidget* target, UISignal* signal) {
    if (!target || !signal || UISignal_GetType(signal) != UI_SIGNAL_INT) return NULL;
    UIBinding* b = uibind_alloc(target, NULL);
    if (!b) return NULL;
    uibind_finalize(b, signal, uibind_visible_cb);
    return b;
}

UIBinding* UIBind_OpacityToSignal(UIWidget* target, UISignal* signal) {
    if (!target || !signal || UISignal_GetType(signal) != UI_SIGNAL_FLOAT) return NULL;
    UIBinding* b = uibind_alloc(target, NULL);
    if (!b) return NULL;
    uibind_finalize(b, signal, uibind_opacity_cb);
    return b;
}

UIBinding* UIBind_PositionXToSignal(UIWidget* target, UISignal* signal) {
    if (!target || !signal || UISignal_GetType(signal) != UI_SIGNAL_FLOAT) return NULL;
    UIBinding* b = uibind_alloc(target, NULL);
    if (!b) return NULL;
    uibind_finalize(b, signal, uibind_x_cb);
    return b;
}

UIBinding* UIBind_PositionYToSignal(UIWidget* target, UISignal* signal) {
    if (!target || !signal || UISignal_GetType(signal) != UI_SIGNAL_FLOAT) return NULL;
    UIBinding* b = uibind_alloc(target, NULL);
    if (!b) return NULL;
    uibind_finalize(b, signal, uibind_y_cb);
    return b;
}

void UIBind_Destroy(UIBinding* binding) {
    if (!binding) return;
    if (binding->sub) UISignal_Unsubscribe(binding->sub);
    free(binding->aux);
    free(binding);
}
