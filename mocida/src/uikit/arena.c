#include <uikit/arena.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <uikit/mocida_alloc.h>

#define UIARENA_DEFAULT_CHUNK ((size_t)(16 * 1024))
#define UIARENA_MIN_CHUNK     ((size_t)1024)

/** One linked-list node of arena memory. */
typedef struct UIArenaChunk {
    struct UIArenaChunk* next; /**< Next chunk in the singly-linked list, or NULL. */
    size_t capacity;           /**< Size of `data` in bytes. */
    size_t used;               /**< Bytes already handed out from `data`. */
    /* Flexible array - the bump region starts immediately after the
       bookkeeping. Aligned to max_align_t so the first allocation always
       lands on a valid boundary regardless of the requested alignment. */
    _Alignas(max_align_t) unsigned char data[]; /**< Aligned bump-allocation region. */
} UIArenaChunk;

/**
 * Bump-allocator arena. Hands out memory linearly from one or more
 * fixed-size chunks; freeing happens all at once via UIArena_Destroy.
 * Intended for short-lived per-frame / per-request allocations where
 * individual frees would be expensive.
 */
struct UIArena {
    UIArenaChunk* head;        /**< First chunk; never freed until UIArena_Destroy. */
    UIArenaChunk* current;     /**< Chunk we are currently bumping into. */
    size_t default_chunk_size; /**< Size used when allocating new chunks. */
    size_t bytes_reserved;     /**< Total bytes committed across every live chunk. */
};

/* Aligns `used` so that (chunk->data + used) lands on an address that
   is a multiple of `alignment`. The flex array's declared alignment is
   only max_align_t (typically 8 on Windows), so callers asking for a
   stronger alignment (16/32/64) need the absolute-address path. */
static size_t uiarena_align_used(const unsigned char* base, size_t used, size_t alignment) {
    uintptr_t addr = (uintptr_t)base + (uintptr_t)used;
    uintptr_t aligned_addr = (addr + (alignment - 1)) & ~(uintptr_t)(alignment - 1);
    return used + (size_t)(aligned_addr - addr);
}

static UIArenaChunk* uiarena_new_chunk(size_t cap) {
    UIArenaChunk* c = (UIArenaChunk*)malloc(sizeof(UIArenaChunk) + cap);
    if (!c) return NULL;
    c->next = NULL;
    c->capacity = cap;
    c->used = 0;
    return c;
}

UIArena* UIArena_Create(size_t initial_capacity) {
    if (initial_capacity < UIARENA_MIN_CHUNK) initial_capacity = UIARENA_DEFAULT_CHUNK;

    UIArena* a = (UIArena*)malloc(sizeof(UIArena));
    if (!a) return NULL;

    a->head = uiarena_new_chunk(initial_capacity);
    if (!a->head) {
        free(a);
        return NULL;
    }
    a->current = a->head;
    a->default_chunk_size = initial_capacity;
    a->bytes_reserved = initial_capacity;
    return a;
}

void* UIArena_Alloc(UIArena* arena, size_t size, size_t alignment) {
    if (!arena || size == 0) return NULL;
    if (alignment == 0) alignment = sizeof(void*);

    UIArenaChunk* c = arena->current;
    size_t aligned = uiarena_align_used(c->data, c->used, alignment);

    if (aligned + size > c->capacity) {
        /* Reuse the next chunk if Reset previously left one chained and
           it's big enough; otherwise allocate a new one and splice it
           in front of whatever followed. */
        UIArenaChunk* nc = c->next;
        if (!nc || nc->capacity < size + alignment) {
            size_t cap = arena->default_chunk_size;
            if (size + alignment > cap) cap = size + alignment;
            UIArenaChunk* fresh = uiarena_new_chunk(cap);
            if (!fresh) return NULL;
            fresh->next = c->next;
            c->next = fresh;
            arena->bytes_reserved += cap;
            nc = fresh;
        }
        arena->current = nc;
        c = nc;
        aligned = uiarena_align_used(c->data, c->used, alignment);
    }

    void* ptr = c->data + aligned;
    c->used = aligned + size;
    return ptr;
}

void* UIArena_AllocZero(UIArena* arena, size_t size, size_t alignment) {
    void* p = UIArena_Alloc(arena, size, alignment);
    if (p) memset(p, 0, size);
    return p;
}

char* UIArena_Strdup(UIArena* arena, const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char* dst = (char*)UIArena_Alloc(arena, n, 1);
    if (!dst) return NULL;
    memcpy(dst, s, n);
    return dst;
}

char* UIArena_Strndup(UIArena* arena, const char* s, size_t n) {
    if (!s) return NULL;
    size_t len = 0;
    while (len < n && s[len] != '\0') len++;
    char* dst = (char*)UIArena_Alloc(arena, len + 1, 1);
    if (!dst) return NULL;
    memcpy(dst, s, len);
    dst[len] = '\0';
    return dst;
}

void UIArena_Reset(UIArena* arena) {
    if (!arena) return;
    for (UIArenaChunk* c = arena->head; c; c = c->next) c->used = 0;
    arena->current = arena->head;
}

void UIArena_Destroy(UIArena* arena) {
    if (!arena) return;
    UIArenaChunk* c = arena->head;
    while (c) {
        UIArenaChunk* nx = c->next;
        free(c);
        c = nx;
    }
    free(arena);
}

size_t UIArena_BytesUsed(const UIArena* arena) {
    if (!arena) return 0;
    size_t total = 0;
    for (const UIArenaChunk* c = arena->head; c; c = c->next) total += c->used;
    return total;
}

size_t UIArena_BytesReserved(const UIArena* arena) {
    return arena ? arena->bytes_reserved : 0;
}
