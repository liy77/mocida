#ifndef UIKIT_FILE_DROP_H
#define UIKIT_FILE_DROP_H

#include <uikit/widget.h>
#include <uikit/children.h>
#include <uikit/color.h>

#define UI_WIDGET_FILE_DROP "@uikit/file_drop"

typedef struct UIFileDrop UIFileDrop;
typedef void (*UIFileDropCallback)(UIFileDrop* fd, const char* path, void* userdata);

/**
 * Drop target. When the OS drops a file inside this widget's bounds,
 * `onDrop` fires with the absolute path. Visually it renders a
 * dashed-style background with a prompt; during a drag-over the
 * border switches to `activeColor` so the user knows the drop will
 * land here.
 *
 * Used together with SDL's drag-and-drop events; the dispatchers
 * (called by app.c) translate `SDL_EVENT_DROP_*` into FileDrop state.
 */
struct UIFileDrop {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_FILE_DROP). */

    char*   prompt;            /**< Heap-owned prompt text shown in the centre. */
    char*   fontFamily;        /**< Heap-owned font path for the prompt. */
    float   fontSize;          /**< Prompt font size in points. */

    UIColor bgColor;           /**< Background fill when idle. */
    UIColor activeBgColor;     /**< Background fill while a file is being dragged over. */
    UIColor borderColor;       /**< Border color when idle. */
    UIColor activeBorderColor; /**< Border color while a file is being dragged over. */
    UIColor textColor;         /**< Prompt text color. */
    float   borderWidth;       /**< Border thickness (pixels). */
    float   radius;            /**< Corner radius (pixels). */

    int     dragOver;          /**< Internal: 1 while a file is being dragged over. */

    UIFileDropCallback onDrop; /**< Fires once per dropped file with its absolute path. */
    void*              userdata; /**< Opaque pointer forwarded to onDrop. */
};

UIFileDrop* UIFileDrop_Create(const char* prompt);
UIFileDrop* UIFileDrop_SetFontFamily(UIFileDrop* fd, char* family);
UIFileDrop* UIFileDrop_SetFontSize  (UIFileDrop* fd, float size);
UIFileDrop* UIFileDrop_SetColors    (UIFileDrop* fd, UIColor bg, UIColor activeBg,
                                     UIColor border, UIColor activeBorder, UIColor text);
UIFileDrop* UIFileDrop_OnDrop       (UIFileDrop* fd, UIFileDropCallback cb, void* userdata);
void        UIFileDrop_Destroy      (UIFileDrop* fd);

// ---------------------------------------------------------------------
// Dispatchers wired by app.c around SDL_EVENT_DROP_BEGIN /
// _POSITION / _FILE / _COMPLETE.
// ---------------------------------------------------------------------
void UIFileDrop_DispatchDragPosition(UIChildren* children, float x, float y);
void UIFileDrop_DispatchDragEnd     (UIChildren* children);
void UIFileDrop_DispatchDropFile    (UIChildren* children, float x, float y, const char* path);

#endif // UIKIT_FILE_DROP_H
