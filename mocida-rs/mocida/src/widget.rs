//! `UIWidget` wrapper.
//!
//! A [`Widget`] is the polymorphic node of mocida's scene graph. It
//! wraps a typed payload (rectangle, text, button, ...) plus position
//! / size / z-order / visibility / opacity / alignment.
//!
//! Ownership is tricky on the C side: once a widget is added to a
//! parent (`UIApp_SetChildren` / a parent `UIChildren_Add`), the
//! parent's destructor frees it. Re-freeing from Rust would double
//! free, so [`Widget`] tracks an `owned` flag and skips `Drop` once
//! ownership is transferred via [`Widget::into_raw`] (used by
//! [`App::add_child`](crate::App::add_child) and friends).

use std::ffi::{CStr, CString};
use std::marker::PhantomData;

use mocida_sys as sys;

use crate::error::{Error, Result};

/// A widget node in mocida's scene graph.
///
/// Constructed via [`Widget::new`] (auto-size) or [`Widget::with_size`]
/// (explicit size), then attached to an [`App`](crate::App) or another
/// container. The payload is one of [`Rectangle`](crate::Rectangle),
/// [`Text`](crate::Text), [`Button`](crate::Button), etc.
pub struct Widget {
    ptr: *mut sys::UIWidget,
    /// `true` while this Rust handle owns the C allocation.
    /// Flipped to `false` once ownership is transferred to a parent.
    owned: bool,
    /// Tie the lifetime of the borrowed data to the widget; PhantomData
    /// doesn't change codegen, it just documents non-Send/Sync.
    _marker: PhantomData<*mut ()>,
}

impl Widget {
    /// Wrap a raw `UIWidget*` with auto-size semantics (`width =
    /// height = NULL` on the C side, i.e. intrinsic sizing).
    ///
    /// `data` must be a non-NULL pointer to one of the typed widget
    /// payloads accepted by `UIWidget_Create` (e.g. `UIRectangle*`,
    /// `UIText*`, `UIButton*`).
    ///
    /// # Safety
    /// The caller must ensure `data` was produced by a matching
    /// `UI<X>_Create` and is not freed elsewhere.
    pub unsafe fn new(data: *mut std::ffi::c_void) -> Result<Self> {
        let ptr = unsafe { sys::widgc(data) };
        if ptr.is_null() {
            return Err(Error::Null("UIWidget_Create"));
        }
        Ok(Self {
            ptr,
            owned: true,
            _marker: PhantomData,
        })
    }

    /// Like [`Widget::new`], but with an explicit width and height
    /// (`UI_DYNAMIC_SIZE = -1.0` means "use intrinsic").
    ///
    /// # Safety
    /// Same contract as [`Widget::new`].
    pub unsafe fn with_size(
        data: *mut std::ffi::c_void,
        width: f32,
        height: f32,
    ) -> Result<Self> {
        let ptr = unsafe { sys::widgcs(data, width, height) };
        if ptr.is_null() {
            return Err(Error::Null("UIWidget_Create"));
        }
        Ok(Self {
            ptr,
            owned: true,
            _marker: PhantomData,
        })
    }

    /// Borrow the underlying `UIWidget*`. Stays valid as long as the
    /// [`Widget`] (or its parent, after transfer) outlives the borrow.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UIWidget {
        self.ptr
    }

    /// Consume the wrapper and hand the raw pointer to a C caller
    /// that will take ownership (e.g. `UIChildren_Add`). Skips
    /// [`Drop`] so the parent's destructor can free it once.
    pub fn into_raw(mut self) -> *mut sys::UIWidget {
        self.owned = false;
        self.ptr
    }

    /// Sets the widget's lookup ID (mirrors `UIWidget_SetId`).
    /// Returns the widget for chaining.
    pub fn id(self, id: &str) -> Result<Self> {
        let c = CString::new(id)?;
        unsafe {
            sys::UIWidget_SetId(self.ptr, c.as_ptr());
        }
        Ok(self)
    }

    /// Read back the lookup ID, if one was set.
    pub fn get_id(&self) -> Option<&str> {
        unsafe {
            let id = (*self.ptr).id;
            if id.is_null() {
                None
            } else {
                CStr::from_ptr(id).to_str().ok()
            }
        }
    }

    /// Sets the size (mirrors `UIWidget_SetSize`).
    pub fn size(self, width: f32, height: f32) -> Self {
        unsafe {
            sys::UIWidget_SetSize(self.ptr, width, height);
        }
        self
    }

    /// Sets the (x, y) position in parent space.
    pub fn position(self, x: f32, y: f32) -> Self {
        unsafe {
            sys::UIWidget_SetPosition(self.ptr, x, y);
        }
        self
    }

    /// Sets the stacking order. Higher draws on top.
    pub fn z_index(self, z: i32) -> Self {
        unsafe {
            sys::UIWidget_SetZIndex(self.ptr, z);
        }
        self
    }

    /// Toggle visibility.
    pub fn visible(self, visible: bool) -> Self {
        unsafe {
            sys::UIWidget_SetVisible(self.ptr, visible as i32);
        }
        self
    }

    /// Request or release keyboard focus.
    pub fn focus(self, focused: bool) -> Self {
        unsafe {
            sys::UIWidget_SetFocus(self.ptr, focused as i32);
        }
        self
    }

    /// True when this widget currently owns keyboard focus.
    pub fn is_focused(&self) -> bool {
        unsafe { sys::UIWidget_IsFocused(self.ptr) != 0 }
    }
}

impl Drop for Widget {
    fn drop(&mut self) {
        if self.owned && !self.ptr.is_null() {
            // Safety: still owned, not yet transferred to a parent.
            unsafe {
                sys::UIWidget_Destroy(self.ptr);
            }
        }
    }
}

// Widgets are not thread-safe; SDL/mocida assumes a single UI thread.
// Leaving Send/Sync un-implemented enforces that at the type level.
