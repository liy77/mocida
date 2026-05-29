#include <uikit/anim.h>
#include <stdlib.h>
#include <math.h>

// Bounded tween table. 64 is plenty for UI work - typical scenes have
// fewer than a dozen concurrent animations. Going past the cap drops
// the request (returns 0 from UIAnim_To) rather than allocating.
#define MAX_TWEENS 64

/** One animation slot in the global tween table. */
typedef struct {
    float* target;            /**< Borrowed pointer to the float being driven. */
    float  from;              /**< Starting value (captured at tween start). */
    float  to;                /**< Target value at the end of the tween. */
    uint32_t durationMs;      /**< Total duration of the interpolation, in milliseconds. */
    uint32_t elapsedMs;       /**< Time elapsed since the tween started. */
    UIEase ease;              /**< Easing function id (linear / cubic / ...). */
    UIAnimDoneCallback onDone;/**< Callback fired when elapsedMs >= durationMs. */
    void* userdata;           /**< Opaque pointer forwarded to onDone. */
    int   active;             /**< 0 = empty slot, 1 = currently animating. */
} Tween;

static Tween g_tweens[MAX_TWEENS] = {0};

static float Clamp01(float t) {
    if (t < 0.0f) return 0.0f;
    if (t > 1.0f) return 1.0f;
    return t;
}

// Easing functions. All take t in [0, 1] and return the eased value.
static float EaseLinear     (float t) { return t; }
static float EaseInQuad     (float t) { return t * t; }
static float EaseOutQuad    (float t) { return t * (2.0f - t); }
static float EaseInOutQuad  (float t) { return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t; }
static float EaseInCubic    (float t) { return t * t * t; }
static float EaseOutCubic   (float t) { float u = t - 1.0f; return u * u * u + 1.0f; }
static float EaseInOutCubic (float t) {
    return t < 0.5f
        ? 4.0f * t * t * t
        : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}
static float EaseOutBack    (float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    const float u  = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}
static float EaseOutElastic (float t) {
    const float c4 = (2.0f * 3.14159265f) / 3.0f;
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return powf(2.0f, -10.0f * t) * sinf((t * 10.0f - 0.75f) * c4) + 1.0f;
}

static float ApplyEase(UIEase ease, float t) {
    switch (ease) {
        case UI_EASE_IN_QUAD:      return EaseInQuad(t);
        case UI_EASE_OUT_QUAD:     return EaseOutQuad(t);
        case UI_EASE_IN_OUT_QUAD:  return EaseInOutQuad(t);
        case UI_EASE_IN_CUBIC:     return EaseInCubic(t);
        case UI_EASE_OUT_CUBIC:    return EaseOutCubic(t);
        case UI_EASE_IN_OUT_CUBIC: return EaseInOutCubic(t);
        case UI_EASE_OUT_BACK:     return EaseOutBack(t);
        case UI_EASE_OUT_ELASTIC:  return EaseOutElastic(t);
        case UI_EASE_LINEAR:
        default:                   return EaseLinear(t);
    }
}

// Finds an existing tween for `target` (or returns NULL).
static Tween* FindTween(float* target) {
    for (int i = 0; i < MAX_TWEENS; i++) {
        if (g_tweens[i].active && g_tweens[i].target == target) {
            return &g_tweens[i];
        }
    }
    return NULL;
}

int UIAnim_To(float* target, float to, uint32_t durationMs,
              UIEase ease,
              UIAnimDoneCallback onDone, void* userdata) {
    if (!target || durationMs == 0) return 0;

    Tween* t = FindTween(target);
    if (!t) {
        for (int i = 0; i < MAX_TWEENS; i++) {
            if (!g_tweens[i].active) { t = &g_tweens[i]; break; }
        }
        if (!t) return 0; // table full
    }

    t->target     = target;
    t->from       = *target;
    t->to         = to;
    t->durationMs = durationMs;
    t->elapsedMs  = 0;
    t->ease       = ease;
    t->onDone     = onDone;
    t->userdata   = userdata;
    t->active     = 1;
    return 1;
}

void UIAnim_Cancel(float* target) {
    if (!target) return;
    Tween* t = FindTween(target);
    if (t) t->active = 0;
}

void UIAnim_Tick(uint32_t dtMs) {
    for (int i = 0; i < MAX_TWEENS; i++) {
        Tween* t = &g_tweens[i];
        if (!t->active) continue;

        t->elapsedMs += dtMs;
        const float progress = Clamp01((float)t->elapsedMs / (float)t->durationMs);
        const float eased    = ApplyEase(t->ease, progress);
        *t->target = t->from + (t->to - t->from) * eased;

        if (t->elapsedMs >= t->durationMs) {
            *t->target = t->to;        // snap to exact target
            UIAnimDoneCallback cb = t->onDone;
            void* ud = t->userdata;
            t->active = 0;
            // Callback fires AFTER the slot is freed so it can register
            // a new tween on the same target without re-entrancy issues.
            if (cb) cb(ud);
        }
    }
}

void UIAnim_ClearAll(void) {
    for (int i = 0; i < MAX_TWEENS; i++) g_tweens[i].active = 0;
}
