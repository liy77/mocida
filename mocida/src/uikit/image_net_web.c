#include <emscripten/fetch.h>
#include <uikit/debug.h>
#include <stdlib.h>
#include <string.h>

/* Async fetch state handed back to image.c. */
typedef struct WebFetch {
    emscripten_fetch_t* fetch;
    int done;
    int ok;
    void* data;
    size_t size;
} WebFetch;

static void on_success(emscripten_fetch_t* fetch) {
    WebFetch* wf = (WebFetch*)fetch->userData;
    wf->data = malloc(fetch->numBytes);
    if (wf->data) {
        memcpy(wf->data, fetch->data, fetch->numBytes);
        wf->size = fetch->numBytes;
        wf->ok = 1;
    }
    wf->done = 1;
    emscripten_fetch_close(fetch);
    wf->fetch = NULL;
}

static void on_error(emscripten_fetch_t* fetch) {
    WebFetch* wf = (WebFetch*)fetch->userData;
    UI_ERROR(UI_CAT_IMAGE,
             "web fetch failed for '%s': HTTP %d",
             fetch->url, fetch->status);
    wf->done = 1;
    wf->ok = 0;
    emscripten_fetch_close(fetch);
    wf->fetch = NULL;
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
    return wf;
}

/* Poll a fetch. Returns 1 when finished; on success fills outData/outSize
 * (ownership transferred to caller). */
int UIImage_RemoteHttpPoll_Web(void* cookie, void** outData, size_t* outSize) {
    WebFetch* wf = (WebFetch*)cookie;
    if (!wf) return 1;
    if (!wf->done) return 0;
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
    if (wf->fetch) {
        emscripten_fetch_close(wf->fetch);
    }
    free(wf->data);
    free(wf);
}
