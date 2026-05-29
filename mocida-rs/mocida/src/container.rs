//! `UIGrid` and `UIScroll` containers (plus `ListView` / `GridView` sugar).

use mocida_sys as sys;

use crate::error::{Error, Result};
use crate::widget::Widget;

/// Fixed-cell grid container. Lays children row-major from the
/// top-left into `columns` columns of `cell_w x cell_h` cells.
pub struct Grid {
    ptr: *mut sys::UIGrid,
    moved: bool,
}

impl Grid {
    /// Creates a grid with the given column count.
    pub fn new(columns: i32) -> Result<Self> {
        let ptr = unsafe { sys::UIGrid_Create(columns) };
        if ptr.is_null() {
            return Err(Error::Null("UIGrid_Create"));
        }
        Ok(Self { ptr, moved: false })
    }

    /// Updates the column count.
    pub fn columns(self, columns: i32) -> Self {
        unsafe { sys::UIGrid_SetColumns(self.ptr, columns) };
        self
    }

    /// Sets the horizontal and vertical gap between cells.
    pub fn gap(self, gap_x: f32, gap_y: f32) -> Self {
        unsafe { sys::UIGrid_SetGap(self.ptr, gap_x, gap_y) };
        self
    }

    /// Sets the cell dimensions.
    pub fn cell_size(self, width: f32, height: f32) -> Self {
        unsafe { sys::UIGrid_SetCellSize(self.ptr, width, height) };
        self
    }

    /// Sets inner padding (left, top, right, bottom).
    pub fn padding(self, left: f32, top: f32, right: f32, bottom: f32) -> Self {
        unsafe { sys::UIGrid_SetPadding(self.ptr, left, top, right, bottom) };
        self
    }

    /// Appends an item. The grid takes ownership of the widget.
    pub fn add(&mut self, item: Widget) -> Result<()> {
        let raw = item.into_raw();
        let rc = unsafe { sys::UIGrid_AddItem(self.ptr, raw) };
        if rc != 1 {
            return Err(Error::Null("UIGrid_AddItem"));
        }
        Ok(())
    }

    /// Returns the total content `(width, height)` reported by mocida.
    pub fn content_size(&self) -> (f32, f32) {
        let mut w = 0.0f32;
        let mut h = 0.0f32;
        unsafe { sys::UIGrid_GetContentSize(self.ptr, &mut w, &mut h) };
        (w, h)
    }

    /// Borrow the raw `UIGrid*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UIGrid {
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

impl Drop for Grid {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UIGrid_Destroy(self.ptr) };
        }
    }
}

/// Scrolling viewport widget with a single owned content child.
pub struct Scroll {
    ptr: *mut sys::UIScroll,
    moved: bool,
}

impl Scroll {
    /// Allocates a new scroll viewport.
    pub fn new() -> Result<Self> {
        let ptr = unsafe { sys::UIScroll_Create() };
        if ptr.is_null() {
            return Err(Error::Null("UIScroll_Create"));
        }
        Ok(Self { ptr, moved: false })
    }

    /// Sets the inner content. The scroll takes ownership of the
    /// widget.
    pub fn content(self, widget: Widget) -> Self {
        let raw = widget.into_raw();
        unsafe { sys::UIScroll_SetContent(self.ptr, raw) };
        self
    }

    /// Sets the scroll offsets.
    pub fn scroll(self, x: f32, y: f32) -> Self {
        unsafe { sys::UIScroll_SetScroll(self.ptr, x, y) };
        self
    }

    /// Enables / disables vertical and horizontal scrolling.
    pub fn axes(self, vertical: bool, horizontal: bool) -> Self {
        unsafe { sys::UIScroll_SetAxes(self.ptr, vertical as i32, horizontal as i32) };
        self
    }

    /// Enables drag-to-pan with the left mouse button.
    pub fn drag_scroll(self, enabled: bool) -> Self {
        unsafe { sys::UIScroll_SetDragScroll(self.ptr, enabled as i32) };
        self
    }

    /// Sets the wheel scroll speed in pixels per notch.
    pub fn wheel_speed(self, px_per_notch: f32) -> Self {
        unsafe { sys::UIScroll_SetWheelSpeed(self.ptr, px_per_notch) };
        self
    }

    /// Invalidates the cached content size.
    pub fn invalidate_content_size(&mut self) {
        unsafe { sys::UIScroll_InvalidateContentSize(self.ptr) };
    }

    /// Borrow the raw `UIScroll*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UIScroll {
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

impl Drop for Scroll {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UIScroll_Destroy(self.ptr) };
        }
    }
}

/// Vertical list view (scroll + single-column grid). Mirrors
/// `UIListView_Create`.
pub struct ListView {
    inner: Scroll,
}

impl ListView {
    /// Creates a vertical list with rows of `item_height`.
    pub fn new(item_height: f32) -> Result<Self> {
        let ptr = unsafe { sys::UIListView_Create(item_height) };
        if ptr.is_null() {
            return Err(Error::Null("UIListView_Create"));
        }
        Ok(Self {
            inner: Scroll {
                ptr,
                moved: false,
            },
        })
    }

    /// Appends an item to the list. The list owns the widget.
    pub fn add(&mut self, item: Widget) -> Result<()> {
        let raw = item.into_raw();
        let rc = unsafe { sys::UIListView_AddItem(self.inner.ptr, raw) };
        if rc != 1 {
            return Err(Error::Null("UIListView_AddItem"));
        }
        Ok(())
    }

    /// Borrow the underlying [`Scroll`].
    pub fn as_scroll(&self) -> &Scroll {
        &self.inner
    }

    /// Consume into the underlying [`Scroll`].
    pub fn into_scroll(self) -> Scroll {
        self.inner
    }

    /// Convenience: lift the list view directly into a [`Widget`].
    pub fn into_widget_sized(self, width: f32, height: f32) -> Result<Widget> {
        self.inner.into_widget_sized(width, height)
    }
}

/// Two-dimensional grid view (scroll + grid).
pub struct GridView {
    inner: Scroll,
}

impl GridView {
    /// Creates a grid view with the given column count and cell size.
    pub fn new(columns: i32, cell_w: f32, cell_h: f32) -> Result<Self> {
        let ptr = unsafe { sys::UIGridView_Create(columns, cell_w, cell_h) };
        if ptr.is_null() {
            return Err(Error::Null("UIGridView_Create"));
        }
        Ok(Self {
            inner: Scroll {
                ptr,
                moved: false,
            },
        })
    }

    /// Appends an item.
    pub fn add(&mut self, item: Widget) -> Result<()> {
        let raw = item.into_raw();
        let rc = unsafe { sys::UIGridView_AddItem(self.inner.ptr, raw) };
        if rc != 1 {
            return Err(Error::Null("UIGridView_AddItem"));
        }
        Ok(())
    }

    /// Convenience: lift directly into a [`Widget`].
    pub fn into_widget_sized(self, width: f32, height: f32) -> Result<Widget> {
        self.inner.into_widget_sized(width, height)
    }
}
