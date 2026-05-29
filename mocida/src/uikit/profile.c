/*
 * Mocida profiler — see profile.h for the design notes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <SDL3/SDL.h>

#include <uikit/profile.h>
#include <uikit/debug.h>

/** One profiler trace sample. */
typedef struct {
    Uint64      tsTicks;   /**< Performance-counter ticks at scope start. */
    Uint64      durTicks;  /**< Duration of the scope, in performance-counter ticks. */
    const char* name;      /**< Scope name (must outlive the sample). */
    const char* category;  /**< Optional category tag (must outlive the sample). */
    Uint64      tid;       /**< Thread id that produced the sample. */
} UITraceEvt;

/** Module-level profiler state. Lives in the static singleton `P`. */
typedef struct {
    int        initialized;     /**< 0 until ensure_init() runs once. */
    int        enabled;         /**< Master switch; reflects MOCIDA_DEBUG_ENABLED at init. */
    SDL_Mutex* mu;              /**< Guards every mutation of this struct. */
    Uint64     perfFreq;        /**< SDL_GetPerformanceFrequency() cached at init. */

    int          tracing;       /**< 1 while a trace recording is active. */
    UITraceEvt*  events;        /**< Ring buffer of trace events. */
    size_t       cap;           /**< Allocated capacity of `events`. */
    size_t       count;         /**< Number of valid entries in `events`. */
    int          overflowed;    /**< 1 if more events arrived than `cap`. */
    Uint64       traceStartTicks; /**< Tick value when the current trace started. */

    Uint64       frameStartTicks; /**< SDL_GetPerformanceCounter() at FrameBegin. */
    Uint64       eventAccum;    /**< Accumulated event-loop ticks this frame. */
    Uint64       layoutAccum;   /**< Accumulated layout ticks this frame. */
    Uint64       renderAccum;   /**< Accumulated render ticks this frame. */
    Uint64       presentAccum;  /**< Accumulated present-call ticks this frame. */
    Uint32       drawCalls;     /**< Draw-call counter this frame. */

    UIFrameStats stats;         /**< Most recent completed-frame stats. */
} UIProfileState;

static UIProfileState P = {0};

/* ---- init ---- */

static void ensure_init(void) {
    if (P.initialized) return;
    if (!P.mu) P.mu = SDL_CreateMutex();
    P.perfFreq    = SDL_GetPerformanceFrequency();
    P.enabled     = MOCIDA_DEBUG_ENABLED;
    P.initialized = 1;
}

void UIProfile_SetEnabled(int enabled) {
    ensure_init();
    P.enabled = enabled ? 1 : 0;
}

int UIProfile_IsEnabled(void) {
    ensure_init();
    return P.enabled;
}

/* ---- trace control ---- */

int UIProfile_TraceStart(size_t max_events) {
    ensure_init();
    if (max_events == 0) max_events = 65536;

    SDL_LockMutex(P.mu);
    free(P.events);
    P.events = (UITraceEvt*)malloc(sizeof(UITraceEvt) * max_events);
    if (!P.events) {
        P.cap = 0;
        P.tracing = 0;
        SDL_UnlockMutex(P.mu);
        UI_ERROR(UI_CAT_CORE, "UIProfile_TraceStart: out of memory for %zu events", max_events);
        return 0;
    }
    P.cap            = max_events;
    P.count          = 0;
    P.overflowed     = 0;
    P.traceStartTicks = SDL_GetPerformanceCounter();
    P.tracing        = 1;
    SDL_UnlockMutex(P.mu);

    UI_INFO(UI_CAT_CORE, "profile trace started (%zu events, ~%zu KB)",
            max_events, (sizeof(UITraceEvt) * max_events) / 1024);
    return 1;
}

void UIProfile_TraceStop(void) {
    ensure_init();
    SDL_LockMutex(P.mu);
    P.tracing = 0;
    SDL_UnlockMutex(P.mu);
}

void UIProfile_TraceClear(void) {
    ensure_init();
    SDL_LockMutex(P.mu);
    P.count = 0;
    P.overflowed = 0;
    P.traceStartTicks = SDL_GetPerformanceCounter();
    SDL_UnlockMutex(P.mu);
}

size_t UIProfile_TraceEventCount(void) {
    ensure_init();
    return P.count;
}

/* Chrome trace JSON-escape: handles \, ", and control chars. Names and
 * categories in our codebase are string literals so this is mostly a
 * safety net. */
static void write_escaped(FILE* f, const char* s) {
    if (!s) { fputs("?", f); return; }
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
            case '"':  fputs("\\\"", f); break;
            case '\\': fputs("\\\\", f); break;
            case '\b': fputs("\\b", f);  break;
            case '\f': fputs("\\f", f);  break;
            case '\n': fputs("\\n", f);  break;
            case '\r': fputs("\\r", f);  break;
            case '\t': fputs("\\t", f);  break;
            default:
                if (c < 0x20) fprintf(f, "\\u%04x", c);
                else fputc((int)c, f);
        }
    }
}

int UIProfile_TraceSave(const char* path) {
    ensure_init();
    if (!path) return 0;
    FILE* f = fopen(path, "wb");
    if (!f) {
        UI_ERROR(UI_CAT_CORE, "UIProfile_TraceSave: cannot open '%s' for writing", path);
        return 0;
    }

    SDL_LockMutex(P.mu);
    const Uint64 freq = P.perfFreq ? P.perfFreq : 1;
    fputs("{\"traceEvents\":[\n", f);
    for (size_t i = 0; i < P.count; ++i) {
        UITraceEvt* e = &P.events[i];
        Uint64 ts_us  = ((e->tsTicks - P.traceStartTicks) * 1000000ULL) / freq;
        Uint64 dur_us = (e->durTicks * 1000000ULL) / freq;
        if (i) fputs(",\n", f);
        fputs("{\"name\":\"", f);  write_escaped(f, e->name);
        fputs("\",\"cat\":\"", f); write_escaped(f, e->category ? e->category : "ui");
        fprintf(f,
                "\",\"ph\":\"X\",\"ts\":%" PRIu64 ",\"dur\":%" PRIu64
                ",\"pid\":1,\"tid\":%" PRIu64 "}",
                ts_us, dur_us, e->tid);
    }
    fputs("\n],\"displayTimeUnit\":\"ms\"}\n", f);
    const size_t count = P.count;
    const int overflowed = P.overflowed;
    SDL_UnlockMutex(P.mu);

    fclose(f);
    UI_INFO(UI_CAT_CORE, "profile trace saved: %s (%zu events%s)",
            path, count, overflowed ? ", OVERFLOWED — increase max_events" : "");
    return 1;
}

/* ---- scope record ---- */

void UIProfile_Record(const char* name, const char* category, Uint64 startTicks) {
    if (!P.initialized) ensure_init();
    if (!P.enabled) return;

    const Uint64 now = SDL_GetPerformanceCounter();
    const Uint64 dur = (now > startTicks) ? (now - startTicks) : 0;

    SDL_LockMutex(P.mu);

    /* Per-frame accumulator: bin by category. */
    if (category) {
        if      (!strcmp(category, UI_PROF_EVENT))   P.eventAccum   += dur;
        else if (!strcmp(category, UI_PROF_LAYOUT))  P.layoutAccum  += dur;
        else if (!strcmp(category, UI_PROF_RENDER))  P.renderAccum  += dur;
        else if (!strcmp(category, UI_PROF_PRESENT)) P.presentAccum += dur;
    }

    if (P.tracing) {
        if (P.count < P.cap) {
            UITraceEvt* e = &P.events[P.count++];
            e->tsTicks  = startTicks;
            e->durTicks = dur;
            e->name     = name;
            e->category = category;
            e->tid      = (Uint64)SDL_GetCurrentThreadID();
        } else {
            P.overflowed = 1;
        }
    }

    SDL_UnlockMutex(P.mu);
}

/* ---- frame stats ---- */

void UIProfile_FrameBegin(void) {
    ensure_init();
    SDL_LockMutex(P.mu);
    P.frameStartTicks = SDL_GetPerformanceCounter();
    P.eventAccum = P.layoutAccum = P.renderAccum = P.presentAccum = 0;
    P.drawCalls = 0;
    SDL_UnlockMutex(P.mu);
}

void UIProfile_FrameEnd(void) {
    ensure_init();
    const Uint64 now = SDL_GetPerformanceCounter();

    SDL_LockMutex(P.mu);
    const Uint64 freq = P.perfFreq ? P.perfFreq : 1;
    const double ms_per_tick = 1000.0 / (double)freq;
    const Uint64 dur = (now > P.frameStartTicks) ? (now - P.frameStartTicks) : 0;

    P.stats.frameTimeMs   = (double)dur            * ms_per_tick;
    P.stats.eventTimeMs   = (double)P.eventAccum   * ms_per_tick;
    P.stats.layoutTimeMs  = (double)P.layoutAccum  * ms_per_tick;
    P.stats.renderTimeMs  = (double)P.renderAccum  * ms_per_tick;
    P.stats.presentTimeMs = (double)P.presentAccum * ms_per_tick;
    P.stats.lastDrawCalls = P.drawCalls;
    P.stats.frameCount++;

    const double fpsNow = (P.stats.frameTimeMs > 0.0) ? (1000.0 / P.stats.frameTimeMs) : 0.0;
    if (P.stats.fpsSmoothed <= 0.0)  P.stats.fpsSmoothed = fpsNow;
    else                              P.stats.fpsSmoothed = P.stats.fpsSmoothed * 0.9 + fpsNow * 0.1;
    SDL_UnlockMutex(P.mu);
}

void UIProfile_AddDrawCalls(Uint32 n) {
    if (!P.initialized) return;
    SDL_LockMutex(P.mu);
    P.drawCalls += n;
    SDL_UnlockMutex(P.mu);
}

void UIProfile_GetFrameStats(UIFrameStats* out) {
    if (!out) return;
    ensure_init();
    SDL_LockMutex(P.mu);
    *out = P.stats;
    SDL_UnlockMutex(P.mu);
}
