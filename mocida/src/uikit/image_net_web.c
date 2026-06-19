#include <emscripten/fetch.h>
#include <uikit/debug.h>
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

/* Async fetch state. Callbacks run on the main thread in Emscripten,
 * but we still use atomics for state flags to be safe across yields. */
typedef struct WebFetch {
    emscripten_fetch_t* fetch;
    SDL_AtomicInt done;      /* 0 = in flight, 1 = finished (ok or failed) */
    SDL_AtomicInt abandoned; /* 1 if the owner asked to cancel */
    int ok;
    void* data;
    size_t size;
} WebFetch;

static void finish_fetch(emscripten_fetch_t* fetch) {
    WebFetch* wf = (WebFetch*)fetch->userData;
    if (!wf) {
        emscripten_fetch_close(fetch);
        return;
    }

    /* If abandoned, just signal completion so the owner can clean up. */
    if (SDL_GetAtomicInt(&wf->abandoned)) {
        SDL_SetAtomicInt(&wf->done, 1);
        return;
    }

    wf->fetch = NULL;
    emscripten_fetch_close(fetch);
    SDL_SetAtomicInt(&wf->done, 1);
}

static void on_success(emscripten_fetch_t* fetch) {
    WebFetch* wf = (WebFetch*)fetch->userData;
    if (!wf) { emscripten_fetch_close(fetch); return; }

    if (!SDL_GetAtomicInt(&wf->abandoned)) {
        if (fetch->numBytes > 0) {
            wf->data = malloc(fetch->numBytes);
            if (wf->data) {
                memcpy(wf->data, fetch->data, fetch->numBytes);
                wf->size = fetch->numBytes;
                wf->ok = 1;
            } else {
                UI_ERROR(UI_CAT_IMAGE,
                         "web fetch: out of memory allocating %llu bytes",
                         (unsigned long long)fetch->numBytes);
            }
        }
    }
    finish_fetch(fetch);
}

static void on_error(emscripten_fetch_t* fetch) {
    WebFetch* wf = (WebFetch*)fetch->userData;
    if (wf && !SDL_GetAtomicInt(&wf->abandoned)) {
        UI_ERROR(UI_CAT_IMAGE,
                 "web fetch failed for '%s': HTTP %d",
                 fetch->url, fetch->status);
    }
    finish_fetch(fetch);
}

/* Start an async download. Returns a cookie the caller owns. */
void* UIImage_RemoteHttpStart_Web(const char* url) {
    WebFetch* wf = (WebFetch*)calloc(1, sizeof(WebFetch));
    if (!wf) return NULL;

    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = on_success;
    attr.onerror = on_error;
    attr.userData = wf;

    wf->fetch = emscripten_fetch(&attr, url);
    if (!wf->fetch) {
        UI_ERROR(UI_CAT_IMAGE, "web fetch: failed to start request for '%s'", url);
        free(wf);
        return NULL;
    }
    return wf;
}

/* Poll a fetch. Returns 1 when finished; on success fills outData/outSize
 * (ownership transferred to caller). */
int UIImage_RemoteHttpPoll_Web(void* cookie, void** outData, size_t* outSize) {
    WebFetch* wf = (WebFetch*)cookie;
    if (!wf) return 1;
    if (!SDL_GetAtomicInt(&wf->done)) return 0;

    if (wf->ok && outData && outSize) {
        *outData = wf->data;
        *outSize = wf->size;
        wf->data = NULL;
        wf->size = 0;
    }
    return 1;
}

void WebFetch_Free(void* cookie) {
    WebFetch* wf = (WebFetch*)cookie;
    if (!wf) return;

    /* Mark abandoned first so callbacks don't touch owned data. */
    SDL_SetAtomicInt(&wf->abandoned, 1);

    if (wf->fetch) {
        emscripten_fetch_close(wf->fetch);
        wf->fetch = NULL;
    }

    /* If the callback already ran, we can free now; otherwise the callback
     * will free the struct when it fires. */
    if (SDL_GetAtomicInt(&wf->done)) {
        free(wf->data);
        free(wf);
    }
}
