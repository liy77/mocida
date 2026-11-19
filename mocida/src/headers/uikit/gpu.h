#ifndef UIKIT_GPU_H
#define UIKIT_GPU_H

#include <SDL3/SDL.h>

/**
 * Soft GPU device handle for shaders and 2.5D/3D work.
 *
 * Creation never hard-fails the app: when the platform cannot open an
 * SDL_GPU device (software renderer, missing driver, WebAssembly without
 * GPU), UIGpu_Create returns a handle with UIGpu_IsAvailable() == 0 and
 * the rest of the toolkit keeps painting through SDL_Renderer.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UIGpu UIGpu;

/**
 * Attempts to open an SDL_GPU device for `window`. Always returns a
 * non-NULL handle on success of the allocation; check
 * UIGpu_IsAvailable() to know whether GPU shaders will run.
 *
 * @param window Borrowed SDL window (may be NULL — then only capability
 *               probing is attempted).
 * @return Heap-owned UIGpu, or NULL on out-of-memory.
 */
UIGpu* UIGpu_Create(SDL_Window* window);

/** Releases the GPU device (if any) and frees the handle. NULL-safe. */
void UIGpu_Destroy(UIGpu* gpu);

/** 1 when an SDL_GPUDevice is live, 0 when running in software fallback. */
int UIGpu_IsAvailable(const UIGpu* gpu);

/** Driver name ("metal", "vulkan", "direct3d12", …) or "software". */
const char* UIGpu_DriverName(const UIGpu* gpu);

/** 1 when custom shader passes can be compiled on this device. */
int UIGpu_SupportsShaders(const UIGpu* gpu);

/** 1 when compute shaders are advertised by the device. */
int UIGpu_SupportsCompute(const UIGpu* gpu);

/**
 * Opaque SDL_GPUDevice pointer for advanced callers. NULL when
 * unavailable. Ownership stays with the UIGpu.
 */
void* UIGpu_Device(UIGpu* gpu);

/**
 * Process-wide default GPU (created lazily on first call). Used by
 * UIShader / UIScene3D when no explicit device is passed.
 */
UIGpu* UIGpu_Default(void);

/** Tears down the process-wide default. Called from UIApp teardown. */
void UIGpu_ShutdownDefault(void);

#ifdef __cplusplus
}
#endif

#endif /* UIKIT_GPU_H */
