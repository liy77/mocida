#ifndef UIKIT_WINDOW_H
#define UIKIT_WINDOW_H

#ifdef WIN32
#include <windows.h>
#endif

#include <uikit/color.h>
#include <uikit/children.h>
#include <SDL3/SDL.h>

typedef int UIWindowScaleMode;

#define UIWindowWindowed (UIWindowScaleMode)0
#define UIWindowFullscreen (UIWindowScaleMode)1
#define UIWindowBorderless (UIWindowScaleMode)2

typedef struct {
    int width;
    int height;
    int x;
    int y;
    int z;
    char* title;
    int visible;
    UIChildren* children;
    UIColor* backgroundColor;
    UIWindowScaleMode scaleMode;

    SDL_Renderer* sdlRenderer;
    SDL_Window* sdlWindow;
} UIWindow;

int UIWindow_Render(UIWindow* window);
UIWindow* UIWindow_Create(const char* title, int width, int height);

#endif // UIKIT_WINDOW_H