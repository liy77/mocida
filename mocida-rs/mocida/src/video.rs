//! `UIVideo` — Media-Foundation-backed video widget (Windows-only).

use std::ffi::{c_void, CString};

use mocida_sys as sys;

use crate::error::{Error, Result};
use crate::image::FillMode;
use crate::widget::Widget;

/// Video widget. Streaming decode happens lazily inside the renderer
/// tick; you just configure playback state from Rust.
pub struct Video {
    ptr: *mut sys::UIVideo,
    moved: bool,
    on_ended: Option<Box<EndedState>>,
}

struct EndedState {
    handler: Box<dyn FnMut() + 'static>,
}

impl Video {
    /// Loads a video file. Heavy init (MF startup, codec probe)
    /// happens here; the first frame is decoded on the next render.
    pub fn load(path: &str) -> Result<Self> {
        let c = CString::new(path)?;
        let ptr = unsafe { sys::UIVideo_Create(c.as_ptr()) };
        if ptr.is_null() {
            return Err(Error::Null("UIVideo_Create"));
        }
        Ok(Self {
            ptr,
            moved: false,
            on_ended: None,
        })
    }

    /// Starts (or resumes) playback.
    pub fn play(&mut self) {
        unsafe { sys::UIVideo_Play(self.ptr) };
    }

    /// Pauses playback.
    pub fn pause(&mut self) {
        unsafe { sys::UIVideo_Pause(self.ptr) };
    }

    /// Stops playback and seeks back to 0.
    pub fn stop(&mut self) {
        unsafe { sys::UIVideo_Stop(self.ptr) };
    }

    /// Enables or disables looping.
    pub fn loop_playback(self, enabled: bool) -> Self {
        unsafe { sys::UIVideo_SetLoop(self.ptr, enabled as i32) };
        self
    }

    /// Volume in `0.0..=1.0` (anything above 1.0 amplifies).
    pub fn volume(self, volume: f32) -> Self {
        unsafe { sys::UIVideo_SetVolume(self.ptr, volume) };
        self
    }

    /// Picks a fill mode (same enum as [`Image`](crate::Image)).
    pub fn fill_mode(self, mode: FillMode) -> Self {
        unsafe { sys::UIVideo_SetFillMode(self.ptr, sys::UIFillMode(mode as i32)) };
        self
    }

    /// Mutes or unmutes the audio track.
    pub fn muted(self, muted: bool) -> Self {
        unsafe { sys::UIVideo_SetMuted(self.ptr, muted as i32) };
        self
    }

    /// Registers a callback fired when playback reaches the end.
    pub fn on_ended<F>(mut self, handler: F) -> Self
    where
        F: FnMut() + 'static,
    {
        let state = Box::new(EndedState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const EndedState as *mut c_void;
        unsafe { sys::UIVideo_OnEnded(self.ptr, Some(ended_trampoline), userdata) };
        self.on_ended = Some(state);
        self
    }

    /// True while audio/video is currently playing.
    pub fn is_playing(&self) -> bool {
        unsafe { sys::UIVideo_IsPlaying(self.ptr) != 0 }
    }

    /// Native width of the video stream in pixels.
    pub fn width(&self) -> i32 {
        unsafe { sys::UIVideo_GetWidth(self.ptr) }
    }

    /// Native height of the video stream in pixels.
    pub fn height(&self) -> i32 {
        unsafe { sys::UIVideo_GetHeight(self.ptr) }
    }

    /// Current playback position in seconds.
    pub fn time(&self) -> f64 {
        unsafe { sys::UIVideo_GetTime(self.ptr) }
    }

    /// Total length in seconds (0 when unknown).
    pub fn duration(&self) -> f64 {
        unsafe { sys::UIVideo_GetDuration(self.ptr) }
    }

    /// Seeks to `seconds` from the start.
    pub fn seek(&mut self, seconds: f64) {
        unsafe { sys::UIVideo_Seek(self.ptr, seconds) };
    }

    /// Borrow the raw `UIVideo*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UIVideo {
        self.ptr
    }

    /// Lift into a [`Widget`] with an explicit size.
    pub fn into_widget_sized(mut self, width: f32, height: f32) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::with_size(self.ptr as *mut _, width, height) }
    }
}

impl Drop for Video {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UIVideo_Destroy(self.ptr) };
            return;
        }
        if let Some(state) = self.on_ended.take() {
            std::mem::forget(state);
        }
    }
}

extern "C" fn ended_trampoline(_v: *mut sys::UIVideo, userdata: *mut c_void) {
    if userdata.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut EndedState) };
    (state.handler)();
}
