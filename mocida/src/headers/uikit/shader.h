#ifndef UIKIT_SHADER_H
#define UIKIT_SHADER_H

#include <uikit/gpu.h>

/**
 * Shader program library for 2.5D / post-FX / custom GPU passes.
 *
 * Built-ins (`unlit`, `lit_sprite`, `hd2d`) always exist — on software
 * backends they select CPU lighting approximations inside UIScene3D.
 * Custom passes register Metal / HLSL / GLSL / WGSL / SPIR-V files; the
 * host picks the language via UI_SHADER_LANG_AUTO.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_SHADER_LANG_AUTO = 0,
    UI_SHADER_LANG_METAL,
    UI_SHADER_LANG_HLSL,
    UI_SHADER_LANG_GLSL,
    UI_SHADER_LANG_WGSL,
    UI_SHADER_LANG_SPIRV
} UIShaderLang;

typedef struct UIShaderPass {
    char* name;
    char* vertexPath;
    char* fragmentPath;
    char* computePath;
    UIShaderLang lang;
    int requiresCompute;
    int requiresRt;
    int loaded;          /**< 1 when GPU bytecode was accepted (or built-in). */
} UIShaderPass;

typedef struct UIShaderLibrary UIShaderLibrary;

/** Creates a library bound to `gpu` (NULL → UIGpu_Default()). */
UIShaderLibrary* UIShader_CreateLibrary(UIGpu* gpu);

void UIShader_DestroyLibrary(UIShaderLibrary* lib);

/** Registers the three built-ins. Safe to call more than once. */
int UIShader_RegisterBuiltins(UIShaderLibrary* lib);

/**
 * Registers a named pass from source/bytecode paths. Missing files do
 * not abort — the pass stays registered but paints as unlit and logs a
 * warning. Returns 1 on success, 0 on OOM / duplicate name.
 */
int UIShader_RegisterPass(UIShaderLibrary* lib,
                          const char* name,
                          const char* vertexPath,
                          const char* fragmentPath,
                          const char* computePath,
                          UIShaderLang lang,
                          int requiresCompute,
                          int requiresRt);

/** Selects the active program for subsequent Scene3D / host draws. */
int UIShader_Use(UIShaderLibrary* lib, const char* name);

const char* UIShader_Active(const UIShaderLibrary* lib);

int UIShader_Has(const UIShaderLibrary* lib, const char* name);

/** Looks up a pass by name (borrowed). NULL if missing. */
const UIShaderPass* UIShader_Get(const UIShaderLibrary* lib, const char* name);

UIGpu* UIShader_Gpu(UIShaderLibrary* lib);

#ifdef __cplusplus
}
#endif

#endif /* UIKIT_SHADER_H */
