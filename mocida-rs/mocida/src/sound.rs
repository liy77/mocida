//! `UISound` — one-shot audio clip.

use std::ffi::CString;

use mocida_sys as sys;

use crate::error::{Error, Result};

/// One-shot audio clip backed by SDL3's audio stream API.
pub struct Sound {
    ptr: *mut sys::UISound,
}

impl Sound {
    /// Loads a WAV file from disk.
    pub fn load_wav(path: &str) -> Result<Self> {
        let c = CString::new(path)?;
        let ptr = unsafe { sys::UISound_LoadWav(c.as_ptr()) };
        if ptr.is_null() {
            return Err(Error::Null("UISound_LoadWav"));
        }
        Ok(Self { ptr })
    }

    /// Plays from the start. Calling again while playing restarts.
    pub fn play(&mut self) {
        unsafe { sys::UISound_Play(self.ptr) };
    }

    /// Sets the gain (0.0 = silent, 1.0 = original, >1.0 amplifies).
    pub fn set_gain(&mut self, gain: f32) {
        unsafe { sys::UISound_SetGain(self.ptr, gain) };
    }

    /// Stops playback and clears the queue.
    pub fn stop(&mut self) {
        unsafe { sys::UISound_Stop(self.ptr) };
    }

    /// True while audio is still queued.
    pub fn is_playing(&self) -> bool {
        unsafe { sys::UISound_IsPlaying(self.ptr) != 0 }
    }

    /// Borrow the raw `UISound*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UISound {
        self.ptr
    }
}

impl Drop for Sound {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { sys::UISound_Destroy(self.ptr) };
        }
    }
}
