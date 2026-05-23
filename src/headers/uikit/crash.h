#ifndef UIKIT_CRASH_H
#define UIKIT_CRASH_H

/**
 * Mocida crash handler.
 *
 * Installs a process-wide handler (SetUnhandledExceptionFilter on Win32,
 * sigaction on POSIX) that, when the app dies from an access violation,
 * abort, divide-by-zero or similar, writes a report covering:
 *
 *   - the exception/signal type and faulting address
 *   - a stack backtrace (symbolicated when DbgHelp / addr2line can)
 *   - the last 64 log messages from UIDebug
 *   - a top-level snapshot of the widget tree
 *   - allocator stats (mimalloc, if available)
 *
 * Output goes to stderr AND to a timestamped file in the working dir
 * (mocida_crash_YYYYMMDD_HHMMSS.log). The handler is robust against
 * re-entry: a second fault during reporting just bypasses the rest.
 *
 * Zero cost in normal operation — the handler only runs on a fatal
 * exception. UIApp_Create calls UICrash_Install for you.
 */

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Optional callback the handler invokes with the full report text
 *  before exiting. Useful for telemetry uploads / extra logging.
 *  Keep the implementation minimal — process state is suspect. */
typedef void (*UICrashCallback)(const char* report, void* user);

/** Optional widget-tree dumper, called by the handler while writing
 *  the report. Mocida installs its own from UIApp_Create. */
typedef void (*UICrashTreeDumper)(FILE* f, void* user);

void UICrash_Install(void);
void UICrash_Uninstall(void);

void UICrash_SetCallback(UICrashCallback cb, void* user);
void UICrash_SetTreeDumper(UICrashTreeDumper dumper, void* user);

/** Override the default log path. Pass NULL to revert to the
 *  timestamped default. */
void        UICrash_SetLogFile(const char* path);
const char* UICrash_GetLogFile(void);

/** Manually trigger a report without crashing (e.g. from an assertion
 *  handler). Does NOT terminate the process. */
void UICrash_DumpReport(const char* reason);

#ifdef __cplusplus
}
#endif

#endif /* UIKIT_CRASH_H */
