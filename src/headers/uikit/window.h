#ifndef UIKIT_WINDOW_H
#define UIKIT_WINDOW_H

#include <uikit/widget.h>
#include <uikit/text.h>
#include <uikit/color.h>
#include <uikit/children.h>
#include <uikit/event.h>
#include <SDL3/SDL.h>

typedef int UIWindowDisplayMode;

#define UIWindowWindowed (UIWindowDisplayMode)0
#define UIWindowFullscreen (UIWindowDisplayMode)1
#define UIWindowBorderless (UIWindowDisplayMode)2

// Properties
#define UI_PROP_MAX_EVENTS (char*)"mocida.events.max"

typedef struct {
    const char* key;
    void* value;
} UIProp;

typedef struct {
    UIProp** props;
    unsigned int count;
    unsigned int capacity;
} UIProps;

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
    float framerate;
    UIChildren* children;
    UIColor backgroundColor;
    UIWindowDisplayMode scaleMode;

    SDL_Renderer* sdlRenderer;
    SDL_Window* sdlWindow;
    UIEventCallbackData** events;
    UIProps __ui_props;
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

/**
 * Sets the event callback for the UIWindow object.
 * @param window Pointer to the UIWindow object.
 * @param event Event type to be set.
 * @param callback Callback function to be called on the event.
 * @return None.
 */
void UIWindow_SetEventCallback(UIWindow* window, UI_EVENT event, UIEventCallback callback);

/**
 * Gets a property of the UIWindow object.
 * @param window Pointer to the UIWindow object.
 * @param property Property name to be retrieved.
 * @return Pointer to the property value.
 */
void* UIWindow_GetProperty(UIWindow* window, const char* property);

/**
 * Sets a property of the UIWindow object.
 * @param window Pointer to the UIWindow object.
 * @param property Property name to be set.
 * @param value Value to be set for the property.
 * @return None.
 */
void UIWindow_SetProperty(UIWindow* window, const char* property, void* value);

/**
 * Destroys the UIWindow object and frees its resources.
 * @param window Pointer to the UIWindow object to be destroyed.
 * @return None.
 */
void UIWindow_Destroy(UIWindow* window);

/**
 * Emits an event to the UIWindow object.
 * @param window Pointer to the UIWindow object.
 * @param event Event type to be emitted.
 * @param data Data associated with the event.
 * @return None.
 */
void UIWindow_EmitEvent(UIWindow* app, UI_EVENT event, UIEventData data);

#endif // UIKIT_WINDOW_H