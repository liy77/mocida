//! `UIMouseArea` wrapper — invisible interaction surface.

use std::ffi::c_void;

use mocida_sys as sys;

use crate::cursor::Cursor;
use crate::error::{Error, Result};
use crate::widget::Widget;

/// Data passed to every mouse-area callback (mirrors `UIMouseEvent`).
#[derive(Debug, Clone, Copy)]
pub struct MouseEvent {
    /// Mouse X in renderer space.
    pub x: f32,
    /// Mouse Y in renderer space.
    pub y: f32,
    /// Horizontal delta since the previous event of the same type.
    pub dx: f32,
    /// Vertical delta since the previous event of the same type.
    pub dy: f32,
    /// X at the start of the drag (drag events only).
    pub start_x: f32,
    /// Y at the start of the drag (drag events only).
    pub start_y: f32,
    /// Mouse button (1 = left, 2 = middle, 3 = right).
    pub button: i32,
}

impl From<sys::UIMouseEvent> for MouseEvent {
    fn from(e: sys::UIMouseEvent) -> Self {
        Self {
            x: e.x,
            y: e.y,
            dx: e.dx,
            dy: e.dy,
            start_x: e.startX,
            start_y: e.startY,
            button: e.button,
        }
    }
}

/// Which mouse-area callback slot to register.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MouseAreaEvent {
    /// Cursor entered the bounds.
    HoverEnter,
    /// Cursor left the bounds.
    HoverExit,
    /// Mouse button pressed inside the bounds.
    MouseDown,
    /// Mouse button released.
    MouseUp,
    /// Mouse moved while hovered.
    MouseMove,
    /// Quick second click on the same spot.
    DoubleClick,
    /// A drag started.
    DragStart,
    /// A drag is ongoing.
    Drag,
    /// A drag completed.
    DragEnd,
}

/// Invisible interaction surface that captures mouse events inside
/// its bounds. Mirrors `UIMouseArea`.
pub struct MouseArea {
    ptr: *mut sys::UIMouseArea,
    moved: bool,
    /// One slot per [`MouseAreaEvent`]; ten boxes alive for the life
    /// of the wrapper. Boxed for stable addresses.
    callbacks: [Option<Box<TrampolineState>>; 9],
}

struct TrampolineState {
    handler: Box<dyn FnMut(MouseEvent) + 'static>,
}

impl MouseArea {
    /// Allocates a new mouse area (mirrors `UIMouseArea_Create`).
    pub fn new() -> Result<Self> {
        let ptr = unsafe { sys::UIMouseArea_Create() };
        if ptr.is_null() {
            return Err(Error::Null("UIMouseArea_Create"));
        }
        Ok(Self {
            ptr,
            moved: false,
            callbacks: Default::default(),
        })
    }

    /// Enables / disables drag-to-move behaviour.
    pub fn draggable(self, enabled: bool) -> Self {
        unsafe { sys::UIMouseArea_SetDraggable(self.ptr, enabled as i32) };
        self
    }

    /// Enables or disables the area entirely.
    pub fn enabled(self, enabled: bool) -> Self {
        unsafe { sys::UIMouseArea_SetEnabled(self.ptr, enabled as i32) };
        self
    }

    /// Cursor displayed while hovering.
    pub fn cursor(self, cursor: Cursor) -> Self {
        unsafe { sys::UIMouseArea_SetCursor(self.ptr, cursor.into_raw()) };
        self
    }

    /// Sibling widget that moves together with the area during drags.
    /// Pass [`None`] to clear. The target is borrowed; you remain
    /// responsible for its lifetime.
    pub fn drag_target(self, target: Option<&Widget>) -> Self {
        let raw = target.map(|w| w.as_ptr()).unwrap_or(std::ptr::null_mut());
        unsafe { sys::UIMouseArea_SetDragTarget(self.ptr, raw) };
        self
    }

    /// Confines the dragged position to the given rectangle. Pass a
    /// zero-sized rect to disable.
    pub fn drag_bounds(self, x: f32, y: f32, w: f32, h: f32) -> Self {
        unsafe { sys::UIMouseArea_SetDragBounds(self.ptr, x, y, w, h) };
        self
    }

    /// Registers a callback for a specific mouse-area event slot.
    /// Replaces any previous handler in that slot.
    pub fn on<F>(mut self, event: MouseAreaEvent, handler: F) -> Self
    where
        F: FnMut(MouseEvent) + 'static,
    {
        let state = Box::new(TrampolineState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const TrampolineState as *mut c_void;
        unsafe {
            match event {
                MouseAreaEvent::HoverEnter => sys::UIMouseArea_OnHoverEnter(self.ptr, Some(trampoline), userdata),
                MouseAreaEvent::HoverExit => sys::UIMouseArea_OnHoverExit(self.ptr, Some(trampoline), userdata),
                MouseAreaEvent::MouseDown => sys::UIMouseArea_OnMouseDown(self.ptr, Some(trampoline), userdata),
                MouseAreaEvent::MouseUp => sys::UIMouseArea_OnMouseUp(self.ptr, Some(trampoline), userdata),
                MouseAreaEvent::MouseMove => sys::UIMouseArea_OnMouseMove(self.ptr, Some(trampoline), userdata),
                MouseAreaEvent::DoubleClick => sys::UIMouseArea_OnDoubleClick(self.ptr, Some(trampoline), userdata),
                MouseAreaEvent::DragStart => sys::UIMouseArea_OnDragStart(self.ptr, Some(trampoline), userdata),
                MouseAreaEvent::Drag => sys::UIMouseArea_OnDrag(self.ptr, Some(trampoline), userdata),
                MouseAreaEvent::DragEnd => sys::UIMouseArea_OnDragEnd(self.ptr, Some(trampoline), userdata),
            };
        }
        let slot = event as usize;
        self.callbacks[slot] = Some(state);
        self
    }

    /// Borrow the raw `UIMouseArea*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UIMouseArea {
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

impl Drop for MouseArea {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UIMouseArea_Destroy(self.ptr) };
            return;
        }
        // Ownership transferred — the parent will free the area, but
        // it still holds our trampoline userdata pointers. Leak the
        // slot states so subsequent events don't deref freed memory.
        for slot in self.callbacks.iter_mut() {
            if let Some(state) = slot.take() {
                std::mem::forget(state);
            }
        }
    }
}

// Manual Default impl on [Option<Box<_>>; 9] since the std impl only
// covers arrays up to length 32 starting on 1.47 — fine — but the
// inner Default for Option<Box<_>> needs an explicit constructor.
impl MouseAreaEvent {
    /// Returns this event's slot index (kept private — exposed only
    /// for the implementation of [`MouseArea::on`]).
    #[allow(dead_code)]
    pub(crate) fn slot(self) -> usize {
        self as usize
    }
}

extern "C" fn trampoline(area: *mut sys::UIMouseArea, ev: sys::UIMouseEvent, userdata: *mut c_void) {
    let _ = area; // not needed; the wrapper would have to be reborrowed and we don't expose mutation here
    if userdata.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut TrampolineState) };
    (state.handler)(ev.into());
}
