//! `UIDebugOverlay_*` — debug overlay flags + hotkey passthrough.

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
