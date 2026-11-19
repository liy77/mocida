#include <uikit/net.h>
#include <uikit/debug.h>

#include <SDL3/SDL.h>
#if !defined(MOCIDA_WEB)
#include <curl/curl.h>
#endif

#include <stdio.h>
#include <string.h>

#if defined(MOCIDA_WEB)
#include <emscripten.h>

static char g_web_error[256];
static float g_web_progress = 1.0f;
static int g_web_done = 0;
static int g_web_total = 0;

static int UrlAllowedWeb(const char* url) {
    if (!url || !url[0]) return 0;
    /* Same-origin relative paths (e.g. /fonts/Inter.ttf) and absolute http(s). */
    if (url[0] == '/') return 1;
    if (SDL_strncmp(url, "https://", 8) != 0 && SDL_strncmp(url, "http://", 7) != 0) return 0;
    if (SDL_strstr(url, "..") || SDL_strstr(url, "\r") || SDL_strstr(url, "\n")) return 0;
    return 1;
}

int UINet_Reachable(const char* url) {
    void* data = NULL;
    int size = 0;
    if (!UINet_Get(url, &data, &size)) return 0;
    SDL_free(data);
    return 1;
}

int UINet_Get(const char* url, void** out_data, int* out_size) {
    if (out_data) *out_data = NULL;
    if (out_size) *out_size = 0;
    g_web_error[0] = 0;
    g_web_progress = 0.0f;
    g_web_done = 0;
    g_web_total = 0;
    if (!UrlAllowedWeb(url)) {
        SDL_strlcpy(g_web_error, "only http(s) or same-origin paths are accepted", sizeof(g_web_error));
        return 0;
    }
    /* Sync HTTP on the main thread needs ASYNCIFY/JSPI. We deliberately do
       NOT call emscripten_wget_data here — without those link flags it
       aborts the whole module ("Please compile with async support").
       Callers (FontFace, etc.) should stage same-origin files into MEMFS
       from JS preRun (fetch → FS.writeFile) and open them as local paths. */
    SDL_snprintf(g_web_error, sizeof(g_web_error),
                 "sync fetch unavailable for '%s' (stage into FS from JS, or link with ASYNCIFY)",
                 url);
    return 0;
}

int UINet_Download(const char* url, const char* path) {
    void* data = NULL;
    int size = 0;
    if (!UINet_Get(url, &data, &size)) return 0;
    /* Ensure parent dir exists (best-effort). */
    char dir[1024];
    SDL_strlcpy(dir, path ? path : "", sizeof(dir));
    char* slash = strrchr(dir, '/');
    if (slash) {
        *slash = 0;
        if (dir[0]) EM_ASM({ try { FS.mkdirTree(UTF8ToString($0)); } catch (e) {} }, dir);
    }
    FILE* f = fopen(path, "wb");
    if (!f) {
        SDL_free(data);
        SDL_strlcpy(g_web_error, "could not write download path", sizeof(g_web_error));
        return 0;
    }
    size_t n = fwrite(data, 1, (size_t)size, f);
    fclose(f);
    SDL_free(data);
    if (n != (size_t)size) {
        remove(path);
        SDL_strlcpy(g_web_error, "short write", sizeof(g_web_error));
        return 0;
    }
    return 1;
}

float UINet_Progress(void) { return g_web_progress; }
int UINet_BytesDone(void) { return g_web_done; }
int UINet_BytesTotal(void) { return g_web_total; }
const char* UINet_Error(void) { return g_web_error; }
#else
static float g_progress = 1.0f;
static int g_done = 0;
static int g_total = 0;
static char g_error[256];

static void NetInit(void) {
    static int once = 0;
    if (once) return;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    once = 1;
}

typedef struct {
    unsigned char* data;
    size_t size;
    size_t cap;
} NetBuf;

static size_t WriteMem(char* ptr, size_t size, size_t nmemb, void* ud) {
    NetBuf* b = (NetBuf*)ud;
    size_t add = size * nmemb;
    if (b->size + add + 1 > b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 65536;
        while (ncap < b->size + add + 1) ncap *= 2;
        unsigned char* nb = (unsigned char*)SDL_realloc(b->data, ncap);
        if (!nb) return 0;
        b->data = nb;
        b->cap = ncap;
    }
    memcpy(b->data + b->size, ptr, add);
    b->size += add;
    b->data[b->size] = 0;
    g_done = (int)b->size;
    if (g_total > 0) g_progress = (float)g_done / (float)g_total;
    return add;
}

static int ProgressCb(void* ud, curl_off_t dl_total, curl_off_t dl_now, curl_off_t ul_total, curl_off_t ul_now) {
    (void)ud; (void)ul_total; (void)ul_now;
    if (dl_total > 0) {
        g_total = (int)dl_total;
        g_done = (int)dl_now;
        g_progress = (float)dl_now / (float)dl_total;
    }
    return 0;
}

static int UrlAllowed(const char* url) {
    if (!url || !url[0]) return 0;
    if (SDL_strncmp(url, "https://", 8) != 0 && SDL_strncmp(url, "http://", 7) != 0) return 0;
    if (SDL_strstr(url, "..") || SDL_strstr(url, "\r") || SDL_strstr(url, "\n")) return 0;
    return 1;
}

static CURL* Begin(const char* url) {
    if (!UrlAllowed(url)) {
        SDL_strlcpy(g_error, "only http and https URLs are accepted", sizeof(g_error));
        return NULL;
    }
    NetInit();
    g_error[0] = 0;
    g_progress = 0.0f;
    g_done = 0;
    g_total = 0;
    CURL* curl = curl_easy_init();
    if (!curl) {
        SDL_strlcpy(g_error, "curl init failed", sizeof(g_error));
        return NULL;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mocidad/1.0");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCb);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    return curl;
}

int UINet_Reachable(const char* url) {
    if (!url || !url[0]) return 0;
    CURL* curl = Begin(url);
    if (!curl) return 0;
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK) {
        SDL_strlcpy(g_error, curl_easy_strerror(rc), sizeof(g_error));
        return 0;
    }
    g_progress = 1.0f;
    return status >= 200 && status < 400;
}

int UINet_Get(const char* url, void** out_data, int* out_size) {
    if (out_data) *out_data = NULL;
    if (out_size) *out_size = 0;
    if (!url || !url[0]) return 0;
    NetBuf buf = { NULL, 0, 0 };
    CURL* curl = Begin(url);
    if (!curl) return 0;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMem);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK) {
        SDL_strlcpy(g_error, curl_easy_strerror(rc), sizeof(g_error));
        SDL_free(buf.data);
        return 0;
    }
    if (out_data) *out_data = buf.data ? buf.data : SDL_calloc(1, 1);
    if (out_size) *out_size = (int)buf.size;
    if (!out_data) SDL_free(buf.data);
    g_progress = 1.0f;
    return 1;
}

int UINet_Download(const char* url, const char* path) {
    if (!url || !path || !path[0]) return 0;
    // Parent folder.
    char folder[1024];
    SDL_strlcpy(folder, path, sizeof(folder));
    char* slash = strrchr(folder, '/');
    if (!slash) slash = strrchr(folder, '\\');
    if (slash) {
        *slash = 0;
        SDL_CreateDirectory(folder);
    }
    FILE* file = fopen(path, "wb");
    if (!file) {
        SDL_strlcpy(g_error, "could not open destination", sizeof(g_error));
        return 0;
    }
    CURL* curl = Begin(url);
    if (!curl) {
        fclose(file);
        return 0;
    }
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    fclose(file);
    if (rc != CURLE_OK) {
        SDL_strlcpy(g_error, curl_easy_strerror(rc), sizeof(g_error));
        remove(path);
        return 0;
    }
    g_progress = 1.0f;
    return 1;
}

float UINet_Progress(void) { return g_progress; }
int UINet_BytesDone(void) { return g_done; }
int UINet_BytesTotal(void) { return g_total; }
const char* UINet_Error(void) { return g_error; }
#endif
