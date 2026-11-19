#include <uikit/shader.h>
#include <uikit/debug.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define UI_SHADER_MAX_PASSES 64

struct UIShaderLibrary {
    UIGpu* gpu;
    int ownsGpu; /* 0 — borrowed from caller / default */
    UIShaderPass passes[UI_SHADER_MAX_PASSES];
    int passCount;
    char active[64];
};

static char* DupStr(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char* o = (char*)malloc(n);
    if (o) memcpy(o, s, n);
    return o;
}

static void FreePass(UIShaderPass* p) {
    free(p->name);
    free(p->vertexPath);
    free(p->fragmentPath);
    free(p->computePath);
    memset(p, 0, sizeof(*p));
}

static int FindPassIndex(const UIShaderLibrary* lib, const char* name) {
    if (!lib || !name) return -1;
    for (int i = 0; i < lib->passCount; i++) {
        if (lib->passes[i].name && strcmp(lib->passes[i].name, name) == 0)
            return i;
    }
    return -1;
}

static int FileReadable(const char* path) {
    if (!path || !path[0]) return 0;
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

UIShaderLibrary* UIShader_CreateLibrary(UIGpu* gpu) {
    UIShaderLibrary* lib = (UIShaderLibrary*)calloc(1, sizeof(UIShaderLibrary));
    if (!lib) return NULL;
    lib->gpu = gpu ? gpu : UIGpu_Default();
    lib->ownsGpu = 0;
    snprintf(lib->active, sizeof(lib->active), "unlit");
    UIShader_RegisterBuiltins(lib);
    return lib;
}

void UIShader_DestroyLibrary(UIShaderLibrary* lib) {
    if (!lib) return;
    for (int i = 0; i < lib->passCount; i++) FreePass(&lib->passes[i]);
    free(lib);
}

static int AddBuiltin(UIShaderLibrary* lib, const char* name) {
    if (FindPassIndex(lib, name) >= 0) return 1;
    if (lib->passCount >= UI_SHADER_MAX_PASSES) return 0;
    UIShaderPass* p = &lib->passes[lib->passCount++];
    memset(p, 0, sizeof(*p));
    p->name = DupStr(name);
    p->lang = UI_SHADER_LANG_AUTO;
    p->loaded = 1; /* built-ins always "loaded" — CPU or GPU path */
    return p->name != NULL;
}

int UIShader_RegisterBuiltins(UIShaderLibrary* lib) {
    if (!lib) return 0;
    int ok = 1;
    ok &= AddBuiltin(lib, "unlit");
    ok &= AddBuiltin(lib, "lit_sprite");
    ok &= AddBuiltin(lib, "hd2d");
    return ok;
}

int UIShader_RegisterPass(UIShaderLibrary* lib,
                          const char* name,
                          const char* vertexPath,
                          const char* fragmentPath,
                          const char* computePath,
                          UIShaderLang lang,
                          int requiresCompute,
                          int requiresRt) {
    if (!lib || !name || !name[0]) return 0;
    if (FindPassIndex(lib, name) >= 0) {
        UI_WARN(UI_CAT_RENDER, "UIShader: pass '%s' already registered", name);
        return 0;
    }
    if (lib->passCount >= UI_SHADER_MAX_PASSES) return 0;

    UIShaderPass* p = &lib->passes[lib->passCount++];
    memset(p, 0, sizeof(*p));
    p->name = DupStr(name);
    p->vertexPath = DupStr(vertexPath);
    p->fragmentPath = DupStr(fragmentPath);
    p->computePath = DupStr(computePath);
    p->lang = lang;
    p->requiresCompute = requiresCompute || (computePath && computePath[0]);
    p->requiresRt = requiresRt;

    int gpuOk = UIGpu_IsAvailable(lib->gpu) && UIGpu_SupportsShaders(lib->gpu);
    if (p->requiresCompute && !UIGpu_SupportsCompute(lib->gpu)) {
        UI_WARN(UI_CAT_RENDER,
                "UIShader: pass '%s' needs compute — skipped on this device",
                name);
        p->loaded = 0;
    } else if (p->requiresRt) {
        UI_WARN(UI_CAT_RENDER,
                "UIShader: pass '%s' needs ray tracing — skipped for now",
                name);
        p->loaded = 0;
    } else if (gpuOk) {
        /* Bytecode upload / MSL compile lands with the full GPU pipeline.
         * For now we accept the registration when source files exist so
         * hosts can select the pass; Scene3D approximates until then. */
        int hasSrc = FileReadable(vertexPath) || FileReadable(fragmentPath)
                     || FileReadable(computePath);
        if (!hasSrc && (vertexPath || fragmentPath || computePath)) {
            UI_WARN(UI_CAT_RENDER,
                    "UIShader: pass '%s' source missing — paints unlit",
                    name);
            p->loaded = 0;
        } else {
            p->loaded = 1;
        }
    } else {
        p->loaded = 0;
        UI_INFO(UI_CAT_RENDER,
                "UIShader: pass '%s' registered (software fallback)", name);
    }
    return p->name != NULL;
}

int UIShader_Use(UIShaderLibrary* lib, const char* name) {
    if (!lib || !name) return 0;
    if (FindPassIndex(lib, name) < 0) {
        UI_WARN(UI_CAT_RENDER, "UIShader: unknown program '%s'", name);
        return 0;
    }
    snprintf(lib->active, sizeof(lib->active), "%s", name);
    return 1;
}

const char* UIShader_Active(const UIShaderLibrary* lib) {
    return lib ? lib->active : "unlit";
}

int UIShader_Has(const UIShaderLibrary* lib, const char* name) {
    return FindPassIndex(lib, name) >= 0;
}

const UIShaderPass* UIShader_Get(const UIShaderLibrary* lib, const char* name) {
    int i = FindPassIndex(lib, name);
    return i >= 0 ? &lib->passes[i] : NULL;
}

UIGpu* UIShader_Gpu(UIShaderLibrary* lib) {
    return lib ? lib->gpu : NULL;
}
