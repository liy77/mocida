//! `UITextField` wrapper — single-line editable input.

use std::ffi::{c_void, CStr, CString};

use mocida_sys as sys;

use crate::color::Color;
use crate::cursor::Cursor;
use crate::error::{Error, Result};
use crate::widget::Widget;

/// Single-line editable text input.
pub struct TextField {
    ptr: *mut sys::UITextField,
    moved: bool,
    on_change: Option<Box<ChangeState>>,
    on_submit: Option<Box<SubmitState>>,
}

struct ChangeState {
    handler: Box<dyn FnMut(&str) + 'static>,
}

struct SubmitState {
    handler: Box<dyn FnMut(&str) + 'static>,
}

impl TextField {
    /// Allocates a new text field with the given initial text and
    /// font size.
    pub fn new(initial: &str, font_size: f32) -> Result<Self> {
        let c = CString::new(initial)?;
        let ptr = unsafe { sys::UITextField_Create(c.as_ptr(), font_size) };
        if ptr.is_null() {
            return Err(Error::Null("UITextField_Create"));
        }
        Ok(Self {
            ptr,
            moved: false,
            on_change: None,
            on_submit: None,
        })
    }

    /// Replaces the field's text.
    pub fn set_text(&mut self, text: &str) -> Result<()> {
        let c = CString::new(text)?;
        unsafe { sys::UITextField_SetText(self.ptr, c.as_ptr()) };
        Ok(())
    }

    /// Reads the current contents.
    pub fn text(&self) -> Option<String> {
        let p = unsafe { sys::UITextField_GetText(self.ptr) };
        if p.is_null() {
            None
        } else {
            unsafe { CStr::from_ptr(p) }.to_str().ok().map(str::to_owned)
        }
    }

    /// Sets the placeholder text shown when empty.
    pub fn placeholder(self, text: &str) -> Result<Self> {
        let c = CString::new(text)?;
        unsafe { sys::UITextField_SetPlaceholder(self.ptr, c.as_ptr()) };
        Ok(self)
    }

    /// Enables / disables placeholder breathing animation.
    pub fn placeholder_animated(self, yes: bool) -> Self {
        unsafe { sys::UITextField_SetPlaceholderAnimated(self.ptr, yes as i32) };
        self
    }

    /// Sets the caret blink rate (half-period in ms). 0 disables blink.
    pub fn caret_blink_rate(self, ms: i32) -> Self {
        unsafe { sys::UITextField_SetCaretBlinkRate(self.ptr, ms) };
        self
    }

    /// Sets the font family file path.
    pub fn font_family(self, path: &str) -> Result<Self> {
        let c = CString::new(path)?;
        unsafe { sys::UITextField_SetFontFamily(self.ptr, c.as_ptr() as *mut _) };
        Ok(self)
    }

    /// Sets the font size in points.
    pub fn font_size(self, size: f32) -> Self {
        unsafe { sys::UITextField_SetFontSize(self.ptr, size) };
        self
    }

    /// Toggles password-mask rendering.
    pub fn password(self, yes: bool) -> Self {
        unsafe { sys::UITextField_SetPassword(self.ptr, yes as i32) };
        self
    }

    /// Sets the maximum length in bytes (-1 for unlimited).
    pub fn max_length(self, len: i32) -> Self {
        unsafe { sys::UITextField_SetMaxLength(self.ptr, len) };
        self
    }

    /// Sets the background fill.
    pub fn bg_color(self, color: Color) -> Self {
        unsafe { sys::UITextField_SetBgColor(self.ptr, color.into_raw()) };
        self
    }

    /// Sets the glyph color.
    pub fn text_color(self, color: Color) -> Self {
        unsafe { sys::UITextField_SetTextColor(self.ptr, color.into_raw()) };
        self
    }

    /// Sets border (normal, focused, width).
    pub fn border(self, normal: Color, focused: Color, width: f32) -> Self {
        unsafe {
            sys::UITextField_SetBorder(self.ptr, normal.into_raw(), focused.into_raw(), width)
        };
        self
    }

    /// Sets the corner radius.
    pub fn radius(self, radius: f32) -> Self {
        unsafe { sys::UITextField_SetRadius(self.ptr, radius) };
        self
    }

    /// Symmetric padding (`x` horizontal, `y` vertical).
    pub fn padding(self, x: f32, y: f32) -> Self {
        unsafe { sys::UITextField_SetPadding(self.ptr, x, y) };
        self
    }

    /// Cursor displayed while hovering.
    pub fn cursor(self, cursor: Cursor) -> Self {
        unsafe { sys::UITextField_SetCursor(self.ptr, cursor.into_raw()) };
        self
    }

    /// Registers a callback fired on every textual change. Receives
    /// the field's contents.
    pub fn on_change<F>(mut self, handler: F) -> Self
    where
        F: FnMut(&str) + 'static,
    {
        let state = Box::new(ChangeState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const ChangeState as *mut c_void;
        unsafe { sys::UITextField_OnChange(self.ptr, Some(change_trampoline), userdata) };
        self.on_change = Some(state);
        self
    }

    /// Registers a callback fired on Enter.
    pub fn on_submit<F>(mut self, handler: F) -> Self
    where
        F: FnMut(&str) + 'static,
    {
        let state = Box::new(SubmitState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const SubmitState as *mut c_void;
        unsafe { sys::UITextField_OnSubmit(self.ptr, Some(submit_trampoline), userdata) };
        self.on_submit = Some(state);
        self
    }

    /// Programmatic focus control.
    pub fn focus(self, focused: bool) -> Self {
        unsafe { sys::UITextField_SetFocus(self.ptr, focused as i32) };
        self
    }

    /// True when this field has keyboard focus.
    pub fn is_focused(&self) -> bool {
        unsafe { sys::UITextField_IsFocused(self.ptr) != 0 }
    }

    /// Borrow the raw `UITextField*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UITextField {
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

impl Drop for TextField {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UITextField_Destroy(self.ptr) };
            return;
        }
        if let Some(state) = self.on_change.take() {
            std::mem::forget(state);
        }
        if let Some(state) = self.on_submit.take() {
            std::mem::forget(state);
        }
    }
}

extern "C" fn change_trampoline(
    _tf: *mut sys::UITextField,
    text: *const std::ffi::c_char,
    userdata: *mut c_void,
) {
    if userdata.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut ChangeState) };
    let s = if text.is_null() {
        ""
    } else {
        match unsafe { CStr::from_ptr(text) }.to_str() {
            Ok(s) => s,
            Err(_) => return,
        }
    };
    (state.handler)(s);
}

extern "C" fn submit_trampoline(
    _tf: *mut sys::UITextField,
    text: *const std::ffi::c_char,
    userdata: *mut c_void,
) {
    if userdata.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut SubmitState) };
    let s = if text.is_null() {
        ""
    } else {
        match unsafe { CStr::from_ptr(text) }.to_str() {
            Ok(s) => s,
            Err(_) => return,
        }
    };
    (state.handler)(s);
}
