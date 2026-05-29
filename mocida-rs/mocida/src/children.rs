//! `UIChildren` wrapper — a flat container that owns its widgets.

use std::ffi::CString;

use mocida_sys as sys;

use crate::error::{Error, Result};
use crate::widget::Widget;

/// Owning collection of child widgets. After [`Children::add`], the
/// container is responsible for freeing the widget.
///
/// Once handed to an [`App`](crate::App) via [`App::set_children`],
/// the app takes over destruction.
pub struct Children {
    ptr: *mut sys::UIChildren,
    /// `true` once the collection has been transferred to a parent.
    moved: bool,
}

impl Children {
    /// Allocates a new children collection with the given initial
    /// capacity.
    pub fn new(capacity: i32) -> Result<Self> {
        let ptr = unsafe { sys::UIChildren_Create(capacity) };
        if ptr.is_null() {
            return Err(Error::Null("UIChildren_Create"));
        }
        Ok(Self { ptr, moved: false })
    }

    /// Adds a widget. Ownership of the widget transfers to this
    /// collection.
    ///
    /// Note: upstream's `children.h` documents `UIChildren_Add` as
    /// returning `0` on success / `-1` on failure, but the actual
    /// implementation returns `1` on success and `0` on failure
    /// (capacity exceeded, duplicate ID, NULL args). We follow the
    /// behaviour, not the doc-comment.
    pub fn add(&mut self, widget: Widget) -> Result<()> {
        let raw = widget.into_raw();
        let rc = unsafe { sys::UIChildren_Add(self.ptr, raw) };
        if rc != 1 {
            return Err(Error::Null("UIChildren_Add"));
        }
        Ok(())
    }

    /// Returns the number of children currently stored.
    pub fn len(&self) -> i32 {
        unsafe { (*self.ptr).count }
    }

    /// True when no children have been added yet.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Finds a child by the ID set via [`Widget::id`]. Returns a raw
    /// pointer because the widget is owned by this collection — wrap
    /// it manually if you need to mutate fields beyond what's exposed.
    pub fn get_by_id(&self, id: &str) -> Option<*mut sys::UIWidget> {
        let c = CString::new(id).ok()?;
        let ptr = unsafe { sys::UIChildren_GetById(self.ptr, c.as_ptr()) };
        if ptr.is_null() {
            None
        } else {
            Some(ptr)
        }
    }

    /// Sort children by z-index (lower first).
    pub fn sort_by_z(&mut self) {
        unsafe {
            sys::UIChildren_SortByZ(self.ptr);
        }
    }

    /// Re-runs alignment on every child with anchor rules.
    pub fn relayout(&mut self) {
        unsafe {
            sys::UIChildren_Relayout(self.ptr);
        }
    }

    /// Borrow the raw `UIChildren*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UIChildren {
        self.ptr
    }

    /// Consume the wrapper and return the raw pointer. Used when
    /// handing ownership to an [`App`](crate::App).
    pub(crate) fn into_raw(mut self) -> *mut sys::UIChildren {
        self.moved = true;
        self.ptr
    }
}

impl Drop for Children {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe {
                sys::UIChildren_Destroy(self.ptr);
            }
        }
    }
}
