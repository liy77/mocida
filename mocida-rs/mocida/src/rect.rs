//! `UIRectangle` wrapper.

use mocida_sys as sys;

use crate::color::Color;
use crate::error::{Error, Result};
use crate::shadow::Shadow;
use crate::widget::Widget;

/// Filled, optionally rounded rectangle with an optional border and
/// drop shadow. Use [`Rectangle::into_widget`] to lift it into a
/// [`Widget`] that can be added to an app or container.
pub struct Rectangle {
    ptr: *mut sys::UIRectangle,
    /// Set once the rectangle has been moved into a [`Widget`]; from
    /// that point on the widget owns the allocation.
    moved: bool,
}

impl Rectangle {
    /// Allocates a new rectangle (mirrors `UIRectangle_Create`).
    pub fn new() -> Result<Self> {
        let ptr = unsafe { sys::UIRectangle_Create() };
        if ptr.is_null() {
            return Err(Error::Null("UIRectangle_Create"));
        }
        Ok(Self { ptr, moved: false })
    }

    /// Sets the corner radius. Equal to `min(w, h) / 2` to draw a circle.
    pub fn radius(self, radius: f32) -> Self {
        unsafe {
            sys::UIRectangle_SetRadius(self.ptr, radius);
        }
        self
    }

    /// Sets the border width (0 = no border).
    pub fn border_width(self, width: f32) -> Self {
        unsafe {
            sys::UIRectangle_SetBorderWidth(self.ptr, width);
        }
        self
    }

    /// Sets the fill color.
    pub fn color(self, color: Color) -> Self {
        unsafe {
            sys::UIRectangle_SetColor(self.ptr, color.into_raw());
        }
        self
    }

    /// Sets the border color (only painted when `border_width > 0`).
    pub fn border_color(self, color: Color) -> Self {
        unsafe {
            sys::UIRectangle_SetBorderColor(self.ptr, color.into_raw());
        }
        self
    }

    /// Sets the outer margins (left, top, right, bottom).
    pub fn margins(self, left: f32, top: f32, right: f32, bottom: f32) -> Self {
        unsafe {
            sys::UIRectangle_SetMargins(self.ptr, left, top, right, bottom);
        }
        self
    }

    /// Attaches a drop shadow.
    pub fn shadow(self, shadow: Shadow) -> Self {
        unsafe {
            sys::UIRectangle_SetShadow(self.ptr, shadow.into_raw());
        }
        self
    }

    /// Clears any previously configured shadow.
    pub fn clear_shadow(self) -> Self {
        unsafe {
            sys::UIRectangle_ClearShadow(self.ptr);
        }
        self
    }

    /// Lift the rectangle into a [`Widget`] (auto-sized).
    pub fn into_widget(mut self) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::new(self.ptr as *mut _) }
    }

    /// Lift the rectangle into a [`Widget`] with an explicit size.
    pub fn into_widget_sized(mut self, width: f32, height: f32) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::with_size(self.ptr as *mut _, width, height) }
    }
}

impl Drop for Rectangle {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            // The user dropped the rectangle without attaching it to a
            // widget — free the underlying C allocation.
            unsafe {
                sys::UIRectangle_Destroy(self.ptr);
            }
        }
    }
}
