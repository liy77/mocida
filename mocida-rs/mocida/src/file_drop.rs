//! `UIFileDrop` — drop-target widget.

use std::ffi::{c_void, CStr, CString};

use mocida_sys as sys;

use crate::color::Color;
use crate::error::{Error, Result};
use crate::widget::Widget;

/// Drop target. Renders a prompt; fires `on_drop` once per dropped
/// file with the absolute path.
pub struct FileDrop {
    ptr: *mut sys::UIFileDrop,
    moved: bool,
    on_drop: Option<Box<DropState>>,
}

struct DropState {
    handler: Box<dyn FnMut(&str) + 'static>,
}

impl FileDrop {
    /// Creates a drop target with the given prompt text.
    pub fn new(prompt: &str) -> Result<Self> {
        let c = CString::new(prompt)?;
        let ptr = unsafe { sys::UIFileDrop_Create(c.as_ptr()) };
        if ptr.is_null() {
            return Err(Error::Null("UIFileDrop_Create"));
        }
        Ok(Self {
            ptr,
            moved: false,
            on_drop: None,
        })
    }

    /// Sets the font family path.
    pub fn font_family(self, path: &str) -> Result<Self> {
        let c = CString::new(path)?;
        unsafe { sys::UIFileDrop_SetFontFamily(self.ptr, c.as_ptr() as *mut _) };
        Ok(self)
    }

    /// Sets the prompt font size.
    pub fn font_size(self, size: f32) -> Self {
        unsafe { sys::UIFileDrop_SetFontSize(self.ptr, size) };
        self
    }

    /// Sets the full color palette: idle background, drag-over background,
    /// idle border, drag-over border, and prompt text.
    pub fn colors(
        self,
        bg: Color,
        active_bg: Color,
        border: Color,
        active_border: Color,
        text: Color,
    ) -> Self {
        unsafe {
            sys::UIFileDrop_SetColors(
                self.ptr,
                bg.into_raw(),
                active_bg.into_raw(),
                border.into_raw(),
                active_border.into_raw(),
                text.into_raw(),
            )
        };
        self
    }

    /// Registers a drop handler. Receives the absolute path.
    pub fn on_drop<F>(mut self, handler: F) -> Self
    where
        F: FnMut(&str) + 'static,
    {
        let state = Box::new(DropState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const DropState as *mut c_void;
        unsafe { sys::UIFileDrop_OnDrop(self.ptr, Some(drop_trampoline), userdata) };
        self.on_drop = Some(state);
        self
    }

    /// Borrow the raw `UIFileDrop*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UIFileDrop {
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

impl Drop for FileDrop {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UIFileDrop_Destroy(self.ptr) };
            return;
        }
        if let Some(state) = self.on_drop.take() {
            std::mem::forget(state);
        }
    }
}

extern "C" fn drop_trampoline(
    _fd: *mut sys::UIFileDrop,
    path: *const std::ffi::c_char,
    userdata: *mut c_void,
) {
    if userdata.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut DropState) };
    let s = if path.is_null() {
        ""
    } else {
        match unsafe { CStr::from_ptr(path) }.to_str() {
            Ok(s) => s,
            Err(_) => return,
        }
    };
    (state.handler)(s);
}
