#ifndef UIKIT_ARENA_H
#define UIKIT_ARENA_H

#include <stddef.h>

/**
 * UIArena - chunked bump allocator for "many small allocations that die
 * together". Use it for per-frame transients (formatted label strings,
 * layout temporaries, hit-test scratch) or for the entire object tree of
 * a popup/dialog whose lifetime is "open .. close".
 *
 * Properties:
 *   - O(1) allocation (pointer bump + alignment).
 *   - No individual free; UIArena_Reset wipes everything in O(chunks).
 *   - UIArena_Reset keeps reserved memory, only zeroes offsets - cheap
 *     to call every frame.
 *   - Growth is via chained chunks, never realloc, so pointers handed
 *     out before a grow stay valid.
 *
 * NOT a drop-in replacement for malloc. Do not store arena pointers in
 * places whose lifetime exceeds the next UIArena_Reset / UIArena_Destroy.
 */
typedef struct UIArena UIArena;

/**
 * Creates an arena with the given initial chunk capacity (bytes). Values
 * below 1KB are clamped up to a 16KB default. The arena starts with one
 * chunk; more are allocated on demand when allocations don't fit.
 */
UIArena* UIArena_Create(size_t initial_capacity);

/**
 * Allocates `size` bytes aligned to `alignment` (must be a power of two;
 * pass 0 to default to sizeof(void*)). Returns NULL only on OOM.
 * The returned memory is uninitialized - use UIArena_AllocZero if you
 * need zeroing.
 */
void* UIArena_Alloc(UIArena* arena, size_t size, size_t alignment);

/**
 * Same as UIArena_Alloc but zero-initializes the block.
 */
void* UIArena_AllocZero(UIArena* arena, size_t size, size_t alignment);

/**
 * Copies a NUL-terminated string into the arena. Returns NULL if `s` is
 * NULL or on OOM. The returned pointer is valid until the next reset.
 */
char* UIArena_Strdup(UIArena* arena, const char* s);

/**
 * Copies at most `n` bytes of `s` into the arena and appends a NUL
 * terminator. Stops early at the first NUL in the source. Useful for
 * sub-strings without an intermediate malloc.
 */
char* UIArena_Strndup(UIArena* arena, const char* s, size_t n);

/**
 * Resets all chunks to empty without releasing memory. After this call,
 * every pointer previously returned by the arena is invalid - the caller
 * must guarantee none are still in use.
 */
void UIArena_Reset(UIArena* arena);

/**
 * Frees the arena and every chunk it owns. Passing NULL is a no-op.
 */
void UIArena_Destroy(UIArena* arena);

/**
 * Live bytes currently bumped across all chunks (sums per-chunk offsets).
 * Useful for diagnostics; not free of cost (walks the chunk list).
 */
size_t UIArena_BytesUsed(const UIArena* arena);

/**
 * Total bytes reserved by the arena's backing chunks (capacity, not used).
 */
size_t UIArena_BytesReserved(const UIArena* arena);

/* Typed convenience wrappers. */
#define UIArena_New(arena, T)        ((T*)UIArena_Alloc((arena), sizeof(T), _Alignof(T)))
#define UIArena_NewZero(arena, T)    ((T*)UIArena_AllocZero((arena), sizeof(T), _Alignof(T)))
#define UIArena_NewArr(arena, T, n)  ((T*)UIArena_Alloc((arena), sizeof(T) * (size_t)(n), _Alignof(T)))

#endif /* UIKIT_ARENA_H */
