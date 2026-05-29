#ifndef UIKIT_PROFILE_H
#define UIKIT_PROFILE_H

/**
 * Mocida's profiler. Two things in one:
 *
 *  1. Per-frame stats — UIApp_Run already calls FrameBegin/FrameEnd and
 *     wraps its event/animation/render phases in UI_SCOPEC, so reading
 *     UIProfile_GetFrameStats gives you the breakdown live.
 *
 *  2. Chrome trace recording — UIProfile_TraceStart() begins capturing
 *     scope events, UIProfile_TraceSave("trace.json") flushes them to a
 *     file you can open in chrome://tracing or ui.perfetto.dev. Scopes
 *     are recorded with start + duration ("complete" events), so the
 *     buffer cost is one record per scope close.
 *
 * Scope macros use __attribute__((cleanup)) so they auto-end at block
 * scope exit (clang/gcc; the project's compiler is clang). On unsupported
 * compilers, UI_SCOPE compiles to ((void)0) — use the manual
 * UI_PROFILE_BEGIN/END pair instead.
 *
 * Everything is gated behind MOCIDA_DEBUG_ENABLED, so release builds get
 * no overhead at all.
 *
 * Usage:
 *
 *     void my_layout(void) {
 *         UI_SCOPEC("my_layout", "layout");
 *         ...
 *     }
 *
 *     // somewhere at startup:
 *     UIProfile_TraceStart(0);     // 0 = default 64k events
 *     ...
 *     UIProfile_TraceSave("trace.json");
 */

#include <stddef.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <uikit/debug.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double frameTimeMs;     /**< last frame wall-clock time */
    double eventTimeMs;     /**< accumulated time in "event" scopes */
    double layoutTimeMs;    /**< accumulated time in "layout" scopes */
    double renderTimeMs;    /**< accumulated time in "render" scopes */
    double presentTimeMs;   /**< accumulated time in "present" scopes */
    double fpsSmoothed;     /**< EMA, alpha=0.1 */
    Uint32 frameCount;      /**< monotonic, since process start */
    Uint32 lastDrawCalls;   /**< via UIProfile_AddDrawCalls() */
} UIFrameStats;

/* ---- global enable ---- */
void UIProfile_SetEnabled(int enabled);
int  UIProfile_IsEnabled(void);

/* ---- trace recording ---- */

/**
 * Start collecting scope events into an in-memory buffer.
 * @param max_events 0 = default 65536 events (~3MB). Trace stops appending
 *                   silently once the buffer fills.
 * @return 1 on success, 0 if allocation failed.
 */
int    UIProfile_TraceStart(size_t max_events);
void   UIProfile_TraceStop(void);
int    UIProfile_TraceSave(const char* path);
size_t UIProfile_TraceEventCount(void);
void   UIProfile_TraceClear(void);

/* ---- per-frame stats (UIApp_Run drives these) ---- */
void UIProfile_FrameBegin(void);
void UIProfile_FrameEnd(void);
void UIProfile_AddDrawCalls(Uint32 n);
void UIProfile_GetFrameStats(UIFrameStats* out);

/* ---- scope primitive ---- */
/**
 * RAII-style profiler scope marker. Captures a name + start timestamp
 * when constructed; on close, UIProfile_Record() is called with the
 * elapsed duration. Use the UI_PROFILE_SCOPE() macro instead of
 * constructing one by hand.
 */
typedef struct {
    Uint64      startTicks; /**< SDL_GetPerformanceCounter() at scope begin. */
    const char* name;       /**< User-facing scope name (must outlive the scope). */
    const char* category;   /**< Optional grouping label (e.g. "render"). */
} UIProfileScope;

void UIProfile_Record(const char* name, const char* category, Uint64 startTicks);

static inline UIProfileScope ui_profile_begin_(const char* name, const char* category) {
    UIProfileScope s;
    s.startTicks = SDL_GetPerformanceCounter();
    s.name = name;
    s.category = category;
    return s;
}
static inline void ui_profile_end_(UIProfileScope* s) {
    UIProfile_Record(s->name, s->category, s->startTicks);
}

/* Two-level concat so __COUNTER__ expands before pasting. */
#define UI__CAT2_(a,b) a##b
#define UI__CAT_(a,b)  UI__CAT2_(a,b)

#if MOCIDA_DEBUG_ENABLED && (defined(__clang__) || defined(__GNUC__))
#  define UI_SCOPE(name_lit) \
      __attribute__((cleanup(ui_profile_end_))) \
      UIProfileScope UI__CAT_(_ui_scope_, __COUNTER__) = ui_profile_begin_((name_lit), NULL)
#  define UI_SCOPEC(name_lit, cat_lit) \
      __attribute__((cleanup(ui_profile_end_))) \
      UIProfileScope UI__CAT_(_ui_scope_, __COUNTER__) = ui_profile_begin_((name_lit), (cat_lit))
#else
#  define UI_SCOPE(name_lit)             ((void)0)
#  define UI_SCOPEC(name_lit, cat_lit)   ((void)0)
#endif

/* Portable manual fallback (works on MSVC etc). */
#if MOCIDA_DEBUG_ENABLED
#  define UI_PROFILE_BEGIN(var, name_lit)        \
      UIProfileScope var = ui_profile_begin_((name_lit), NULL)
#  define UI_PROFILE_BEGINC(var, name_lit, cat)  \
      UIProfileScope var = ui_profile_begin_((name_lit), (cat))
#  define UI_PROFILE_END(var)                    ui_profile_end_(&(var))
#else
#  define UI_PROFILE_BEGIN(var, name_lit)        ((void)0)
#  define UI_PROFILE_BEGINC(var, name_lit, cat)  ((void)0)
#  define UI_PROFILE_END(var)                    ((void)0)
#endif

/* Conventional categories for the frame breakdown. */
#define UI_PROF_EVENT   "event"
#define UI_PROF_LAYOUT  "layout"
#define UI_PROF_RENDER  "render"
#define UI_PROF_PRESENT "present"
#define UI_PROF_ANIM    "anim"
#define UI_PROF_IO      "io"

#ifdef __cplusplus
}
#endif

#endif /* UIKIT_PROFILE_H */
