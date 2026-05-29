//! `UIDebugOverlay_*` — debug overlay flags + hotkey passthrough.
//!
//! The overlay is **opt-in**: call [`set_enabled(true)`](set_enabled)
//! before it draws or responds to hotkeys. Once enabled, the app event
//! loop routes **F9** (bounds) · **F10** (stats HUD) · **F8** (heatmap) ·
//! **F12** (toggle all). It works in any build (Debug or Release).

use mocida_sys as sys;

/// Overlay rendering options. Bitwise-or to combine.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u32)]
pub enum OverlayFlag {
    /// All overlays off.
    None = 0,
    /// Colored outlines around every widget.
    Bounds = 1 << 0,
    /// FPS / phase-time / memory HUD.
    Stats = 1 << 1,
    /// Tints widgets by depth (overdraw proxy).
    Heatmap = 1 << 2,
    /// Prints widget IDs next to their rect.
    Ids = 1 << 3,
    /// All flags on.
    All = 0xFFFF,
}

impl OverlayFlag {
    /// Raw bit mask.
    #[inline]
    pub fn bits(self) -> u32 {
        self as u32
    }
}

/// Master on/off switch (default off). The overlay only draws and only
/// reacts to its hotkeys while enabled.
pub fn set_enabled(enabled: bool) {
    unsafe { sys::UIDebugOverlay_SetEnabled(enabled as i32) };
}

/// Returns whether the overlay is currently enabled.
pub fn is_enabled() -> bool {
    unsafe { sys::UIDebugOverlay_IsEnabled() != 0 }
}

/// Replaces the overlay flag mask.
pub fn set_flags(flags: u32) {
    unsafe { sys::UIDebugOverlay_SetFlags(flags) };
}

/// Returns the current overlay flag mask.
pub fn flags() -> u32 {
    unsafe { sys::UIDebugOverlay_GetFlags() }
}

/// Toggles an individual flag.
pub fn toggle_flag(flag: OverlayFlag) {
    unsafe { sys::UIDebugOverlay_ToggleFlag(sys::UIOverlayFlag(flag as i32)) };
}

/// Hands an SDL scancode to the overlay. Returns `true` if it matched
/// an overlay binding (callers can swallow the key).
pub fn handle_scancode(scancode: i32) -> bool {
    unsafe { sys::UIDebugOverlay_HandleScancode(scancode) != 0 }
}
