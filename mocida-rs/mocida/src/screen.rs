//! Screen / display metrics (`UIScreen`) for responsive, size-aware layout.

use mocida_sys as sys;

/// Primary-display size in logical points.
///
/// Use it to compute sizes against the actual device screen — essential
/// on mobile, where one fixed pixel size can't fit every device:
///
/// ```no_run
/// use mocida::Screen;
/// let s = Screen::get();
/// let card_w = s.width as f32 / 4.0;   // four cards across
/// ```
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Screen {
    /// Screen width in logical points.
    pub width: i32,
    /// Screen height in logical points.
    pub height: i32,
}

/// Safe-area insets (logical points) — the margins to keep clear of the
/// notch / Dynamic Island / status bar / home indicator / rounded corners.
/// On iOS `top` is typically the status-bar or notch height; everything is
/// 0 on a desktop window. Inset your top-level content by these so nothing
/// hides behind a device cutout.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Insets {
    /// Inset from the top edge (notch / status bar).
    pub top: i32,
    /// Inset from the left edge.
    pub left: i32,
    /// Inset from the bottom edge (home indicator).
    pub bottom: i32,
    /// Inset from the right edge.
    pub right: i32,
}

impl Screen {
    /// Returns the primary screen size (both 0 before SDL video init).
    pub fn get() -> Screen {
        let s = unsafe { sys::UIScreen_GetSize() };
        Screen {
            width: s.width,
            height: s.height,
        }
    }

    /// Screen width in points.
    pub fn width() -> i32 {
        unsafe { sys::UIScreen_GetWidth() }
    }

    /// Screen height in points.
    pub fn height() -> i32 {
        unsafe { sys::UIScreen_GetHeight() }
    }

    /// Safe-area insets of the active window (all 0 on desktop / if unknown).
    /// Use for notch- / Dynamic-Island-aware layout on iOS.
    pub fn safe_area() -> Insets {
        let i = unsafe { sys::UIScreen_GetSafeArea() };
        Insets {
            top: i.top,
            left: i.left,
            bottom: i.bottom,
            right: i.right,
        }
    }
}
