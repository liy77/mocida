#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <uikit/app.h>
#include <uikit/color.h>
#include <uikit/rect.h>
#include <uikit/window.h>
#include <uikit/widget.h>

UIApp* UIApp_Create(const char* title, int width, int height) {
    UIApp* app = (UIApp*)malloc(sizeof(UIApp));
    if (!app) {
        fprintf(stderr, "Failed to allocate memory for UIApp\n");
        return NULL;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 1) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        free(app);
        return NULL;
    }

    app->window = UIWindow_Create(title, width, height);
    if (!app->window) {
        fprintf(stderr, "UIWindow_Create returned NULL\n");
        SDL_Quit();
        free(app);
        return NULL;
    }

    UIApp_SetBackgroundColor(app, UIColorWhite);
    return app;
}

void UIApp_SetChildren(UIApp* app, UIChildren* children) {
    if (!app || !children) return;
    app->window->children = children;
}

void UIApp_SetBackgroundColor(UIApp* app, UIColor color) {
    if (!app || !app->window) {
        fprintf(stderr, "UIApp or UIWindow is NULL\n");
        return;
    };
    app->backgroundColor = color;
    app->window->backgroundColor = color;
}

void UIApp_SetWindowTitle(UIApp* app, const char* title) {
    if (!app || !title || !app->window || !app->window->sdlWindow) return;

    free(app->window->title);
    app->window->title = _strdup(title);
    SDL_SetWindowTitle(app->window->sdlWindow, title);
}

void UIApp_SetWindowSize(UIApp* app, int width, int height) {
    if (!app || !app->window || !app->window->sdlWindow) return;

    app->window->width = width;
    app->window->height = height;
    SDL_SetWindowSize(app->window->sdlWindow, width, height);
}

void UIApp_SetWindowPosition(UIApp* app, int x, int y) {
    if (!app || !app->window || !app->window->sdlWindow) return;

    app->window->x = x;
    app->window->y = y;
    SDL_SetWindowPosition(app->window->sdlWindow, x, y);
}

void UIApp_SetWindowScaleMode(UIApp* app, UIWindowScaleMode scaleMode) {
    if (!app || !app->window || !app->window->sdlWindow) return;

    switch (scaleMode) {
        case UIWindowWindowed:
            SDL_SetWindowBordered(app->window->sdlWindow, 1);
            SDL_SetWindowFullscreen(app->window->sdlWindow, 0);
            break;
        case UIWindowFullscreen:
            SDL_SetWindowFullscreen(app->window->sdlWindow, SDL_WINDOW_FULLSCREEN);
            break;
        case UIWindowBorderless:
            SDL_SetWindowBordered(app->window->sdlWindow, 0);
            break;
    }
    app->window->scaleMode = scaleMode;
}

void UIApp_ShowWindow(UIApp* app) {
    if (!app || !app->window || !app->window->sdlWindow) return;
    app->window->visible = 1;
    SDL_ShowWindow(app->window->sdlWindow);
}

void UIApp_HideWindow(UIApp* app) {
    if (!app || !app->window || !app->window->sdlWindow) return;
    app->window->visible = 0;
    SDL_HideWindow(app->window->sdlWindow);
}

void UIApp_Destroy(UIApp* app) {
    if (!app) return;

    if (app->window) {
        if (app->window->sdlRenderer) {
            SDL_DestroyRenderer(app->window->sdlRenderer);
        }
        if (app->window->sdlWindow) {
            SDL_DestroyWindow(app->window->sdlWindow);
        }
        free(app->window->title);
        free(app->window);
    }

    SDL_Quit();
    free(app);
}

void UIApp_Run(UIApp* app) {
    if (!app || !app->window || !app->window->sdlWindow || !app->window->sdlRenderer) return;

    SDL_Event e;
    while (app->window->visible) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                app->window->visible = 0;
            }
            // TODO - Add other event handling here (keyboard, mouse, etc.)
        }

        // Render the window
        UIWindow_Render(app->window);

        SDL_Delay(16); // Approximating 60 FPS
    }
}