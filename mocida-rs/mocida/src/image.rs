//! `UIImage` wrapper.

use std::ffi::CString;

use mocida_sys as sys;

use crate::color::Color;
use crate::error::{Error, Result};
use crate::widget::Widget;

/// How an image fills its widget rectangle.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum FillMode {
    /// Draw at the source size, no scaling.
    None = 0,
    /// Stretch to fill (ignores aspect ratio).
    Stretch = 1,
    /// Scale uniformly to fit.
    Scale = 2,
    /// Tile the source across the bounds.
    Tile = 3,
    /// Center without scaling.
    Center = 4,
    /// Fit while preserving aspect ratio (letterboxes).
    Fit = 5,
    /// Fit to width, preserve aspect ratio.
    FitWidth = 6,
    /// Fit to height, preserve aspect ratio.
    FitHeight = 7,
    /// Cover the bounds, cropping if needed.
    Cover = 8,
}

/// Current load state of an image.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum ImageLoadState {
    /// Texture loaded.
    Success = 0,
    /// Load failed (see stderr).
    Failure = 1,
    /// Still loading.
    InProgress = 2,
}

/// Image widget that lazily loads its texture on first render.
pub struct Image {
    ptr: *mut sys::UIImage,
    moved: bool,
}

impl Image {
    /// Creates an image from a file path with default settings
    /// (no tint, [`FillMode::None`]).
    pub fn load(source: &str) -> Result<Self> {
        let c = CString::new(source)?;
        let ptr = unsafe { sys::UIImage_LoadSource(c.as_ptr(), 0) };
        if ptr.is_null() {
            return Err(Error::Null("UIImage_LoadSource"));
        }
        Ok(Self { ptr, moved: false })
    }

    /// Creates an image with a full configuration.
    pub fn new(
        source: &str,
        animated: bool,
        fill_mode: FillMode,
        tint: Color,
    ) -> Result<Self> {
        let c = CString::new(source)?;
        let ptr = unsafe {
            sys::UIImage_Create(
                c.as_ptr(),
                animated as i32,
                0,
                std::ptr::null_mut(),
                sys::UIFillMode(fill_mode as i32),
                tint.into_raw(),
            )
        };
        if ptr.is_null() {
            return Err(Error::Null("UIImage_Create"));
        }
        Ok(Self { ptr, moved: false })
    }

    /// Borrow the raw `UIImage*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UIImage {
        self.ptr
    }

    /// Lift into a [`Widget`] (auto-sized).
    pub fn into_widget(mut self) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::new(self.ptr as *mut _) }
    }

    /// Lift into a [`Widget`] with an explicit size.
    pub fn into_widget_sized(mut self, width: f32, height: f32) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::with_size(self.ptr as *mut _, width, height) }
    }
}

impl Drop for Image {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UIImage_Destroy(self.ptr) };
        }
    }
}
