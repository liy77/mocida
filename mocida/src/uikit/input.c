#include <uikit/input.h>

static Uint8 g_keys[SDL_SCANCODE_COUNT];
static Uint8 g_mouse[8];
static SDL_Gamepad* g_pad = NULL;
static int g_padTried = 0;

static SDL_Scancode ScancodeOf(const char* name) {
    if (!name || !*name) return SDL_SCANCODE_UNKNOWN;
    SDL_Scancode sc = SDL_GetScancodeFromName(name);
    if (sc == SDL_SCANCODE_UNKNOWN) {
        const SDL_Keycode k = SDL_GetKeyFromName(name);
        if (k != SDLK_UNKNOWN) sc = SDL_GetScancodeFromKey(k, NULL);
    }
    return sc;
}

int UIInput_KeyDown(const char* name) {
    const SDL_Scancode sc = ScancodeOf(name);
    return (sc > SDL_SCANCODE_UNKNOWN && sc < SDL_SCANCODE_COUNT) ? g_keys[sc] : 0;
}

int UIInput_MouseDown(int button) {
    return (button > 0 && button < 8) ? g_mouse[button] : 0;
}

void UIInput_SetKey(const char* name, int down) {
    const SDL_Scancode sc = ScancodeOf(name);
    if (sc > SDL_SCANCODE_UNKNOWN && sc < SDL_SCANCODE_COUNT) g_keys[sc] = down ? 1 : 0;
}

void UIInput_Clear(void) {
    SDL_memset(g_keys, 0, sizeof(g_keys));
    SDL_memset(g_mouse, 0, sizeof(g_mouse));
}

void UIInput__OnEvent(const SDL_Event* e) {
    switch (e->type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if (e->key.scancode > SDL_SCANCODE_UNKNOWN && e->key.scancode < SDL_SCANCODE_COUNT)
                g_keys[e->key.scancode] = e->key.down ? 1 : 0;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e->button.button < 8) g_mouse[e->button.button] = e->button.down ? 1 : 0;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            UIInput_Clear();
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (g_pad && SDL_GetGamepadID(g_pad) == e->gdevice.which) {
                SDL_CloseGamepad(g_pad);
                g_pad = NULL;
            }
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            g_padTried = 0;
            break;
        default:
            break;
    }
}

int UIInput_PollGamepad(void) {
    if (g_pad) return 1;
    if (g_padTried) return 0;
    g_padTried = 1;
    if (!(SDL_WasInit(SDL_INIT_GAMEPAD) & SDL_INIT_GAMEPAD)) {
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) return 0;
    }
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids) {
        if (count > 0) g_pad = SDL_OpenGamepad(ids[0]);
        SDL_free(ids);
    }
    return g_pad != NULL;
}

int UIInput_PadButton(int button) {
    if (!g_pad || button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT) return 0;
    return SDL_GetGamepadButton(g_pad, (SDL_GamepadButton)button) ? 1 : 0;
}

float UIInput_PadAxis(int axis) {
    if (!g_pad || axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT) return 0.0f;
    const Sint16 v = SDL_GetGamepadAxis(g_pad, (SDL_GamepadAxis)axis);
    return v < 0 ? (float)v / 32768.0f : (float)v / 32767.0f;
}

const char* UIInput_PadName(void) {
    const char* n = g_pad ? SDL_GetGamepadName(g_pad) : NULL;
    return n ? n : "";
}
