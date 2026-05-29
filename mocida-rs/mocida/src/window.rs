//! `UIWindow` — backing window object that owns SDL state.
//!
//! Most users go through [`App`](crate::App), which creates a window
//! for you. This module exposes the lower-level surface for
//! multi-window setups, manual control of MSAA / TAA tuning, and
//! reaching the raw `SDL_Window*` / `SDL_Renderer*` for FFI.

use mocida_sys as sys;

/// Window display mode (mirrors `UIWindowDisplayMode`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum WindowDisplayMode {
    /// Floating, decorated window.
    Windowed = 0,
    /// Exclusive fullscreen.
    Fullscreen = 1,
    /// Borderless fullscreen.
    Borderless = 2,
}

/// Borrowed reference to the active `UIWindow`.
///
/// The active window is whatever was last created or assigned via
/// [`set_active`]. It's owned by mocida (the `App` does the
/// allocation/destruction), so this wrapper never frees anything.
#[derive(Debug, Clone, Copy)]
pub struct Window {
    ptr: *mut sys::UIWindow,
}

impl Window {
    /// Returns the active window, if any.
    pub fn active() -> Option<Self> {
        let ptr = unsafe { sys::UIWindow_GetActive() };
        if ptr.is_null() {
            None
        } else {
            Some(Self { ptr })
        }
    }

    /// Wraps a raw `UIWindow*`.
    ///
    /// # Safety
    /// `ptr` must point to a live `UIWindow` for the duration of the
    /// returned wrapper.
    #[inline]
    pub unsafe fn from_raw(ptr: *mut sys::UIWindow) -> Self {
        Self { ptr }
    }

    /// Borrow the raw pointer.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UIWindow {
        self.ptr
    }

    /// Logical width in pixels.
    pub fn width(&self) -> i32 {
        unsafe { (*self.ptr).width }
    }

    /// Logical height in pixels.
    pub fn height(&self) -> i32 {
        unsafe { (*self.ptr).height }
    }

    /// Last measured FPS.
    pub fn framerate(&self) -> f32 {
        unsafe { (*self.ptr).framerate }
    }

    /// Raw `SDL_Window*` (useful for [`file_dialog`](crate::file_dialog)).
    pub fn sdl_window(&self) -> *mut sys::SDL_Window {
        unsafe { (*self.ptr).sdlWindow }
    }

    /// Raw `SDL_Renderer*`.
    pub fn sdl_renderer(&self) -> *mut sys::SDL_Renderer {
        unsafe { (*self.ptr).sdlRenderer }
    }

    /// Manually triggers a render pass.
    pub fn render(&mut self) -> i32 {
        unsafe { sys::UIWindow_Render(self.ptr) }
    }
}

/// Set the active window pointer. Pass `None` to clear.
pub fn set_active(window: Option<&Window>) {
    let raw = window.map(|w| w.ptr).unwrap_or(std::ptr::null_mut());
    unsafe { sys::UIWindow_SetActive(raw) };
}

/// Global MSAA samples-per-side (forwarded to the renderer).
pub fn set_msaa_samples(samples: i32) {
    unsafe { sys::UIWindow_SetMSAASamples(samples) };
}

/// Reads the global MSAA samples-per-side.
pub fn msaa_samples() -> i32 {
    unsafe { sys::UIWindow_GetMSAASamples() }
}

/// Global AA pipeline mode (matches [`AAMode`](crate::AAMode) values).
pub fn set_aa_mode(mode: i32) {
    unsafe { sys::UIWindow_SetAAMode(mode) };
}

/// Reads the global AA mode.
pub fn aa_mode() -> i32 {
    unsafe { sys::UIWindow_GetAAMode() }
}

/// TAA history weight in `0.0..=1.0`.
pub fn set_taa_blend(alpha: f32) {
    unsafe { sys::UIWindow_SetTAABlend(alpha) };
}

/// Reads the TAA history weight.
pub fn taa_blend() -> f32 {
    unsafe { sys::UIWindow_GetTAABlend() }
}

/// TAA motion threshold (`0..=255`).
pub fn set_taa_motion_threshold(threshold: i32) {
    unsafe { sys::UIWindow_SetTAAMotionThreshold(threshold) };
}

/// Reads the TAA motion threshold.
pub fn taa_motion_threshold() -> i32 {
    unsafe { sys::UIWindow_GetTAAMotionThreshold() }
}

/// Frees every renderer-owned cache (circle/shadow textures, TAA
/// history, …). Next render rebuilds whatever is needed.
pub fn trim_caches() {
    unsafe { sys::UIWindow_TrimCaches() };
}
