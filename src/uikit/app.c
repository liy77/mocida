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

void UIApp_EmitEvent(UIApp* app, UI_EVENT event, UIEventData data) {
    if (!app || !app->window || !app->window->events) return;

    UIWindow_EmitEvent(app->window, event, data);
}

UIApp* UIApp_Create(const char* title, int width, int height) {
    UIApp* app = (UIApp*)malloc(sizeof(UIApp));
    if (!app) {
        fprintf(stderr, "Failed to allocate memory for UIApp\n");
        return NULL;
    }

    // Initialize properties before creating the window, to avoid accessing uninitialized memory
    app->mainWidget = NULL;
    app->window = NULL;
    app->backgroundColor = UI_COLOR_WHITE;

    UIWidget* mainWidget = widgc(NULL);
    if (!mainWidget) {
        fprintf(stderr, "Failed to create main widget\n");
        free(app);
        return NULL;
    }
    UIWidget_SetSize(mainWidget, (float)width, (float)height);
    app->mainWidget = mainWidget;

    app->window = UIWindow_Create(title, width, height);
    if (app->window == NULL) {
        fprintf(stderr, "UIWindow_Create returned NULL\n");
        UIWidget_Destroy(mainWidget);
        free(app);
        return NULL;
    }

    UIApp_SetBackgroundColor(app, UI_COLOR_WHITE);
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
    if (!app || !app->window || !children) return;
    
    // Free the previous children if they exist
    if (app->window->children) {
        UIChildren_Destroy(app->window->children);
    }
    
    app->window->children = children;
}

void UIApp_SetBackgroundColor(UIApp* app, UIColor color) {
    if (!app || !app->window) {
        fprintf(stderr, "UIApp or UIWindow is NULL\n");
        return;
    };
    
    app->backgroundColor = app->window->backgroundColor = color;
}

void UIApp_SetWindowTitle(UIApp* app, const char* title) {
    if (!app || !app->window || !app->window->sdlWindow || !title) return;
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

void UIApp_SetEventCallback(UIApp* app, UI_EVENT event, UIEventCallback callback) {
    if (!app || !app->window || !callback) return;
    UIWindow_SetEventCallback(app->window, event, callback);
}

void UIApp_SetWindowDisplayMode(UIApp* app, UIWindowDisplayMode displayMode) {
    if (!app || !app->window || !app->window->sdlWindow) return;

    switch (displayMode) {
        case WINDOW_WINDOWED:
            SDL_SetWindowBordered(app->window->sdlWindow, 1);
            SDL_SetWindowFullscreen(app->window->sdlWindow, 0);
            break;
        case WINDOW_FULLSCREEN:
            SDL_SetWindowFullscreen(app->window->sdlWindow, 1);
            break;
        case WINDOW_BORDERLESS:
            SDL_SetWindowBordered(app->window->sdlWindow, 0);
            break;
    }
    app->window->displayMode = displayMode;
}

void UIApp_SetRenderDriver(UIApp* app, UIRenderDriver renderDriver) {
    if (!app || !app->window || !app->window->sdlWindow) return;

    // Store the current renderer to free it only if we successfully create a new one
    SDL_Renderer* currentRenderer = app->window->sdlRenderer;
    SDL_Renderer* newRenderer = NULL;
    const char* driverName = NULL;

    switch (renderDriver) {
        case UI_RENDER_OPENGL:
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
            driverName = "opengl";
            break;
        case UI_RENDER_SOFTWARE:
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
            driverName = "software";
            break;
        case UI_RENDER_VULKAN:
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "vulkan");
            driverName = "vulkan";
            break;
        case UI_RENDER_3D9:
            // Used for Direct3D 9 - For legacy systems
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d");
            driverName = "direct3d";
            break;
        case UI_RENDER_3D11:
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
            driverName = "direct3d11";
            break;
        case UI_RENDER_3D12:
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d12");
            driverName = "direct3d12";
            break;
        case UI_RENDER_GPU:
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "gpu");
            driverName = "gpu";
            break;
        #ifdef __APPLE__
        case UI_RENDER_METAL:
            // Note: Metal is only available on macOS and iOS
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
            driverName = "metal";
            break;
        #endif
        default:
            fprintf(stderr, "Unknown render driver\n");
            return;
    }

    // Create a new renderer with the specified driver
    if (driverName) {
        newRenderer = SDL_CreateRenderer(app->window->sdlWindow, driverName);
        if (newRenderer) {
            // Destroys the current renderer only if the new renderer is created successfully
            if (currentRenderer) {
                SDL_DestroyRenderer(currentRenderer);
            }
            app->window->sdlRenderer = newRenderer;
        } else {
            fprintf(stderr, "Failed to create renderer with driver %s: %s\n", 
                    driverName, SDL_GetError());
        }
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

void UIApp_SetProperty(UIApp* app, const char* property, void* value) {
    if (!app || !app->window || !property || !value) return;

    UIWindow_SetProperty(app->window, property, value);
}

void* UIApp_GetProperty(UIApp* app, const char* property) {
    if (!app || !app->window || !property) return NULL;

    return UIWindow_GetProperty(app->window, property);
}

void UIApp_Destroy(UIApp* app) {
    if (!app) return;
    
    if (app->window) {
        UIWindow_Destroy(app->window);
        app->window = NULL;
    }
    
    if (app->mainWidget) {
        UIWidget_Destroy(app->mainWidget);
        app->mainWidget = NULL;
    }
    
    SDL_Quit();
    free(app);
}

void UIApp_Run(UIApp* app) {
    if (!app || !app->window) return;
    SDL_Event e;
    int frameDelayMs = 14; // Approximating 71 FPS (1000ms/71 ≈ 14ms)
    
    while (app->window->visible) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                app->window->visible = 0;
            }
            HandleEvent(app, &e);
        }
        
        // Render and wait
        UIWindow_Render(app->window);
        
        // SDL_Delay(frameDelayMs);
    }
}