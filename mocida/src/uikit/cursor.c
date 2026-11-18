#include <uikit/cursor.h>
#include <uikit/asset.h>
#include <uikit/debug.h>
#include <SDL3/SDL.h>

#define UI_CURSOR_COUNT 12

static SDL_Cursor* g_cache[UI_CURSOR_COUNT];
static SDL_Cursor* g_custom[UI_CURSOR_COUNT];
static UICursor    g_active = (UICursor)-1;
static int         g_active_is_custom = 0;

// Platform hook: defined in cursor_cocoa.mm on macOS (calls
// [NSCursor set] to refresh the visible cursor immediately, since
// the default SDL_SetCursor path is event-driven and updates only
// on the next mouseMoved:). On non-Apple builds the weak symbol
// resolves to NULL and the call is skipped.
__attribute__((weak)) void ui_cursor__hook_apply_done(UICursor kind);
__attribute__((weak)) void ui_cursor__hook_apply_done(UICursor kind) { (void)kind; }

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

static int KindOk(UICursor kind) {
    return kind >= 0 && (int)kind < UI_CURSOR_COUNT;
}

void UICursor_Apply(UICursor kind) {
    if (!KindOk(kind)) kind = UI_CURSOR_DEFAULT;
    SDL_Cursor* custom = g_custom[kind];
    if (kind == g_active && ((custom != NULL) == g_active_is_custom)) return;
    SDL_Cursor* cursor = custom;
    if (!cursor) {
        if (!g_cache[kind]) {
            g_cache[kind] = SDL_CreateSystemCursor(MapToSDL(kind));
            if (!g_cache[kind]) return; // leave g_active unchanged on failure
        }
        cursor = g_cache[kind];
    }
    SDL_SetCursor(cursor);
    if (ui_cursor__hook_apply_done) ui_cursor__hook_apply_done(kind);
    g_active = kind;
    g_active_is_custom = custom != NULL;
}

int UICursor_SetImage(UICursor kind, const char* path, int hot_x, int hot_y) {
    if (!KindOk(kind)) return 0;
    if (!path || !path[0]) {
        if (g_custom[kind]) {
            SDL_DestroyCursor(g_custom[kind]);
            g_custom[kind] = NULL;
        }
        if (g_active == kind) g_active = (UICursor)-1; // force the next Apply
        return 1;
    }

    SDL_Surface* surf = UIAsset_LoadSurface(path);
    if (!surf) return 0;

    // SDL colour cursors want a direct RGBA surface. IMG_Load may hand
    // back a paletted or premultiplied format; convert when it isn't.
    if (surf->format != SDL_PIXELFORMAT_RGBA32 &&
        surf->format != SDL_PIXELFORMAT_ARGB8888) {
        SDL_Surface* conv = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surf);
        surf = conv;
        if (!surf) {
            UI_ERROR(UI_CAT_ASSET, "UICursor_SetImage: convert failed: %s", SDL_GetError());
            return 0;
        }
    }

    if (hot_x < 0) hot_x = 0;
    if (hot_y < 0) hot_y = 0;
    if (hot_x >= surf->w) hot_x = surf->w > 0 ? surf->w - 1 : 0;
    if (hot_y >= surf->h) hot_y = surf->h > 0 ? surf->h - 1 : 0;

    SDL_Cursor* cursor = SDL_CreateColorCursor(surf, hot_x, hot_y);
    SDL_DestroySurface(surf);
    if (!cursor) {
        UI_ERROR(UI_CAT_ASSET, "UICursor_SetImage: SDL_CreateColorCursor failed: %s", SDL_GetError());
        return 0;
    }

    if (g_custom[kind]) SDL_DestroyCursor(g_custom[kind]);
    g_custom[kind] = cursor;
    if (g_active == kind) g_active = (UICursor)-1; // next Apply installs it
    return 1;
}

void UICursor_ClearImages(void) {
    for (int i = 0; i < UI_CURSOR_COUNT; i++) {
        if (g_custom[i]) {
            SDL_DestroyCursor(g_custom[i]);
            g_custom[i] = NULL;
        }
    }
    g_active = (UICursor)-1;
}

void UICursor_Shutdown(void) {
    for (int i = 0; i < UI_CURSOR_COUNT; i++) {
        if (g_cache[i]) {
            SDL_DestroyCursor(g_cache[i]);
            g_cache[i] = NULL;
        }
        if (g_custom[i]) {
            SDL_DestroyCursor(g_custom[i]);
            g_custom[i] = NULL;
        }
    }
    g_active = (UICursor)-1;
    g_active_is_custom = 0;
}
