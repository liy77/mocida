#include <uikit/clipboard.h>
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

char* UIClipboard_GetText(void) {
    char* sdlText = SDL_GetClipboardText();
    if (!sdlText) return NULL;
    if (*sdlText == '\0') {
        SDL_free(sdlText);
        return NULL;
    }
    // Re-allocate with the C runtime so callers can use free() without
    // worrying about which allocator SDL used. Avoids exposing
    // SDL_free() through our public API.
    const size_t n = strlen(sdlText) + 1;
    char* copy = (char*)malloc(n);
    if (copy) memcpy(copy, sdlText, n);
    SDL_free(sdlText);
    return copy;
}

void UIClipboard_FreeText(char* text) {
    free(text);
}

int UIClipboard_SetText(const char* text) {
    return SDL_SetClipboardText(text ? text : "") ? 1 : 0;
}

int UIClipboard_HasText(void) {
    return SDL_HasClipboardText() ? 1 : 0;
}
