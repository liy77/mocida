//! `UIButton` wrapper.

use std::ffi::{c_void, CString};

use mocida_sys as sys;

use crate::color::Color;
use crate::cursor::Cursor;
use crate::error::{Error, Result};
use crate::shadow::Shadow;
use crate::text::FontStyle;
use crate::widget::Widget;

/// Visual state of a button. Painted with the matching [`ButtonStyle`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum ButtonState {
    /// Idle state.
    Normal = 0,
    /// Cursor is hovering inside the bounds.
    Hover = 1,
    /// Mouse button held down inside the bounds.
    Pressed = 2,
    /// Input is disabled.
    Disabled = 3,
}

/// Per-state color set applied to a [`Button`].
#[derive(Debug, Clone, Copy)]
pub struct ButtonStyle {
    /// Fill color for this state.
    pub background: Color,
    /// Border color (only painted when border_width > 0).
    pub border: Color,
    /// Label color.
    pub text: Color,
}

impl ButtonStyle {
    fn into_raw(self) -> sys::UIButtonStyle {
        sys::UIButtonStyle {
            background: self.background.into_raw(),
            border: self.border.into_raw(),
            text: self.text.into_raw(),
        }
    }
}

/// Clickable button widget with a per-state style table.
///
/// The Rust handle keeps the [`Box`]'ed click callback alive for the
/// lifetime of the button; the heap address is passed through as
/// `userdata` so the C trampoline can recover it.
pub struct Button {
    ptr: *mut sys::UIButton,
    moved: bool,
    /// Owned trampoline state. `None` until [`Button::on_click`] is
    /// called. Stays alive as long as `Button` does.
    callback: Option<Box<TrampolineState>>,
}

struct TrampolineState {
    handler: Box<dyn FnMut(&mut Button) + 'static>,
}

impl Button {
    /// Allocates a new button with the given label and font size.
    pub fn new(text: &str, font_size: f32) -> Result<Self> {
        let c = CString::new(text)?;
        let ptr = unsafe { sys::UIButton_Create(c.as_ptr(), font_size) };
        if ptr.is_null() {
            return Err(Error::Null("UIButton_Create"));
        }
        Ok(Self {
            ptr,
            moved: false,
            callback: None,
        })
    }

    /// Replaces the label text.
    pub fn set_text(&mut self, text: &str) -> Result<&mut Self> {
        let c = CString::new(text)?;
        unsafe {
            sys::UIButton_SetText(self.ptr, c.as_ptr());
        }
        Ok(self)
    }

    /// Sets the font family file path (e.g. from
    /// [`crate::text::get_font`]).
    pub fn font_family(self, path: &str) -> Result<Self> {
        let c = CString::new(path)?;
        unsafe {
            sys::UIButton_SetFontFamily(self.ptr, c.as_ptr() as *mut _);
        }
        Ok(self)
    }

    /// Sets the label font size in points.
    pub fn font_size(self, size: f32) -> Self {
        unsafe {
            sys::UIButton_SetFontSize(self.ptr, size);
        }
        self
    }

    /// Sets the label font style (e.g. `FontStyle::BOLD | FontStyle::ITALIC`).
    pub fn font_style(self, style: FontStyle) -> Self {
        unsafe {
            sys::UIButton_SetFontStyle(self.ptr, style.bits());
        }
        self
    }

    /// Cursor shown while hovering.
    pub fn cursor(self, cursor: Cursor) -> Self {
        unsafe {
            sys::UIButton_SetCursor(self.ptr, cursor.into_raw());
        }
        self
    }

    /// Sets the corner radius of the background.
    pub fn radius(self, radius: f32) -> Self {
        unsafe {
            sys::UIButton_SetRadius(self.ptr, radius);
        }
        self
    }

    /// Sets the border thickness (0 = borderless).
    pub fn border_width(self, width: f32) -> Self {
        unsafe {
            sys::UIButton_SetBorderWidth(self.ptr, width);
        }
        self
    }

    /// Sets the outer margins.
    pub fn margins(self, left: f32, top: f32, right: f32, bottom: f32) -> Self {
        unsafe {
            sys::UIButton_SetMargins(self.ptr, left, top, right, bottom);
        }
        self
    }

    /// Replaces the style of a specific state.
    pub fn state_style(self, state: ButtonState, style: ButtonStyle) -> Self {
        unsafe {
            sys::UIButton_SetStateStyle(self.ptr, sys::UIButtonState(state as i32), style.into_raw());
        }
        self
    }

    /// Convenience: sets NORMAL background + text colors and lets
    /// mocida derive HOVER / PRESSED / DISABLED variants automatically.
    pub fn colors(self, background: Color, text: Color) -> Self {
        unsafe {
            sys::UIButton_SetColors(self.ptr, background.into_raw(), text.into_raw());
        }
        self
    }

    /// Enables or disables input handling.
    pub fn enabled(self, enabled: bool) -> Self {
        unsafe {
            sys::UIButton_SetEnabled(self.ptr, enabled as i32);
        }
        self
    }

    /// Applies a drop shadow consistent across all states.
    pub fn shadow(self, shadow: Shadow) -> Self {
        unsafe {
            sys::UIButton_SetShadow(self.ptr, shadow.into_raw());
        }
        self
    }

    /// Registers a click handler. The closure receives a mutable
    /// reference to the [`Button`] so it can call [`Button::set_text`]
    /// etc. from inside.
    ///
    /// Replaces any previous callback set on this button.
    pub fn on_click<F>(mut self, handler: F) -> Self
    where
        F: FnMut(&mut Button) + 'static,
    {
        let state = Box::new(TrampolineState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const TrampolineState as *mut c_void;
        unsafe {
            sys::UIButton_OnClick(self.ptr, Some(trampoline), userdata);
        }
        self.callback = Some(state);
        self
    }

    /// Lift the button into a [`Widget`] (auto-sized).
    pub fn into_widget(mut self) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::new(self.ptr as *mut _) }
    }

    /// Lift the button into a [`Widget`] with an explicit size.
    pub fn into_widget_sized(mut self, width: f32, height: f32) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::with_size(self.ptr as *mut _, width, height) }
    }
}

impl Drop for Button {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            // We still own the button — the C destructor will tear
            // down its callback registration, so dropping our Box
            // afterwards is safe.
            unsafe {
                sys::UIButton_Destroy(self.ptr);
            }
            return;
        }
        // Ownership of the underlying UIButton was transferred to a
        // parent (typically a Children collection or App). The C side
        // still holds our trampoline `userdata` pointer, so dropping
        // the Box now would dangle that pointer — the next click
        // would deref freed memory. Leak the closure on purpose; the
        // parent's destructor will tear the button down without
        // touching userdata.
        if let Some(state) = self.callback.take() {
            std::mem::forget(state);
        }
    }
}

/// C-ABI trampoline: receives the userdata we registered and calls
/// the Rust closure on the matching `Button`.
extern "C" fn trampoline(button_ptr: *mut sys::UIButton, userdata: *mut c_void) {
    if userdata.is_null() || button_ptr.is_null() {
        return;
    }
    // Safety: `userdata` is the same `Box::as_ref(&state)` pointer we
    // stored on registration. The box outlives the registration as
    // long as the `Button` (or its owning app) is alive.
    let state = unsafe { &mut *(userdata as *mut TrampolineState) };

    // Reconstruct a *borrowed* Button view for the closure. We don't
    // own the pointer here, so flag `moved = true` to skip Drop and
    // we don't try to manage the callback storage from inside.
    let mut view = Button {
        ptr: button_ptr,
        moved: true,
        callback: None,
    };
    (state.handler)(&mut view);
    // Forget the view's callback slot just in case (it's already None
    // here, but being explicit avoids future footguns).
    std::mem::forget(view.callback.take());
}
