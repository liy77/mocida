/*
 * Mocida debug & logging subsystem. See debug.h for the design notes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <ctype.h>

#include <SDL3/SDL.h>

#include <uikit/debug.h>

/* ----- Platform shim for sockets + isatty ----- */
#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <io.h>
#  pragma comment(lib, "ws2_32.lib")
   typedef SOCKET     ui_socket_t;
#  define UI_INVALID_SOCKET INVALID_SOCKET
#  define ui_close_socket   closesocket
#  define ui_isatty         _isatty
#  define ui_fileno         _fileno
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
   typedef int        ui_socket_t;
#  define UI_INVALID_SOCKET (-1)
#  define ui_close_socket   close
#  define ui_isatty         isatty
#  define ui_fileno         fileno
#endif

/* ----- State ----- */
#define UI_MAX_CLIENTS 8
#define UI_LOG_BUF_SZ  1024

/* Recent-log ring buffer. Sized to fit in a CPU cache region and add
 * only one strncpy per log call. Used by the crash handler. */
#define UI_LOG_RING_SLOTS 64
#define UI_LOG_RING_LINE  256
static char g_logRing[UI_LOG_RING_SLOTS][UI_LOG_RING_LINE];
static int  g_logRingHead  = 0;
static int  g_logRingCount = 0;

/** One entry in the "warn-once" linked list. */
typedef struct UIWarnOnceEntry {
    char* key;                          /**< Owned warning key (already-fired marker). */
    struct UIWarnOnceEntry* next;       /**< Next entry in the bucket. */
} UIWarnOnceEntry;

#define UI_LEAK_BUCKETS 32
/** One bucket entry in the leak tracker. */
typedef struct UILeakEntry {
    const char* category;               /**< Allocation category (pointer-compared first, strcmp fallback). */
    long         live;                  /**< Currently-live count for this category. */
    long         peak;                  /**< High-water mark of live count. */
    struct UILeakEntry* next;           /**< Next entry in the bucket. */
} UILeakEntry;

/** Module-level debug state. Lives in the static singleton `g_dbg`. */
typedef struct {
    int            initialized;         /**< 0 until ensure_init() runs once. */
    int            envParsed;           /**< 1 after MOCIDA_LOG_* env vars were applied. */
    SDL_Mutex*     mutex;               /**< Guards every mutation of this struct. */
    UILogLevel     level;               /**< Current minimum log level (verbose..error). */
    int            colorEnabled;        /**< Non-zero enables ANSI color in stdout sink. */

    UILogSink      sink;                /**< Active sink id (stdout / file / socket / custom). */
    FILE*          fileSink;            /**< File handle when sink == file. */

    unsigned short port;                /**< TCP port for the optional socket sink. */
    ui_socket_t    listenSock;          /**< Listening socket for new debug clients. */
    ui_socket_t    clients[UI_MAX_CLIENTS]; /**< Connected debug clients. */
    int            clientCount;         /**< Number of valid entries in `clients`. */
    SDL_Thread*    acceptThread;        /**< Worker accepting connections asynchronously. */
    int            acceptStop;          /**< Sentinel: when non-zero the accept loop exits. */
#if defined(_WIN32)
    int            wsaInited;           /**< 1 once WSAStartup has been called. */
#endif

    UILogHandler   handler;             /**< Optional user-supplied log handler. */
    void*          handlerUser;         /**< Opaque pointer forwarded to `handler`. */

    UIWarnOnceEntry* warnOnce;          /**< Singly-linked list of already-fired warning keys. */

    UILeakEntry*   leaks[UI_LEAK_BUCKETS]; /**< Open-chained hash buckets for the leak tracker. */
} UIDebugState;

static UIDebugState g_dbg = {0};

/* ----- Helpers ----- */

static const char* level_name(UILogLevel lvl) {
    switch (lvl) {
        case UI_LOG_TRACE: return "TRACE";
        case UI_LOG_DEBUG: return "DEBUG";
        case UI_LOG_INFO:  return "INFO";
        case UI_LOG_WARN:  return "WARN";
        case UI_LOG_ERROR: return "ERROR";
        case UI_LOG_FATAL: return "FATAL";
        default:           return "?";
    }
}

static const char* level_color(UILogLevel lvl) {
    switch (lvl) {
        case UI_LOG_TRACE: return "\x1b[90m"; /* bright black */
        case UI_LOG_DEBUG: return "\x1b[36m"; /* cyan */
        case UI_LOG_INFO:  return "\x1b[32m"; /* green */
        case UI_LOG_WARN:  return "\x1b[33m"; /* yellow */
        case UI_LOG_ERROR: return "\x1b[31m"; /* red */
        case UI_LOG_FATAL: return "\x1b[1;41m"; /* bold on red bg */
        default:           return "";
    }
}

static UILogLevel parse_level(const char* s) {
    if (!s) return UI_LOG_INFO;
    /* lowercase compare without mutating env */
    char buf[16] = {0};
    size_t i = 0;
    for (; i < sizeof(buf) - 1 && s[i]; ++i) buf[i] = (char)tolower((unsigned char)s[i]);
    if (!strcmp(buf, "trace"))  return UI_LOG_TRACE;
    if (!strcmp(buf, "debug"))  return UI_LOG_DEBUG;
    if (!strcmp(buf, "info"))   return UI_LOG_INFO;
    if (!strcmp(buf, "warn"))   return UI_LOG_WARN;
    if (!strcmp(buf, "warning"))return UI_LOG_WARN;
    if (!strcmp(buf, "error"))  return UI_LOG_ERROR;
    if (!strcmp(buf, "fatal"))  return UI_LOG_FATAL;
    if (!strcmp(buf, "silent")) return UI_LOG_SILENT;
    if (!strcmp(buf, "off"))    return UI_LOG_SILENT;
    return UI_LOG_INFO;
}

static unsigned hash_str(const char* s) {
    unsigned h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

static const char* base_name(const char* path) {
    if (!path) return "?";
    const char* p = path;
    const char* last = path;
    for (; *p; ++p) if (*p == '/' || *p == '\\') last = p + 1;
    return last;
}

/* ----- Socket plumbing ----- */

#if defined(_WIN32)
static int ensure_wsa(void) {
    if (g_dbg.wsaInited) return 1;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;
    g_dbg.wsaInited = 1;
    return 1;
}
#endif

static void socket_drop_client_locked(int idx) {
    ui_close_socket(g_dbg.clients[idx]);
    g_dbg.clients[idx] = g_dbg.clients[g_dbg.clientCount - 1];
    g_dbg.clientCount--;
}

static int accept_thread_main(void* user) {
    (void)user;
    for (;;) {
        if (g_dbg.acceptStop) break;
        if (g_dbg.listenSock == UI_INVALID_SOCKET) break;

        struct sockaddr_in addr;
#if defined(_WIN32)
        int alen = (int)sizeof(addr);
#else
        socklen_t alen = sizeof(addr);
#endif
        ui_socket_t c = accept(g_dbg.listenSock, (struct sockaddr*)&addr, &alen);
        if (c == UI_INVALID_SOCKET) {
            if (g_dbg.acceptStop) break;
            /* transient error; brief pause to avoid busy-spin */
            SDL_Delay(20);
            continue;
        }

        SDL_LockMutex(g_dbg.mutex);
        if (g_dbg.clientCount < UI_MAX_CLIENTS) {
            g_dbg.clients[g_dbg.clientCount++] = c;
            /* greet so the user knows the channel is live */
            const char hello[] = "[mocida] debug stream connected\n";
            send(c, hello, (int)sizeof(hello) - 1, 0);
        } else {
            const char busy[] = "[mocida] too many debug clients, rejected\n";
            send(c, busy, (int)sizeof(busy) - 1, 0);
            ui_close_socket(c);
        }
        SDL_UnlockMutex(g_dbg.mutex);
    }
    return 0;
}

static void close_socket_sink_locked(void) {
    g_dbg.acceptStop = 1;
    if (g_dbg.listenSock != UI_INVALID_SOCKET) {
        ui_close_socket(g_dbg.listenSock);
        g_dbg.listenSock = UI_INVALID_SOCKET;
    }
    /* unlock while waiting so the thread can drain */
    SDL_UnlockMutex(g_dbg.mutex);
    if (g_dbg.acceptThread) {
        SDL_WaitThread(g_dbg.acceptThread, NULL);
        g_dbg.acceptThread = NULL;
    }
    SDL_LockMutex(g_dbg.mutex);

    for (int i = 0; i < g_dbg.clientCount; ++i) ui_close_socket(g_dbg.clients[i]);
    g_dbg.clientCount = 0;
    g_dbg.acceptStop = 0;
}

/* ----- Lazy init ----- */

static void parse_env_locked(void) {
    if (g_dbg.envParsed) return;
    g_dbg.envParsed = 1;

    const char* lvl = SDL_getenv("MOCIDA_DEBUG_LEVEL");
    if (lvl) g_dbg.level = parse_level(lvl);

    const char* nc = SDL_getenv("MOCIDA_DEBUG_NO_COLOR");
    if (nc && nc[0] && nc[0] != '0') g_dbg.colorEnabled = 0;

    const char* port = SDL_getenv("MOCIDA_DEBUG_PORT");
    const char* file = SDL_getenv("MOCIDA_DEBUG_FILE");
    if (port && port[0]) {
        int p = atoi(port);
        if (p > 0 && p <= 65535) {
            /* Release the lock while opening - OpenPort relocks. */
            SDL_UnlockMutex(g_dbg.mutex);
            UIDebug_OpenPort((unsigned short)p);
            SDL_LockMutex(g_dbg.mutex);
        }
    } else if (file && file[0]) {
        SDL_UnlockMutex(g_dbg.mutex);
        UIDebug_OpenFile(file);
        SDL_LockMutex(g_dbg.mutex);
    }
}

static void ensure_init(void) {
    if (g_dbg.initialized) return;
    /* SDL_CreateMutex is safe pre-SDL_Init. */
    if (!g_dbg.mutex) g_dbg.mutex = SDL_CreateMutex();
    g_dbg.level         = MOCIDA_DEBUG_ENABLED ? UI_LOG_DEBUG : UI_LOG_INFO;
    g_dbg.colorEnabled  = ui_isatty(ui_fileno(stderr)) ? 1 : 0;
    g_dbg.sink          = UI_LOG_SINK_TERMINAL;
    g_dbg.listenSock    = UI_INVALID_SOCKET;
    g_dbg.initialized   = 1;
}

/* ----- Public configuration ----- */

void UIDebug_SetLevel(UILogLevel level) {
    ensure_init();
    SDL_LockMutex(g_dbg.mutex);
    g_dbg.level = level;
    SDL_UnlockMutex(g_dbg.mutex);
}

UILogLevel UIDebug_GetLevel(void) {
    ensure_init();
    return g_dbg.level;
}

void UIDebug_SetColorEnabled(int enabled) {
    ensure_init();
    SDL_LockMutex(g_dbg.mutex);
    g_dbg.colorEnabled = enabled ? 1 : 0;
    SDL_UnlockMutex(g_dbg.mutex);
}

int UIDebug_OpenTerminal(void) {
    ensure_init();
    SDL_LockMutex(g_dbg.mutex);
    if (g_dbg.sink == UI_LOG_SINK_FILE && g_dbg.fileSink) {
        fclose(g_dbg.fileSink);
        g_dbg.fileSink = NULL;
    }
    if (g_dbg.sink == UI_LOG_SINK_SOCKET) {
        close_socket_sink_locked();
    }
    g_dbg.sink = UI_LOG_SINK_TERMINAL;
    SDL_UnlockMutex(g_dbg.mutex);
    return 1;
}

int UIDebug_OpenFile(const char* path) {
    ensure_init();
    if (!path) return 0;
    FILE* f = fopen(path, "ab");
    if (!f) return 0;

    SDL_LockMutex(g_dbg.mutex);
    if (g_dbg.sink == UI_LOG_SINK_SOCKET) close_socket_sink_locked();
    if (g_dbg.fileSink) fclose(g_dbg.fileSink);
    g_dbg.fileSink = f;
    g_dbg.sink = UI_LOG_SINK_FILE;
    SDL_UnlockMutex(g_dbg.mutex);
    return 1;
}

int UIDebug_OpenPort(unsigned short port) {
    ensure_init();

#if defined(_WIN32)
    if (!ensure_wsa()) return 0;
#endif

    ui_socket_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == UI_INVALID_SOCKET) return 0;

    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    /* Bind to loopback only — debug stream is intentionally local. */
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ui_close_socket(s);
        return 0;
    }
    if (listen(s, 4) < 0) {
        ui_close_socket(s);
        return 0;
    }

    SDL_LockMutex(g_dbg.mutex);
    if (g_dbg.sink == UI_LOG_SINK_FILE && g_dbg.fileSink) {
        fclose(g_dbg.fileSink);
        g_dbg.fileSink = NULL;
    }
    if (g_dbg.sink == UI_LOG_SINK_SOCKET) close_socket_sink_locked();

    g_dbg.listenSock  = s;
    g_dbg.port        = port;
    g_dbg.sink        = UI_LOG_SINK_SOCKET;
    g_dbg.acceptStop  = 0;
    g_dbg.acceptThread = SDL_CreateThread(accept_thread_main, "mocida-dbg", NULL);
    SDL_UnlockMutex(g_dbg.mutex);

    /* announce on the terminal too, so the dev knows where to connect. */
    fprintf(stderr, "[mocida] debug stream listening on 127.0.0.1:%u\n", (unsigned)port);
    fflush(stderr);
    return 1;
}

void UIDebug_SetHandler(UILogHandler handler, void* user) {
    ensure_init();
    SDL_LockMutex(g_dbg.mutex);
    g_dbg.handler     = handler;
    g_dbg.handlerUser = user;
    if (handler) g_dbg.sink = UI_LOG_SINK_CUSTOM;
    else if (g_dbg.sink == UI_LOG_SINK_CUSTOM) g_dbg.sink = UI_LOG_SINK_TERMINAL;
    SDL_UnlockMutex(g_dbg.mutex);
}

UILogSink UIDebug_GetSink(void) {
    ensure_init();
    return g_dbg.sink;
}

unsigned short UIDebug_GetPort(void) {
    ensure_init();
    return g_dbg.port;
}

void UIDebug_Close(void) {
    if (!g_dbg.initialized) return;
    SDL_LockMutex(g_dbg.mutex);
    if (g_dbg.sink == UI_LOG_SINK_SOCKET) close_socket_sink_locked();
    if (g_dbg.fileSink) { fclose(g_dbg.fileSink); g_dbg.fileSink = NULL; }
    g_dbg.sink    = UI_LOG_SINK_TERMINAL;
    g_dbg.handler = NULL;

    /* Release warn-once table. */
    UIWarnOnceEntry* w = g_dbg.warnOnce;
    while (w) {
        UIWarnOnceEntry* n = w->next;
        free(w->key);
        free(w);
        w = n;
    }
    g_dbg.warnOnce = NULL;

    /* Release leak table. */
    for (int i = 0; i < UI_LEAK_BUCKETS; ++i) {
        UILeakEntry* e = g_dbg.leaks[i];
        while (e) { UILeakEntry* n = e->next; free(e); e = n; }
        g_dbg.leaks[i] = NULL;
    }
    SDL_UnlockMutex(g_dbg.mutex);

#if defined(_WIN32)
    if (g_dbg.wsaInited) { WSACleanup(); g_dbg.wsaInited = 0; }
#endif
}

void UIDebug_DumpRecentTo(FILE* f) {
    if (!f) return;
    /* Snapshot indices outside the mutex — these are ints and worst
     * case we print a single duplicate or skipped line. The crash
     * handler context can't safely block on the mutex anyway. */
    int count = g_logRingCount;
    int head  = g_logRingHead;
    int start = (head - count + UI_LOG_RING_SLOTS) % UI_LOG_RING_SLOTS;
    fprintf(f, "--- recent log (%d entries) ---\n", count);
    for (int i = 0; i < count; ++i) {
        int idx = (start + i) % UI_LOG_RING_SLOTS;
        /* Lines already end with '\n' as written by emit_locked. */
        fputs(g_logRing[idx], f);
    }
}

void UIDebug_Flush(void) {
    if (!g_dbg.initialized) return;
    SDL_LockMutex(g_dbg.mutex);
    if (g_dbg.fileSink) fflush(g_dbg.fileSink);
    else if (g_dbg.sink == UI_LOG_SINK_TERMINAL) fflush(stderr);
    SDL_UnlockMutex(g_dbg.mutex);
}

/* ----- Core formatting + emit ----- */

static void emit_locked(UILogLevel level,
                        const char* category,
                        const char* file, int line, const char* func,
                        const char* message) {
    /* timestamp HH:MM:SS.mmm */
    Uint64 ms = SDL_GetTicks();
    unsigned h = (unsigned)((ms / 3600000ULL) % 24);
    unsigned m = (unsigned)((ms / 60000ULL)   % 60);
    unsigned s = (unsigned)((ms / 1000ULL)    % 60);
    unsigned u = (unsigned)(ms % 1000ULL);

    const char* base = base_name(file);
    const char* lvl  = level_name(level);
    const char* cat  = category ? category : "mocida";

    /* Custom handler short-circuits everything else. */
    if (g_dbg.handler) {
        g_dbg.handler(level, cat, file, line, func, message, g_dbg.handlerUser);
        return;
    }

    char line_buf[UI_LOG_BUF_SZ];
    int n;

    if (g_dbg.sink == UI_LOG_SINK_TERMINAL && g_dbg.colorEnabled) {
        const char* col   = level_color(level);
        const char* reset = "\x1b[0m";
        const char* dim   = "\x1b[2m";
        n = snprintf(line_buf, sizeof(line_buf),
                     "%s%02u:%02u:%02u.%03u%s %s%-5s%s %s[%s]%s %s(%s:%d %s)%s %s\n",
                     dim, h, m, s, u, reset,
                     col, lvl, reset,
                     dim, cat, reset,
                     dim, base, line, func ? func : "?", reset,
                     message);
    } else {
        n = snprintf(line_buf, sizeof(line_buf),
                     "%02u:%02u:%02u.%03u %-5s [%s] (%s:%d %s) %s\n",
                     h, m, s, u, lvl, cat, base, line, func ? func : "?", message);
    }
    if (n < 0) return;
    if (n >= (int)sizeof(line_buf)) n = (int)sizeof(line_buf) - 1;

    /* Always mirror into the recent-log ring (cheap: one strncpy per
     * log call). The crash handler dumps this on fault. */
    {
        int slot = g_logRingHead;
        g_logRingHead = (g_logRingHead + 1) % UI_LOG_RING_SLOTS;
        if (g_logRingCount < UI_LOG_RING_SLOTS) g_logRingCount++;
        int copy = n < (UI_LOG_RING_LINE - 1) ? n : (UI_LOG_RING_LINE - 1);
        memcpy(g_logRing[slot], line_buf, (size_t)copy);
        g_logRing[slot][copy] = '\0';
    }

    switch (g_dbg.sink) {
        case UI_LOG_SINK_TERMINAL:
            fwrite(line_buf, 1, (size_t)n, stderr);
            if (level >= UI_LOG_ERROR) fflush(stderr);
            break;
        case UI_LOG_SINK_FILE:
            if (g_dbg.fileSink) {
                fwrite(line_buf, 1, (size_t)n, g_dbg.fileSink);
                if (level >= UI_LOG_WARN) fflush(g_dbg.fileSink);
            }
            break;
        case UI_LOG_SINK_SOCKET: {
            for (int i = g_dbg.clientCount - 1; i >= 0; --i) {
                int sent = send(g_dbg.clients[i], line_buf, n, 0);
                if (sent <= 0) socket_drop_client_locked(i);
            }
            /* Mirror to stderr so the dev still sees output before
             * anyone has connected, similar to QML debug stream. */
            if (g_dbg.clientCount == 0) fwrite(line_buf, 1, (size_t)n, stderr);
            break;
        }
        default: break;
    }
}

void UIDebug_LogV(UILogLevel level,
                  const char* category,
                  const char* file, int line, const char* func,
                  const char* fmt, va_list ap) {
    ensure_init();
    if (level < g_dbg.level || level == UI_LOG_SILENT) return;

    char msg[UI_LOG_BUF_SZ];
    int n = vsnprintf(msg, sizeof(msg), fmt ? fmt : "", ap);
    if (n < 0) msg[0] = '\0';
    else if (n >= (int)sizeof(msg)) msg[sizeof(msg) - 1] = '\0';

    SDL_LockMutex(g_dbg.mutex);
    parse_env_locked();
    if (level >= g_dbg.level && level != UI_LOG_SILENT) {
        emit_locked(level, category, file, line, func, msg);
    }
    SDL_UnlockMutex(g_dbg.mutex);
}

void UIDebug_Logf(UILogLevel level,
                  const char* category,
                  const char* file, int line, const char* func,
                  const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    UIDebug_LogV(level, category, file, line, func, fmt, ap);
    va_end(ap);
}

/* ----- Warn-once ----- */

void UIDebug_WarnOnce(const char* key,
                      const char* category,
                      const char* file, int line, const char* func,
                      const char* fmt, ...) {
    ensure_init();
    if (!key) key = fmt ? fmt : "?";

    SDL_LockMutex(g_dbg.mutex);
    for (UIWarnOnceEntry* e = g_dbg.warnOnce; e; e = e->next) {
        if (!strcmp(e->key, key)) { SDL_UnlockMutex(g_dbg.mutex); return; }
    }
    UIWarnOnceEntry* e = (UIWarnOnceEntry*)malloc(sizeof(*e));
    if (e) {
        size_t kl = strlen(key);
        e->key = (char*)malloc(kl + 1);
        if (e->key) memcpy(e->key, key, kl + 1);
        e->next = g_dbg.warnOnce;
        g_dbg.warnOnce = e;
    }
    SDL_UnlockMutex(g_dbg.mutex);

    va_list ap;
    va_start(ap, fmt);
    UIDebug_LogV(UI_LOG_WARN, category, file, line, func, fmt, ap);
    va_end(ap);
}

/* ----- Leak tracker ----- */

static UILeakEntry* leak_get_locked(const char* category, int create) {
    if (!category) return NULL;
    unsigned h = hash_str(category) % UI_LEAK_BUCKETS;
    for (UILeakEntry* e = g_dbg.leaks[h]; e; e = e->next) {
        if (e->category == category || !strcmp(e->category, category)) return e;
    }
    if (!create) return NULL;
    UILeakEntry* e = (UILeakEntry*)malloc(sizeof(*e));
    if (!e) return NULL;
    e->category = category; /* assume caller string outlives us (it's a literal) */
    e->live = 0;
    e->peak = 0;
    e->next = g_dbg.leaks[h];
    g_dbg.leaks[h] = e;
    return e;
}

void UIDebug_TrackAlloc(const char* category) {
    if (!MOCIDA_DEBUG_ENABLED) return;
    ensure_init();
    SDL_LockMutex(g_dbg.mutex);
    UILeakEntry* e = leak_get_locked(category, 1);
    if (e) {
        e->live++;
        if (e->live > e->peak) e->peak = e->live;
    }
    SDL_UnlockMutex(g_dbg.mutex);
}

void UIDebug_TrackFree(const char* category) {
    if (!MOCIDA_DEBUG_ENABLED) return;
    ensure_init();
    SDL_LockMutex(g_dbg.mutex);
    UILeakEntry* e = leak_get_locked(category, 0);
    if (e) {
        e->live--;
        if (e->live < 0) {
            /* Drop the lock so UIDebug_Logf can take it. */
            const char* cat = e->category;
            long live = e->live;
            SDL_UnlockMutex(g_dbg.mutex);
            UIDebug_Logf(UI_LOG_WARN, UI_CAT_MEMORY, __FILE__, __LINE__, __func__,
                         "free without matching alloc in '%s' (live=%ld) — likely double-free",
                         cat, live);
            return;
        }
    }
    SDL_UnlockMutex(g_dbg.mutex);
}

void UIDebug_ReportLeaks(void) {
    if (!g_dbg.initialized) return;
    int leaked = 0;
    SDL_LockMutex(g_dbg.mutex);
    for (int i = 0; i < UI_LEAK_BUCKETS; ++i) {
        for (UILeakEntry* e = g_dbg.leaks[i]; e; e = e->next) {
            if (e->live != 0) leaked++;
        }
    }
    /* Snapshot under lock, log without. */
    if (leaked == 0) {
        SDL_UnlockMutex(g_dbg.mutex);
        return;
    }
    /* Build a compact summary while holding the lock. */
    char buf[UI_LOG_BUF_SZ];
    int off = snprintf(buf, sizeof(buf), "possible leaks at shutdown:");
    for (int i = 0; i < UI_LEAK_BUCKETS && off < (int)sizeof(buf) - 1; ++i) {
        for (UILeakEntry* e = g_dbg.leaks[i]; e && off < (int)sizeof(buf) - 1; e = e->next) {
            if (e->live != 0) {
                off += snprintf(buf + off, sizeof(buf) - (size_t)off,
                                " %s=%ld(peak %ld)", e->category, e->live, e->peak);
            }
        }
    }
    SDL_UnlockMutex(g_dbg.mutex);
    UIDebug_Logf(UI_LOG_WARN, UI_CAT_MEMORY, __FILE__, __LINE__, __func__, "%s", buf);
}
