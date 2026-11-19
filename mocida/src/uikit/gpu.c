#include <uikit/gpu.h>
#include <uikit/debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL_gpu.h>

struct UIGpu {
    SDL_GPUDevice* device;
    char driver[64];
    int available;
    int supportsShaders;
    int supportsCompute;
};

static UIGpu* g_defaultGpu = NULL;

static void ProbeFormats(UIGpu* gpu) {
    if (!gpu || !gpu->device) return;
    SDL_GPUShaderFormat fmt = SDL_GetGPUShaderFormats(gpu->device);
    gpu->supportsShaders = (fmt != SDL_GPU_SHADERFORMAT_INVALID) ? 1 : 0;
    /* Compute is available whenever we have a real device; finer
     * feature bits land when SDL exposes them per-driver. */
    gpu->supportsCompute = gpu->supportsShaders;
}

UIGpu* UIGpu_Create(SDL_Window* window) {
    (void)window;
    UIGpu* gpu = (UIGpu*)calloc(1, sizeof(UIGpu));
    if (!gpu) return NULL;
    snprintf(gpu->driver, sizeof(gpu->driver), "software");

    /* Prefer any shader format the platform can compile. Soft-fail. */
    SDL_GPUShaderFormat formats =
        SDL_GPU_SHADERFORMAT_SPIRV |
        SDL_GPU_SHADERFORMAT_DXIL |
        SDL_GPU_SHADERFORMAT_MSL |
        SDL_GPU_SHADERFORMAT_METALLIB |
        SDL_GPU_SHADERFORMAT_DXBC;

    gpu->device = SDL_CreateGPUDevice(formats, false, NULL);
    if (!gpu->device) {
        UI_INFO(UI_CAT_RENDER,
                "UIGpu: no SDL_GPU device (%s) — software / renderer path",
                SDL_GetError());
        return gpu;
    }

    const char* name = SDL_GetGPUDeviceDriver(gpu->device);
    if (name && name[0]) {
        snprintf(gpu->driver, sizeof(gpu->driver), "%s", name);
    }
    gpu->available = 1;
    ProbeFormats(gpu);
    UI_INFO(UI_CAT_RENDER, "UIGpu: opened driver='%s' shaders=%d",
            gpu->driver, gpu->supportsShaders);
    return gpu;
}

void UIGpu_Destroy(UIGpu* gpu) {
    if (!gpu) return;
    if (gpu == g_defaultGpu) g_defaultGpu = NULL;
    if (gpu->device) {
        SDL_DestroyGPUDevice(gpu->device);
        gpu->device = NULL;
    }
    free(gpu);
}

int UIGpu_IsAvailable(const UIGpu* gpu) {
    return gpu && gpu->available && gpu->device;
}

const char* UIGpu_DriverName(const UIGpu* gpu) {
    return gpu ? gpu->driver : "software";
}

int UIGpu_SupportsShaders(const UIGpu* gpu) {
    return gpu && gpu->supportsShaders;
}

int UIGpu_SupportsCompute(const UIGpu* gpu) {
    return gpu && gpu->supportsCompute;
}

void* UIGpu_Device(UIGpu* gpu) {
    return gpu ? (void*)gpu->device : NULL;
}

UIGpu* UIGpu_Default(void) {
    if (!g_defaultGpu) {
        g_defaultGpu = UIGpu_Create(NULL);
    }
    return g_defaultGpu;
}

void UIGpu_ShutdownDefault(void) {
    if (g_defaultGpu) {
        UIGpu* g = g_defaultGpu;
        g_defaultGpu = NULL;
        UIGpu_Destroy(g);
    }
}
