#ifndef UIKIT_DEBUG_H
#define UIKIT_DEBUG_H

/**
 * Mocida's debug & logging subsystem.
 *
 * Inspired by Qt's QML debug channel: all the lib's internal warnings,
 * errors, and (in debug builds) trace/debug messages flow through a
 * single sink. The default sink is the terminal (stderr, with ANSI
 * colors when stdout is a TTY). Callers can redirect the stream to a
 * local TCP port — exactly like `qmldebug` — so an external inspector
 * can connect and tail it without the app printing to the terminal.
 *
 * Compile-time gating
 * -------------------
 * UI_TRACE and UI_DEBUG expand to a no-op when NDEBUG is defined and
 * MOCIDA_DEBUG is NOT. UI_INFO / UI_WARN / UI_ERROR / UI_FATAL are
 * always present (they are useful in release too) but still go through
 * the runtime level filter set by UIDebug_SetLevel.
 *
 * Runtime configuration
 * ---------------------
 *   UIDebug_OpenTerminal()        // default - stderr
 *   UIDebug_OpenPort(12345)       // listens on 127.0.0.1:12345
 *   UIDebug_OpenFile("debug.log") // appends to file
 *   UIDebug_SetHandler(fn, user)  // forward to a user callback
 *   UIDebug_SetLevel(UI_LOG_INFO) // suppress anything below
 *
 * Environment variables (read once on first log call):
 *   MOCIDA_DEBUG_PORT=12345       // auto-open the socket sink
 *   MOCIDA_DEBUG_LEVEL=warn       // trace|debug|info|warn|error|fatal|silent
 *   MOCIDA_DEBUG_FILE=path        // auto-open file sink
 *   MOCIDA_DEBUG_NO_COLOR=1       // disable ANSI colors
 *
 * Use UI_WARN_ONCE(key, ...) for "the user is doing something dumb"
 * warnings that you don't want spamming the log every frame.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>   /* FILE* for UIDebug_DumpRecentTo */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_LOG_TRACE  = 0, /**< extremely chatty; gated to debug builds. */
    UI_LOG_DEBUG  = 1, /**< developer-only info; gated to debug builds. */
    UI_LOG_INFO   = 2, /**< user-relevant lifecycle / config events. */
    UI_LOG_WARN   = 3, /**< something looks wrong but recoverable.   */
    UI_LOG_ERROR  = 4, /**< failed operation; caller will see it.    */
    UI_LOG_FATAL  = 5, /**< unrecoverable; usually right before abort. */
    UI_LOG_SILENT = 6  /**< pass to SetLevel to suppress everything. */
} UILogLevel;

typedef enum {
    UI_LOG_SINK_TERMINAL = 0,
    UI_LOG_SINK_FILE     = 1,
    UI_LOG_SINK_SOCKET   = 2,
    UI_LOG_SINK_CUSTOM   = 3,
    UI_LOG_SINK_NONE     = 4
} UILogSink;

/**
 * Custom log handler signature. Receives the already-formatted message
 * plus the structured fields, so consumers can either print verbatim or
 * re-serialize as JSON / protobuf / etc.
 */
typedef void (*UILogHandler)(UILogLevel level,
                             const char* category,
                             const char* file,
                             int         line,
                             const char* func,
                             const char* message,
                             void*       user);

/* -------- Configuration -------- */

void       UIDebug_SetLevel(UILogLevel level);
UILogLevel UIDebug_GetLevel(void);

/** Enable/disable ANSI color escapes when writing to the terminal sink. */
void UIDebug_SetColorEnabled(int enabled);

/** Default sink: stderr (and stdout on Windows so MSVC's debugger sees it). */
int UIDebug_OpenTerminal(void);

/** Append-mode log file. Returns 1 on success, 0 on failure. */
int UIDebug_OpenFile(const char* path);

/**
 * TCP listener on 127.0.0.1:port. A background thread accepts clients
 * and broadcasts every log line to all connected sockets. Run e.g.
 * `nc 127.0.0.1 <port>` (or telnet) to tail the stream.
 *
 * Returns 1 on success, 0 on failure (port busy, networking init failed).
 */
int UIDebug_OpenPort(unsigned short port);

/** Install a custom handler. Pass NULL to clear. */
void UIDebug_SetHandler(UILogHandler handler, void* user);

/** Returns the currently active sink. */
UILogSink     UIDebug_GetSink(void);
unsigned short UIDebug_GetPort(void);

/** Closes the active sink and reverts to terminal. */
void UIDebug_Close(void);

/** Flushes pending output (only meaningful for file/terminal sinks). */
void UIDebug_Flush(void);

/**
 * Writes the last ~64 log lines (a small in-memory ring) to a FILE*.
 * Used by the crash handler to surface "what was happening before the
 * fault". Safe to call from any context — uses a static buffer and no
 * malloc. */
void UIDebug_DumpRecentTo(FILE* f);

/* -------- Logging -------- */

void UIDebug_Logf(UILogLevel level,
                  const char* category,
                  const char* file,
                  int         line,
                  const char* func,
                  const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 6, 7)))
#endif
    ;

void UIDebug_LogV(UILogLevel level,
                  const char* category,
                  const char* file,
                  int         line,
                  const char* func,
                  const char* fmt,
                  va_list     ap);

/** Logs at WARN, but only the first time it is called with `key`. */
void UIDebug_WarnOnce(const char* key,
                      const char* category,
                      const char* file,
                      int         line,
                      const char* func,
                      const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 6, 7)))
#endif
    ;

/* -------- Cheap leak / lifecycle tracker --------
 *
 * Bump a per-category counter every time the lib allocates a resource,
 * decrement it on free. At shutdown UIApp_Destroy calls ReportLeaks,
 * which logs a WARN for every category with a non-zero balance. Cheap,
 * non-intrusive, and catches the dumbest leaks (forgot to Destroy()).
 * Only active in debug builds. */
void UIDebug_TrackAlloc(const char* category);
void UIDebug_TrackFree (const char* category);
void UIDebug_ReportLeaks(void);

/* -------- Compile-time gating -------- */

#if defined(MOCIDA_DEBUG) || !defined(NDEBUG)
#  define MOCIDA_DEBUG_ENABLED 1
#else
#  define MOCIDA_DEBUG_ENABLED 0
#endif

#if MOCIDA_DEBUG_ENABLED
#  define UI_TRACE(cat, ...) \
       UIDebug_Logf(UI_LOG_TRACE, (cat), __FILE__, __LINE__, __func__, __VA_ARGS__)
#  define UI_DEBUG(cat, ...) \
       UIDebug_Logf(UI_LOG_DEBUG, (cat), __FILE__, __LINE__, __func__, __VA_ARGS__)
#  define UI_WARN_ONCE(key, cat, ...) \
       UIDebug_WarnOnce((key), (cat), __FILE__, __LINE__, __func__, __VA_ARGS__)
#  define UI_TRACK_ALLOC(cat) UIDebug_TrackAlloc(cat)
#  define UI_TRACK_FREE(cat)  UIDebug_TrackFree(cat)
#  define UI_CHECK(cond, cat, ...) \
       do { if (!(cond)) UIDebug_Logf(UI_LOG_WARN, (cat), __FILE__, __LINE__, __func__, __VA_ARGS__); } while (0)
#else
#  define UI_TRACE(cat, ...)          ((void)0)
#  define UI_DEBUG(cat, ...)          ((void)0)
#  define UI_WARN_ONCE(key, cat, ...) ((void)0)
#  define UI_TRACK_ALLOC(cat)         ((void)0)
#  define UI_TRACK_FREE(cat)          ((void)0)
#  define UI_CHECK(cond, cat, ...)    ((void)0)
#endif

#define UI_INFO(cat, ...) \
    UIDebug_Logf(UI_LOG_INFO,  (cat), __FILE__, __LINE__, __func__, __VA_ARGS__)
#define UI_WARN(cat, ...) \
    UIDebug_Logf(UI_LOG_WARN,  (cat), __FILE__, __LINE__, __func__, __VA_ARGS__)
#define UI_ERROR(cat, ...) \
    UIDebug_Logf(UI_LOG_ERROR, (cat), __FILE__, __LINE__, __func__, __VA_ARGS__)
#define UI_FATAL(cat, ...) \
    UIDebug_Logf(UI_LOG_FATAL, (cat), __FILE__, __LINE__, __func__, __VA_ARGS__)

/* -------- Assertions --------
 *
 * UI_ASSERT(cond) — always active, even in release. On failure: logs
 * FATAL, flushes the sink, writes a crash report (full backtrace +
 * widget tree via UICrash_DumpReport), then abort()s. Use for
 * invariants that, if broken, mean the process must die.
 *
 * UI_DCHECK(cond) — debug-only, compiled out in release. Use for hot-
 * path sanity checks that you don't want paying for in shipped builds.
 *
 * UI_ASSERTF(cond, fmt, ...) — variant with an explanatory message.
 *
 * Forward-declared here so debug.h doesn't have to include crash.h
 * (and risk a cycle). The actual definition lives in crash.c. */
void UICrash_DumpReport(const char* reason);

#include <stdlib.h>   /* abort */

#define UI_ASSERT(cond) \
    do { if (!(cond)) { \
        UIDebug_Logf(UI_LOG_FATAL, "mocida.assert", \
                     __FILE__, __LINE__, __func__, \
                     "assertion failed: %s", #cond); \
        UIDebug_Flush(); \
        UICrash_DumpReport("assertion failed: " #cond); \
        abort(); \
    } } while (0)

#define UI_ASSERTF(cond, ...) \
    do { if (!(cond)) { \
        UIDebug_Logf(UI_LOG_FATAL, "mocida.assert", \
                     __FILE__, __LINE__, __func__, \
                     "assertion failed: " #cond); \
        UIDebug_Logf(UI_LOG_FATAL, "mocida.assert", \
                     __FILE__, __LINE__, __func__, __VA_ARGS__); \
        UIDebug_Flush(); \
        UICrash_DumpReport("assertion failed: " #cond); \
        abort(); \
    } } while (0)

#if MOCIDA_DEBUG_ENABLED
#  define UI_DCHECK(cond)       UI_ASSERT(cond)
#  define UI_DCHECKF(cond, ...) UI_ASSERTF(cond, __VA_ARGS__)
#else
#  define UI_DCHECK(cond)       ((void)0)
#  define UI_DCHECKF(cond, ...) ((void)0)
#endif

/* Common category strings — use whatever you want, these are conventions. */
#define UI_CAT_CORE     "mocida.core"
#define UI_CAT_WIDGET   "mocida.widget"
#define UI_CAT_WINDOW   "mocida.window"
#define UI_CAT_RENDER   "mocida.render"
#define UI_CAT_ASSET    "mocida.asset"
#define UI_CAT_FONT     "mocida.font"
#define UI_CAT_MEMORY   "mocida.memory"
#define UI_CAT_EVENT    "mocida.event"
#define UI_CAT_LAYOUT   "mocida.layout"
#define UI_CAT_TEXT     "mocida.text"
#define UI_CAT_IMAGE    "mocida.image"
#define UI_CAT_VIDEO    "mocida.video"
#define UI_CAT_SOUND    "mocida.sound"
#define UI_CAT_WEBVIEW  "mocida.webview"

#ifdef __cplusplus
}
#endif

#endif /* UIKIT_DEBUG_H */
