#include <uikit/cursor.h>
#include <SDL3/SDL.h>

#define UI_CURSOR_COUNT 12

static SDL_Cursor* g_cache[UI_CURSOR_COUNT];
static UICursor    g_active = (UICursor)-1;

static SDL_SystemCursor MapToSDL(UICursor kind) {
    switch (kind) {
        case UI_CURSOR_POINTER:     return SDL_SYSTEM_CURSOR_POINTER;
        case UI_CURSOR_TEXT:        return SDL_SYSTEM_CURSOR_TEXT;
        case UI_CURSOR_CROSSHAIR:   return SDL_SYSTEM_CURSOR_CROSSHAIR;
        case UI_CURSOR_MOVE:        return SDL_SYSTEM_CURSOR_MOVE;
        case UI_CURSOR_NOT_ALLOWED: return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
        case UI_CURSOR_WAIT:        return SDL_SYSTEM_CURSOR_WAIT;
        case UI_CURSOR_PROGRESS:    return SDL_SYSTEM_CURSOR_PROGRESS;
        case UI_CURSOR_EW_RESIZE:   return SDL_SYSTEM_CURSOR_EW_RESIZE;
        case UI_CURSOR_NS_RESIZE:   return SDL_SYSTEM_CURSOR_NS_RESIZE;
        case UI_CURSOR_NWSE_RESIZE: return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
        case UI_CURSOR_NESW_RESIZE: return SDL_SYSTEM_CURSOR_NESW_RESIZE;
        case UI_CURSOR_DEFAULT:
        default:                    return SDL_SYSTEM_CURSOR_DEFAULT;
    }
}

void UICursor_Apply(UICursor kind) {
    if (kind < 0 || (int)kind >= UI_CURSOR_COUNT) kind = UI_CURSOR_DEFAULT;
    if (kind == g_active) return;
    if (!g_cache[kind]) {
        g_cache[kind] = SDL_CreateSystemCursor(MapToSDL(kind));
        if (!g_cache[kind]) return; // leave g_active unchanged on failure
    }
    SDL_SetCursor(g_cache[kind]);
    g_active = kind;
}

void UICursor_Shutdown(void) {
    for (int i = 0; i < UI_CURSOR_COUNT; i++) {
        if (g_cache[i]) {
            SDL_DestroyCursor(g_cache[i]);
            g_cache[i] = NULL;
        }
    }
    g_active = (UICursor)-1;
}
