#include <uikit/file_dialog.h>
#include <stdlib.h>
#include <string.h>

/**
 * Heap-owned context carried from the open/save call into SDL's
 * dialog callback. SDL_DialogFileFilter holds borrowed const char*
 * pointers; we stash the duplicated strings here so they survive the
 * async call.
 */
typedef struct {
    UIFileDialogCallback cb;   /**< User callback invoked with the result. */
    void*                userdata; /**< Opaque pointer forwarded to `cb`. */
    char* filterDescDup;       /**< Owned copy of the filter description (e.g. "Images"). */
    char* filterExtsDup;       /**< Owned copy of the filter extensions list (e.g. "png;jpg"). */
} DialogCtx;

static void FreeCtx(DialogCtx* ctx) {
    if (!ctx) return;
    free(ctx->filterDescDup);
    free(ctx->filterExtsDup);
    free(ctx);
}

static void SdlDialogCb(void* userdata, const char* const* filelist, int filter) {
    (void)filter;
    DialogCtx* ctx = (DialogCtx*)userdata;
    if (!ctx) return;

    const char* picked = NULL;
    if (filelist && filelist[0]) {
        picked = filelist[0]; // first selected path (we never ask for many)
    }
    // NULL = user cancelled, error, etc. - forward as NULL so the
    // caller can detect.
    if (ctx->cb) ctx->cb(picked, ctx->userdata);
    FreeCtx(ctx);
}

// Builds an SDL_DialogFileFilter[] from our two-string convention.
// Returns the number of valid filters set into `filters`. `filters`
// must be capacity 2 at minimum (the user's filter + the "*" catch-all).
static int BuildFilters(const char* desc, const char* exts,
                        SDL_DialogFileFilter* filters,
                        char** outDescDup, char** outExtsDup) {
    int n = 0;
    if (exts && *exts) {
        char* descDup = _strdup(desc && *desc ? desc : "Files");
        char* extsDup = _strdup(exts);
        if (descDup && extsDup) {
            filters[n].name    = descDup;
            filters[n].pattern = extsDup;
            n++;
            *outDescDup = descDup;
            *outExtsDup = extsDup;
        } else {
            free(descDup);
            free(extsDup);
        }
    }
    // Always include an "All files" fallback so the user can override.
    filters[n].name    = "All files";
    filters[n].pattern = "*";
    n++;
    return n;
}

void UIFileDialog_OpenFile(SDL_Window* window,
                           const char* filterDesc,
                           const char* filterExts,
                           UIFileDialogCallback cb,
                           void* userdata) {
    DialogCtx* ctx = (DialogCtx*)calloc(1, sizeof(DialogCtx));
    if (!ctx) { if (cb) cb(NULL, userdata); return; }
    ctx->cb = cb;
    ctx->userdata = userdata;

    SDL_DialogFileFilter filters[2];
    const int nfilters = BuildFilters(filterDesc, filterExts, filters,
                                      &ctx->filterDescDup, &ctx->filterExtsDup);

    SDL_ShowOpenFileDialog(SdlDialogCb, ctx, window, filters, nfilters,
                           NULL, false);
}

void UIFileDialog_SaveFile(SDL_Window* window,
                           const char* filterDesc,
                           const char* filterExts,
                           UIFileDialogCallback cb,
                           void* userdata) {
    DialogCtx* ctx = (DialogCtx*)calloc(1, sizeof(DialogCtx));
    if (!ctx) { if (cb) cb(NULL, userdata); return; }
    ctx->cb = cb;
    ctx->userdata = userdata;

    SDL_DialogFileFilter filters[2];
    const int nfilters = BuildFilters(filterDesc, filterExts, filters,
                                      &ctx->filterDescDup, &ctx->filterExtsDup);

    SDL_ShowSaveFileDialog(SdlDialogCb, ctx, window, filters, nfilters,
                           NULL);
}
