#ifndef UIKIT_INPUT_H
#define UIKIT_INPUT_H

#include <SDL3/SDL.h>

/**
 * Polled input state for games: which keys, mouse buttons and gamepad
 * buttons are held right now, and where the gamepad sticks are. Keys and
 * mouse buttons follow the window's events (and the automation API's
 * `keydown` / `keyup`), so a hidden window can be driven by a test.
 */

/** 1 while the key named `name` (an SDL key name: "Space", "A", "Left",
 *  "Left Shift", "Return") is held. */
int UIInput_KeyDown(const char* name);

/** 1 while mouse button `button` (1 = left, 2 = middle, 3 = right) is held. */
int UIInput_MouseDown(int button);

/** Opens the first connected gamepad (if any). Cheap; call once per frame.
 *  Returns 1 when a gamepad is available. */
int UIInput_PollGamepad(void);

/** 1 while gamepad button `button` (an SDL_GamepadButton value: 0 = south,
 *  1 = east, 2 = west, 3 = north, ... 14 = dpad right) is held. */
int UIInput_PadButton(int button);

/** Gamepad axis `axis` (an SDL_GamepadAxis value: 0..3 sticks in -1..1,
 *  4..5 triggers in 0..1). */
float UIInput_PadAxis(int axis);

/** The connected gamepad's name, or "" (for picking button labels). */
const char* UIInput_PadName(void);

/** Marks `name` held or released as if its key event had arrived. */
void UIInput_SetKey(const char* name, int down);

/** Releases every key and mouse button (focus lost). */
void UIInput_Clear(void);

#endif // UIKIT_INPUT_H
