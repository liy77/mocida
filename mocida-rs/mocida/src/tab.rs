//! `UITabView` — tab strip + content panel.

use std::ffi::{c_void, CString};

use mocida_sys as sys;

use crate::error::{Error, Result};
use crate::widget::Widget;

/// Tab view with a header strip and a content panel below.
pub struct TabView {
    ptr: *mut sys::UITabView,
    moved: bool,
    on_change: Option<Box<ChangeState>>,
}

struct ChangeState {
    handler: Box<dyn FnMut(i32) + 'static>,
}

impl TabView {
    /// Creates a tab view with the given header strip height.
    pub fn new(tab_height: f32) -> Result<Self> {
        let ptr = unsafe { sys::UITabView_Create(tab_height) };
        if ptr.is_null() {
            return Err(Error::Null("UITabView_Create"));
        }
        Ok(Self {
            ptr,
            moved: false,
            on_change: None,
        })
    }

    /// Sets the font family + size for tab labels.
    pub fn font(self, family: &str, size: f32) -> Result<Self> {
        let c = CString::new(family)?;
        unsafe { sys::UITabView_SetFont(self.ptr, c.as_ptr() as *mut _, size) };
        Ok(self)
    }

    /// Adds a tab. Returns the index of the new tab. The tab view
    /// owns the content widget.
    pub fn add_tab(&mut self, title: &str, content: Widget) -> Result<i32> {
        let c = CString::new(title)?;
        let raw = content.into_raw();
        let idx = unsafe { sys::UITabView_AddTab(self.ptr, c.as_ptr(), raw) };
        if idx < 0 {
            return Err(Error::Null("UITabView_AddTab"));
        }
        Ok(idx)
    }

    /// Switches the active tab.
    pub fn set_active(&mut self, index: i32) {
        unsafe { sys::UITabView_SetActive(self.ptr, index) };
    }

    /// Registers a callback fired when the active index changes.
    pub fn on_change<F>(mut self, handler: F) -> Self
    where
        F: FnMut(i32) + 'static,
    {
        let state = Box::new(ChangeState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const ChangeState as *mut c_void;
        unsafe { sys::UITabView_OnChange(self.ptr, Some(change_trampoline), userdata) };
        self.on_change = Some(state);
        self
    }

    /// Borrow the raw `UITabView*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UITabView {
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

impl Drop for TabView {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UITabView_Destroy(self.ptr) };
            return;
        }
        if let Some(state) = self.on_change.take() {
            std::mem::forget(state);
        }
    }
}

extern "C" fn change_trampoline(_tv: *mut sys::UITabView, index: i32, userdata: *mut c_void) {
    if userdata.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut ChangeState) };
    (state.handler)(index);
}
