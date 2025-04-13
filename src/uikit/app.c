#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <uikit/app.h>
#include <uikit/color.h>
#include <uikit/rect.h>
#include <uikit/window.h>
#include <uikit/widget.h>

void HandleEvent(UIApp* app, SDL_Event* event) {
    if (!app || !app->window) return;

    switch (event->type) {
        case SDL_EVENT_DROP_POSITION: {
            float x = event->drop.x;
            float y = event->drop.y;

            app->window->x = x;
            app->window->y = y;
            break;
        }
        case SDL_EVENT_WINDOW_RESIZED: {
            int new_width = event->window.data1;
            int new_height = event->window.data2;

            app->window->width = new_width;
            app->window->height = new_height;

            // Update the main widget size for layout purposes
            if (app->mainWidget == NULL) {
                fprintf(stderr, "Main widget is NULL\n");
            } else {
                UIWidget_SetSize(app->mainWidget, (float)new_width, (float)new_height);
            }
            
            if (app->window->sdlRenderer) {
                if (SDL_SetRenderLogicalPresentation(app->window->sdlRenderer, new_width, new_height, SDL_LOGICAL_PRESENTATION_LETTERBOX) != 1) {
                    fprintf(stderr, "SDL_SetRenderLogicalPresentation Error: %s\n", SDL_GetError());
                }
            } else {
                fprintf(stderr, "SDL_Renderer is NULL\n");
            }

            break;
        }
        default:
            break;
    }
}

UIApp* UIApp_Create(const char* title, int width, int height) {
    UIApp* app = (UIApp*)malloc(sizeof(UIApp));
    if (!app) {
        fprintf(stderr, "Failed to allocate memory for UIApp\n");
        return NULL;
    }

    UIWidget* mainWidget = widgc(NULL);
    UIWidget_SetSize(mainWidget, (float)width, (float)height);

    app->mainWidget = mainWidget;

    app->window = UIWindow_Create(title, width, height);
    if (!app->window) {
        fprintf(stderr, "UIWindow_Create returned NULL\n");
        SDL_Quit();
        free(app);
        return NULL;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 1) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        free(app);
        return NULL;
    }

    UIApp_SetBackgroundColor(app, UIColorWhite);
    return app;
}

UIWidget* UIApp_GetWindow(UIApp* app) {
    if (!app || !app->window) {
        fprintf(stderr, "UIApp or UIWindow is NULL\n");
        return NULL;
    }

    UIWidget* widget = widgcs(app->window, (float)app->window->width, (float)app->window->height); 
    return widget;
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
    SDL_SetWindowSize(app->window->sdlWindow, width, height);
}

void UIApp_SetWindowPosition(UIApp* app, int x, int y) {
    if (!app || !app->window || !app->window->sdlWindow) return;
    SDL_SetWindowPosition(app->window->sdlWindow, x, y);
}

void UIApp_SetWindowDisplayMode(UIApp* app, UIWindowDisplayMode scaleMode) {
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

void UIApp_SetRenderDriver(UIApp* app, UIRenderDriver renderDriver) {
    if (!app || !app->window || !app->window->sdlWindow) return;

    switch (renderDriver) {
        case UI_RENDER_OPENGL:
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 16);
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
            SDL_DestroyRenderer(app->window->sdlRenderer);
            app->window->sdlRenderer = SDL_CreateRenderer(app->window->sdlWindow, "opengl");
            break;
        case UI_RENDER_SOFTWARE:
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
            SDL_DestroyRenderer(app->window->sdlRenderer);
            app->window->sdlRenderer = SDL_CreateRenderer(app->window->sdlWindow, "software");
            break;
        case UI_RENDER_VULKAN:
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "vulkan");
            SDL_DestroyRenderer(app->window->sdlRenderer);
            app->window->sdlRenderer = SDL_CreateRenderer(app->window->sdlWindow, "vulkan");
            break;
        #ifdef __APPLE__
        case UI_RENDER_METAL:
            // Note: Metal is only available on macOS and iOS
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
            break;
        #endif
        default:
            fprintf(stderr, "Unknown render driver\n");
            break;
    }
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
            
            HandleEvent(app, &e);
        }

        // Render the window
        UIWindow_Render(app->window);

        SDL_Delay(16); // Approximating 60 FPS
    }
}