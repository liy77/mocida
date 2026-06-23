#include "device_simulator_internal.h"
#include <uikit/app.h>
#include <uikit/window.h>

int UIWindow_IsDeviceSimulatorEnabled(UIWindow* window) {
    (void)window;
    return 0;
}

void UIApp_EnableDeviceSimulator(UIApp* app, const UIDeviceProfile* profile,
                                 SDL_Texture* frameTexture, float hostScale,
                                 UIDeviceOrientation orientation) {
    (void)app; (void)profile; (void)frameTexture; (void)hostScale; (void)orientation;
}

void UIApp_EnableDeviceSimulatorFromFile(UIApp* app, const UIDeviceProfile* profile,
                                         const char* framePath, float hostScale,
                                         UIDeviceOrientation orientation) {
    (void)app; (void)profile; (void)framePath; (void)hostScale; (void)orientation;
}

void UIApp_DisableDeviceSimulator(UIApp* app) { (void)app; }
bool UIApp_IsDeviceSimulatorEnabled(UIApp* app) { (void)app; return false; }
const UIDeviceProfile* UIApp_GetCurrentDeviceProfile(UIApp* app) { (void)app; return NULL; }
UIDeviceOrientation UIApp_GetCurrentDeviceOrientation(UIApp* app) { (void)app; return UI_DEVICE_ORIENTATION_PORTRAIT; }
void UIApp_SetDeviceOrientation(UIApp* app, UIDeviceOrientation orientation) { (void)app; (void)orientation; }

bool UIApp_GetDeviceSimulatorViewport(UIApp* app, SDL_FRect* outViewport) {
    (void)app; (void)outViewport; return false;
}
SDL_Texture* UIApp_GetDeviceSimulatorRenderTarget(UIApp* app) {
    (void)app; return NULL;
}

void UIApp_DeviceSimulatorWindowToDevice(UIApp* app, float wx, float wy, float* dx, float* dy) {
    if (!dx || !dy) return;
    (void)app; *dx = wx; *dy = wy;
}
void UIApp_DeviceSimulatorDeviceToWindow(UIApp* app, float dx, float dy, float* wx, float* wy) {
    if (!wx || !wy) return;
    (void)app; *wx = dx; *wy = dy;
}
