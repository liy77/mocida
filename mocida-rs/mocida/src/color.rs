//! `UIColor` wrapper.

use mocida_sys as sys;

/// RGBA color. Channels are in `0..=255`, alpha is in `0.0..=1.0`
/// (matches the C `UIColor` layout exactly so it can be passed by
/// value).
#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(C)]
pub struct Color {
    /// Red channel (`0..=255`).
    pub r: i32,
    /// Green channel (`0..=255`).
    pub g: i32,
    /// Blue channel (`0..=255`).
    pub b: i32,
    /// Alpha (`0.0..=1.0`). `0.0` is fully transparent.
    pub a: f32,
}

impl Color {
    // Sanity check: the layout has to match the C side at compile time.
    const _LAYOUT_OK: () = {
        assert!(std::mem::size_of::<Color>() == std::mem::size_of::<sys::UIColor>());
    };

    /// Opaque color from 8-bit RGB.
    pub const fn rgb(r: i32, g: i32, b: i32) -> Self {
        Self { r, g, b, a: 1.0 }
    }

    /// Color with explicit alpha.
    pub const fn rgba(r: i32, g: i32, b: i32, a: f32) -> Self {
        Self { r, g, b, a }
    }

    /// Convert to the raw `UIColor` expected by `mocida-sys`.
    #[inline]
    pub fn into_raw(self) -> sys::UIColor {
        sys::UIColor {
            r: self.r,
            g: self.g,
            b: self.b,
            a: self.a,
        }
    }

    /// Wrap an existing raw `UIColor` (passes through; only the layout
    /// has to match).
    #[inline]
    pub fn from_raw(c: sys::UIColor) -> Self {
        Self {
            r: c.r,
            g: c.g,
            b: c.b,
            a: c.a,
        }
    }

    // -- Predefined colors (mirrors UI_COLOR_* macros in color.h). --

    /// Pure black, fully opaque.
    pub const BLACK: Self = Self::rgb(0, 0, 0);
    /// Pure white, fully opaque.
    pub const WHITE: Self = Self::rgb(255, 255, 255);
    /// Pure red.
    pub const RED: Self = Self::rgb(255, 0, 0);
    /// Pure green.
    pub const GREEN: Self = Self::rgb(0, 255, 0);
    /// Pure blue.
    pub const BLUE: Self = Self::rgb(0, 0, 255);
    /// Yellow.
    pub const YELLOW: Self = Self::rgb(255, 255, 0);
    /// Cyan.
    pub const CYAN: Self = Self::rgb(0, 255, 255);
    /// Magenta.
    pub const MAGENTA: Self = Self::rgb(255, 0, 255);
    /// Mid gray.
    pub const GRAY: Self = Self::rgb(128, 128, 128);
    /// Orange.
    pub const ORANGE: Self = Self::rgb(255, 165, 0);
    /// Purple.
    pub const PURPLE: Self = Self::rgb(128, 0, 128);
    /// Fully transparent black.
    pub const TRANSPARENT: Self = Self::rgba(0, 0, 0, 0.0);
}

impl Default for Color {
    fn default() -> Self {
        Self::BLACK
    }
}

impl From<sys::UIColor> for Color {
    fn from(c: sys::UIColor) -> Self {
        Color::from_raw(c)
    }
}

impl From<Color> for sys::UIColor {
    fn from(c: Color) -> Self {
        c.into_raw()
    }
}
