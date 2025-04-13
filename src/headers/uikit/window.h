#ifndef UIKIT_WINDOW_H
#define UIKIT_WINDOW_H

#include <uikit/widget.h>
#include <uikit/text.h>
#include <uikit/color.h>
#include <uikit/children.h>
#include <SDL3/SDL.h>

typedef int UIWindowDisplayMode;

#define UIWindowWindowed (UIWindowDisplayMode)0
#define UIWindowFullscreen (UIWindowDisplayMode)1
#define UIWindowBorderless (UIWindowDisplayMode)2

/**
 * UIWindow structure representing a window in the UI framework.
 * It contains properties for size, position, visibility,
 * title, background color, and child widgets.
 */
typedef struct {
    int width;
    int height;
    float x;
    float y;
    int z;
    char* title;
    int visible;
    UIChildren* children;
    UIColor backgroundColor;
    UIWindowDisplayMode scaleMode;

    SDL_Renderer* sdlRenderer;
    SDL_Window* sdlWindow;
} UIWindow;

/**
 * Renders the UIWindow and its child widgets.
 * @param window Pointer to the UIWindow object.
 * @return 0 on success, -1 on failure.
 */
int UIWindow_Render(UIWindow* window);

/**
 * Creates a UIWindow object with the specified title, width, and height.
 * @param title Title of the window.
 * @param width Width of the window.
 * @param height Height of the window.
 * @return A pointer to the created UIWindow object.
 */
UIWindow* UIWindow_Create(const char* title, int width, int height);

#endif // UIKIT_WINDOW_H