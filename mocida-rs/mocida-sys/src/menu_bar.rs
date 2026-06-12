//! Safe RAII wrappers around the raw FFI bindings for the menu bar
//! (`mocida/src/uikit/menu_bar.h`). The bindgen-generated declarations
//! live in `crate::bindings` (re-exported at the crate root) and are
//! `unsafe`; this file adds the `Menu` / `Item` structs with `Drop`
//! and builder ergonomics.

use std::os::raw::c_uint;

// The bindgen wrapper exports the C types and functions at the crate
// root via `pub use bindings::*` in lib.rs. We import them here.
pub use crate::{
    ui_menu_bar_create, ui_menu_bar_destroy, ui_menu_bar_item_create,
    ui_menu_bar_item_destroy, ui_menu_bar_separator, ui_menu_bar_append_item,
    ui_menu_bar_append_submenu, ui_menu_bar_append_separator,
    ui_menu_bar_set, ui_menu_bar_set_item_callback,
    UIMenuBar, UIMenuBarItem,
};
pub use crate::bindings::ui_menu_bar_item_callback_t as RawMenuItemCallback;

/// Owning handle to a `UIMenuBar`. Drop calls `ui_menu_bar_destroy`
/// unless the menu has been transferred to the app via
/// `Menu::set_as_app_menu_bar`.
pub struct Menu(*mut UIMenuBar);

impl Menu {
    /// Wrap a raw pointer returned by the C side. Returns `None` if
    /// the pointer is null (the C allocators return null on OOM).
    fn from_raw(ptr: *mut UIMenuBar) -> Option<Self> {
        if ptr.is_null() { None } else { Some(Self(ptr)) }
    }

    /// Borrow the raw handle. Do NOT outlive the wrapper.
    pub fn as_raw(&self) -> *mut UIMenuBar { self.0 }

    /// Append a leaf item. Consumes the item.
    pub fn append_item(&mut self, item: Item) {
        let raw = item.into_raw();
        unsafe { ui_menu_bar_append_item(self.0, raw) }
    }

    /// Append a submenu. Consumes the submenu.
    pub fn append_submenu(&mut self, sub: Menu) {
        let raw = sub.into_raw();
        unsafe { ui_menu_bar_append_submenu(self.0, raw) }
    }

    /// Append a separator.
    pub fn append_separator(&mut self) {
        unsafe { ui_menu_bar_append_separator(self.0) }
    }

    /// Install this menu as the application's main menu (Cocoa:
    /// `[NSApp setMainMenu:]`). Consumes the wrapper — the C side
    /// owns the tree from here on.
    pub fn set_as_app_menu_bar(self) {
        let raw = self.into_raw();
        unsafe { ui_menu_bar_set(raw) }
    }

    /// Release ownership without running Drop.
    fn into_raw(self) -> *mut UIMenuBar {
        let raw = self.0;
        std::mem::forget(self);
        raw
    }
}

impl Drop for Menu {
    fn drop(&mut self) {
        if !self.0.is_null() {
            unsafe { ui_menu_bar_destroy(self.0) }
        }
    }
}

// SAFETY: UIMenuBar access is single-threaded (Cocoa main thread). The
// type system can't enforce this — it's a contract at the call site.
unsafe impl Send for Menu {}

/// Owning handle to a `UIMenuBarItem`. Drop calls
/// `ui_menu_bar_item_destroy` — but only if the item has NOT been
/// attached to a parent menu.
pub struct Item(*mut UIMenuBarItem);

impl Item {
    fn from_raw(ptr: *mut UIMenuBarItem) -> Option<Self> {
        if ptr.is_null() { None } else { Some(Self(ptr)) }
    }

    /// Build a leaf menu item. `shortcut` is SDL-style
    /// ("CmdOrCtrl+N", "Alt+Shift+Z") or `None` for no key equivalent.
    pub fn new(label: &str, shortcut: Option<&str>, id: c_uint) -> Self {
        let label_c = std::ffi::CString::new(label).expect("label has no NUL");
        let sc_c;
        let sc_ptr = match shortcut {
            Some(s) => {
                sc_c = std::ffi::CString::new(s).expect("shortcut has no NUL");
                sc_c.as_ptr()
            }
            None => std::ptr::null(),
        };
        let raw = unsafe {
            ui_menu_bar_item_create(label_c.as_ptr(), sc_ptr, id)
        };
        Self::from_raw(raw).expect("ui_menu_bar_item_create returned NULL (OOM)")
    }

    /// Build a separator leaf.
    pub fn separator() -> Self {
        let raw = unsafe { ui_menu_bar_separator() };
        Self::from_raw(raw).expect("ui_menu_bar_separator returned NULL (OOM)")
    }

    fn into_raw(self) -> *mut UIMenuBarItem {
        let raw = self.0;
        std::mem::forget(self);
        raw
    }
}

impl Drop for Item {
    fn drop(&mut self) {
        if !self.0.is_null() {
            unsafe { ui_menu_bar_item_destroy(self.0) }
        }
    }
}

unsafe impl Send for Item {}

/// Register a callback fired when the user activates any item in the
/// currently-installed menu bar. Pass `None` to clear. The callback is
/// invoked on the main thread.
///
/// # Safety
/// The callback must be `unsafe extern "C"` and must not panic. The
/// `user` pointer is whatever you passed in.
pub unsafe fn set_item_callback(
    cb: RawMenuItemCallback,
    user: *mut std::os::raw::c_void,
) {
    ui_menu_bar_set_item_callback(cb, user);
}
