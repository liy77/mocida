/*
 * Mocida crash handler. See crash.h for the design notes.
 *
 * Zero overhead in normal operation — the only work this file does at
 * runtime is set a few globals during UICrash_Install. The handler is
 * invoked by the OS when the process is already dying.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL.h>

#include <uikit/crash.h>
#include <uikit/debug.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <dbghelp.h>
#  pragma comment(lib, "dbghelp.lib")
#else
#  include <signal.h>
#  include <unistd.h>
#  if defined(__has_include)
#    if __has_include(<execinfo.h>)
#      include <execinfo.h>
#      define UI_HAVE_EXECINFO 1
#    endif
#  endif
#endif

#if defined(MOCIDA_USE_MIMALLOC)
#  include <mimalloc.h>
#endif

#define UI_CRASH_PATH_MAX 512

static UICrashCallback   g_cb        = NULL;
static void*             g_cbUser    = NULL;
static UICrashTreeDumper g_treeDump  = NULL;
static void*             g_treeUser  = NULL;
static char              g_logPath[UI_CRASH_PATH_MAX] = {0};
static int               g_installed = 0;
/* Re-entry guard: if the dump itself faults, bail out instead of
 * looping forever. Read once, set once. */
static volatile long     g_inHandler = 0;

/* ---- helpers ---- */

static void format_timestamp(char* buf, size_t cap) {
    time_t t = time(NULL);
    struct tm tmv;
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    snprintf(buf, cap, "%04d%02d%02d_%02d%02d%02d",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
}

static void default_log_path(char* buf, size_t cap) {
    char ts[32];
    format_timestamp(ts, sizeof(ts));
    snprintf(buf, cap, "mocida_crash_%s.log", ts);
}

static const char* resolve_log_path(char* fallback, size_t cap) {
    if (g_logPath[0]) return g_logPath;
    default_log_path(fallback, cap);
    return fallback;
}

#if defined(_WIN32)

static const char* win_exception_name(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:         return "ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_DATATYPE_MISALIGNMENT:    return "DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_INVALID_OPERATION:    return "FLT_INVALID_OPERATION";
        case EXCEPTION_ILLEGAL_INSTRUCTION:      return "ILLEGAL_INSTRUCTION";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "INT_DIVIDE_BY_ZERO";
        case EXCEPTION_PRIV_INSTRUCTION:         return "PRIV_INSTRUCTION";
        case EXCEPTION_STACK_OVERFLOW:           return "STACK_OVERFLOW";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE";
        case EXCEPTION_IN_PAGE_ERROR:            return "IN_PAGE_ERROR";
        default:                                 return "UNKNOWN";
    }
}

static int symbols_ready = 0;
static void ensure_symbols(void) {
    if (symbols_ready) return;
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    if (SymInitialize(GetCurrentProcess(), NULL, TRUE)) {
        symbols_ready = 1;
    }
}

static void write_stack_win(FILE* f, CONTEXT* ctx) {
    ensure_symbols();
    HANDLE proc = GetCurrentProcess();

    /* CaptureStackBackTrace is simpler and reliable. For full fidelity
     * StackWalk64 would be needed, but on x64 + clang the backtrace
     * captured below is usually enough to find the offender. */
    void* frames[64];
    USHORT n = CaptureStackBackTrace(0, 64, frames, NULL);
    (void)ctx;

    fprintf(f, "--- stack (%u frames) ---\n", (unsigned)n);

    /* SYMBOL_INFO with extra room for the name. */
    char buf[sizeof(SYMBOL_INFO) + 512];
    SYMBOL_INFO* sym = (SYMBOL_INFO*)buf;
    memset(buf, 0, sizeof(buf));
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen   = 510;

    IMAGEHLP_LINE64 lineInfo;
    memset(&lineInfo, 0, sizeof(lineInfo));
    lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    for (USHORT i = 0; i < n; ++i) {
        DWORD64 addr = (DWORD64)(uintptr_t)frames[i];
        const char* name = "??";
        const char* file = "";
        DWORD       line = 0;
        DWORD64     disp = 0;
        DWORD       dispLine = 0;

        if (symbols_ready && SymFromAddr(proc, addr, &disp, sym)) {
            name = sym->Name;
        }
        if (symbols_ready && SymGetLineFromAddr64(proc, addr, &dispLine, &lineInfo)) {
            file = lineInfo.FileName ? lineInfo.FileName : "";
            line = lineInfo.LineNumber;
        }
        fprintf(f, "  #%-2u 0x%016llx  %s", (unsigned)i, (unsigned long long)addr, name);
        if (file[0]) fprintf(f, "  (%s:%u)", file, (unsigned)line);
        fputc('\n', f);
    }
}

#endif /* _WIN32 */

static void write_memory_stats(FILE* f) {
    fputs("--- allocator ---\n", f);
#if defined(MOCIDA_USE_MIMALLOC)
    size_t elapsed = 0, user = 0, sys = 0, cur = 0, peak = 0;
    size_t pf = 0, pr = 0, pc = 0;
    mi_process_info(&elapsed, &user, &sys, &cur, &peak, &pf, &pr, &pc);
    fprintf(f, "  mimalloc: current=%zu bytes  peak=%zu  committed=%zu\n",
            cur, peak, pc);
#else
    fputs("  (no mimalloc)\n", f);
#endif
}

static void write_report(FILE* f, const char* reason,
#if defined(_WIN32)
                         EXCEPTION_POINTERS* ep
#else
                         int sig
#endif
                         ) {
    char ts[32];
    format_timestamp(ts, sizeof(ts));
    fprintf(f, "============================================================\n");
    fprintf(f, "  Mocida crash report  %s\n", ts);
    fprintf(f, "  Reason: %s\n", reason ? reason : "(unset)");
    fprintf(f, "============================================================\n");

#if defined(_WIN32)
    if (ep && ep->ExceptionRecord) {
        EXCEPTION_RECORD* er = ep->ExceptionRecord;
        fprintf(f, "  Exception : 0x%08lX (%s)\n",
                er->ExceptionCode, win_exception_name(er->ExceptionCode));
        fprintf(f, "  Address   : 0x%016llx\n",
                (unsigned long long)(uintptr_t)er->ExceptionAddress);
        if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
            er->NumberParameters >= 2) {
            fprintf(f, "  AV %s at 0x%016llx\n",
                    er->ExceptionInformation[0] == 0 ? "read" :
                    er->ExceptionInformation[0] == 1 ? "write" : "execute",
                    (unsigned long long)er->ExceptionInformation[1]);
        }
    }
    write_stack_win(f, ep ? ep->ContextRecord : NULL);
#else
    fprintf(f, "  Signal    : %d\n", sig);
#  if defined(UI_HAVE_EXECINFO)
    void* frames[64];
    int n = backtrace(frames, 64);
    fprintf(f, "--- stack (%d frames) ---\n", n);
    char** syms = backtrace_symbols(frames, n);
    if (syms) {
        for (int i = 0; i < n; ++i) fprintf(f, "  #%-2d %s\n", i, syms[i]);
        free(syms);
    }
#  else
    fputs("--- stack: unavailable (no execinfo) ---\n", f);
#  endif
#endif

    write_memory_stats(f);

    if (g_treeDump) {
        fputs("--- widget tree ---\n", f);
        g_treeDump(f, g_treeUser);
    }

    UIDebug_DumpRecentTo(f);

    fputs("============================================================\n", f);
    fflush(f);
}

void UICrash_DumpReport(const char* reason) {
    char fallback[UI_CRASH_PATH_MAX];
    const char* path = resolve_log_path(fallback, sizeof(fallback));

    fprintf(stderr, "\n[mocida] crash dump → %s\n", path);

    /* Stderr first so it's there even if the file open fails. */
#if defined(_WIN32)
    write_report(stderr, reason, NULL);
#else
    write_report(stderr, reason, 0);
#endif

    FILE* f = fopen(path, "wb");
    if (f) {
#if defined(_WIN32)
        write_report(f, reason, NULL);
#else
        write_report(f, reason, 0);
#endif
        fclose(f);
    }
}

#if defined(_WIN32)

static LONG WINAPI win_filter(EXCEPTION_POINTERS* ep) {
    /* Atomically check + set the re-entry guard. */
    if (InterlockedExchange(&g_inHandler, 1) != 0) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    char fallback[UI_CRASH_PATH_MAX];
    const char* path = resolve_log_path(fallback, sizeof(fallback));

    const char* reason = ep && ep->ExceptionRecord ?
                         win_exception_name(ep->ExceptionRecord->ExceptionCode) :
                         "unknown exception";

    fprintf(stderr, "\n[mocida] crash → %s  (%s)\n", path, reason);
    write_report(stderr, reason, ep);

    FILE* f = fopen(path, "wb");
    if (f) {
        write_report(f, reason, ep);
        fclose(f);
    }

    if (g_cb) {
        /* Tiny snapshot for the callback; full report already on disk. */
        char small[256];
        snprintf(small, sizeof(small), "mocida crash: %s; log=%s", reason, path);
        g_cb(small, g_cbUser);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

#else

static void posix_handler(int sig) {
    if (__sync_lock_test_and_set(&g_inHandler, 1)) {
        _exit(128 + sig);
    }
    char fallback[UI_CRASH_PATH_MAX];
    const char* path = resolve_log_path(fallback, sizeof(fallback));
    fprintf(stderr, "\n[mocida] crash → %s  (signal %d)\n", path, sig);
    write_report(stderr, "signal", sig);
    FILE* f = fopen(path, "wb");
    if (f) {
        write_report(f, "signal", sig);
        fclose(f);
    }
    if (g_cb) {
        char small[256];
        snprintf(small, sizeof(small), "mocida crash: signal %d; log=%s", sig, path);
        g_cb(small, g_cbUser);
    }
    /* Restore default handler and re-raise so the OS records the crash
     * (core dump etc) as the user expects. */
    signal(sig, SIG_DFL);
    raise(sig);
}

#endif

void UICrash_Install(void) {
    if (g_installed) return;
    g_installed = 1;

#if defined(_WIN32)
    SetUnhandledExceptionFilter(win_filter);
    ensure_symbols();
#else
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = posix_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
#  ifdef SIGBUS
    sigaction(SIGBUS,  &sa, NULL);
#  endif
#endif

    UI_INFO(UI_CAT_CORE, "crash handler installed");
}

void UICrash_Uninstall(void) {
    if (!g_installed) return;
#if defined(_WIN32)
    SetUnhandledExceptionFilter(NULL);
#else
    signal(SIGSEGV, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
    signal(SIGFPE,  SIG_DFL);
    signal(SIGILL,  SIG_DFL);
#  ifdef SIGBUS
    signal(SIGBUS, SIG_DFL);
#  endif
#endif
    g_installed = 0;
}

void UICrash_SetCallback(UICrashCallback cb, void* user) {
    g_cb     = cb;
    g_cbUser = user;
}

void UICrash_SetTreeDumper(UICrashTreeDumper dumper, void* user) {
    g_treeDump = dumper;
    g_treeUser = user;
}

void UICrash_SetLogFile(const char* path) {
    if (!path || !path[0]) { g_logPath[0] = '\0'; return; }
    strncpy(g_logPath, path, sizeof(g_logPath) - 1);
    g_logPath[sizeof(g_logPath) - 1] = '\0';
}

const char* UICrash_GetLogFile(void) {
    return g_logPath[0] ? g_logPath : NULL;
}
