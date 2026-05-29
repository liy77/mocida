#ifndef UIKIT_PERF_H
#define UIKIT_PERF_H

// Cross-platform performance hints. Every macro has a portable no-op
// fallback so consumers can use them unconditionally; the actual
// codegen-helping form fires only on the toolchains that understand it.
//
//   UI_FORCE_INLINE   — same as `static inline` plus a hard hint that
//                        the compiler should inline this regardless of
//                        size/threshold heuristics. Useful for tiny
//                        helpers called inside per-frame hot loops
//                        where the call overhead can dominate.
//
//   UI_LIKELY(x) /    — branch prediction hints. `if (UI_LIKELY(cond))`
//   UI_UNLIKELY(x)      tells the optimizer to lay out the taken/not-
//                        taken paths so the common case stays on the
//                        primary code path (better i-cache density).
//
//   UI_PREFETCH(p)    — read prefetch into L1. Schedules the cache line
//                        containing `p` to be pulled in before the CPU
//                        actually loads from it. Helps when iterating
//                        through irregular memory where the hardware
//                        prefetcher can't predict the next access.
//
//   UI_PREFETCH_RW(p) — same as UI_PREFETCH but signals "I'm about to
//                        write here" so the cache line is allocated in
//                        the modified state, skipping a Read-For-
//                        Ownership transaction on the first store.
//
//   UI_RESTRICT       — `restrict` qualifier (C99) on pointers — tells
//                        the compiler the two pointers don't alias.
//                        Lets the optimizer reorder loads/stores in
//                        tight loops. Plain `restrict` works on GCC,
//                        Clang, MSVC; this macro normalizes it.

#if defined(__GNUC__) || defined(__clang__)
    #define UI_FORCE_INLINE     static inline __attribute__((always_inline))
    #define UI_LIKELY(x)        __builtin_expect(!!(x), 1)
    #define UI_UNLIKELY(x)      __builtin_expect(!!(x), 0)
    #define UI_PREFETCH(p)      __builtin_prefetch((const void*)(p), 0, 3)
    #define UI_PREFETCH_RW(p)   __builtin_prefetch((const void*)(p), 1, 3)
    #define UI_RESTRICT         __restrict__
#elif defined(_MSC_VER)
    #include <intrin.h>
    #define UI_FORCE_INLINE     static __forceinline
    #define UI_LIKELY(x)        (x)
    #define UI_UNLIKELY(x)      (x)
    #define UI_PREFETCH(p)      _mm_prefetch((const char*)(p), _MM_HINT_T0)
    #define UI_PREFETCH_RW(p)   _mm_prefetch((const char*)(p), _MM_HINT_T0)
    #define UI_RESTRICT         __restrict
#else
    #define UI_FORCE_INLINE     static inline
    #define UI_LIKELY(x)        (x)
    #define UI_UNLIKELY(x)      (x)
    #define UI_PREFETCH(p)      ((void)0)
    #define UI_PREFETCH_RW(p)   ((void)0)
    #define UI_RESTRICT
#endif

#endif // UIKIT_PERF_H
