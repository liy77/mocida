#include <emscripten/fetch.h>
#include <uikit/debug.h>
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

/* Async fetch state. The callback and the owning UIImage each release this
 * object exactly once; the second releaser frees it. This avoids calling
 * emscripten_fetch_close outside the callback, which Emscripten documents as
 * unsafe for in-flight async fetches. */
typedef struct WebFetch {
    emscripten_fetch_t* fetch;
    SDL_AtomicInt done;      /* 0 = in flight, 1 = finished */
    SDL_AtomicInt abandoned; /* 1 if owner released before callback finished */
    int ok;
    void* data;
    size_t size;
    int releases;
    SDL_Mutex* releaseMutex;
} WebFetch;

static int WebFetch_Release(WebFetch* wf) {
    if (!wf) return 1;
    SDL_LockMutex(wf->releaseMutex);
    int n = ++wf->releases;
    SDL_UnlockMutex(wf->releaseMutex);
    return n == 2;
}

static void WebFetch_Free(WebFetch* wf) {
    if (!wf) return;
    if (wf->fetch) {
        emscripten_fetch_close(wf->fetch);
        wf->fetch = NULL;
    }
    free(wf->data);
    SDL_DestroyMutex(wf->releaseMutex);
    free(wf);
}

static void on_done(emscripten_fetch_t* fetch) {
    WebFetch* wf = (WebFetch*)fetch->userData;
    if (!wf) {
        emscripten_fetch_close(fetch);
        return;
    }

    /* If not abandoned, copy the body into wf->data. */
    if (!SDL_GetAtomicInt(&wf->abandoned)) {
        if (fetch->numBytes > 0) {
            wf->data = malloc(fetch->numBytes);
            if (wf->data) {
                memcpy(wf->data, fetch->data, fetch->numBytes);
                wf->size = (size_t)fetch->numBytes;
                wf->ok = 1;
            } else {
                UI_ERROR(UI_CAT_IMAGE,
                         "web fetch: out of memory allocating %llu bytes",
                         (unsigned long long)fetch->numBytes);
            }
        }
    }

    wf->fetch = NULL;
    emscripten_fetch_close(fetch);
    SDL_SetAtomicInt(&wf->done, 1);

    if (WebFetch_Release(wf)) WebFetch_Free(wf);
}

static void on_success(emscripten_fetch_t* fetch) { on_done(fetch); }
static void on_error(emscripten_fetch_t* fetch) {
    WebFetch* wf = (WebFetch*)fetch->userData;
    if (wf && !SDL_GetAtomicInt(&wf->abandoned)) {
        UI_ERROR(UI_CAT_IMAGE,
                 "web fetch failed for '%s': HTTP %d",
                 fetch->url, fetch->status);
    }
    on_done(fetch);
}

void* UIImage_RemoteHttpStart_Web(const char* url) {
    WebFetch* wf = (WebFetch*)calloc(1, sizeof(WebFetch));
    if (!wf) return NULL;

    wf->releaseMutex = SDL_CreateMutex();
    if (!wf->releaseMutex) {
        free(wf);
        return NULL;
    }

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
        SDL_DestroyMutex(wf->releaseMutex);
        free(wf);
        return NULL;
    }
    return wf;
}

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

void UIImage_RemoteHttpFree_Web(void* cookie) {
    WebFetch* wf = (WebFetch*)cookie;
    if (!wf) return;

    /* Signal the callback that it should not copy data. */
    SDL_SetAtomicInt(&wf->abandoned, 1);

    /* Release our side. The callback will free when it finishes.
     * If the callback already finished, this release is the second one
     * and frees immediately. */
    if (WebFetch_Release(wf)) WebFetch_Free(wf);
}
