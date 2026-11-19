#ifndef UIKIT_ANIM_H
#define UIKIT_ANIM_H

#include <stdint.h>

/**
 * Animation system — float tweens, timelines, and sequenced tracks.
 *
 * UIAnim_To drives a single float (positions, opacity, sizes). Timelines
 * schedule many tracks with delays, loops, and callbacks so 2.5D scenes
 * and UI transitions share one clock (ticked from UIApp_Run).
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
    UI_EASE_OUT_ELASTIC,     /**< Spring-like settle. */
    UI_EASE_IN_SINE,
    UI_EASE_OUT_SINE,
    UI_EASE_IN_OUT_SINE,
    UI_EASE_IN_EXPO,
    UI_EASE_OUT_EXPO,
    UI_EASE_IN_OUT_EXPO,
    UI_EASE_OUT_BOUNCE,
    UI_EASE_IN_OUT_BACK
} UIEase;

typedef void (*UIAnimDoneCallback)(void* userdata);

/**
 * Registers a tween that drives `*target` from its current value to
 * `to` over `durationMs` milliseconds using the given easing curve.
 *
 * Returns 1 on success, 0 if the global tween table is full. Calling
 * UIAnim_To again for the same `target` replaces any in-flight tween.
 */
int UIAnim_To(float* target, float to, uint32_t durationMs,
              UIEase ease,
              UIAnimDoneCallback onDone, void* userdata);

/**
 * Like UIAnim_To but starts from `from` (writes `*target = from` first)
 * and waits `delayMs` before the interpolation begins.
 */
int UIAnim_FromTo(float* target, float from, float to,
                  uint32_t durationMs, uint32_t delayMs,
                  UIEase ease,
                  UIAnimDoneCallback onDone, void* userdata);

/** Cancels any tween targeting `target`. */
void UIAnim_Cancel(float* target);

/** 1 when a tween is currently driving `target`. */
int UIAnim_IsAnimating(const float* target);

/** Global pause / resume for every tween and timeline. */
void UIAnim_Pause(void);
void UIAnim_Resume(void);
int  UIAnim_IsPaused(void);

/** Multiplier applied to every tick (1.0 = realtime, 0.5 = half speed). */
void  UIAnim_SetTimeScale(float scale);
float UIAnim_GetTimeScale(void);

/**
 * Advances every live tween and registered timeline by `dtMs`.
 * Called once per frame by UIApp_Run.
 */
void UIAnim_Tick(uint32_t dtMs);

/** Frees all in-flight tweens (timelines keep their definitions). */
void UIAnim_ClearAll(void);

/* -------- Timelines -------- */

typedef struct UIAnimTimeline UIAnimTimeline;

UIAnimTimeline* UIAnimTimeline_Create(void);
void            UIAnimTimeline_Destroy(UIAnimTimeline* tl);

/**
 * Adds a float track. `startMs` is relative to timeline play start.
 * Returns track index (>=0) or -1 on failure.
 */
int UIAnimTimeline_Add(UIAnimTimeline* tl,
                       float* target, float to,
                       uint32_t startMs, uint32_t durationMs,
                       UIEase ease);

/** From/to variant with an explicit start value. */
int UIAnimTimeline_AddFromTo(UIAnimTimeline* tl,
                             float* target, float from, float to,
                             uint32_t startMs, uint32_t durationMs,
                             UIEase ease);

/** Fires `cb` once when the timeline clock reaches `atMs`. */
int UIAnimTimeline_AddCallback(UIAnimTimeline* tl, uint32_t atMs,
                               UIAnimDoneCallback cb, void* userdata);

void UIAnimTimeline_SetLoop(UIAnimTimeline* tl, int loop);
void UIAnimTimeline_Play(UIAnimTimeline* tl);
void UIAnimTimeline_Stop(UIAnimTimeline* tl);
void UIAnimTimeline_Pause(UIAnimTimeline* tl);
void UIAnimTimeline_Resume(UIAnimTimeline* tl);
int  UIAnimTimeline_IsPlaying(const UIAnimTimeline* tl);
uint32_t UIAnimTimeline_GetTime(const UIAnimTimeline* tl);

/**
 * Stagger helper: for each target, schedules a FromTo starting at
 * `baseDelayMs + i * staggerMs`. Returns number of tracks added.
 */
int UIAnimTimeline_Stagger(UIAnimTimeline* tl,
                           float** targets, int count,
                           float to, uint32_t durationMs,
                           uint32_t baseDelayMs, uint32_t staggerMs,
                           UIEase ease);

#endif // UIKIT_ANIM_H
