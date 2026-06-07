#include <uikit/image.h>
#include <uikit/asset.h>
#include <uikit/debug.h>
#include <SDL3/SDL.h>
#include <string.h>

// Remote image downloads use libcurl on every platform that links it
// (Windows, Linux, macOS, Android — see CMakeLists). iOS has no libcurl in
// this setup, so it falls back to an NSURLSession implementation compiled
// from image_net_ios.mm and reached through the extern below. The trimmed
// installer build defines MOCIDA_NO_REMOTE_IMAGES (it links no HTTP backend
// and never shows remote images), which stubs the loader out entirely.
#if defined(MOCIDA_NO_REMOTE_IMAGES)
   /* no HTTP backend */
#elif defined(MOCIDA_IOS)
extern int UIImage_RemoteHttpDownload_iOS(const char* url,
                                          void** outData, size_t* outSize);
#else
#  include <curl/curl.h>
#endif

UIImage* UIImage_Create(const char* source, int animated, int nineSlice,
                          UIMarginsObject* nineSliceMargins, UIFillMode fillMode,
                          UIColor tintColor) {
    // calloc so every internal cache/scratch field (__roundedCache, __rc*,
    // __download, ...) starts zeroed - UIImage_Destroy reads some of them.
    UIImage* image = (UIImage*)calloc(1, sizeof(UIImage));
    if (image == NULL) {
        UI_ERROR(UI_CAT_IMAGE, "out of memory allocating UIImage");
        return NULL; // Memory allocation failed
    }

    image->source = _strdup(source); // Duplicate the source string
    image->animated = animated;
    image->nineSlice = nineSlice;
    image->nineSliceMargins = nineSliceMargins;
    image->fillMode = fillMode;
    image->tintColor = tintColor;
    image->__widget_type = UI_WIDGET_IMAGE; // Set the widget type
    image->loadState = IMAGE_LOAD_IN_PROGRESS; // Default load state
    image->cache = 1; // Cache remote downloads in memory by default
    image->antialiasing = 1; // Smooth (linear) scaling by default

    return image;
}

UIImage* UIImage_LoadSource(const char* source, int animated) {
    if (source == NULL) {
        UI_WARN(UI_CAT_IMAGE, "UIImage_LoadSource: source is NULL");
        return NULL; // Invalid source
    }

    UIImage* image = UIImage_Create(source, animated, 0, NULL, FILL_NONE, UI_COLOR_TRANSPARENT);
    return image;
}

UIImage* UIImage_FromMemory(SDL_Renderer* renderer,
                            const void* data, size_t size,
                            UIFillMode fillMode, UIColor tintColor) {
    if (!renderer || !data || size == 0) {
        UI_WARN(UI_CAT_IMAGE,
                "UIImage_FromMemory: invalid args (renderer=%p data=%p size=%zu)",
                (void*)renderer, data, size);
        return NULL;
    }
    SDL_Surface* surf = UIAsset_LoadSurfaceFromMemory(data, size);
    if (!surf) return NULL;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_DestroySurface(surf);
    if (!tex) {
        UI_ERROR(UI_CAT_IMAGE,
                 "UIImage_FromMemory: SDL_CreateTextureFromSurface failed: %s",
                 SDL_GetError());
        return NULL;
    }
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);

    // Empty source string keeps the renderer's lazy-load path from
    // trying to reload it from disk (it short-circuits when
    // __SDL_texture is already set, but better to be explicit).
    UIImage* image = UIImage_Create("", 0, 0, NULL, fillMode, tintColor);
    if (!image) {
        SDL_DestroyTexture(tex);
        return NULL;
    }
    image->__SDL_texture = tex;
    image->loadState     = IMAGE_LOAD_SUCCESS;
    return image;
}

/* ====================================================================== */
/* Remote (http/https) image sources                                       */
/*                                                                         */
/* A UIImage whose source is an http:// or https:// URL is fetched over    */
/* the network on a background thread (so the first frame never blocks on  */
/* I/O) and decoded once the bytes arrive. Downloads are kept in a         */
/* process-wide in-memory cache keyed by URL, so the same URL reused after */
/* the widget tree is rebuilt / the source is redefined is not refetched.  */
/* The cache lives only for the process lifetime - it is never written to  */
/* disk - so a fresh launch always refetches and picks up a changed server */
/* image (matching Qt's `Image { cache: true }`). `image->cache == 0` opts */
/* a single image out entirely: it always refetches and stores nothing.    */
/* ====================================================================== */

int UIImage_IsRemoteSource(const char* source) {
    if (!source) return 0;
    return (SDL_strncasecmp(source, "http://", 7) == 0 ||
            SDL_strncasecmp(source, "https://", 8) == 0) ? 1 : 0;
}

void UIImage_SetCache(UIImage* image, int cache) {
    if (image) image->cache = cache ? 1 : 0;
}

void UIImage_SetAntialiasing(UIImage* image, int on) {
    if (!image) return;
    image->antialiasing = on ? 1 : 0;
    if (image->__SDL_texture)
        SDL_SetTextureScaleMode(image->__SDL_texture,
            image->antialiasing ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
}

/* --- process-wide URL -> bytes cache (FIFO eviction past a size cap) --- */

typedef struct RemoteCacheNode {
    char*  url;
    void*  data;
    size_t size;
    struct RemoteCacheNode* next;
} RemoteCacheNode;

#define IMAGE_REMOTE_CACHE_MAX_BYTES (96u * 1024u * 1024u)

static RemoteCacheNode* g_remoteCacheHead = NULL;   /* newest */
static RemoteCacheNode* g_remoteCacheTail = NULL;   /* oldest */
static size_t           g_remoteCacheBytes = 0;
static SDL_Mutex*       g_remoteCacheMutex = NULL;
static SDL_Mutex*       g_remoteJobMutex   = NULL;

/* Created on the main (render) thread before any download thread spawns. */
static void RemoteEnsureInit(void) {
    if (!g_remoteCacheMutex) g_remoteCacheMutex = SDL_CreateMutex();
    if (!g_remoteJobMutex)   g_remoteJobMutex   = SDL_CreateMutex();
#if !defined(MOCIDA_IOS) && !defined(MOCIDA_NO_REMOTE_IMAGES)
    // curl_global_init is not thread-safe; do it once here on the main
    // thread before any worker thread calls curl_easy_init.
    static int curlInited = 0;
    if (!curlInited) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curlInited = 1;
    }
#endif
}

/* Returns a heap copy of the cached bytes for `url` (caller frees), or 0. */
static int RemoteCache_Get(const char* url, void** outData, size_t* outSize) {
    int found = 0;
    if (!g_remoteCacheMutex) return 0;
    SDL_LockMutex(g_remoteCacheMutex);
    for (RemoteCacheNode* n = g_remoteCacheHead; n; n = n->next) {
        if (strcmp(n->url, url) == 0) {
            void* copy = malloc(n->size);
            if (copy) {
                memcpy(copy, n->data, n->size);
                *outData = copy;
                *outSize = n->size;
                found = 1;
            }
            break;
        }
    }
    SDL_UnlockMutex(g_remoteCacheMutex);
    return found;
}

/* Stores a copy of `data` under `url` (no-op if already present). */
static void RemoteCache_Put(const char* url, const void* data, size_t size) {
    if (!g_remoteCacheMutex || size == 0 ||
        size > IMAGE_REMOTE_CACHE_MAX_BYTES) {
        return;
    }
    SDL_LockMutex(g_remoteCacheMutex);
    for (RemoteCacheNode* n = g_remoteCacheHead; n; n = n->next) {
        if (strcmp(n->url, url) == 0) {        /* already cached */
            SDL_UnlockMutex(g_remoteCacheMutex);
            return;
        }
    }
    RemoteCacheNode* node = (RemoteCacheNode*)malloc(sizeof(RemoteCacheNode));
    if (!node) { SDL_UnlockMutex(g_remoteCacheMutex); return; }
    node->url  = _strdup(url);
    node->data = malloc(size);
    if (!node->url || !node->data) {
        free(node->url);
        free(node->data);
        free(node);
        SDL_UnlockMutex(g_remoteCacheMutex);
        return;
    }
    memcpy(node->data, data, size);
    node->size = size;
    node->next = g_remoteCacheHead;            /* prepend = newest at head */
    g_remoteCacheHead = node;
    if (!g_remoteCacheTail) g_remoteCacheTail = node;
    g_remoteCacheBytes += size;

    /* Evict the oldest (tail) entries until back under the byte budget. */
    while (g_remoteCacheBytes > IMAGE_REMOTE_CACHE_MAX_BYTES &&
           g_remoteCacheHead != g_remoteCacheTail) {
        RemoteCacheNode* prev = g_remoteCacheHead;
        while (prev->next != g_remoteCacheTail) prev = prev->next;
        g_remoteCacheBytes -= g_remoteCacheTail->size;
        free(g_remoteCacheTail->url);
        free(g_remoteCacheTail->data);
        free(g_remoteCacheTail);
        prev->next = NULL;
        g_remoteCacheTail = prev;
    }
    SDL_UnlockMutex(g_remoteCacheMutex);
}

void UIImage_ClearRemoteCache(void) {
    if (!g_remoteCacheMutex) return;
    SDL_LockMutex(g_remoteCacheMutex);
    RemoteCacheNode* n = g_remoteCacheHead;
    while (n) {
        RemoteCacheNode* next = n->next;
        free(n->url);
        free(n->data);
        free(n);
        n = next;
    }
    g_remoteCacheHead = g_remoteCacheTail = NULL;
    g_remoteCacheBytes = 0;
    SDL_UnlockMutex(g_remoteCacheMutex);
}

/* --- blocking HTTP GET into a heap buffer (runs on the worker thread) -- */

#if defined(MOCIDA_NO_REMOTE_IMAGES)

/* Builds with no HTTP backend (e.g. the trimmed installer): remote URLs fail. */
static int RemoteHttp_Download(const char* url, void** outData, size_t* outSize) {
    (void)outData;
    (void)outSize;
    UI_WARN(UI_CAT_IMAGE,
            "remote image URLs are not supported in this build ('%s')",
            url ? url : "(null)");
    return 0;
}

#elif defined(MOCIDA_IOS)

/* iOS: NSURLSession (libcurl is not available there). */
static int RemoteHttp_Download(const char* url, void** outData, size_t* outSize) {
    return UIImage_RemoteHttpDownload_iOS(url, outData, outSize);
}

#else

/* Everything else: libcurl. Grows a heap buffer as the body streams in. */
typedef struct {
    unsigned char* data;
    size_t         size;
    size_t         cap;
} CurlBuf;

static size_t RemoteCurl_Write(char* ptr, size_t size, size_t nmemb, void* ud) {
    CurlBuf* b = (CurlBuf*)ud;
    size_t add = size * nmemb;
    if (b->size + add > b->cap) {
        size_t ncap = b->cap ? b->cap : (64 * 1024);
        while (ncap < b->size + add) ncap *= 2;
        unsigned char* nb = (unsigned char*)realloc(b->data, ncap);
        if (!nb) return 0;           /* a short write tells curl to abort */
        b->data = nb;
        b->cap  = ncap;
    }
    memcpy(b->data + b->size, ptr, add);
    b->size += add;
    return add;
}

static int RemoteHttp_Download(const char* url, void** outData, size_t* outSize) {
    *outData = NULL;
    *outSize = 0;

    CURL* curl = curl_easy_init();
    if (!curl) return 0;

    CurlBuf buf = { NULL, 0, 0 };
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, RemoteCurl_Write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);  /* follow redirects */
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mocida/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);        /* thread-safe timeouts */
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);     /* HTTP >= 400 -> error */

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK || buf.size == 0) {
        UI_ERROR(UI_CAT_IMAGE,
                 "remote image fetch failed for '%s': %s (HTTP %ld)",
                 url, curl_easy_strerror(rc), status);
        free(buf.data);
        return 0;
    }
    *outData = buf.data;
    *outSize = buf.size;
    return 1;
}

#endif

/* --- async download job ------------------------------------------------ */
/*
 * Ownership handshake: the worker thread always "releases" the job once,
 * and the owning UIImage releases it exactly once too - either by consuming
 * the result (UIImage_PumpRemote) or by abandoning it (UIImage_Destroy
 * before the download finished). Whichever release runs second frees the
 * job, so the buffer is never touched after free and never leaked.
 */
typedef struct {
    char*         url;
    int           useCache;
    SDL_AtomicInt done;        /* 0 = downloading, 1 = finished */
    int           releases;    /* guarded by g_remoteJobMutex */
    int           ok;          /* valid once done */
    void*         data;        /* downloaded bytes (job-owned) */
    size_t        size;
} RemoteJob;

static int RemoteJob_Release(RemoteJob* job) {
    SDL_LockMutex(g_remoteJobMutex);
    int n = ++job->releases;
    SDL_UnlockMutex(g_remoteJobMutex);
    return n == 2;             /* the second releaser frees */
}

static void RemoteJob_Free(RemoteJob* job) {
    if (!job) return;
    free(job->data);
    free(job->url);
    free(job);
}

static int SDLCALL RemoteJob_Thread(void* ud) {
    RemoteJob* job = (RemoteJob*)ud;
    void* data = NULL;
    size_t size = 0;
    int ok = 0;

    if (job->useCache && RemoteCache_Get(job->url, &data, &size)) {
        ok = 1;                                /* served from cache */
    } else {
        ok = RemoteHttp_Download(job->url, &data, &size);
        if (ok && job->useCache) RemoteCache_Put(job->url, data, size);
    }

    job->data = data;
    job->size = size;
    job->ok   = ok;
    SDL_SetAtomicInt(&job->done, 1);

    if (RemoteJob_Release(job)) RemoteJob_Free(job);
    return 0;
}

void UIImage_PumpRemote(UIImage* image, SDL_Renderer* renderer) {
    if (!image || !renderer) return;
    RemoteEnsureInit();

    RemoteJob* job = (RemoteJob*)image->__download;
    if (!job) {
        /* First visit: spin up the background download. */
        job = (RemoteJob*)calloc(1, sizeof(RemoteJob));
        if (!job) { image->loadState = IMAGE_LOAD_FAILURE; return; }
        job->url      = _strdup(image->source);
        job->useCache = image->cache ? 1 : 0;
        SDL_SetAtomicInt(&job->done, 0);
        if (!job->url) {
            free(job);
            image->loadState = IMAGE_LOAD_FAILURE;
            return;
        }
        SDL_Thread* t = SDL_CreateThread(RemoteJob_Thread, "mocida-img-dl", job);
        if (!t) {
            free(job->url);
            free(job);
            image->loadState = IMAGE_LOAD_FAILURE;
            return;
        }
        SDL_DetachThread(t);   /* self-reaping - we never join it */
        image->__download = job;
        return;
    }

    if (!SDL_GetAtomicInt(&job->done)) return;   /* still downloading */

    /* Finished: decode the bytes into a texture on the render thread. */
    if (job->ok && job->data && job->size) {
        SDL_Surface* surf = UIAsset_LoadSurfaceFromMemory(job->data, job->size);
        if (surf) {
            image->__SDL_texture = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_DestroySurface(surf);
        }
        if (image->__SDL_texture) {
            image->loadState = IMAGE_LOAD_SUCCESS;
            SDL_SetTextureScaleMode(image->__SDL_texture,
                image->antialiasing ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
        } else {
            image->loadState = IMAGE_LOAD_FAILURE;
        }
    } else {
        image->loadState = IMAGE_LOAD_FAILURE;
    }

    image->__download = NULL;
    if (RemoteJob_Release(job)) RemoteJob_Free(job);
}

void UIImage_Destroy(UIImage* image) {
    if (!image) return;
    if (image->__download) {
        /* Abandon an in-flight download: the worker thread frees the job
           when it finishes (it may keep running, but writes only into its
           own job, never into this freed image). */
        RemoteJob* job = (RemoteJob*)image->__download;
        image->__download = NULL;
        if (RemoteJob_Release(job)) RemoteJob_Free(job);
    }
    if (image->__SDL_texture) {
        SDL_DestroyTexture(image->__SDL_texture);
        image->__SDL_texture = NULL;
    }
    if (image->__roundedCache) {
        SDL_DestroyTexture(image->__roundedCache);
        image->__roundedCache = NULL;
    }
    free(image->source);
    // nineSliceMargins is a borrowed pointer (caller-owned) - don't free it.
    free(image);
}