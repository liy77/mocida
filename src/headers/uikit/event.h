#ifndef UIKIT_EVENT_H
#define UIKIT_EVENT_H
#include <stdint.h>
#include <uikit/children.h>

typedef uint32_t UI_EVENT;
#define UI_EVENT_FRAMERATE_CHANGED (UI_EVENT)0x0001

/**
 * UIEventFramerate structure representing the framerate of the UI.
 * It contains a single field for frames per second (fps).
 */
typedef struct UIEventFramerate {
    double fps; // Frames per second
} UIEventFramerate;

/**
 * UIEventData structure representing data associated with an event.
 * It contains the event type, child widgets, and framerate information.
 */
typedef struct {
    UI_EVENT type;
    UIChildren* children;
    UIEventFramerate framerate;
} UIEventData;

/**
 * UIEventCallback function pointer type for event callbacks.
 * It takes a UIEventData structure as an argument.
 */
typedef void (*UIEventCallback)(UIEventData data);

/**
 * UIEventCallbackData structure for storing event callback data.
 * It contains a function pointer to the callback function.
 */
typedef struct {
    UIEventCallback cb;
} UIEventCallbackData;

#endif // UIKIT_EVENT_H