#ifndef UIKIT_ANIM_H
#define UIKIT_ANIM_H

#include <stdint.h>

/**
 * Easing functions used by UIAnim_To. Mapping `t in [0, 1]` to the
 * normalised animation progress. Mirrors the Robert Penner curves
 * everyone knows from JavaScript / CSS land.
 */
typedef enum {
    UI_EASE_LINEAR = 0,
    UI_EASE_IN_QUAD,
    UI_EASE_OUT_QUAD,
    UI_EASE_IN_OUT_QUAD,
    UI_EASE_IN_CUBIC,
    UI_EASE_OUT_CUBIC,
    UI_EASE_IN_OUT_CUBIC,
    UI_EASE_OUT_BACK,        /**< Overshoots then settles. */
    UI_EASE_OUT_ELASTIC      /**< Spring-like settle. */
} UIEase;

typedef void (*UIAnimDoneCallback)(void* userdata);

/**
 * Registers a tween that drives `*target` from its current value to
 * `to` over `durationMs` milliseconds using the given easing curve.
 *
 *   - `target`: pointer to a `float` that gets updated on every tick.
 *   - `to`: final value.
 *   - `durationMs`: total animation length in milliseconds (> 0).
 *   - `ease`: easing function (see UIEase).
 *   - `onDone`: optional callback fired exactly once when the
 *     animation completes (NULL = no callback).
 *   - `userdata`: opaque pointer passed back to `onDone`.
 *
 * Returns 1 on success, 0 if the global tween table is full. Calling
 * UIAnim_To again for the same `target` replaces any in-flight tween.
 */
int UIAnim_To(float* target, float to, uint32_t durationMs,
              UIEase ease,
              UIAnimDoneCallback onDone, void* userdata);

/**
 * Cancels any tween targeting `target`. Does nothing if none exists.
 */
void UIAnim_Cancel(float* target);

/**
 * Advances every live tween by `dtMs` milliseconds. Called once per
 * frame by UIApp_Run; users normally never call this directly.
 */
void UIAnim_Tick(uint32_t dtMs);

/**
 * Frees all in-flight tweens. Useful during teardown.
 */
void UIAnim_ClearAll(void);

#endif // UIKIT_ANIM_H
