// bundle.c — mocida:// virtual asset registry + app.bundle manifest.
#include <uikit/bundle.h>
#include <uikit/debug.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct BundleEntry {
    char* key;       // "font.ttf"
    char* src;       // registered source path
    char* resolved;  // cached resolved absolute path (NULL until first resolve)
    struct BundleEntry* next;
} BundleEntry;

static BundleEntry* g_bundle     = NULL;
static char*        g_bundleName = NULL;
static char*        g_bundleId   = NULL;

static char* dupstr(const char* s) { return s ? _strdup(s) : NULL; }

static const char* strip_scheme(const char* uri) {
    if (!uri) return NULL;
    size_t n = strlen(UI_BUNDLE_SCHEME);
    if (strncmp(uri, UI_BUNDLE_SCHEME, n) == 0) return uri + n;
    return uri;
}

static int file_exists(const char* p) {
    if (!p || !*p) return 0;
    FILE* f = fopen(p, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

static BundleEntry* find_entry(const char* key) {
    for (BundleEntry* e = g_bundle; e; e = e->next)
        if (strcmp(e->key, key) == 0) return e;
    return NULL;
}

void UIApp_SetBundle(const char* virtualUri, const char* realPath) {
    if (!virtualUri || !realPath) return;
    const char* key = strip_scheme(virtualUri);
    if (!*key) return;
    BundleEntry* e = find_entry(key);
    if (e) {                                   // replace
        free(e->src); free(e->resolved);
        e->src = dupstr(realPath); e->resolved = NULL;
        return;
    }
    e = (BundleEntry*)calloc(1, sizeof(*e));
    if (!e) return;
    e->key  = dupstr(key);
    e->src  = dupstr(realPath);
    e->next = g_bundle;
    g_bundle = e;
}

// Resolve order: registered src (if it exists) -> <base>/assets/key ->
// <base>/key. On desktop the src usually wins; on the iOS sandbox the src
// dev-path is gone and the build-copied <base>/assets/key is used.
static const char* resolve_entry(BundleEntry* e) {
    if (e->resolved) return e->resolved;
    if (file_exists(e->src)) { e->resolved = dupstr(e->src); return e->resolved; }

    const char* base = SDL_GetBasePath();   // owned by SDL
    if (base) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "%sassets/%s", base, e->key);
        if (file_exists(buf)) { e->resolved = dupstr(buf); return e->resolved; }
        snprintf(buf, sizeof(buf), "%s%s", base, e->key);
        if (file_exists(buf)) { e->resolved = dupstr(buf); return e->resolved; }
    }
    // Last resort: hand back the src so the caller's own path probing can
    // still try (UIAsset_* probes a few more locations).
    e->resolved = dupstr(e->src ? e->src : e->key);
    return e->resolved;
}

const char* UIApp_ResolveBundle(const char* uri) {
    if (!uri) return NULL;
    if (strncmp(uri, UI_BUNDLE_SCHEME, strlen(UI_BUNDLE_SCHEME)) != 0)
        return uri;                            // not virtual — pass through
    const char* key = uri + strlen(UI_BUNDLE_SCHEME);
    if (!*key) return NULL;
    BundleEntry* e = find_entry(key);
    if (!e) {
        // Unregistered mocida:// key: register it with src == key so it can
        // still resolve against the bundle (assets/key, key).
        UIApp_SetBundle(uri, key);
        e = find_entry(key);
        if (!e) return NULL;
    }
    return resolve_entry(e);
}

void        UIApp_SetBundleName(const char* name) { free(g_bundleName); g_bundleName = dupstr(name); }
const char* UIApp_GetBundleName(void)             { return g_bundleName; }
const char* UIApp_GetBundleId(void)               { return g_bundleId; }
static void set_bundle_id(const char* id)         { free(g_bundleId);   g_bundleId   = dupstr(id); }

// --------------------------------------------------------------------
// Minimal JSON reader — just enough for app.bundle: a top-level object of
// string fields plus an "assets" object of string->string pairs.
// --------------------------------------------------------------------
static const char* skip_ws(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

// Parse a JSON string at *pp (pointing at the opening quote) into out,
// unescaping the common escapes; advances *pp past the closing quote.
static int parse_str(const char** pp, char* out, size_t cap) {
    const char* p = *pp;
    if (*p != '"') return 0;
    p++;
    size_t i = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\' && *p) {
            char e = *p++;
            switch (e) {
                case 'n': c = '\n'; break; case 't': c = '\t'; break;
                case 'r': c = '\r'; break; case '"': c = '"';  break;
                case '\\': c = '\\'; break; default: c = e;    break;
            }
        }
        if (i + 1 < cap) out[i++] = c;
    }
    if (*p != '"') return 0;
    out[i] = '\0';
    *pp = p + 1;
    return 1;
}

int UIApp_LoadBundleManifest(const char* path) {
    if (!path) return 0;
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > (1 << 20)) { fclose(f); return 0; }
    char* txt = (char*)malloc((size_t)sz + 1);
    if (!txt) { fclose(f); return 0; }
    size_t rd = fread(txt, 1, (size_t)sz, f);
    fclose(f);
    txt[rd] = '\0';

    const char* p = skip_ws(txt);
    int ok = (*p == '{');
    if (ok) p++;
    char key[256], val[1024];
    while (ok) {
        p = skip_ws(p);
        if (*p == '}') { p++; break; }
        if (*p == ',') { p++; continue; }
        if (*p != '"') { ok = 0; break; }
        if (!parse_str(&p, key, sizeof(key))) { ok = 0; break; }
        p = skip_ws(p);
        if (*p != ':') { ok = 0; break; }
        p = skip_ws(p + 1);

        if (strcmp(key, "assets") == 0 && *p == '{') {
            p++;
            while (1) {
                p = skip_ws(p);
                if (*p == '}') { p++; break; }
                if (*p == ',') { p++; continue; }
                if (*p != '"') { ok = 0; break; }
                char ak[256], av[1024];
                if (!parse_str(&p, ak, sizeof(ak))) { ok = 0; break; }
                p = skip_ws(p);
                if (*p != ':') { ok = 0; break; }
                p = skip_ws(p + 1);
                if (!parse_str(&p, av, sizeof(av))) { ok = 0; break; }
                UIApp_SetBundle(ak, av);
            }
        } else if (*p == '"') {
            if (!parse_str(&p, val, sizeof(val))) { ok = 0; break; }
            if      (strcmp(key, "name") == 0) UIApp_SetBundleName(val);
            else if (strcmp(key, "id")   == 0) set_bundle_id(val);
            /* unknown string fields ignored */
        } else {
            // Skip a scalar value we don't model (number/bool/null).
            while (*p && *p != ',' && *p != '}') p++;
        }
    }
    free(txt);
    if (ok) {
        UI_INFO(UI_CAT_CORE, "app.bundle loaded: name='%s' id='%s'",
                g_bundleName ? g_bundleName : "", g_bundleId ? g_bundleId : "");
    } else {
        UI_WARN(UI_CAT_CORE, "app.bundle parse failed: %s", path);
    }
    return ok;
}

void UIApp_BundleShutdown(void) {
    BundleEntry* e = g_bundle;
    while (e) {
        BundleEntry* n = e->next;
        free(e->key); free(e->src); free(e->resolved); free(e);
        e = n;
    }
    g_bundle = NULL;
    free(g_bundleName); g_bundleName = NULL;
    free(g_bundleId);   g_bundleId   = NULL;
}
