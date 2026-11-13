#ifndef UIKIT_DEVICE_SIMULATOR_INTERNAL_H
#define UIKIT_DEVICE_SIMULATOR_INTERNAL_H

#include <uikit/device_simulator.h>
#include <uikit/screen.h>
#include <SDL3/SDL.h>

typedef struct UIWindow UIWindow;
typedef struct UIDeviceSimulatorState UIDeviceSimulatorState;

#ifdef __cplusplus
extern "C" {
#endif

void UIWindow_EnableDeviceSimulator(UIWindow* window,
                                    const UIDeviceProfile* profile,
                                    SDL_Texture* frameTexture,
                                    int ownsFrameTexture,
                                    float hostScale,
                                    UIDeviceOrientation orientation);
void UIWindow_DisableDeviceSimulator(UIWindow* window);
int  UIWindow_IsDeviceSimulatorEnabled(UIWindow* window);
UIDeviceSimulatorState* UIWindow_GetDeviceSimulatorState(UIWindow* window);

void UIWindow_DeviceSimulatorGetDeviceSize(UIWindow* window, int* w, int* h);
void UIWindow_DeviceSimulatorGetSafeArea(UIWindow* window, UIScreenInsets* out);
SDL_Texture* UIWindow_DeviceSimulatorGetRenderTarget(UIWindow* window);
/** Window pixels per point the content renders at (the target is that much
 *  bigger than the device size). */
float UIWindow_DeviceSimulatorGetDensity(UIWindow* window);
/** Rebuilds the render target when the window moved to a display of another
 *  pixel density. Call once per frame before rendering. */
void UIWindow_DeviceSimulatorSyncDensity(UIWindow* window);
float UIWindow_DeviceSimulatorGetHostScale(UIWindow* window);

void UIWindow_DeviceSimulatorSetOrientation(UIWindow* window,
                                            UIDeviceOrientation orientation);
void UIWindow_DeviceSimulatorToggleOrientation(UIWindow* window);

void UIWindow_DeviceSimulatorWindowToDevice(UIWindow* window,
                                            float wx, float wy,
                                            float* dx, float* dy);
void UIWindow_DeviceSimulatorDeviceToWindow(UIWindow* window,
                                            float dx, float dy,
                                            float* wx, float* wy);

void UIWindow_DeviceSimulatorPresentFrame(UIWindow* window,
                                          int hostW, int hostH);

#ifdef __cplusplus
}
#endif

#endif // UIKIT_DEVICE_SIMULATOR_INTERNAL_H
