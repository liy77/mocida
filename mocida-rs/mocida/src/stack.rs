//! `UIStack` — linear container that lays children along one axis.

use mocida_sys as sys;

use crate::error::{Error, Result};
use crate::widget::Widget;

/// Layout axis of a [`Stack`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum StackOrientation {
    /// Lay children top-to-bottom.
    Vertical = 0,
    /// Lay children left-to-right.
    Horizontal = 1,
}

/// Linear container that lays children sequentially along one axis.
pub struct Stack {
    ptr: *mut sys::UIStack,
    moved: bool,
}

impl Stack {
    /// Creates a stack with the given orientation.
    pub fn new(orientation: StackOrientation) -> Result<Self> {
        let ptr = unsafe { sys::UIStack_Create(sys::UIStackOrientation(orientation as i32)) };
        if ptr.is_null() {
            return Err(Error::Null("UIStack_Create"));
        }
        Ok(Self { ptr, moved: false })
    }

    /// Sets the gap between consecutive items.
    pub fn spacing(self, spacing: f32) -> Self {
        unsafe { sys::UIStack_SetSpacing(self.ptr, spacing) };
        self
    }

    /// Sets inner padding (left, top, right, bottom).
    pub fn padding(self, left: f32, top: f32, right: f32, bottom: f32) -> Self {
        unsafe { sys::UIStack_SetPadding(self.ptr, left, top, right, bottom) };
        self
    }

    /// Appends an item. The stack takes ownership of the widget.
    pub fn add(&mut self, item: Widget) -> Result<()> {
        let raw = item.into_raw();
        let rc = unsafe { sys::UIStack_AddItem(self.ptr, raw) };
        if rc != 1 {
            return Err(Error::Null("UIStack_AddItem"));
        }
        Ok(())
    }

    /// Borrow the raw `UIStack*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UIStack {
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

impl Drop for Stack {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UIStack_Destroy(self.ptr) };
        }
    }
}
