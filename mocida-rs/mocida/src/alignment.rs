//! `UIAlignment` — anchor-based positioning helpers.

use std::ffi::c_void;

use mocida_sys as sys;

use crate::widget::Widget;

/// Vertical anchor flags. Bit-or via [`VerticalAlign::bits`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum VerticalAlign {
    /// Center vertically (`UI_ALIGN_V_CENTER`).
    Center = 0x01,
    /// Anchor to the top edge.
    Top = 0x02,
    /// Anchor to the bottom edge.
    Bottom = 0x04,
}

/// Horizontal anchor flags.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum HorizontalAlign {
    /// Center horizontally.
    Center = 0x08,
    /// Anchor to the left edge.
    Left = 0x10,
    /// Anchor to the right edge.
    Right = 0x20,
}

impl VerticalAlign {
    /// Raw bit mask passed to mocida.
    #[inline]
    pub fn bits(self) -> u8 {
        self as u8
    }
}

impl HorizontalAlign {
    /// Raw bit mask passed to mocida.
    #[inline]
    pub fn bits(self) -> u8 {
        self as u8
    }
}

/// Single-axis alignment rule.
#[derive(Debug, Clone, Copy)]
pub struct Align {
    /// Bitmask of `UI_ALIGN_*` flags (one axis only).
    pub value: u8,
    /// Optional sibling widget to align against. Pass `None` to
    /// align against the parent.
    pub target: Option<*mut sys::UIWidget>,
}

impl Align {
    /// Builds a vertical alignment rule with no anchor target.
    #[inline]
    pub fn vertical(v: VerticalAlign) -> Self {
        Self {
            value: v.bits(),
            target: None,
        }
    }

    /// Builds a horizontal alignment rule with no anchor target.
    #[inline]
    pub fn horizontal(h: HorizontalAlign) -> Self {
        Self {
            value: h.bits(),
            target: None,
        }
    }

    /// Builds a rule that anchors against `target`.
    pub fn with_target(mut self, target: &Widget) -> Self {
        self.target = Some(target.as_ptr());
        self
    }

    fn into_raw(self) -> sys::UIAlign {
        sys::UIAlign {
            value: self.value,
            target_widget: self.target.map(|p| p as *mut c_void).unwrap_or(std::ptr::null_mut()),
        }
    }
}

/// Two-axis alignment rule passed to [`Widget::set_alignment`].
#[derive(Debug, Clone, Copy)]
pub struct Alignment {
    /// Vertical rule.
    pub vertical: Align,
    /// Horizontal rule.
    pub horizontal: Align,
}

impl Alignment {
    /// Bundles a pair of single-axis rules.
    pub fn new(vertical: Align, horizontal: Align) -> Self {
        Self {
            vertical,
            horizontal,
        }
    }

    /// Convert to the raw `UIAlignment` value mocida expects.
    #[inline]
    pub fn into_raw(self) -> sys::UIAlignment {
        sys::UIAlignment {
            vertical: self.vertical.into_raw(),
            horizontal: self.horizontal.into_raw(),
        }
    }
}

impl Widget {
    /// Apply an [`Alignment`] rule to this widget.
    pub fn align(&self, alignment: Alignment) -> &Self {
        unsafe { sys::UIWidget_SetAlignment(self.as_ptr(), alignment.into_raw()) };
        self
    }

    /// Aligns against the parent using vertical + horizontal masks.
    pub fn align_to_parent(&self, vertical: VerticalAlign, horizontal: HorizontalAlign) -> &Self {
        unsafe { sys::UIWidget_SetAlignmentByParent(self.as_ptr(), vertical.bits(), horizontal.bits()) };
        self
    }
}
