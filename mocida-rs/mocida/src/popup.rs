//! Tooltip / Menu / Dropdown wrappers (`popup.h`).

use std::ffi::{c_void, CStr, CString};

use mocida_sys as sys;

use crate::color::Color;
use crate::error::{Error, Result};
use crate::widget::Widget;

// =====================================================================
// Tooltip
// =====================================================================

/// Hover-triggered floating label tied to a target widget.
pub struct Tooltip {
    ptr: *mut sys::UITooltip,
    moved: bool,
}

impl Tooltip {
    /// Creates a tooltip anchored to `target` with initial `text`.
    pub fn new(target: &Widget, text: &str, font_size: f32) -> Result<Self> {
        let c = CString::new(text)?;
        let ptr = unsafe { sys::UITooltip_Create(target.as_ptr(), c.as_ptr(), font_size) };
        if ptr.is_null() {
            return Err(Error::Null("UITooltip_Create"));
        }
        Ok(Self { ptr, moved: false })
    }

    /// Updates the tooltip text.
    pub fn set_text(&mut self, text: &str) -> Result<()> {
        let c = CString::new(text)?;
        unsafe { sys::UITooltip_SetText(self.ptr, c.as_ptr()) };
        Ok(())
    }

    /// Sets the font family path.
    pub fn font_family(self, path: &str) -> Result<Self> {
        let c = CString::new(path)?;
        unsafe { sys::UITooltip_SetFontFamily(self.ptr, c.as_ptr() as *mut _) };
        Ok(self)
    }

    /// Hover delay in milliseconds before the tooltip appears.
    pub fn delay(self, ms: u32) -> Self {
        unsafe { sys::UITooltip_SetDelay(self.ptr, ms) };
        self
    }

    /// Sets background and text colors.
    pub fn colors(self, bg: Color, text: Color) -> Self {
        unsafe { sys::UITooltip_SetColors(self.ptr, bg.into_raw(), text.into_raw()) };
        self
    }

    /// Borrow the raw `UITooltip*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UITooltip {
        self.ptr
    }

    /// Lift into a [`Widget`].
    pub fn into_widget(mut self) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::new(self.ptr as *mut _) }
    }
}

impl Drop for Tooltip {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UITooltip_Destroy(self.ptr) };
        }
    }
}

// =====================================================================
// Menu
// =====================================================================

/// Floating popup with a vertical list of clickable items.
pub struct Menu {
    ptr: *mut sys::UIMenu,
    moved: bool,
    on_item: Option<Box<ItemState>>,
}

struct ItemState {
    handler: Box<dyn FnMut(i32, &str) + 'static>,
}

impl Menu {
    /// Creates a menu with the given item dimensions
    /// (`item_width = 0` auto-sizes from the longest label).
    pub fn new(item_height: f32, item_width: f32) -> Result<Self> {
        let ptr = unsafe { sys::UIMenu_Create(item_height, item_width) };
        if ptr.is_null() {
            return Err(Error::Null("UIMenu_Create"));
        }
        Ok(Self {
            ptr,
            moved: false,
            on_item: None,
        })
    }

    /// Adds an item with the given label. Returns the item index.
    pub fn add_item(&mut self, label: &str) -> Result<i32> {
        let c = CString::new(label)?;
        let idx = unsafe { sys::UIMenu_AddItem(self.ptr, c.as_ptr()) };
        if idx < 0 {
            return Err(Error::Null("UIMenu_AddItem"));
        }
        Ok(idx)
    }

    /// Sets the item font family + size.
    pub fn font(self, family: &str, size: f32) -> Result<Self> {
        let c = CString::new(family)?;
        unsafe { sys::UIMenu_SetFont(self.ptr, c.as_ptr() as *mut _, size) };
        Ok(self)
    }

    /// Registers an item-click handler. Receives `(index, label)`.
    pub fn on_item<F>(mut self, handler: F) -> Self
    where
        F: FnMut(i32, &str) + 'static,
    {
        let state = Box::new(ItemState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const ItemState as *mut c_void;
        unsafe { sys::UIMenu_OnItem(self.ptr, Some(menu_item_trampoline), userdata) };
        self.on_item = Some(state);
        self
    }

    /// Shows the menu at `(x, y)`.
    pub fn show_at(&mut self, x: f32, y: f32) {
        unsafe { sys::UIMenu_ShowAt(self.ptr, x, y) };
    }

    /// Hides the menu.
    pub fn hide(&mut self) {
        unsafe { sys::UIMenu_Hide(self.ptr) };
    }

    /// Borrow the raw `UIMenu*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UIMenu {
        self.ptr
    }

    /// Lift into a [`Widget`].
    pub fn into_widget(mut self) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::new(self.ptr as *mut _) }
    }
}

impl Drop for Menu {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UIMenu_Destroy(self.ptr) };
            return;
        }
        if let Some(state) = self.on_item.take() {
            std::mem::forget(state);
        }
    }
}

extern "C" fn menu_item_trampoline(
    _m: *mut sys::UIMenu,
    index: i32,
    label: *const std::ffi::c_char,
    userdata: *mut c_void,
) {
    if userdata.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut ItemState) };
    let s = if label.is_null() {
        ""
    } else {
        match unsafe { CStr::from_ptr(label) }.to_str() {
            Ok(s) => s,
            Err(_) => return,
        }
    };
    (state.handler)(index, s);
}

// =====================================================================
// Dropdown
// =====================================================================

/// Single-select control with a button + popup list.
pub struct Dropdown {
    ptr: *mut sys::UIDropdown,
    moved: bool,
    on_change: Option<Box<DropdownChangeState>>,
}

struct DropdownChangeState {
    handler: Box<dyn FnMut(i32, &str) + 'static>,
}

impl Dropdown {
    /// Allocates an empty dropdown.
    pub fn new() -> Result<Self> {
        let ptr = unsafe { sys::UIDropdown_Create() };
        if ptr.is_null() {
            return Err(Error::Null("UIDropdown_Create"));
        }
        Ok(Self {
            ptr,
            moved: false,
            on_change: None,
        })
    }

    /// Appends an option. Returns its index.
    pub fn add_option(&mut self, label: &str) -> Result<i32> {
        let c = CString::new(label)?;
        let idx = unsafe { sys::UIDropdown_AddOption(self.ptr, c.as_ptr()) };
        if idx < 0 {
            return Err(Error::Null("UIDropdown_AddOption"));
        }
        Ok(idx)
    }

    /// Sets the selected index.
    pub fn set_selected(self, index: i32) -> Self {
        unsafe { sys::UIDropdown_SetSelected(self.ptr, index) };
        self
    }

    /// Reads the current selected index (-1 = none).
    pub fn selected(&self) -> i32 {
        unsafe { sys::UIDropdown_GetSelected(self.ptr) }
    }

    /// Sets the font family + size.
    pub fn font(self, family: &str, size: f32) -> Result<Self> {
        let c = CString::new(family)?;
        unsafe { sys::UIDropdown_SetFont(self.ptr, c.as_ptr() as *mut _, size) };
        Ok(self)
    }

    /// Registers a change handler. Receives `(new_index, label)`.
    pub fn on_change<F>(mut self, handler: F) -> Self
    where
        F: FnMut(i32, &str) + 'static,
    {
        let state = Box::new(DropdownChangeState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const DropdownChangeState as *mut c_void;
        unsafe { sys::UIDropdown_OnChange(self.ptr, Some(dropdown_change_trampoline), userdata) };
        self.on_change = Some(state);
        self
    }

    /// Borrow the raw `UIDropdown*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UIDropdown {
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

impl Drop for Dropdown {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UIDropdown_Destroy(self.ptr) };
            return;
        }
        if let Some(state) = self.on_change.take() {
            std::mem::forget(state);
        }
    }
}

extern "C" fn dropdown_change_trampoline(
    _d: *mut sys::UIDropdown,
    index: i32,
    label: *const std::ffi::c_char,
    userdata: *mut c_void,
) {
    if userdata.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut DropdownChangeState) };
    let s = if label.is_null() {
        ""
    } else {
        match unsafe { CStr::from_ptr(label) }.to_str() {
            Ok(s) => s,
            Err(_) => return,
        }
    };
    (state.handler)(index, s);
}
