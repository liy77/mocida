//! Polled input for games: held keys, mouse buttons and the first gamepad.
//! Key and mouse state follows the window's events (and the automation
//! API's `keydown` / `keyup`).

use std::ffi::{CStr, CString};

use mocida_sys as sys;

/// True while the key named `name` is held (SDL key names: "Space", "A",
/// "Left", "Left Shift", "Return", "Escape").
pub fn key_down(name: &str) -> bool {
    let Ok(c) = CString::new(name) else { return false };
    unsafe { sys::UIInput_KeyDown(c.as_ptr()) != 0 }
}

/// True while mouse button `button` (1 = left, 2 = middle, 3 = right) is held.
pub fn mouse_down(button: u8) -> bool {
    unsafe { sys::UIInput_MouseDown(button as i32) != 0 }
}

/// Opens the first connected gamepad if needed; true when one is available.
pub fn poll_gamepad() -> bool {
    unsafe { sys::UIInput_PollGamepad() != 0 }
}

/// True while gamepad button `button` (SDL_GamepadButton order: 0 south,
/// 1 east, 2 west, 3 north, 4 back, 5 guide, 6 start, 7 left stick,
/// 8 right stick, 9 left shoulder, 10 right shoulder, 11..14 d-pad up,
/// down, left, right) is held.
pub fn pad_button(button: i32) -> bool {
    unsafe { sys::UIInput_PadButton(button) != 0 }
}

/// Gamepad axis (SDL_GamepadAxis order: 0 left x, 1 left y, 2 right x,
/// 3 right y in -1..1; 4 left trigger, 5 right trigger in 0..1).
pub fn pad_axis(axis: i32) -> f32 {
    unsafe { sys::UIInput_PadAxis(axis) }
}

/// The connected gamepad's name, or "".
pub fn pad_name() -> String {
    unsafe {
        let p = sys::UIInput_PadName();
        if p.is_null() { String::new() } else { CStr::from_ptr(p).to_string_lossy().into_owned() }
    }
}

/// Coarse family: `"playstation"`, `"xbox"`, `"nintendo"`, `"generic"`, or `""`.
pub fn pad_kind() -> String {
    unsafe {
        let p = sys::UIInput_PadKind();
        if p.is_null() { String::new() } else { CStr::from_ptr(p).to_string_lossy().into_owned() }
    }
}

/// SDL_GamepadButton indices (shared across PS / Xbox / Switch via SDL DB).
pub mod btn {
    pub const SOUTH: i32 = 0;
    pub const EAST: i32 = 1;
    pub const WEST: i32 = 2;
    pub const NORTH: i32 = 3;
    pub const BACK: i32 = 4;
    pub const GUIDE: i32 = 5;
    pub const START: i32 = 6;
    pub const LEFT_STICK: i32 = 7;
    pub const RIGHT_STICK: i32 = 8;
    pub const LEFT_SHOULDER: i32 = 9;
    pub const RIGHT_SHOULDER: i32 = 10;
    pub const DPAD_UP: i32 = 11;
    pub const DPAD_DOWN: i32 = 12;
    pub const DPAD_LEFT: i32 = 13;
    pub const DPAD_RIGHT: i32 = 14;
}

/// Scales later rumbles. `scale` is 0..1. Returns the stored scale.
pub fn set_rumble_scale(scale: f32) -> f32 {
    unsafe { sys::UIInput_SetRumbleScale(scale) }
}

/// The scale applied on top of every rumble intensity.
pub fn rumble_scale() -> f32 {
    unsafe { sys::UIInput_RumbleScale() }
}

/// Vibrates the connected pad.
///
/// `low` and `high` are the heavy and light motors, 0..1.
/// `left_trigger` and `right_trigger` are the trigger motors, 0..1.
/// `duration_ms` is how long it lasts; 0 stops it.
/// Returns whether a pad accepted the effect.
pub fn rumble(low: f32, high: f32, left_trigger: f32, right_trigger: f32, duration_ms: i32) -> bool {
    unsafe { sys::UIInput_Rumble(low, high, left_trigger, right_trigger, duration_ms) != 0 }
}

/// Stops every rumble motor.
pub fn stop_rumble() {
    unsafe { sys::UIInput_StopRumble() };
}

/// Holds or releases `name` as if its key event had arrived.
pub fn set_key(name: &str, down: bool) {
    if let Ok(c) = CString::new(name) {
        unsafe { sys::UIInput_SetKey(c.as_ptr(), down as i32) };
    }
}
