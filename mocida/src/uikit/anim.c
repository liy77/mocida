#include <uikit/anim.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define MAX_TWEENS 256
#define MAX_TIMELINES 64
#define MAX_TL_TRACKS 128
#define MAX_TL_CBS 32

typedef struct {
    float* target;
    float  from;
    float  to;
    uint32_t durationMs;
    uint32_t elapsedMs;
    uint32_t delayMs;
    UIEase ease;
    UIAnimDoneCallback onDone;
    void* userdata;
    int   active;
    int   started; /* 0 while still in delay */
} Tween;

typedef struct {
    float* target;
    float from;
    float to;
    uint32_t startMs;
    uint32_t durationMs;
    UIEase ease;
    int hasFrom;
    int fired; /* completed this cycle */
} TlTrack;

typedef struct {
    uint32_t atMs;
    UIAnimDoneCallback cb;
    void* userdata;
    int fired;
} TlCb;

struct UIAnimTimeline {
    TlTrack tracks[MAX_TL_TRACKS];
    int trackCount;
    TlCb cbs[MAX_TL_CBS];
    int cbCount;
    uint32_t timeMs;
    uint32_t durationMs; /* max end of tracks/cbs */
    int playing;
    int paused;
    int loop;
    int registered;
};

static Tween g_tweens[MAX_TWEENS];
static UIAnimTimeline* g_timelines[MAX_TIMELINES];
static int g_timelineCount = 0;
static int g_paused = 0;
static float g_timeScale = 1.0f;

static float Clamp01(float t) {
    if (t < 0.0f) return 0.0f;
    if (t > 1.0f) return 1.0f;
    return t;
}

static float EaseLinear(float t) { return t; }
static float EaseInQuad(float t) { return t * t; }
static float EaseOutQuad(float t) { return t * (2.0f - t); }
static float EaseInOutQuad(float t) {
    return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
}
static float EaseInCubic(float t) { return t * t * t; }
static float EaseOutCubic(float t) { float u = t - 1.0f; return u * u * u + 1.0f; }
static float EaseInOutCubic(float t) {
    return t < 0.5f
        ? 4.0f * t * t * t
        : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}
static float EaseOutBack(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    const float u  = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}
static float EaseInOutBack(float t) {
    const float c1 = 1.70158f;
    const float c2 = c1 * 1.525f;
    if (t < 0.5f)
        return (powf(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) * 0.5f;
    return (powf(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) * 0.5f;
}
static float EaseOutElastic(float t) {
    const float c4 = (2.0f * 3.14159265f) / 3.0f;
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return powf(2.0f, -10.0f * t) * sinf((t * 10.0f - 0.75f) * c4) + 1.0f;
}
static float EaseInSine(float t) { return 1.0f - cosf(t * 3.14159265f * 0.5f); }
static float EaseOutSine(float t) { return sinf(t * 3.14159265f * 0.5f); }
static float EaseInOutSine(float t) { return -0.5f * (cosf(3.14159265f * t) - 1.0f); }
static float EaseInExpo(float t) {
    return t <= 0.0f ? 0.0f : powf(2.0f, 10.0f * t - 10.0f);
}
static float EaseOutExpo(float t) {
    return t >= 1.0f ? 1.0f : 1.0f - powf(2.0f, -10.0f * t);
}
static float EaseInOutExpo(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    if (t < 0.5f) return powf(2.0f, 20.0f * t - 10.0f) * 0.5f;
    return (2.0f - powf(2.0f, -20.0f * t + 10.0f)) * 0.5f;
}
static float EaseOutBounce(float t) {
    const float n1 = 7.5625f;
    const float d1 = 2.75f;
    if (t < 1.0f / d1) return n1 * t * t;
    if (t < 2.0f / d1) { t -= 1.5f / d1; return n1 * t * t + 0.75f; }
    if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
    t -= 2.625f / d1; return n1 * t * t + 0.984375f;
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
        case UI_EASE_IN_SINE:      return EaseInSine(t);
        case UI_EASE_OUT_SINE:     return EaseOutSine(t);
        case UI_EASE_IN_OUT_SINE:  return EaseInOutSine(t);
        case UI_EASE_IN_EXPO:      return EaseInExpo(t);
        case UI_EASE_OUT_EXPO:     return EaseOutExpo(t);
        case UI_EASE_IN_OUT_EXPO:  return EaseInOutExpo(t);
        case UI_EASE_OUT_BOUNCE:   return EaseOutBounce(t);
        case UI_EASE_IN_OUT_BACK:  return EaseInOutBack(t);
        case UI_EASE_LINEAR:
        default:                   return EaseLinear(t);
    }
}

static Tween* FindTween(float* target) {
    for (int i = 0; i < MAX_TWEENS; i++) {
        if (g_tweens[i].active && g_tweens[i].target == target)
            return &g_tweens[i];
    }
    return NULL;
}

static Tween* AllocTween(float* target) {
    Tween* t = FindTween(target);
    if (t) return t;
    for (int i = 0; i < MAX_TWEENS; i++) {
        if (!g_tweens[i].active) return &g_tweens[i];
    }
    return NULL;
}

int UIAnim_To(float* target, float to, uint32_t durationMs,
              UIEase ease,
              UIAnimDoneCallback onDone, void* userdata) {
    if (!target || durationMs == 0) return 0;
    Tween* t = AllocTween(target);
    if (!t) return 0;
    t->target = target;
    t->from = *target;
    t->to = to;
    t->durationMs = durationMs;
    t->elapsedMs = 0;
    t->delayMs = 0;
    t->ease = ease;
    t->onDone = onDone;
    t->userdata = userdata;
    t->active = 1;
    t->started = 1;
    return 1;
}

int UIAnim_FromTo(float* target, float from, float to,
                  uint32_t durationMs, uint32_t delayMs,
                  UIEase ease,
                  UIAnimDoneCallback onDone, void* userdata) {
    if (!target || durationMs == 0) return 0;
    Tween* t = AllocTween(target);
    if (!t) return 0;
    *target = from;
    t->target = target;
    t->from = from;
    t->to = to;
    t->durationMs = durationMs;
    t->elapsedMs = 0;
    t->delayMs = delayMs;
    t->ease = ease;
    t->onDone = onDone;
    t->userdata = userdata;
    t->active = 1;
    t->started = (delayMs == 0);
    return 1;
}

void UIAnim_Cancel(float* target) {
    if (!target) return;
    Tween* t = FindTween(target);
    if (t) t->active = 0;
}

int UIAnim_IsAnimating(const float* target) {
    if (!target) return 0;
    for (int i = 0; i < MAX_TWEENS; i++) {
        if (g_tweens[i].active && g_tweens[i].target == target)
            return 1;
    }
    return 0;
}

void UIAnim_Pause(void) { g_paused = 1; }
void UIAnim_Resume(void) { g_paused = 0; }
int  UIAnim_IsPaused(void) { return g_paused; }

void UIAnim_SetTimeScale(float scale) {
    g_timeScale = scale < 0.0f ? 0.0f : scale;
}
float UIAnim_GetTimeScale(void) { return g_timeScale; }

static void TickTweens(uint32_t dtMs) {
    for (int i = 0; i < MAX_TWEENS; i++) {
        Tween* t = &g_tweens[i];
        if (!t->active) continue;

        uint32_t step = dtMs;
        if (!t->started) {
            if (step < t->delayMs) {
                t->delayMs -= step;
                continue;
            }
            step -= t->delayMs;
            t->delayMs = 0;
            t->started = 1;
            t->from = *t->target;
            if (step == 0) continue;
        }

        t->elapsedMs += step;
        const float progress = Clamp01((float)t->elapsedMs / (float)t->durationMs);
        const float eased = ApplyEase(t->ease, progress);
        *t->target = t->from + (t->to - t->from) * eased;

        if (t->elapsedMs >= t->durationMs) {
            *t->target = t->to;
            UIAnimDoneCallback cb = t->onDone;
            void* ud = t->userdata;
            t->active = 0;
            if (cb) cb(ud);
        }
    }
}

static void RecomputeDuration(UIAnimTimeline* tl) {
    uint32_t d = 0;
    for (int i = 0; i < tl->trackCount; i++) {
        uint32_t end = tl->tracks[i].startMs + tl->tracks[i].durationMs;
        if (end > d) d = end;
    }
    for (int i = 0; i < tl->cbCount; i++) {
        if (tl->cbs[i].atMs > d) d = tl->cbs[i].atMs;
    }
    tl->durationMs = d;
}

static void RegisterTimeline(UIAnimTimeline* tl) {
    if (!tl || tl->registered) return;
    if (g_timelineCount >= MAX_TIMELINES) return;
    g_timelines[g_timelineCount++] = tl;
    tl->registered = 1;
}

static void UnregisterTimeline(UIAnimTimeline* tl) {
    if (!tl || !tl->registered) return;
    for (int i = 0; i < g_timelineCount; i++) {
        if (g_timelines[i] == tl) {
            g_timelines[i] = g_timelines[--g_timelineCount];
            break;
        }
    }
    tl->registered = 0;
}

static void TickTimeline(UIAnimTimeline* tl, uint32_t dtMs) {
    if (!tl || !tl->playing || tl->paused) return;
    tl->timeMs += dtMs;

    for (int i = 0; i < tl->trackCount; i++) {
        TlTrack* tr = &tl->tracks[i];
        if (!tr->target || tr->durationMs == 0) continue;
        if (tl->timeMs < tr->startMs) continue;
        uint32_t local = tl->timeMs - tr->startMs;
        if (!tr->hasFrom && local == 0) {
            tr->from = *tr->target;
            tr->hasFrom = 1;
        }
        float progress = Clamp01((float)local / (float)tr->durationMs);
        float eased = ApplyEase(tr->ease, progress);
        float from = tr->hasFrom ? tr->from : *tr->target;
        *tr->target = from + (tr->to - from) * eased;
        if (local >= tr->durationMs && !tr->fired) {
            *tr->target = tr->to;
            tr->fired = 1;
        }
    }

    for (int i = 0; i < tl->cbCount; i++) {
        TlCb* c = &tl->cbs[i];
        if (!c->fired && tl->timeMs >= c->atMs) {
            c->fired = 1;
            if (c->cb) c->cb(c->userdata);
        }
    }

    if (tl->durationMs > 0 && tl->timeMs >= tl->durationMs) {
        if (tl->loop) {
            tl->timeMs = 0;
            for (int i = 0; i < tl->trackCount; i++) {
                tl->tracks[i].fired = 0;
                if (!tl->tracks[i].hasFrom) continue;
                /* keep explicit from; re-capture if was live */
            }
            for (int i = 0; i < tl->cbCount; i++) tl->cbs[i].fired = 0;
        } else {
            tl->playing = 0;
        }
    }
}

void UIAnim_Tick(uint32_t dtMs) {
    if (g_paused || g_timeScale <= 0.0f) return;
    uint32_t scaled = (uint32_t)((float)dtMs * g_timeScale + 0.5f);
    if (scaled == 0 && dtMs > 0 && g_timeScale > 0.0f) scaled = 1;
    TickTweens(scaled);
    for (int i = 0; i < g_timelineCount; i++) {
        TickTimeline(g_timelines[i], scaled);
    }
}

void UIAnim_ClearAll(void) {
    for (int i = 0; i < MAX_TWEENS; i++) g_tweens[i].active = 0;
}

/* ---- Timeline API ---- */

UIAnimTimeline* UIAnimTimeline_Create(void) {
    UIAnimTimeline* tl = (UIAnimTimeline*)calloc(1, sizeof(UIAnimTimeline));
    return tl;
}

void UIAnimTimeline_Destroy(UIAnimTimeline* tl) {
    if (!tl) return;
    UnregisterTimeline(tl);
    free(tl);
}

int UIAnimTimeline_Add(UIAnimTimeline* tl,
                       float* target, float to,
                       uint32_t startMs, uint32_t durationMs,
                       UIEase ease) {
    if (!tl || !target || durationMs == 0 || tl->trackCount >= MAX_TL_TRACKS)
        return -1;
    int idx = tl->trackCount++;
    TlTrack* tr = &tl->tracks[idx];
    memset(tr, 0, sizeof(*tr));
    tr->target = target;
    tr->to = to;
    tr->startMs = startMs;
    tr->durationMs = durationMs;
    tr->ease = ease;
    tr->hasFrom = 0;
    RecomputeDuration(tl);
    return idx;
}

int UIAnimTimeline_AddFromTo(UIAnimTimeline* tl,
                             float* target, float from, float to,
                             uint32_t startMs, uint32_t durationMs,
                             UIEase ease) {
    int idx = UIAnimTimeline_Add(tl, target, to, startMs, durationMs, ease);
    if (idx < 0) return -1;
    tl->tracks[idx].from = from;
    tl->tracks[idx].hasFrom = 1;
    return idx;
}

int UIAnimTimeline_AddCallback(UIAnimTimeline* tl, uint32_t atMs,
                               UIAnimDoneCallback cb, void* userdata) {
    if (!tl || tl->cbCount >= MAX_TL_CBS) return -1;
    int idx = tl->cbCount++;
    tl->cbs[idx].atMs = atMs;
    tl->cbs[idx].cb = cb;
    tl->cbs[idx].userdata = userdata;
    tl->cbs[idx].fired = 0;
    RecomputeDuration(tl);
    return idx;
}

void UIAnimTimeline_SetLoop(UIAnimTimeline* tl, int loop) {
    if (tl) tl->loop = loop ? 1 : 0;
}

void UIAnimTimeline_Play(UIAnimTimeline* tl) {
    if (!tl) return;
    tl->timeMs = 0;
    tl->playing = 1;
    tl->paused = 0;
    for (int i = 0; i < tl->trackCount; i++) tl->tracks[i].fired = 0;
    for (int i = 0; i < tl->cbCount; i++) tl->cbs[i].fired = 0;
    RegisterTimeline(tl);
}

void UIAnimTimeline_Stop(UIAnimTimeline* tl) {
    if (!tl) return;
    tl->playing = 0;
    tl->paused = 0;
    tl->timeMs = 0;
}

void UIAnimTimeline_Pause(UIAnimTimeline* tl) {
    if (tl) tl->paused = 1;
}

void UIAnimTimeline_Resume(UIAnimTimeline* tl) {
    if (tl) tl->paused = 0;
}

int UIAnimTimeline_IsPlaying(const UIAnimTimeline* tl) {
    return tl && tl->playing && !tl->paused;
}

uint32_t UIAnimTimeline_GetTime(const UIAnimTimeline* tl) {
    return tl ? tl->timeMs : 0;
}

int UIAnimTimeline_Stagger(UIAnimTimeline* tl,
                           float** targets, int count,
                           float to, uint32_t durationMs,
                           uint32_t baseDelayMs, uint32_t staggerMs,
                           UIEase ease) {
    if (!tl || !targets || count <= 0) return 0;
    int added = 0;
    for (int i = 0; i < count; i++) {
        if (!targets[i]) continue;
        uint32_t start = baseDelayMs + (uint32_t)i * staggerMs;
        if (UIAnimTimeline_Add(tl, targets[i], to, start, durationMs, ease) >= 0)
            added++;
    }
    return added;
}
