//! Tween-style animations (`UIAnim_*`).
//!
//! Drives a `f32` field from its current value to a target over a
//! duration using a Robert-Penner easing curve. Useful for animating
//! widget positions, opacity, sizes, ...

use std::ffi::c_void;

use mocida_sys as sys;

/// Easing curve.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum Ease {
    /// Constant velocity.
    Linear = 0,
    /// `t^2` (slow start).
    InQuad = 1,
    /// `1 - (1-t)^2` (slow end).
    OutQuad = 2,
    /// Symmetric quadratic.
    InOutQuad = 3,
    /// `t^3` (slow start).
    InCubic = 4,
    /// `1 - (1-t)^3` (slow end).
    OutCubic = 5,
    /// Symmetric cubic.
    InOutCubic = 6,
    /// Overshoots then settles.
    OutBack = 7,
    /// Spring-like settle.
    OutElastic = 8,
}

/// Registers a tween. Returns `true` on success, `false` if the
/// global tween table is full.
///
/// `target` is borrowed for the lifetime of the tween, so the caller
/// must guarantee it stays valid until `on_done` fires (or until
/// [`cancel`] is called for it).
///
/// # Safety
/// `target` must point to a `f32` that lives at least as long as the
/// animation. Concurrent mutation from other code is UB.
pub unsafe fn to<F>(
    target: *mut f32,
    to: f32,
    duration_ms: u32,
    ease: Ease,
    on_done: Option<F>,
) -> bool
where
    F: FnOnce() + 'static,
{
    let (cb, userdata) = if let Some(handler) = on_done {
        let state = Box::into_raw(Box::new(DoneState {
            handler: Some(Box::new(handler)),
        }));
        (
            Some(done_trampoline as unsafe extern "C" fn(*mut c_void)),
            state as *mut c_void,
        )
    } else {
        (None, std::ptr::null_mut())
    };

    let rc = unsafe {
        sys::UIAnim_To(
            target,
            to,
            duration_ms,
            sys::UIEase(ease as i32),
            cb,
            userdata,
        )
    };
    rc == 1
}

/// Cancels any tween targeting `target`. No-op when none exists.
///
/// # Safety
/// `target` must either be null or a valid `*mut f32` previously
/// passed to [`to`].
pub unsafe fn cancel(target: *mut f32) {
    unsafe { sys::UIAnim_Cancel(target) };
}

/// Frees every live tween.
pub fn clear_all() {
    unsafe { sys::UIAnim_ClearAll() };
}

struct DoneState {
    handler: Option<Box<dyn FnOnce() + 'static>>,
}

unsafe extern "C" fn done_trampoline(userdata: *mut c_void) {
    if userdata.is_null() {
        return;
    }
    let mut state = unsafe { Box::from_raw(userdata as *mut DoneState) };
    if let Some(h) = state.handler.take() {
        h();
    }
}
