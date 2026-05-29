#ifndef UIKIT_CLIPBOARD_H
#define UIKIT_CLIPBOARD_H

/**
 * Thin wrapper over SDL_GetClipboardText / SDL_SetClipboardText so app
 * code doesn't have to pull in <SDL3/SDL.h> just to copy/paste.
 */

/**
 * Returns the current clipboard contents as a newly allocated UTF-8
 * string. The caller owns the returned pointer and must free it with
 * UIClipboard_FreeText. Returns NULL when the clipboard is empty or on
 * failure.
 */
char* UIClipboard_GetText(void);

/**
 * Releases a string returned by UIClipboard_GetText. Safe on NULL.
 */
void  UIClipboard_FreeText(char* text);

/**
 * Replaces the clipboard contents with the given UTF-8 string. Returns
 * 1 on success, 0 on failure. Passing NULL clears the clipboard.
 */
int   UIClipboard_SetText(const char* text);

/**
 * Returns 1 when the clipboard currently has text content, 0 otherwise.
 */
int   UIClipboard_HasText(void);

#endif // UIKIT_CLIPBOARD_H
