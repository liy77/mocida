//! Chrome widgets: [`SegmentedControl`], [`TabBar`], [`NavBar`].

use std::ffi::{c_void, CString};

use mocida_sys as sys;

use crate::color::Color;
use crate::cursor::Cursor;
use crate::error::{Error, Result};
use crate::widget::Widget;

struct ChangeState {
    handler: Box<dyn FnMut(i32) + 'static>,
}

struct SimpleState {
    handler: Box<dyn FnMut() + 'static>,
}

extern "C" fn seg_change_trampoline(
    _sc: *mut sys::UISegmentedControl,
    index: i32,
    userdata: *mut c_void,
) {
    if userdata.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut ChangeState) };
    (state.handler)(index);
}

extern "C" fn tab_change_trampoline(
    _tb: *mut sys::UITabBar,
    index: i32,
    userdata: *mut c_void,
) {
    if userdata.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut ChangeState) };
    (state.handler)(index);
}

extern "C" fn nav_change_trampoline(
    _nb: *mut sys::UINavBar,
    index: i32,
    userdata: *mut c_void,
) {
    if userdata.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut ChangeState) };
    (state.handler)(index);
}

extern "C" fn nav_simple_trampoline(_nb: *mut sys::UINavBar, userdata: *mut c_void) {
    if userdata.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut SimpleState) };
    (state.handler)();
}

// =====================================================================
// SegmentedControl
// =====================================================================

/// Equal-width pill segments with optional icons / symbols.
pub struct SegmentedControl {
    ptr: *mut sys::UISegmentedControl,
    moved: bool,
    on_change: Option<Box<ChangeState>>,
}

impl SegmentedControl {
    /// Creates an empty segmented control.
    pub fn new() -> Result<Self> {
        let ptr = unsafe { sys::UISegmentedControl_Create() };
        if ptr.is_null() {
            return Err(Error::Null("UISegmentedControl_Create"));
        }
        Ok(Self {
            ptr,
            moved: false,
            on_change: None,
        })
    }

    /// Adds a segment. `icon` is an image path; `symbol` is a glyph/emoji.
    pub fn add_segment(
        &mut self,
        title: &str,
        icon: Option<&str>,
        symbol: Option<&str>,
    ) -> Result<i32> {
        let t = CString::new(title)?;
        let i = icon.map(CString::new).transpose()?;
        let s = symbol.map(CString::new).transpose()?;
        let idx = unsafe {
            sys::UISegmentedControl_AddSegment(
                self.ptr,
                t.as_ptr(),
                i.as_ref().map(|c| c.as_ptr()).unwrap_or(std::ptr::null()),
                s.as_ref().map(|c| c.as_ptr()).unwrap_or(std::ptr::null()),
            )
        };
        if idx < 0 {
            return Err(Error::Null("UISegmentedControl_AddSegment"));
        }
        Ok(idx)
    }

    /// Selects a segment by index (fires `on_change` when it changes).
    pub fn set_selected(&mut self, index: i32) {
        unsafe { sys::UISegmentedControl_SetSelected(self.ptr, index) };
    }

    /// Track / selected / text colors.
    pub fn colors(
        self,
        track: Color,
        selected: Color,
        text: Color,
        selected_text: Color,
    ) -> Self {
        unsafe {
            sys::UISegmentedControl_SetColors(
                self.ptr,
                track.into_raw(),
                selected.into_raw(),
                text.into_raw(),
                selected_text.into_raw(),
            )
        };
        self
    }

    /// Border color + width.
    pub fn border(self, color: Color, width: f32) -> Self {
        unsafe { sys::UISegmentedControl_SetBorder(self.ptr, color.into_raw(), width) };
        self
    }

    /// Corner radius of the track / pill.
    pub fn radius(self, radius: f32) -> Self {
        unsafe { sys::UISegmentedControl_SetRadius(self.ptr, radius) };
        self
    }

    /// Sliding-pill animation duration (0 = instant).
    pub fn anim_ms(self, ms: i32) -> Self {
        unsafe { sys::UISegmentedControl_SetAnimMs(self.ptr, ms) };
        self
    }

    /// Label font. Takes ownership of a heap `family` string internally.
    pub fn font(self, family: &str, size: f32) -> Result<Self> {
        let c = CString::new(family)?;
        unsafe {
            sys::UISegmentedControl_SetFont(self.ptr, c.into_raw(), size);
        }
        Ok(self)
    }

    /// Icon square size in pixels.
    pub fn icon_size(self, size: f32) -> Self {
        unsafe { sys::UISegmentedControl_SetIconSize(self.ptr, size) };
        self
    }

    /// Hover cursor.
    pub fn cursor(self, cursor: Cursor) -> Self {
        unsafe { sys::UISegmentedControl_SetCursor(self.ptr, cursor.into_raw()) };
        self
    }

    /// Fires with the new selected index.
    pub fn on_change<F>(mut self, handler: F) -> Self
    where
        F: FnMut(i32) + 'static,
    {
        let state = Box::new(ChangeState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const ChangeState as *mut c_void;
        unsafe {
            sys::UISegmentedControl_OnChange(self.ptr, Some(seg_change_trampoline), userdata)
        };
        self.on_change = Some(state);
        self
    }

    /// Borrow the raw pointer.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UISegmentedControl {
        self.ptr
    }

    /// Lift into a [`Widget`] with an explicit size.
    pub fn into_widget_sized(mut self, width: f32, height: f32) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::with_size(self.ptr as *mut _, width, height) }
    }
}

impl Drop for SegmentedControl {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UISegmentedControl_Destroy(self.ptr) };
            return;
        }
        if let Some(state) = self.on_change.take() {
            crate::widget::attach_owned(self.ptr as *const c_void, state);
        }
    }
}

// =====================================================================
// TabBar
// =====================================================================

/// Bottom (or top) icon + label tab bar.
pub struct TabBar {
    ptr: *mut sys::UITabBar,
    moved: bool,
    on_change: Option<Box<ChangeState>>,
}

impl TabBar {
    /// Creates an empty tab bar.
    pub fn new() -> Result<Self> {
        let ptr = unsafe { sys::UITabBar_Create() };
        if ptr.is_null() {
            return Err(Error::Null("UITabBar_Create"));
        }
        Ok(Self {
            ptr,
            moved: false,
            on_change: None,
        })
    }

    /// Adds a tab item.
    pub fn add_item(
        &mut self,
        title: &str,
        icon: Option<&str>,
        symbol: Option<&str>,
    ) -> Result<i32> {
        let t = CString::new(title)?;
        let i = icon.map(CString::new).transpose()?;
        let s = symbol.map(CString::new).transpose()?;
        let idx = unsafe {
            sys::UITabBar_AddItem(
                self.ptr,
                t.as_ptr(),
                i.as_ref().map(|c| c.as_ptr()).unwrap_or(std::ptr::null()),
                s.as_ref().map(|c| c.as_ptr()).unwrap_or(std::ptr::null()),
            )
        };
        if idx < 0 {
            return Err(Error::Null("UITabBar_AddItem"));
        }
        Ok(idx)
    }

    /// Selects a tab by index.
    pub fn set_selected(&mut self, index: i32) {
        unsafe { sys::UITabBar_SetSelected(self.ptr, index) };
    }

    /// Bar / selected / unselected colors.
    pub fn colors(self, bar: Color, selected: Color, unselected: Color) -> Self {
        unsafe {
            sys::UITabBar_SetColors(
                self.ptr,
                bar.into_raw(),
                selected.into_raw(),
                unselected.into_raw(),
            )
        };
        self
    }

    /// Border.
    pub fn border(self, color: Color, width: f32) -> Self {
        unsafe { sys::UITabBar_SetBorder(self.ptr, color.into_raw(), width) };
        self
    }

    /// Corner radius.
    pub fn radius(self, radius: f32) -> Self {
        unsafe { sys::UITabBar_SetRadius(self.ptr, radius) };
        self
    }

    /// Label font.
    pub fn font(self, family: &str, size: f32) -> Result<Self> {
        let c = CString::new(family)?;
        unsafe {
            sys::UITabBar_SetFont(self.ptr, c.into_raw(), size);
        }
        Ok(self)
    }

    /// Icon size.
    pub fn icon_size(self, size: f32) -> Self {
        unsafe { sys::UITabBar_SetIconSize(self.ptr, size) };
        self
    }

    /// Hover cursor.
    pub fn cursor(self, cursor: Cursor) -> Self {
        unsafe { sys::UITabBar_SetCursor(self.ptr, cursor.into_raw()) };
        self
    }

    /// Fires with the new selected index.
    pub fn on_change<F>(mut self, handler: F) -> Self
    where
        F: FnMut(i32) + 'static,
    {
        let state = Box::new(ChangeState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const ChangeState as *mut c_void;
        unsafe { sys::UITabBar_OnChange(self.ptr, Some(tab_change_trampoline), userdata) };
        self.on_change = Some(state);
        self
    }

    /// Borrow the raw pointer.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UITabBar {
        self.ptr
    }

    /// Lift into a [`Widget`] with an explicit size.
    pub fn into_widget_sized(mut self, width: f32, height: f32) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::with_size(self.ptr as *mut _, width, height) }
    }
}

impl Drop for TabBar {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UITabBar_Destroy(self.ptr) };
            return;
        }
        if let Some(state) = self.on_change.take() {
            crate::widget::attach_owned(self.ptr as *const c_void, state);
        }
    }
}

// =====================================================================
// NavBar
// =====================================================================

/// Top navigation bar: brand + center links + optional menu button.
pub struct NavBar {
    ptr: *mut sys::UINavBar,
    moved: bool,
    on_change: Option<Box<ChangeState>>,
    on_brand: Option<Box<SimpleState>>,
    on_menu: Option<Box<SimpleState>>,
}

impl NavBar {
    /// Creates an empty nav bar.
    pub fn new() -> Result<Self> {
        let ptr = unsafe { sys::UINavBar_Create() };
        if ptr.is_null() {
            return Err(Error::Null("UINavBar_Create"));
        }
        Ok(Self {
            ptr,
            moved: false,
            on_change: None,
            on_brand: None,
            on_menu: None,
        })
    }

    /// Sets the leading brand title + optional logo path.
    pub fn brand(self, title: &str, logo: Option<&str>) -> Result<Self> {
        let t = CString::new(title)?;
        let l = logo.map(CString::new).transpose()?;
        unsafe {
            sys::UINavBar_SetBrand(
                self.ptr,
                t.as_ptr(),
                l.as_ref().map(|c| c.as_ptr()).unwrap_or(std::ptr::null()),
            )
        };
        Ok(self)
    }

    /// Adds a center link. `value` is an optional page id string.
    pub fn add_item(&mut self, title: &str, value: Option<&str>) -> Result<i32> {
        let t = CString::new(title)?;
        let v = value.map(CString::new).transpose()?;
        let idx = unsafe {
            sys::UINavBar_AddItem(
                self.ptr,
                t.as_ptr(),
                v.as_ref().map(|c| c.as_ptr()).unwrap_or(std::ptr::null()),
            )
        };
        if idx < 0 {
            return Err(Error::Null("UINavBar_AddItem"));
        }
        Ok(idx)
    }

    /// Selects a center link by index.
    pub fn set_selected(&mut self, index: i32) {
        unsafe { sys::UINavBar_SetSelected(self.ptr, index) };
    }

    /// Show / hide the trailing hamburger.
    pub fn menu_visible(self, visible: bool) -> Self {
        unsafe { sys::UINavBar_SetMenuVisible(self.ptr, visible as i32) };
        self
    }

    /// Visual open state for the hamburger.
    pub fn menu_open(self, open: bool) -> Self {
        unsafe { sys::UINavBar_SetMenuOpen(self.ptr, open as i32) };
        self
    }

    /// Bar / title / link colors.
    pub fn colors(self, bar: Color, title: Color, link: Color, link_active: Color) -> Self {
        unsafe {
            sys::UINavBar_SetColors(
                self.ptr,
                bar.into_raw(),
                title.into_raw(),
                link.into_raw(),
                link_active.into_raw(),
            )
        };
        self
    }

    /// Bottom hairline border.
    pub fn border(self, color: Color, width: f32) -> Self {
        unsafe { sys::UINavBar_SetBorder(self.ptr, color.into_raw(), width) };
        self
    }

    /// Fonts for links + brand.
    pub fn font(self, family: &str, size: f32, brand_size: f32) -> Result<Self> {
        let c = CString::new(family)?;
        unsafe {
            sys::UINavBar_SetFont(self.ptr, c.into_raw(), size, brand_size);
        }
        Ok(self)
    }

    /// Hover cursor.
    pub fn cursor(self, cursor: Cursor) -> Self {
        unsafe { sys::UINavBar_SetCursor(self.ptr, cursor.into_raw()) };
        self
    }

    /// Fires when a center link is chosen.
    pub fn on_change<F>(mut self, handler: F) -> Self
    where
        F: FnMut(i32) + 'static,
    {
        let state = Box::new(ChangeState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const ChangeState as *mut c_void;
        unsafe { sys::UINavBar_OnChange(self.ptr, Some(nav_change_trampoline), userdata) };
        self.on_change = Some(state);
        self
    }

    /// Fires when the brand is clicked.
    pub fn on_brand_click<F>(mut self, handler: F) -> Self
    where
        F: FnMut() + 'static,
    {
        let state = Box::new(SimpleState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const SimpleState as *mut c_void;
        unsafe { sys::UINavBar_OnBrandClick(self.ptr, Some(nav_simple_trampoline), userdata) };
        self.on_brand = Some(state);
        self
    }

    /// Fires when the hamburger is clicked.
    pub fn on_menu_click<F>(mut self, handler: F) -> Self
    where
        F: FnMut() + 'static,
    {
        let state = Box::new(SimpleState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const SimpleState as *mut c_void;
        unsafe { sys::UINavBar_OnMenuClick(self.ptr, Some(nav_simple_trampoline), userdata) };
        self.on_menu = Some(state);
        self
    }

    /// Borrow the raw pointer.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UINavBar {
        self.ptr
    }

    /// Lift into a [`Widget`] with an explicit size.
    pub fn into_widget_sized(mut self, width: f32, height: f32) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::with_size(self.ptr as *mut _, width, height) }
    }
}

impl Drop for NavBar {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UINavBar_Destroy(self.ptr) };
            return;
        }
        // Keep all closures alive for the lifetime of the C widget.
        let bundle = (
            self.on_change.take(),
            self.on_brand.take(),
            self.on_menu.take(),
        );
        if bundle.0.is_some() || bundle.1.is_some() || bundle.2.is_some() {
            crate::widget::attach_owned(self.ptr as *const c_void, bundle);
        }
    }
}
