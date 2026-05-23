#ifndef UIKIT_BIND_H
#define UIKIT_BIND_H

#include <uikit/reactive.h>
#include <uikit/widget.h>
#include <uikit/text.h>
#include <uikit/button.h>

/**
 * UIBind_* helpers wire a widget property to a UISignal in one call.
 * The returned UIBinding owns both the underlying subscription and any
 * internal context allocated by the helper - destroy it with
 * UIBind_Destroy when the widget or signal goes away.
 *
 * Lifetime contract:
 *   - The signal MUST outlive the binding (or be destroyed first and
 *     the binding then dropped without touching it).
 *   - The target widget MUST outlive the binding.
 *   - Calling UIBind_Destroy is mandatory if the widget is destroyed
 *     before the signal - otherwise the next notify would dereference
 *     freed memory.
 */
typedef struct UIBinding UIBinding;

/**
 * Sets the widget's text to the signal's current value and updates it
 * whenever the signal changes. Signal must be UI_SIGNAL_STRING.
 */
UIBinding* UIBind_TextToSignal(UIText* target, UISignal* signal);

/**
 * Formats the signal's value into `fmt` (printf-style) and writes the
 * result into the text widget. Supported signal types:
 *   - UI_SIGNAL_INT     -> use a %d / %i / %u format
 *   - UI_SIGNAL_FLOAT   -> use a %f / %g format
 *   - UI_SIGNAL_STRING  -> use %s
 *
 * Pass exactly one conversion in `fmt`. The format string is copied
 * internally - the caller can free it after this call returns.
 */
UIBinding* UIBind_TextToFormat(UIText* target, UISignal* signal, const char* fmt);

/**
 * Same as UIBind_TextToFormat but writes into a UIButton's label.
 */
UIBinding* UIBind_ButtonTextToFormat(UIButton* target, UISignal* signal, const char* fmt);

/**
 * Sets target->visible from a UI_SIGNAL_INT (truthy = visible).
 */
UIBinding* UIBind_VisibleToSignal(UIWidget* target, UISignal* signal);

/**
 * Sets target->opacity from a UI_SIGNAL_FLOAT.
 */
UIBinding* UIBind_OpacityToSignal(UIWidget* target, UISignal* signal);

/**
 * Sets target->x from a UI_SIGNAL_FLOAT.
 */
UIBinding* UIBind_PositionXToSignal(UIWidget* target, UISignal* signal);

/**
 * Sets target->y from a UI_SIGNAL_FLOAT.
 */
UIBinding* UIBind_PositionYToSignal(UIWidget* target, UISignal* signal);

/**
 * Tears the binding down: unsubscribes from the signal and releases any
 * internal context. Safe to call with NULL.
 */
void UIBind_Destroy(UIBinding* binding);

#endif /* UIKIT_BIND_H */
