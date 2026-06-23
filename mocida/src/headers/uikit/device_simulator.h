#ifndef UIKIT_DEVICE_SIMULATOR_H
#define UIKIT_DEVICE_SIMULATOR_H

#include <SDL3/SDL.h>
#include <stdbool.h>

typedef enum {
    UI_DEVICE_ORIENTATION_PORTRAIT = 0,
    UI_DEVICE_ORIENTATION_LANDSCAPE
} UIDeviceOrientation;

typedef struct {
    const char* name;
    int width;
    int height;
    int safeTop;
    int safeBottom;
    int safeLeft;
    int safeRight;
    float scale;
    int frameScreenX;
    int frameScreenY;
    int frameScreenW;
    int frameScreenH;
    int frameW;
    int frameH;
} UIDeviceProfile;

typedef struct UIApp UIApp;

#ifdef __cplusplus
extern "C" {
#endif

void UIApp_EnableDeviceSimulator(UIApp* app, const UIDeviceProfile* profile,
                                 SDL_Texture* frameTexture, float hostScale,
                                 UIDeviceOrientation orientation);
void UIApp_EnableDeviceSimulatorFromFile(UIApp* app, const UIDeviceProfile* profile,
                                         const char* framePath, float hostScale,
                                         UIDeviceOrientation orientation);
void UIApp_DisableDeviceSimulator(UIApp* app);
bool UIApp_IsDeviceSimulatorEnabled(UIApp* app);
const UIDeviceProfile* UIApp_GetCurrentDeviceProfile(UIApp* app);
UIDeviceOrientation UIApp_GetCurrentDeviceOrientation(UIApp* app);
void UIApp_SetDeviceOrientation(UIApp* app, UIDeviceOrientation orientation);

bool UIApp_GetDeviceSimulatorViewport(UIApp* app, SDL_FRect* outViewport);
SDL_Texture* UIApp_GetDeviceSimulatorRenderTarget(UIApp* app);

void UIApp_DeviceSimulatorWindowToDevice(UIApp* app, float wx, float wy,
                                         float* dx, float* dy);
void UIApp_DeviceSimulatorDeviceToWindow(UIApp* app, float dx, float dy,
                                         float* wx, float* wy);

#ifdef __cplusplus
}
#endif

#endif
