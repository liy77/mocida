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
}
