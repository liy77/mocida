#ifndef UIKIT_APP_H
#define UIKIT_APP_H

#include <uikit/color.h>
#include <uikit/window.h>
#include <uikit/children.h>
#include <uikit/widget.h>
#include <uikit/text.h>
#include <uikit/rect.h>
#include <uikit/alignment.h>
#include <uikit/font.h>

typedef int UIRenderDriver;

#define UI_RENDER_SOFTWARE (UIRenderDriver)0
#define UI_RENDER_OPENGL (UIRenderDriver)1
#define UI_RENDER_VULKAN (UIRenderDriver)2

#ifdef _WIN32
#define UI_RENDER_3D12 (UIRenderDriver)3
#define UI_RENDER_3D11 (UIRenderDriver)4
#endif

#ifdef __APPLE__
#define UI_RENDER_METAL (UIRenderDriver)5
#endif


/**
 * UIApp structure representing the main application.
 * It contains properties for the main window, background color,
 * and other application settings.
 */
typedef struct {
    UIWindow* window;
    UIWidget* mainWidget;
    UIColor backgroundColor;
} UIApp;

/**
 * Creates a UIApp object with the specified title, width, and height.
 * @param title Title of the application window.
 * @param width Width of the application window.
 * @param height Height of the application window.
 * @return A pointer to the created UIApp object.
 */
UIApp* UIApp_Create(const char* title, int width, int height);

UIWidget* UIApp_GetWindow(UIApp* app);

/**
 * Sets the child widgets of the UIApp object.
 * @param app Pointer to the UIApp object.
 * @param children Pointer to the UIChildren object containing child widgets.
 * @return None.
 */
void UIApp_SetChildren(UIApp* app, UIChildren* children);

/**
 * Sets the background color of the UIApp object.
 * @param app Pointer to the UIApp object.
 * @param color Background color to be set.
 * @return None.
 */
void UIApp_SetBackgroundColor(UIApp* app, UIColor color);

/**
 * Sets the title of the application window.
 * @param app Pointer to the UIApp object.
 * @param title Title to be set.
 * @return None.
 */
void UIApp_SetWindowTitle(UIApp* app, const char* title);

/**
 * Sets the size of the application window.
 * @param app Pointer to the UIApp object.
 * @param width Width to be set.
 * @param height Height to be set.
 * @return None.
 */
void UIApp_SetWindowSize(UIApp* app, int width, int height);

/**
 * Sets the position of the application window.
 * @param app Pointer to the UIApp object.
 * @param x X-coordinate to be set.
 * @param y Y-coordinate to be set.
 * @return None.
 */
void UIApp_SetWindowPosition(UIApp* app, int x, int y);

/**
 * Sets the scale mode of the application window.
 * @param app Pointer to the UIApp object.
 * @param scaleMode Scale mode to be set.
 * @return None.
 */
void UIApp_SetWindowDisplayMode(UIApp* app, UIWindowDisplayMode scaleMode);

/**
 * Sets the render driver of the application window.
 * @param app Pointer to the UIApp object.
 * @param renderDriver Render driver to be set.
 * @return None.
 */
void UIApp_ShowWindow(UIApp* app);

/**
 * Hides the application window.
 * @param app Pointer to the UIApp object.
 * @return None.
 */
void UIApp_HideWindow(UIApp* app);

void UIApp_SetRenderDriver(UIApp* app, UIRenderDriver renderDriver);

/**
 * Destroys the UIApp object and frees its resources.
 * @param app Pointer to the UIApp object to be destroyed.
 * @return None.
 */
void UIApp_Destroy(UIApp* app);

/**
 * Runs the main loop of the application.
 * @param app Pointer to the UIApp object.
 * @return None.
 */
void UIApp_Run(UIApp* app);

#endif