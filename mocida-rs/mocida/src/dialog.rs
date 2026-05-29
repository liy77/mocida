//! `UIDialog` — modal-ish dialog widget.

use std::ffi::c_void;

use mocida_sys as sys;

use crate::color::Color;
use crate::error::{Error, Result};
use crate::widget::Widget;

/// Modal-ish dialog: translucent backdrop + centered card.
pub struct Dialog {
    ptr: *mut sys::UIDialog,
    moved: bool,
    on_dismiss: Option<Box<DismissState>>,
}

struct DismissState {
    handler: Box<dyn FnMut() + 'static>,
}

impl Dialog {
    /// Allocates a new dialog with a card of `card_w x card_h`.
    pub fn new(card_w: f32, card_h: f32) -> Result<Self> {
        let ptr = unsafe { sys::UIDialog_Create(card_w, card_h) };
        if ptr.is_null() {
            return Err(Error::Null("UIDialog_Create"));
        }
        Ok(Self {
            ptr,
            moved: false,
            on_dismiss: None,
        })
    }

    /// Card background color.
    pub fn card_color(self, color: Color) -> Self {
        unsafe { sys::UIDialog_SetCardColor(self.ptr, color.into_raw()) };
        self
    }

    /// Backdrop color (use a translucent alpha).
    pub fn backdrop_color(self, color: Color) -> Self {
        unsafe { sys::UIDialog_SetBackdropColor(self.ptr, color.into_raw()) };
        self
    }

    /// Card corner radius.
    pub fn radius(self, radius: f32) -> Self {
        unsafe { sys::UIDialog_SetRadius(self.ptr, radius) };
        self
    }

    /// When `true`, clicking outside the card closes the dialog.
    pub fn dismiss_on_backdrop(self, yes: bool) -> Self {
        unsafe { sys::UIDialog_SetDismissOnBackdrop(self.ptr, yes as i32) };
        self
    }

    /// Registers a dismiss handler.
    pub fn on_dismiss<F>(mut self, handler: F) -> Self
    where
        F: FnMut() + 'static,
    {
        let state = Box::new(DismissState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const DismissState as *mut c_void;
        unsafe { sys::UIDialog_OnDismiss(self.ptr, Some(dismiss_trampoline), userdata) };
        self.on_dismiss = Some(state);
        self
    }

    /// Adds a content widget. The dialog takes ownership.
    /// Position is relative to the card's top-left.
    pub fn add_content(&mut self, widget: Widget) -> Result<()> {
        let raw = widget.into_raw();
        let rc = unsafe { sys::UIDialog_AddContent(self.ptr, raw) };
        if rc != 1 {
            return Err(Error::Null("UIDialog_AddContent"));
        }
        Ok(())
    }

    /// Shows the dialog.
    pub fn show(&mut self) {
        unsafe { sys::UIDialog_Show(self.ptr) };
    }

    /// Hides the dialog.
    pub fn hide(&mut self) {
        unsafe { sys::UIDialog_Hide(self.ptr) };
    }

    /// Borrow the raw `UIDialog*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UIDialog {
        self.ptr
    }

    /// Lift into a [`Widget`]. Add to the window's children with a
    /// high z-index so it draws on top.
    pub fn into_widget(mut self) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::new(self.ptr as *mut _) }
    }
}

impl Drop for Dialog {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UIDialog_Destroy(self.ptr) };
            return;
        }
        if let Some(state) = self.on_dismiss.take() {
            std::mem::forget(state);
        }
    }
}

extern "C" fn dismiss_trampoline(_d: *mut sys::UIDialog, userdata: *mut c_void) {
    if userdata.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut DismissState) };
    (state.handler)();
}
