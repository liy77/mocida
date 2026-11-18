#include <uikit/upscale.h>
#include <SDL3/SDL.h>

#if defined(_WIN32)
#include <windows.h>
static void* OpenLib(const char* path) { return (void*)LoadLibraryA(path); }
static void* LibSym(void* lib, const char* name) { return (void*)GetProcAddress((HMODULE)lib, name); }
static void CloseLib(void* lib) { if (lib) FreeLibrary((HMODULE)lib); }
static const char* LibErr(void) { return "LoadLibrary failed"; }
#else
#include <dlfcn.h>
static void* OpenLib(const char* path) { return dlopen(path, RTLD_NOW); }
static void* LibSym(void* lib, const char* name) { return dlsym(lib, name); }
static void CloseLib(void* lib) { if (lib) dlclose(lib); }
static const char* LibErr(void) {
    const char* e = dlerror();
    return e ? e : "dlopen failed";
}
#endif

typedef int (*MocidadUpscaleFn)(int quality, float sharpness, int src_w, int src_h, int dst_w, int dst_h);

static void* g_lib[4];
static char g_path[4][1024];
static char g_error[256];
static int g_mode = UI_UPSCALE_OFF;
static int g_quality = UI_UPSCALE_QUALITY;
static float g_sharpness = 0.5f;
static int g_render_w = 0;
static int g_render_h = 0;
static float g_scale = 1.0f;

static int ModeOk(int mode) {
    return mode >= UI_UPSCALE_OFF && mode <= UI_UPSCALE_METALFX;
}

int UIUpscale_MetalFXSupported(void);

int UIUpscale_Mode(void) {
    return g_mode;
}

static int HasAny(void* lib, const char** names) {
    for (int i = 0; names[i]; i++) {
        if (LibSym(lib, names[i])) return 1;
    }
    return 0;
}

static int Recognized(void* lib, int mode) {
    if (mode == UI_UPSCALE_DLSS) {
        const char* names[] = {
            "NVSDK_NGX_Init",
            "NVSDK_NGX_D3D12_Init",
            "NVSDK_NGX_VULKAN_Init",
            "NVSDK_NGX_D3D11_Init",
            NULL,
        };
        return HasAny(lib, names);
    }
    if (mode == UI_UPSCALE_FSR) {
        const char* names[] = {
            "ffxFsr3UpscalerContextCreate",
            "ffxFsr3ContextCreate",
            "ffxFsr2ContextCreate",
            "ffxFsrUpscaleContextCreate",
            NULL,
        };
        return HasAny(lib, names);
    }
    if (mode == UI_UPSCALE_CUSTOM) {
        return LibSym(lib, "Mocidad_Upscale") != NULL;
    }
    return 0;
}

static float QualityScale(int quality) {
    switch (quality) {
        case UI_UPSCALE_ULTRA_QUALITY: return 0.77f;
        case UI_UPSCALE_QUALITY: return 0.67f;
        case UI_UPSCALE_BALANCED: return 0.58f;
        case UI_UPSCALE_PERFORMANCE: return 0.50f;
        case UI_UPSCALE_ULTRA_PERFORMANCE: return 0.33f;
        default: return 1.0f;
    }
}

int UIUpscale_Load(const char* path, int mode) {
    g_error[0] = 0;
    if (!path || !path[0] || !ModeOk(mode) || mode == UI_UPSCALE_OFF) {
        SDL_strlcpy(g_error, "pick DLSS, FSR or a custom library", sizeof(g_error));
        return 0;
    }
    void* lib = OpenLib(path);
    if (!lib) {
        SDL_strlcpy(g_error, LibErr(), sizeof(g_error));
        return 0;
    }
    if (!Recognized(lib, mode)) {
        CloseLib(lib);
        if (mode == UI_UPSCALE_CUSTOM) {
            SDL_strlcpy(g_error, "custom library must export Mocidad_Upscale", sizeof(g_error));
        } else if (mode == UI_UPSCALE_DLSS) {
            SDL_strlcpy(g_error, "not a DLSS library (missing NVSDK_NGX_Init)", sizeof(g_error));
        } else {
            SDL_strlcpy(g_error, "not an FSR library (missing ffxFsr context create)", sizeof(g_error));
        }
        return 0;
    }
    if (g_lib[mode]) CloseLib(g_lib[mode]);
    g_lib[mode] = lib;
    SDL_strlcpy(g_path[mode], path, sizeof(g_path[mode]));
    return 1;
}

void UIUpscale_Unload(int mode) {
    if (!ModeOk(mode) || mode == UI_UPSCALE_OFF) return;
    if (g_lib[mode]) CloseLib(g_lib[mode]);
    g_lib[mode] = NULL;
    g_path[mode][0] = 0;
    if (g_mode == mode) g_mode = UI_UPSCALE_OFF;
}

int UIUpscale_Available(int mode) {
    if (mode == UI_UPSCALE_METALFX) return UIUpscale_MetalFXSupported();
    return ModeOk(mode) && mode != UI_UPSCALE_OFF && g_lib[mode] != NULL;
}

int UIUpscale_Configure(int mode, int quality, float sharpness, int display_w, int display_h) {
    g_error[0] = 0;
    if (!ModeOk(mode)) mode = UI_UPSCALE_OFF;
    if (quality < UI_UPSCALE_NATIVE || quality > UI_UPSCALE_ULTRA_PERFORMANCE) {
        quality = UI_UPSCALE_QUALITY;
    }
    if (sharpness < 0.0f) sharpness = 0.0f;
    if (sharpness > 1.0f) sharpness = 1.0f;
    if (mode == UI_UPSCALE_METALFX && !UIUpscale_MetalFXSupported()) {
        SDL_strlcpy(g_error, "MetalFX needs macOS 13 and a supported GPU", sizeof(g_error));
        return 0;
    }
    if (mode != UI_UPSCALE_OFF && mode != UI_UPSCALE_METALFX && !g_lib[mode]) {
        SDL_strlcpy(g_error, "install the library in Packages first", sizeof(g_error));
        return 0;
    }
    g_mode = mode;
    g_quality = quality;
    g_sharpness = sharpness;
    g_scale = mode == UI_UPSCALE_OFF ? 1.0f : QualityScale(quality);
    if (display_w < 1) display_w = 1;
    if (display_h < 1) display_h = 1;
    g_render_w = (int)(display_w * g_scale);
    g_render_h = (int)(display_h * g_scale);
    if (g_render_w < 1) g_render_w = 1;
    if (g_render_h < 1) g_render_h = 1;

    if (mode == UI_UPSCALE_CUSTOM || (g_lib[mode] && LibSym(g_lib[mode], "Mocidad_Upscale"))) {
        MocidadUpscaleFn fn = (MocidadUpscaleFn)LibSym(g_lib[mode], "Mocidad_Upscale");
        if (fn) {
            int ok = fn(quality, sharpness, g_render_w, g_render_h, display_w, display_h);
            if (!ok) {
                SDL_strlcpy(g_error, "library refused the frame size", sizeof(g_error));
                return 0;
            }
        }
    }
    return 1;
}

void UIUpscale_RenderSize(int* w, int* h) {
    if (w) *w = g_render_w;
    if (h) *h = g_render_h;
}

float UIUpscale_Scale(void) { return g_scale; }
const char* UIUpscale_Error(void) { return g_error; }

const char* UIUpscale_Library(int mode) {
    if (!ModeOk(mode) || mode == UI_UPSCALE_OFF) return "";
    return g_path[mode];
}
