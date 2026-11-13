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

/** A simulator toolbar button. */
typedef enum {
    UI_SIM_ACTION_ROTATE = 1,
    UI_SIM_ACTION_RESTART = 2,
    /** Reported after the screenshot is saved (UIApp_DeviceSimulatorLastScreenshot). */
    UI_SIM_ACTION_SCREENSHOT = 3,
    UI_SIM_ACTION_ZOOM = 4
} UIDeviceSimulatorAction;

typedef void (*UIDeviceSimulatorActionFn)(int action, void* userdata);

#ifdef __cplusplus
extern "C" {
#endif

void UIApp_EnableDeviceSimulator(UIApp* app, const UIDeviceProfile* profile,
                                 SDL_Texture* frameTexture, float hostScale,
                                 UIDeviceOrientation orientation);
void UIApp_EnableDeviceSimulatorFromFile(UIApp* app, const UIDeviceProfile* profile,
                                         const char* framePath, float hostScale,
                                         UIDeviceOrientation orientation);
/**
 * Simulates a real device: `framePath` is its frame (a PNG whose screen is
 * transparent, drawn over the content, so the camera island and bezel sit on
 * top) and `maskPath` its screen mask (white = screen, cut to the profile's
 * screen rect) which rounds the content's corners. The images may be much
 * larger than the frame in points; they are scaled down on load. Either path
 * may be NULL (no frame / square corners).
 */
void UIApp_EnableDeviceSimulatorFrames(UIApp* app, const UIDeviceProfile* profile,
                                       const char* framePath, const char* maskPath,
                                       float hostScale, UIDeviceOrientation orientation);
/**
 * Shows a toolbar above the simulated device: its name, orientation and
 * scale, and Screenshot, Rotate, Restart and Zoom buttons. A screenshot is
 * saved to the Desktop (or $MOCIDA_SCREENSHOT_DIR) by the simulator itself; the other buttons call `fn`
 * (without one, Rotate turns the device and resizes the window). Keep the
 * window `UIApp_DeviceSimulatorToolbarHeight()` points taller than the frame.
 * Call before or after enabling; it stays on across re-enables.
 */
void UIApp_SetDeviceSimulatorToolbar(UIApp* app, int show, UIDeviceSimulatorActionFn fn, void* userdata);
/** Usable size (points) of the display the window is on: the real one, where
 *  UIScreen_* report the simulated device's. For fitting the device to it. */
void UIApp_DeviceSimulatorHostDisplaySize(UIApp* app, int* w, int* h);
/** The toolbar's height in points, 0 when it's hidden. */
int UIApp_DeviceSimulatorToolbarHeight(void);
/** Saves the device screen to the Desktop on the next frame (as the toolbar's
 *  Screenshot button does). */
void UIApp_DeviceSimulatorTakeScreenshot(UIApp* app);
/** Path of the last screenshot saved from the toolbar ("" if none). */
const char* UIApp_DeviceSimulatorLastScreenshot(void);
void UIApp_DisableDeviceSimulator(UIApp* app);
bool UIApp_IsDeviceSimulatorEnabled(UIApp* app);
const UIDeviceProfile* UIApp_GetCurrentDeviceProfile(UIApp* app);
UIDeviceOrientation UIApp_GetCurrentDeviceOrientation(UIApp* app);
void UIApp_SetDeviceOrientation(UIApp* app, UIDeviceOrientation orientation);

void UIApp_DeviceSimulatorWindowToDevice(UIApp* app, float wx, float wy,
                                         float* dx, float* dy);
void UIApp_DeviceSimulatorDeviceToWindow(UIApp* app, float dx, float dy,
                                         float* wx, float* wy);

#ifdef __cplusplus
}
#endif

#endif
