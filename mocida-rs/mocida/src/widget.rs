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
    pub unsafe fn with_size(data: *mut std::ffi::c_void, width: f32, height: f32) -> Result<Self> {
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

    /// Sets the opacity multiplier in `[0.0, 1.0]` applied to the whole
    /// subtree's alpha. mocida has no dedicated C setter, so this writes
    /// the public `UIWidget.opacity` field directly.
    pub fn opacity(self, opacity: f32) -> Self {
        unsafe {
            (*self.ptr).opacity = opacity.clamp(0.0, 1.0);
        }
        self
    }

    /// Per-child cross-axis alignment inside a parent Stack (overrides the
    /// stack's `align` for this child): 0 = inherit, 1 = start, 2 = center,
    /// 3 = end. Mirrors `UIWidget_SetSelfAlign`.
    pub fn self_align(self, align: i32) -> Self {
        unsafe {
            sys::UIWidget_SetSelfAlign(self.ptr, align);
        }
        self
    }

    /// Outer margins (left, top, right, bottom), honoured by container layout
    /// (a Stack offsets the item by the leading margin and advances its cursor
    /// past the trailing one). Mirrors `UIWidget_SetMargin`.
    pub fn margin(self, left: f32, top: f32, right: f32, bottom: f32) -> Self {
        unsafe {
            sys::UIWidget_SetMargin(self.ptr, left, top, right, bottom);
        }
        self
    }

    /// Registers a key-down handler on this widget. It fires on EVERY key press
    /// (keyboard isn't spatial) with the SDL key name (`"A"`, `"Return"`,
    /// `"Escape"`, `"Space"`, …) and the SDL keymod bitmask — the handler itself
    /// decides which keys matter. Mirrors `UIWidget_SetOnKeyDown`.
    ///
    /// The closure is boxed and intentionally leaked so its address stays valid
    /// for the C side for the rest of the program (one per `onKeyInput` widget —
    /// the same trade-off the other callback setters make).
    pub fn on_key_down<F>(self, handler: F) -> Self
    where
        F: FnMut(&str, i32) + 'static,
    {
        struct KeyState {
            handler: Box<dyn FnMut(&str, i32) + 'static>,
        }
        unsafe extern "C" fn trampoline(
            _self: *mut std::ffi::c_void,
            key: *const std::os::raw::c_char,
            mods: std::os::raw::c_int,
            userdata: *mut std::ffi::c_void,
        ) {
            if userdata.is_null() {
                return;
            }
            let state = &mut *(userdata as *mut KeyState);
            let k = if key.is_null() {
                ""
            } else {
                CStr::from_ptr(key).to_str().unwrap_or("")
            };
            (state.handler)(k, mods as i32);
        }
        let state = Box::new(KeyState {
            handler: Box::new(handler),
        });
        let userdata = Box::into_raw(state) as *mut std::ffi::c_void;
        unsafe {
            sys::UIWidget_SetOnKeyDown(self.ptr, Some(trampoline), userdata);
        }
        self
    }

    /// Sets the rotation around the widget centre, in degrees. Writes the
    /// public `UIWidget.rotation` field directly (no dedicated C setter).
    pub fn rotation(self, degrees: f32) -> Self {
        unsafe {
            (*self.ptr).rotation = degrees;
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

    /// Clip descendants to this widget's bounds (mirrors
    /// `UIWidget_SetClipChildren`).
    pub fn clip_children(self, enabled: bool) -> Self {
        unsafe {
            sys::UIWidget_SetClipChildren(self.ptr, enabled as i32);
        }
        self
    }

    /// True when children are clipped to this widget's bounds.
    pub fn is_clip_children(&self) -> bool {
        unsafe { sys::UIWidget_GetClipChildren(self.ptr as *const _) != 0 }
    }

    /// Z-order event occlusion (mirrors `UIWidget_SetPropagatingEvents`).
    ///
    /// By default (`false`) the widget OCCLUDES input events: any
    /// wheel/click/hover whose point falls inside it is consumed by the
    /// topmost subtree and never reaches lower-z widgets behind it. Set to
    /// `true` to let the event also pass through to whatever is below.
    pub fn propagating_events(self, enabled: bool) -> Self {
        unsafe {
            sys::UIWidget_SetPropagatingEvents(self.ptr, enabled as i32);
        }
        self
    }

    /// True when this widget lets events pass through to widgets behind it.
    pub fn is_propagating_events(&self) -> bool {
        unsafe { sys::UIWidget_GetPropagatingEvents(self.ptr as *const _) != 0 }
    }

    /// Borrow the raw parent `UIWidget*` (NULL if unparented). The
    /// pointee is owned by the scene graph, not by you — do not free it.
    #[inline]
    pub fn parent_ptr(&self) -> *mut sys::UIWidget {
        unsafe { sys::UIWidget_GetParent(self.ptr) }
    }

    /// The raw `UIWidget*` that currently holds keyboard focus, or NULL
    /// (mirrors `UIWidget_GetFocused`). Borrowed — do not free.
    #[inline]
    pub fn focused_ptr() -> *mut sys::UIWidget {
        unsafe { sys::UIWidget_GetFocused() }
    }

    /// Clear keyboard focus from whichever widget currently holds it
    /// (mirrors `UIWidget_ClearFocus`).
    pub fn clear_focus() {
        unsafe { sys::UIWidget_ClearFocus() };
    }

    /// Look up a widget by its payload `data` pointer, matched by
    /// identity. Returns a borrowed `UIWidget*` (NULL if not found).
    ///
    /// # Safety
    /// `data` should be a pointer obtained from this library; it is only
    /// compared by address, never dereferenced.
    pub unsafe fn find_by_data(data: *mut std::ffi::c_void) -> *mut sys::UIWidget {
        unsafe { sys::UIWidget_FindByData(data) }
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
