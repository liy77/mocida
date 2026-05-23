#ifndef UIKIT_FILE_DIALOG_H
#define UIKIT_FILE_DIALOG_H

#include <SDL3/SDL.h>

/**
 * Native file pickers backed by SDL3's SDL_ShowOpenFileDialog /
 * SDL_ShowSaveFileDialog. On Windows these route to the standard
 * IFileOpenDialog COM API; on Linux/macOS SDL uses Zenity / NSOpenPanel.
 *
 * Both calls are ASYNCHRONOUS: the callback fires from SDL's event
 * pump when the user finishes (or cancels) the dialog. NULL `path`
 * in the callback means "cancelled" - filter on that.
 */

typedef void (*UIFileDialogCallback)(const char* path, void* userdata);

/**
 * Opens a native "Open file" dialog.
 *
 * @param window       Parent window pointer (SDL_Window*). Pass the
 *                     UIApp's window->sdlWindow.
 * @param filterDesc   Human label for the filter group (e.g.
 *                     "Video files"). NULL = "All files".
 * @param filterExts   Semicolon-separated extensions (no dots), e.g.
 *                     "mp4;mkv;mov;avi;webm". NULL or empty = "*".
 * @param cb           Fires when the user finishes. `path` is NULL on
 *                     cancel.
 * @param userdata     Forwarded to `cb`.
 */
void UIFileDialog_OpenFile(SDL_Window* window,
                           const char* filterDesc,
                           const char* filterExts,
                           UIFileDialogCallback cb,
                           void* userdata);

/**
 * Variant: pick a save location. SDL_ShowSaveFileDialog under the hood.
 * Same parameters / callback semantics as the open variant.
 */
void UIFileDialog_SaveFile(SDL_Window* window,
                           const char* filterDesc,
                           const char* filterExts,
                           UIFileDialogCallback cb,
                           void* userdata);

#endif // UIKIT_FILE_DIALOG_H
