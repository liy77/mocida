//! `UIText` wrapper.

use std::ffi::CString;

use mocida_sys as sys;

use crate::color::Color;
use crate::error::{Error, Result};
use crate::widget::Widget;

/// Horizontal alignment of the glyph block within the widget bounds.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum TextHAlign {
    /// Align to the left edge.
    Left = 0,
    /// Center horizontally.
    Center = 1,
    /// Align to the right edge.
    Right = 2,
}

/// Vertical alignment of the glyph block within the widget bounds.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum TextVAlign {
    /// Align to the top edge.
    Top = 0,
    /// Center vertically.
    Center = 1,
    /// Align to the bottom edge.
    Bottom = 2,
}

/// Text wrap strategy.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum WrapMode {
    /// Single line; only explicit `\n` introduces breaks.
    None = 0,
    /// Wrap at word boundaries (spaces).
    Word = 1,
    /// Wrap at any character once the width is exceeded.
    Char = 2,
    /// Shrink-to-fit on a single line.
    Fit = 3,
}

/// Styled text widget.
pub struct Text {
    ptr: *mut sys::UIText,
    moved: bool,
}

impl Text {
    /// Allocates a new text widget.
    pub fn new(text: &str, font_size: f32) -> Result<Self> {
        let c = CString::new(text)?;
        // UIText_Create takes `char*` (non-const), but only reads it to
        // copy the contents into its own heap buffer. Passing a borrowed
        // CString is safe; the C side strdup's immediately.
        let ptr = unsafe { sys::UIText_Create(c.as_ptr() as *mut _, font_size) };
        if ptr.is_null() {
            return Err(Error::Null("UIText_Create"));
        }
        Ok(Self { ptr, moved: false })
    }

    /// Replaces the displayed text. Invalidates the glyph texture cache.
    pub fn set_text(&mut self, text: &str) -> Result<&mut Self> {
        let c = CString::new(text)?;
        unsafe {
            sys::UIText_SetText(self.ptr, c.as_ptr() as *mut _);
        }
        Ok(self)
    }

    /// Sets the font family by file path (typically obtained via
    /// `UIGetFont("FontName")`).
    pub fn font_family(self, path: &str) -> Result<Self> {
        let c = CString::new(path)?;
        unsafe {
            sys::UIText_SetFontFamily(self.ptr, c.as_ptr() as *mut _);
        }
        Ok(self)
    }

    /// Sets the glyph color.
    pub fn color(self, color: Color) -> Self {
        unsafe {
            sys::UIText_SetColor(self.ptr, color.into_raw());
        }
        self
    }

    /// Sets the outer margins (left, top, right, bottom).
    pub fn margins(self, left: f32, top: f32, right: f32, bottom: f32) -> Self {
        unsafe {
            sys::UIText_SetMargins(self.ptr, left, top, right, bottom);
        }
        self
    }

    /// Sets the inner padding (left, top, right, bottom).
    pub fn padding(self, left: f32, top: f32, right: f32, bottom: f32) -> Self {
        unsafe {
            sys::UIText_SetPadding(self.ptr, left, top, right, bottom);
        }
        self
    }

    /// Sets a fixed wrap width in pixels. `0` disables wrapping.
    pub fn wrap_width(self, width: i32) -> Self {
        unsafe {
            sys::UIText_SetWrapWidth(self.ptr, width);
        }
        self
    }

    /// Selects the wrap strategy.
    pub fn wrap_mode(self, mode: WrapMode) -> Self {
        unsafe {
            sys::UIText_SetWrapMode(self.ptr, sys::UIWrapMode(mode as i32));
        }
        self
    }

    /// When `true`, wrap follows the widget's explicit width.
    pub fn wrap_to_bounds(self, enabled: bool) -> Self {
        unsafe {
            sys::UIText_SetWrapToBounds(self.ptr, enabled as i32);
        }
        self
    }

    /// Sets horizontal alignment within the widget bounds.
    pub fn h_align(self, align: TextHAlign) -> Self {
        unsafe {
            sys::UIText_SetHAlign(self.ptr, sys::UITextHAlign(align as i32));
        }
        self
    }

    /// Sets vertical alignment within the widget bounds.
    pub fn v_align(self, align: TextVAlign) -> Self {
        unsafe {
            sys::UIText_SetVAlign(self.ptr, sys::UITextVAlign(align as i32));
        }
        self
    }

    /// Enables interactive text selection.
    pub fn selectable(self, enabled: bool) -> Self {
        unsafe {
            sys::UIText_SetSelectable(self.ptr, enabled as i32);
        }
        self
    }

    /// Borrow the raw `UIText*`. Stays valid as long as `self`
    /// (or, after [`Text::into_widget`], the owning widget tree)
    /// outlives the borrow.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UIText {
        self.ptr
    }

    /// Lift the text into a [`Widget`] (auto-sized).
    pub fn into_widget(mut self) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::new(self.ptr as *mut _) }
    }

    /// Lift the text into a [`Widget`] with an explicit size.
    pub fn into_widget_sized(mut self, width: f32, height: f32) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::with_size(self.ptr as *mut _, width, height) }
    }
}

impl Drop for Text {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe {
                sys::UIText_Destroy(self.ptr);
            }
        }
    }
}

/// Mutate a [`Text`] payload after it has been moved into a
/// [`Widget`]. The wrapper consumes itself in
/// [`Text::into_widget`] / [`Text::into_widget_sized`], so once the
/// widget is parented you only have a raw `*mut UIText`. These
/// helpers keep the raw-pointer interactions in one place and apply
/// the texture-cache invalidations mocida requires.
pub mod by_ptr {
    use std::ffi::CString;

    use mocida_sys as sys;

    use crate::color::Color;
    use crate::error::Result;

    /// Replace the displayed text. `UIText_SetText` already
    /// invalidates the glyph cache, so the new text shows up on the
    /// next render.
    ///
    /// # Safety
    /// `ptr` must point to a live `UIText` (typically obtained from
    /// [`Text::as_ptr`](super::Text::as_ptr) before the wrapper was
    /// consumed by `into_widget`).
    pub unsafe fn set_text(ptr: *mut sys::UIText, text: &str) -> Result<()> {
        if ptr.is_null() {
            return Ok(());
        }
        let c = CString::new(text)?;
        unsafe { sys::UIText_SetText(ptr, c.as_ptr() as *mut _) };
        Ok(())
    }

    /// Replace the glyph color. The next render reflects the new
    /// color directly — recent mocida releases invalidate the glyph
    /// texture on color change.
    ///
    /// # Safety
    /// Same contract as [`set_text`].
    pub unsafe fn set_color(ptr: *mut sys::UIText, color: Color) {
        if ptr.is_null() {
            return;
        }
        unsafe { sys::UIText_SetColor(ptr, color.into_raw()) };
    }
}

/// Loads the system font registry (mirrors `UISearchFonts`).
///
/// Call once on startup before resolving fonts via [`get_font`].
pub fn search_fonts() {
    unsafe { sys::UISearchFonts() };
}

/// Resolves a font family name to an on-disk path (mirrors
/// `UIGetFont`). Returns `None` if the family is unknown.
pub fn get_font(family: &str) -> Option<String> {
    let c = CString::new(family).ok()?;
    let raw = unsafe { sys::UIGetFont(c.as_ptr()) };
    if raw.is_null() {
        return None;
    }
    // SAFETY: the C side returns a pointer into its internal font table
    // (not heap-allocated for this call), so we copy out.
    let s = unsafe { std::ffi::CStr::from_ptr(raw) };
    s.to_str().ok().map(|s| s.to_owned())
}
