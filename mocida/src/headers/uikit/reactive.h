#ifndef UIKIT_REACTIVE_H
#define UIKIT_REACTIVE_H

#include <stddef.h>

/**
 * UISignal - a value cell that notifies subscribers when it changes.
 *
 * Typical use:
 *
 *   UISignal* fps = UISignal_CreateInt(60);
 *   UISubscription* sub = UISignal_Subscribe(fps, OnFpsChanged, ctx);
 *   ...
 *   UISignal_SetInt(fps, 120);      // fires OnFpsChanged
 *   UISignal_SetInt(fps, 120);      // no-op (dedupe)
 *   ...
 *   UISignal_Unsubscribe(sub);
 *   UISignal_Destroy(fps);
 *
 * Bindings (see UIBind_* in bind.h) wrap this so widget properties can
 * be wired to a signal in one call.
 *
 * Lifetime rules:
 *   - Every subscription MUST be unsubscribed before its signal is
 *     destroyed. UISignal_Destroy walks remaining subs and frees them,
 *     but the holder of the UISubscription* must not use it after that.
 *   - String signals own their storage (set strdup's the new value).
 *   - Pointer signals do NOT take ownership - the producer manages
 *     lifetime of the pointee.
 */

typedef struct UISignal       UISignal;
typedef struct UISubscription UISubscription;

typedef void (*UISignalCallback)(UISignal* sig, void* userdata);

typedef enum {
    UI_SIGNAL_INT,
    UI_SIGNAL_FLOAT,
    UI_SIGNAL_STRING,
    UI_SIGNAL_POINTER
} UISignalType;

/* Constructors. */
UISignal* UISignal_CreateInt(int v);
UISignal* UISignal_CreateFloat(float v);
UISignal* UISignal_CreateString(const char* v);
UISignal* UISignal_CreatePointer(void* v);

UISignalType UISignal_GetType(const UISignal* s);

/* Accessors - return zero/NULL on type mismatch or NULL signal. */
int         UISignal_GetInt(const UISignal* s);
float       UISignal_GetFloat(const UISignal* s);
const char* UISignal_GetString(const UISignal* s);
void*       UISignal_GetPointer(const UISignal* s);

/**
 * Setters. They:
 *   1. dedupe (no-op when the new value equals the current value), and
 *   2. notify all subscribers in registration order.
 *
 * Calling a setter of the wrong type is a no-op.
 */
void UISignal_SetInt(UISignal* s, int v);
void UISignal_SetFloat(UISignal* s, float v);
void UISignal_SetString(UISignal* s, const char* v);
void UISignal_SetPointer(UISignal* s, void* v);

/**
 * Force-notify subscribers without changing the value. Use when the
 * signal carries a pointer whose pointee was mutated in place.
 */
void UISignal_Notify(UISignal* s);

/**
 * Registers a listener. Returns an opaque handle to be passed to
 * UISignal_Unsubscribe (or NULL on OOM).
 *
 * The callback is invoked synchronously from UISignal_Set* / Notify.
 * If a callback mutates the same signal, the recursive Set is dropped
 * (reentrancy guard) - design your handlers so that does not happen.
 */
UISubscription* UISignal_Subscribe(UISignal* s, UISignalCallback cb, void* userdata);

/**
 * Removes a listener. Safe to call from inside a callback - the unsub
 * is deferred until after the current notification finishes.
 */
void UISignal_Unsubscribe(UISubscription* sub);

/**
 * Frees the signal and all remaining subscriptions. Any UISubscription*
 * previously returned for this signal becomes invalid.
 */
void UISignal_Destroy(UISignal* s);

#endif /* UIKIT_REACTIVE_H */
