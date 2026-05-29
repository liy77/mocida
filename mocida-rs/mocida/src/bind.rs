//! `UIBind_*` — one-call hookup of a widget property to a [`Signal<T>`].

use std::ffi::CString;

use mocida_sys as sys;

use crate::button::Button;
use crate::error::{Error, Result};
use crate::reactive::{Signal, SignalValue};
use crate::text::Text;
use crate::widget::Widget;

/// Owns the underlying subscription + helper context allocated by a
/// `UIBind_*` call. Drop it (or call [`Binding::destroy`]) to tear
/// the binding down.
pub struct Binding {
    ptr: *mut sys::UIBinding,
}

impl Binding {
    fn wrap(ptr: *mut sys::UIBinding, ctx: &'static str) -> Result<Self> {
        if ptr.is_null() {
            return Err(Error::Null(ctx));
        }
        Ok(Self { ptr })
    }

    /// Mirror a string signal directly into a [`Text`] widget.
    pub fn text_to_signal(target: &Text, signal: &Signal<String>) -> Result<Self> {
        let ptr = unsafe { sys::UIBind_TextToSignal(target.as_ptr(), signal.as_ptr()) };
        Self::wrap(ptr, "UIBind_TextToSignal")
    }

    /// Format a signal's value through `fmt` (printf-style) into a
    /// [`Text`] widget. Choose `fmt` to match the signal's payload:
    /// `%d`/`%i`/`%u` for [`i32`], `%f`/`%g` for [`f32`], `%s` for
    /// [`String`].
    pub fn text_to_format<T: SignalValue>(
        target: &Text,
        signal: &Signal<T>,
        fmt: &str,
    ) -> Result<Self> {
        let c = CString::new(fmt)?;
        let ptr =
            unsafe { sys::UIBind_TextToFormat(target.as_ptr(), signal.as_ptr(), c.as_ptr()) };
        Self::wrap(ptr, "UIBind_TextToFormat")
    }

    /// Like [`text_to_format`](Self::text_to_format) but writes into
    /// a [`Button`]'s label.
    pub fn button_text_to_format<T: SignalValue>(
        target: &Button,
        signal: &Signal<T>,
        fmt: &str,
    ) -> Result<Self> {
        let c = CString::new(fmt)?;
        let ptr = unsafe {
            sys::UIBind_ButtonTextToFormat(
                target as *const Button as *const _ as *mut sys::UIButton,
                signal.as_ptr(),
                c.as_ptr(),
            )
        };
        Self::wrap(ptr, "UIBind_ButtonTextToFormat")
    }

    /// Wires `widget.visible` to a boolean-style signal (non-zero = visible).
    pub fn visible_to_signal(target: &Widget, signal: &Signal<i32>) -> Result<Self> {
        let ptr = unsafe { sys::UIBind_VisibleToSignal(target.as_ptr(), signal.as_ptr()) };
        Self::wrap(ptr, "UIBind_VisibleToSignal")
    }

    /// Same as [`visible_to_signal`](Self::visible_to_signal) but
    /// accepts a [`Signal<bool>`] (transparently shares the
    /// `UI_SIGNAL_INT` representation).
    pub fn visible_to_bool(target: &Widget, signal: &Signal<bool>) -> Result<Self> {
        let ptr = unsafe { sys::UIBind_VisibleToSignal(target.as_ptr(), signal.as_ptr()) };
        Self::wrap(ptr, "UIBind_VisibleToSignal")
    }

    /// Wires `widget.opacity` to a [`Signal<f32>`].
    pub fn opacity_to_signal(target: &Widget, signal: &Signal<f32>) -> Result<Self> {
        let ptr = unsafe { sys::UIBind_OpacityToSignal(target.as_ptr(), signal.as_ptr()) };
        Self::wrap(ptr, "UIBind_OpacityToSignal")
    }

    /// Wires the X position to a [`Signal<f32>`].
    pub fn position_x_to_signal(target: &Widget, signal: &Signal<f32>) -> Result<Self> {
        let ptr = unsafe { sys::UIBind_PositionXToSignal(target.as_ptr(), signal.as_ptr()) };
        Self::wrap(ptr, "UIBind_PositionXToSignal")
    }

    /// Wires the Y position to a [`Signal<f32>`].
    pub fn position_y_to_signal(target: &Widget, signal: &Signal<f32>) -> Result<Self> {
        let ptr = unsafe { sys::UIBind_PositionYToSignal(target.as_ptr(), signal.as_ptr()) };
        Self::wrap(ptr, "UIBind_PositionYToSignal")
    }

    /// Explicit teardown. Same effect as letting [`Binding`] drop.
    pub fn destroy(self) {
        let _ = self;
    }
}

impl Drop for Binding {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { sys::UIBind_Destroy(self.ptr) };
        }
    }
}
