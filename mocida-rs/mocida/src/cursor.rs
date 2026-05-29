//! `UICursor` enum + global cursor switch.

use mocida_sys as sys;

/// Logical mouse cursor. Mirrors the upstream `UICursor` enum 1:1.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum Cursor {
    /// Plain arrow.
    Default = 0,
    /// Pointing hand (clickables).
    Pointer = 1,
    /// I-beam (text fields).
    Text = 2,
    /// Crosshair (precision picks).
    Crosshair = 3,
    /// Four-arrow move.
    Move = 4,
    /// Forbidden / disabled.
    NotAllowed = 5,
    /// Busy / hourglass.
    Wait = 6,
    /// Working in background.
    Progress = 7,
    /// Left-right resize.
    EwResize = 8,
    /// Up-down resize.
    NsResize = 9,
    /// Diagonal resize (top-left ↔ bottom-right).
    NwseResize = 10,
    /// Diagonal resize (top-right ↔ bottom-left).
    NeswResize = 11,
}

impl Cursor {
    /// Applies the cursor immediately (mirrors `UICursor_Apply`).
    pub fn apply(self) {
        unsafe { sys::UICursor_Apply(sys::UICursor(self as i32)) };
    }

    /// Convert to the raw bindgen newtype.
    #[inline]
    pub fn into_raw(self) -> sys::UICursor {
        sys::UICursor(self as i32)
    }
}

/// Frees cached system cursors. Normally called from
/// [`App::drop`](crate::App), but callable manually.
pub fn shutdown() {
    unsafe { sys::UICursor_Shutdown() };
}
