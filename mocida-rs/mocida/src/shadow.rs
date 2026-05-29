//! `UIShadow` wrapper.

use crate::color::Color;
use mocida_sys as sys;

/// CSS-style drop shadow descriptor.
///
/// See `shadow.h` in the C source for the full semantics — briefly:
/// `offset_x` / `offset_y` move the halo, `blur` controls how soft the
/// edge is (0 = hard shadow), `spread` expands or contracts the shape
/// before blur, and `color`'s alpha gates intensity.
#[derive(Debug, Clone, Copy)]
pub struct Shadow {
    /// Horizontal displacement; positive = right.
    pub offset_x: f32,
    /// Vertical displacement; positive = down.
    pub offset_y: f32,
    /// Soft halo radius in pixels (0 = hard).
    pub blur: f32,
    /// Pre-blur expansion (+) or contraction (-).
    pub spread: f32,
    /// Shadow tint. Alpha controls intensity.
    pub color: Color,
}

impl Shadow {
    /// Material-design-ish default (matches `UI_SHADOW_DEFAULT`).
    pub const DEFAULT: Self = Self {
        offset_x: 0.0,
        offset_y: 4.0,
        blur: 12.0,
        spread: 0.0,
        color: Color::rgba(0, 0, 0, 0.25),
    };

    /// Sentinel "no shadow" value (matches `UI_SHADOW_NONE`).
    pub const NONE: Self = Self {
        offset_x: 0.0,
        offset_y: 0.0,
        blur: 0.0,
        spread: 0.0,
        color: Color::TRANSPARENT,
    };

    /// Convert to the raw `UIShadow` consumed by `mocida-sys`.
    #[inline]
    pub fn into_raw(self) -> sys::UIShadow {
        sys::UIShadow {
            offsetX: self.offset_x,
            offsetY: self.offset_y,
            blur: self.blur,
            spread: self.spread,
            color: self.color.into_raw(),
        }
    }
}

impl Default for Shadow {
    fn default() -> Self {
        Self::DEFAULT
    }
}

impl From<Shadow> for sys::UIShadow {
    fn from(s: Shadow) -> Self {
        s.into_raw()
    }
}
