//! Tween + timeline animations (`UIAnim_*` / `UIAnimTimeline_*`).
//!
//! Float tweens drive widget fields (opacity, position, size). Timelines
//! schedule many tracks with delays, stagger, loops, and callbacks —
//! shared by UI transitions and 2.5D scene work.

use std::ffi::c_void;

use mocida_sys as sys;

use crate::error::{Error, Result};

/// Easing curve (Robert Penner + extras).
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
    /// Sine ease-in.
    InSine = 9,
    /// Sine ease-out.
    OutSine = 10,
    /// Symmetric sine.
    InOutSine = 11,
    /// Exponential ease-in.
    InExpo = 12,
    /// Exponential ease-out.
    OutExpo = 13,
    /// Symmetric exponential.
    InOutExpo = 14,
    /// Bounce settle.
    OutBounce = 15,
    /// Symmetric overshoot.
    InOutBack = 16,
}

/// Registers a tween. Returns `true` on success.
///
/// # Safety
/// `target` must point to a `f32` that lives at least as long as the
/// animation.
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
    let (cb, userdata) = pack_done(on_done);
    let rc = unsafe {
        sys::UIAnim_To(
            target,
            to,
            duration_ms,
            sys::UIEase(ease as _),
            cb,
            userdata,
        )
    };
    rc == 1
}

/// From/to tween with an optional delay before interpolation starts.
///
/// # Safety
/// Same as [`to`].
pub unsafe fn from_to<F>(
    target: *mut f32,
    from: f32,
    to: f32,
    duration_ms: u32,
    delay_ms: u32,
    ease: Ease,
    on_done: Option<F>,
) -> bool
where
    F: FnOnce() + 'static,
{
    let (cb, userdata) = pack_done(on_done);
    let rc = unsafe {
        sys::UIAnim_FromTo(
            target,
            from,
            to,
            duration_ms,
            delay_ms,
            sys::UIEase(ease as _),
            cb,
            userdata,
        )
    };
    rc == 1
}

/// Cancels any tween targeting `target`.
///
/// # Safety
/// `target` must be null or a valid pointer previously passed to [`to`].
pub unsafe fn cancel(target: *mut f32) {
    unsafe { sys::UIAnim_Cancel(target) };
}

/// Whether a tween is currently driving `target`.
///
/// # Safety
/// `target` must be null or a valid `*const f32`.
pub unsafe fn is_animating(target: *const f32) -> bool {
    unsafe { sys::UIAnim_IsAnimating(target) == 1 }
}

/// Pause every tween and timeline.
pub fn pause() {
    unsafe { sys::UIAnim_Pause() };
}

/// Resume after [`pause`].
pub fn resume() {
    unsafe { sys::UIAnim_Resume() };
}

/// Global pause state.
pub fn is_paused() -> bool {
    unsafe { sys::UIAnim_IsPaused() == 1 }
}

/// Playback rate (1.0 = realtime).
pub fn set_time_scale(scale: f32) {
    unsafe { sys::UIAnim_SetTimeScale(scale) };
}

/// Current playback rate.
pub fn time_scale() -> f32 {
    unsafe { sys::UIAnim_GetTimeScale() }
}

/// Frees every live tween.
pub fn clear_all() {
    unsafe { sys::UIAnim_ClearAll() };
}

/// Multi-track animation timeline.
pub struct Timeline {
    ptr: *mut sys::UIAnimTimeline,
}

impl Timeline {
    /// Create an empty timeline.
    pub fn new() -> Result<Self> {
        let ptr = unsafe { sys::UIAnimTimeline_Create() };
        if ptr.is_null() {
            return Err(Error::Null("UIAnimTimeline_Create"));
        }
        Ok(Self { ptr })
    }

    /// Schedule a float track.
    ///
    /// # Safety
    /// `target` must outlive the timeline playback.
    pub unsafe fn add(
        &mut self,
        target: *mut f32,
        to: f32,
        start_ms: u32,
        duration_ms: u32,
        ease: Ease,
    ) -> Result<i32> {
        let idx = unsafe {
            sys::UIAnimTimeline_Add(
                self.ptr,
                target,
                to,
                start_ms,
                duration_ms,
                sys::UIEase(ease as _),
            )
        };
        if idx < 0 {
            Err(Error::Null("UIAnimTimeline_Add"))
        } else {
            Ok(idx)
        }
    }

    /// From/to track with an explicit start value.
    ///
    /// # Safety
    /// Same as [`Timeline::add`].
    pub unsafe fn add_from_to(
        &mut self,
        target: *mut f32,
        from: f32,
        to: f32,
        start_ms: u32,
        duration_ms: u32,
        ease: Ease,
    ) -> Result<i32> {
        let idx = unsafe {
            sys::UIAnimTimeline_AddFromTo(
                self.ptr,
                target,
                from,
                to,
                start_ms,
                duration_ms,
                sys::UIEase(ease as _),
            )
        };
        if idx < 0 {
            Err(Error::Null("UIAnimTimeline_AddFromTo"))
        } else {
            Ok(idx)
        }
    }

    /// Fire `on_done` once when the clock reaches `at_ms`.
    pub fn add_callback<F>(&mut self, at_ms: u32, on_done: F) -> Result<i32>
    where
        F: FnOnce() + 'static,
    {
        let (cb, userdata) = pack_done(Some(on_done));
        let idx = unsafe { sys::UIAnimTimeline_AddCallback(self.ptr, at_ms, cb, userdata) };
        if idx < 0 {
            Err(Error::Null("UIAnimTimeline_AddCallback"))
        } else {
            Ok(idx)
        }
    }

    /// Stagger many targets with a shared destination value.
    ///
    /// # Safety
    /// Every pointer in `targets` must outlive playback.
    pub unsafe fn stagger(
        &mut self,
        targets: &mut [*mut f32],
        to: f32,
        duration_ms: u32,
        base_delay_ms: u32,
        stagger_ms: u32,
        ease: Ease,
    ) -> i32 {
        unsafe {
            sys::UIAnimTimeline_Stagger(
                self.ptr,
                targets.as_mut_ptr(),
                targets.len() as i32,
                to,
                duration_ms,
                base_delay_ms,
                stagger_ms,
                sys::UIEase(ease as _),
            )
        }
    }

    /// Loop when the last track ends.
    pub fn set_loop(&mut self, loop_: bool) -> &mut Self {
        unsafe { sys::UIAnimTimeline_SetLoop(self.ptr, loop_ as i32) };
        self
    }

    /// Start (or restart) playback. Ticked automatically by the app loop.
    pub fn play(&mut self) -> &mut Self {
        unsafe { sys::UIAnimTimeline_Play(self.ptr) };
        self
    }

    /// Stop and rewind.
    pub fn stop(&mut self) -> &mut Self {
        unsafe { sys::UIAnimTimeline_Stop(self.ptr) };
        self
    }

    /// Pause this timeline only.
    pub fn pause(&mut self) -> &mut Self {
        unsafe { sys::UIAnimTimeline_Pause(self.ptr) };
        self
    }

    /// Resume after [`Timeline::pause`].
    pub fn resume(&mut self) -> &mut Self {
        unsafe { sys::UIAnimTimeline_Resume(self.ptr) };
        self
    }

    /// Whether this timeline is actively advancing.
    pub fn is_playing(&self) -> bool {
        unsafe { sys::UIAnimTimeline_IsPlaying(self.ptr) == 1 }
    }

    /// Current clock in milliseconds.
    pub fn time_ms(&self) -> u32 {
        unsafe { sys::UIAnimTimeline_GetTime(self.ptr) }
    }
}

impl Drop for Timeline {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { sys::UIAnimTimeline_Destroy(self.ptr) };
            self.ptr = std::ptr::null_mut();
        }
    }
}

fn pack_done<F>(
    on_done: Option<F>,
) -> (
    Option<unsafe extern "C" fn(*mut c_void)>,
    *mut c_void,
)
where
    F: FnOnce() + 'static,
{
    if let Some(handler) = on_done {
        let state = Box::into_raw(Box::new(DoneState {
            handler: Some(Box::new(handler)),
        }));
        (
            Some(done_trampoline as unsafe extern "C" fn(*mut c_void)),
            state as *mut c_void,
        )
    } else {
        (None, std::ptr::null_mut())
    }
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
