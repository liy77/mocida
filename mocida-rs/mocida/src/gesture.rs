//! SwiftUI-style gesture surface: [`GestureArea`].
//!
//! Recognizers: tap, double-tap, long-press, drag, swipe (single pointer).

use std::ffi::c_void;

use mocida_sys as sys;

use crate::cursor::Cursor;
use crate::error::{Error, Result};
use crate::widget::Widget;

/// Cardinal swipe direction.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum SwipeDirection {
    None = 0,
    Left = 1,
    Right = 2,
    Up = 3,
    Down = 4,
}

impl SwipeDirection {
    pub fn from_raw(v: i32) -> Self {
        match v {
            1 => Self::Left,
            2 => Self::Right,
            3 => Self::Up,
            4 => Self::Down,
            _ => Self::None,
        }
    }
}

/// Richer event payload for gesture callbacks.
#[derive(Debug, Clone, Copy)]
pub struct GestureEvent {
    pub x: f32,
    pub y: f32,
    pub dx: f32,
    pub dy: f32,
    pub start_x: f32,
    pub start_y: f32,
    pub translation_x: f32,
    pub translation_y: f32,
    pub velocity_x: f32,
    pub velocity_y: f32,
    pub distance: f32,
    pub progress: f32,
    pub button: i32,
    pub tap_count: i32,
    pub direction: SwipeDirection,
}

impl From<sys::UIGestureEvent> for GestureEvent {
    fn from(e: sys::UIGestureEvent) -> Self {
        Self {
            x: e.x,
            y: e.y,
            dx: e.dx,
            dy: e.dy,
            start_x: e.startX,
            start_y: e.startY,
            translation_x: e.translationX,
            translation_y: e.translationY,
            velocity_x: e.velocityX,
            velocity_y: e.velocityY,
            distance: e.distance,
            progress: e.progress,
            button: e.button,
            tap_count: e.tapCount,
            direction: SwipeDirection::from_raw(e.direction),
        }
    }
}

/// Which gesture callback slot to register.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum GestureKind {
    Tap = 0,
    DoubleTap = 1,
    LongPress = 2,
    LongPressProgress = 3,
    DragStart = 4,
    DragChanged = 5,
    DragEnded = 6,
    Swipe = 7,
    HoverEnter = 8,
    HoverExit = 9,
}

/// Invisible gesture recognizer surface (SwiftUI-style).
pub struct GestureArea {
    ptr: *mut sys::UIGestureArea,
    moved: bool,
    callbacks: [Option<Box<TrampolineState>>; 10],
}

struct TrampolineState {
    handler: Box<dyn FnMut(GestureEvent) + 'static>,
}

extern "C" fn trampoline(
    _g: *mut sys::UIGestureArea,
    ev: sys::UIGestureEvent,
    userdata: *mut c_void,
) {
    if userdata.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut TrampolineState) };
    (state.handler)(ev.into());
}

impl GestureArea {
    pub fn new() -> Result<Self> {
        let ptr = unsafe { sys::UIGestureArea_Create() };
        if ptr.is_null() {
            return Err(Error::Null("UIGestureArea_Create"));
        }
        Ok(Self {
            ptr,
            moved: false,
            callbacks: Default::default(),
        })
    }

    pub fn enabled(self, on: bool) -> Self {
        unsafe { sys::UIGestureArea_SetEnabled(self.ptr, on as i32) };
        self
    }

    pub fn cursor(self, cursor: Cursor) -> Self {
        unsafe { sys::UIGestureArea_SetCursor(self.ptr, cursor.into_raw()) };
        self
    }

    pub fn nested_first(self, on: bool) -> Self {
        unsafe { sys::UIGestureArea_SetNestedFirst(self.ptr, on as i32) };
        self
    }

    pub fn exclusive(self, on: bool) -> Self {
        unsafe { sys::UIGestureArea_SetExclusive(self.ptr, on as i32) };
        self
    }

    pub fn long_press_ms(self, ms: i32) -> Self {
        unsafe { sys::UIGestureArea_SetLongPressMs(self.ptr, ms) };
        self
    }

    pub fn drag_min_distance(self, px: f32) -> Self {
        unsafe { sys::UIGestureArea_SetDragMinDistance(self.ptr, px) };
        self
    }

    pub fn tap_max_distance(self, px: f32) -> Self {
        unsafe { sys::UIGestureArea_SetTapMaxDistance(self.ptr, px) };
        self
    }

    pub fn swipe_min_distance(self, px: f32) -> Self {
        unsafe { sys::UIGestureArea_SetSwipeMinDistance(self.ptr, px) };
        self
    }

    pub fn swipe_min_velocity(self, px_per_ms: f32) -> Self {
        unsafe { sys::UIGestureArea_SetSwipeMinVelocity(self.ptr, px_per_ms) };
        self
    }

    pub fn on<F>(mut self, kind: GestureKind, handler: F) -> Self
    where
        F: FnMut(GestureEvent) + 'static,
    {
        let state = Box::new(TrampolineState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const TrampolineState as *mut c_void;
        unsafe {
            match kind {
                GestureKind::Tap => {
                    sys::UIGestureArea_OnTap(self.ptr, Some(trampoline), userdata)
                }
                GestureKind::DoubleTap => {
                    sys::UIGestureArea_OnDoubleTap(self.ptr, Some(trampoline), userdata)
                }
                GestureKind::LongPress => {
                    sys::UIGestureArea_OnLongPress(self.ptr, Some(trampoline), userdata)
                }
                GestureKind::LongPressProgress => {
                    sys::UIGestureArea_OnLongPressProgress(self.ptr, Some(trampoline), userdata)
                }
                GestureKind::DragStart => {
                    sys::UIGestureArea_OnDragStart(self.ptr, Some(trampoline), userdata)
                }
                GestureKind::DragChanged => {
                    sys::UIGestureArea_OnDragChanged(self.ptr, Some(trampoline), userdata)
                }
                GestureKind::DragEnded => {
                    sys::UIGestureArea_OnDragEnded(self.ptr, Some(trampoline), userdata)
                }
                GestureKind::Swipe => {
                    sys::UIGestureArea_OnSwipe(self.ptr, Some(trampoline), userdata)
                }
                GestureKind::HoverEnter => {
                    sys::UIGestureArea_OnHoverEnter(self.ptr, Some(trampoline), userdata)
                }
                GestureKind::HoverExit => {
                    sys::UIGestureArea_OnHoverExit(self.ptr, Some(trampoline), userdata)
                }
            };
        }
        self.callbacks[kind as usize] = Some(state);
        self
    }

    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UIGestureArea {
        self.ptr
    }

    pub fn into_widget(mut self) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::new(self.ptr as *mut _) }
    }

    pub fn into_widget_sized(mut self, width: f32, height: f32) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::with_size(self.ptr as *mut _, width, height) }
    }
}

impl Drop for GestureArea {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UIGestureArea_Destroy(self.ptr) };
            return;
        }
        for slot in self.callbacks.iter_mut() {
            if let Some(state) = slot.take() {
                crate::widget::attach_owned(self.ptr as *const c_void, state);
            }
        }
    }
}
