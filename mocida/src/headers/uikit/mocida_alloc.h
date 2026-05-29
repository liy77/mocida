#ifndef UIKIT_MOCIDA_ALLOC_H
#define UIKIT_MOCIDA_ALLOC_H

/**
 * Mocida's central allocator switch. When the project is built with
 * MOCIDA_USE_MIMALLOC defined (CMake handles this automatically when
 * mimalloc/ exists), this header pulls in `mimalloc-override.h`, which
 * redefines `malloc`, `calloc`, `realloc` and `free` as macros that
 * forward to `mi_malloc`, `mi_calloc`, `mi_realloc` and `mi_free`.
 *
 * IMPORTANT: this header MUST be included AFTER any system header that
 * declares those functions (stdlib.h, string.h, etc.), otherwise the
 * macros would mangle their prototypes. In practice we include this
 * from the BOTTOM of widget.h, which transitively pulls stdlib.h
 * first - then anyone including <uikit/widget.h> (or any header that
 * itself includes widget.h) automatically picks up the override.
 *
 * Users that want to call mi_* directly (e.g. to query stats) can
 * include this header and use the prototypes from <mimalloc.h>.
 */
#if defined(MOCIDA_USE_MIMALLOC)
#  if defined(__has_include)
#    if __has_include(<mimalloc.h>)
#      include <mimalloc.h>
#    endif
#    if __has_include(<mimalloc-override.h>)
#      include <mimalloc-override.h>
#    endif
#  else
#    include <mimalloc.h>
#    include <mimalloc-override.h>
#  endif
#endif

#endif // UIKIT_MOCIDA_ALLOC_H
