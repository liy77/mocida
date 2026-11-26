#include <uikit/input.h>

static Uint8 g_keys[SDL_SCANCODE_COUNT];
static Uint8 g_mouse[8];
static SDL_Gamepad* g_pad = NULL;
static float g_rumbleScale = 1.0f;
static char g_padKind[32] = "";

static float Clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static Uint16 Motor(float intensity) {
    float v = Clamp01(intensity) * g_rumbleScale;
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 0xFFFF;
    return (Uint16)(v * 65535.0f);
}

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
                SDL_RumbleGamepad(g_pad, 0, 0, 0);
                SDL_CloseGamepad(g_pad);
                g_pad = NULL;
                g_padKind[0] = '\0';
            }
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            /* Next PollGamepad will open the new pad. */
            if (!g_pad) { /* keep existing if already open */ }
            break;
        default:
            break;
    }
}

static void RefreshPadKind(void) {
    g_padKind[0] = '\0';
    if (!g_pad) return;
    SDL_GamepadType t = SDL_GetGamepadType(g_pad);
    const char* name = SDL_GetGamepadName(g_pad);
    const char* kind = "generic";
    switch (t) {
        case SDL_GAMEPAD_TYPE_PS3:
        case SDL_GAMEPAD_TYPE_PS4:
        case SDL_GAMEPAD_TYPE_PS5:
            kind = "playstation";
            break;
        case SDL_GAMEPAD_TYPE_XBOX360:
        case SDL_GAMEPAD_TYPE_XBOXONE:
            kind = "xbox";
            break;
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
            kind = "nintendo";
            break;
        default:
            if (name) {
                /* Name fallback when SDL type is standard/unknown. */
                if (SDL_strcasestr(name, "dualshock") || SDL_strcasestr(name, "dualsense")
                    || SDL_strcasestr(name, "playstation") || SDL_strcasestr(name, "sony"))
                    kind = "playstation";
                else if (SDL_strcasestr(name, "xbox") || SDL_strcasestr(name, "xinput"))
                    kind = "xbox";
                else if (SDL_strcasestr(name, "switch") || SDL_strcasestr(name, "joy-con")
                         || SDL_strcasestr(name, "joycon") || SDL_strcasestr(name, "pro controller"))
                    kind = "nintendo";
            }
            break;
    }
    SDL_strlcpy(g_padKind, kind, sizeof(g_padKind));
}

int UIInput_PollGamepad(void) {
    if (!(SDL_WasInit(SDL_INIT_GAMEPAD) & SDL_INIT_GAMEPAD)) {
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) return 0;
    }
    /* Still holding a live pad? */
    if (g_pad) {
        SDL_JoystickID id = SDL_GetGamepadID(g_pad);
        if (SDL_IsGamepad(id)) return 1;
        SDL_CloseGamepad(g_pad);
        g_pad = NULL;
        g_padKind[0] = '\0';
    }
    /* Hotplug: open the first available pad every time none is held. */
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids) {
        if (count > 0) {
            g_pad = SDL_OpenGamepad(ids[0]);
            if (g_pad) RefreshPadKind();
        }
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

const char* UIInput_PadKind(void) {
    if (!g_pad) return "";
    if (!g_padKind[0]) RefreshPadKind();
    return g_padKind;
}

float UIInput_SetRumbleScale(float scale) {
    g_rumbleScale = Clamp01(scale);
    return g_rumbleScale;
}

float UIInput_RumbleScale(void) {
    return g_rumbleScale;
}

int UIInput_Rumble(float low, float high, float left_trigger, float right_trigger, int duration_ms) {
    if (!UIInput_PollGamepad() || !g_pad) return 0;
    if (duration_ms < 0) duration_ms = 0;
    const int body = SDL_RumbleGamepad(
        g_pad, Motor(low), Motor(high), (Uint32)duration_ms) ? 1 : 0;
    const int triggers = SDL_RumbleGamepadTriggers(
        g_pad, Motor(left_trigger), Motor(right_trigger), (Uint32)duration_ms) ? 1 : 0;
    SDL_UpdateJoysticks();
    return body || triggers;
}

void UIInput_StopRumble(void) {
    if (!g_pad) return;
    SDL_RumbleGamepad(g_pad, 0, 0, 0);
    SDL_RumbleGamepadTriggers(g_pad, 0, 0, 0);
    SDL_UpdateJoysticks();
}
