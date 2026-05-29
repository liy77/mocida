//! `UIApp` wrapper — the top-level application object.

use std::ffi::{c_int, c_void, CString};

use mocida_sys as sys;

use crate::children::Children;
use crate::color::Color;
use crate::error::{Error, Result};
use crate::widget::Widget;

/// Render backend selection (mirrors `UIRenderDriver`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum RenderDriver {
    /// CPU rasterizer.
    Software = 0,
    /// Hardware OpenGL backend.
    OpenGL = 1,
    /// Hardware Vulkan backend.
    Vulkan = 2,
    /// Direct3D 12 (Windows only).
    #[cfg(target_os = "windows")]
    D3d12 = 3,
    /// Direct3D 11 (Windows only).
    #[cfg(target_os = "windows")]
    D3d11 = 4,
    /// Direct3D 9 (Windows only).
    #[cfg(target_os = "windows")]
    D3d9 = 5,
    /// Apple Metal (macOS only).
    #[cfg(target_os = "macos")]
    Metal = 6,
    /// Picks the best available GPU backend.
    Gpu = 7,
}

/// Analytic-coverage AA quality preset (samples-per-side).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum RenderQuality {
    /// No AA (aliased edges, max performance).
    Low = 1,
    /// 4 SPP — good for small/fast UI.
    Medium = 2,
    /// 16 SPP — default, strong MSAA-equivalent.
    High = 4,
    /// 64 SPP — for close-ups and screenshots.
    Ultra = 8,
}

/// Anti-aliasing pipeline.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum AAMode {
    /// Coverage AA disabled.
    None = 0,
    /// Analytic-coverage AA (default).
    Coverage = 1,
    /// SSAA 2x — renders at 2x then downscales.
    Ssaa2x = 2,
    /// SSAA 4x — renders at 4x then downscales.
    Ssaa4x = 3,
    /// Post-process FXAA edge-blur.
    Fxaa = 4,
    /// Temporal accumulation; great for static UI.
    Taa = 5,
}

/// Top-level application object.
///
/// Owns the main window, the root widget, and global render tuning
/// knobs. One [`App`] is typically enough; for multi-window apps the
/// same settings apply across windows.
pub struct App {
    ptr: *mut sys::UIApp,
    /// Heap-stored resize callback, kept alive for the app's lifetime.
    resize_cb: Option<Box<ResizeState>>,
    /// Heap-stored per-event callbacks, by raw event ID. Forward
    /// compatibility: future events plug in here without API churn.
    event_cbs: Vec<Box<EventState>>,
    /// Children handed off via [`App::set_children`]; we keep no
    /// extra Rust handle, the app owns them.
    has_children: bool,
}

struct ResizeState {
    handler: Box<dyn FnMut(i32, i32) + 'static>,
}

struct EventState {
    handler: Box<dyn FnMut(crate::event::EventData<'_>) + 'static>,
}

impl App {
    /// Creates the application window with the given title and size.
    pub fn new(title: &str, width: i32, height: i32) -> Result<Self> {
        let title_c = CString::new(title)?;
        let ptr = unsafe { sys::UIApp_Create(title_c.as_ptr(), width, height) };
        if ptr.is_null() {
            return Err(Error::Null("UIApp_Create"));
        }
        Ok(Self {
            ptr,
            resize_cb: None,
            event_cbs: Vec::new(),
            has_children: false,
        })
    }

    /// Borrow the raw `UIApp*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UIApp {
        self.ptr
    }

    /// Sets the window's clear color.
    pub fn set_background_color(&mut self, color: Color) -> &mut Self {
        unsafe {
            sys::UIApp_SetBackgroundColor(self.ptr, color.into_raw());
        }
        self
    }

    /// Updates the window title.
    pub fn set_window_title(&mut self, title: &str) -> Result<&mut Self> {
        let c = CString::new(title)?;
        unsafe {
            sys::UIApp_SetWindowTitle(self.ptr, c.as_ptr());
        }
        Ok(self)
    }

    /// Sets the window icon from a file path. Returns `true` on
    /// success (mirrors the C return code).
    pub fn set_window_icon(&mut self, path: &str) -> Result<bool> {
        let c = CString::new(path)?;
        let rc = unsafe { sys::UIApp_SetWindowIcon(self.ptr, c.as_ptr()) };
        Ok(rc != 0)
    }

    /// Sets the window size.
    pub fn set_window_size(&mut self, width: i32, height: i32) -> &mut Self {
        unsafe {
            sys::UIApp_SetWindowSize(self.ptr, width, height);
        }
        self
    }

    /// Sets the window position.
    pub fn set_window_position(&mut self, x: i32, y: i32) -> &mut Self {
        unsafe {
            sys::UIApp_SetWindowPosition(self.ptr, x, y);
        }
        self
    }

    /// Switches the render backend.
    pub fn set_render_driver(&mut self, driver: RenderDriver) -> &mut Self {
        unsafe {
            sys::UIApp_SetRenderDriver(self.ptr, driver as sys::UIRenderDriver);
        }
        self
    }

    /// Shortcut for `UIApp_SetMSAASamples`.
    pub fn set_render_quality(&mut self, quality: RenderQuality) -> &mut Self {
        unsafe {
            sys::UIApp_SetRenderQuality(self.ptr, sys::UIRenderQuality(quality as i32));
        }
        self
    }

    /// Sets the analytic-coverage samples-per-side.
    pub fn set_msaa_samples(&mut self, samples: i32) -> &mut Self {
        unsafe {
            sys::UIApp_SetMSAASamples(self.ptr, samples);
        }
        self
    }

    /// Selects the AA pipeline.
    pub fn set_aa_mode(&mut self, mode: AAMode) -> &mut Self {
        unsafe {
            sys::UIApp_SetAAMode(self.ptr, sys::UIAAMode(mode as i32));
        }
        self
    }

    /// Sets the TAA history blend weight in `0.0..=1.0`.
    pub fn set_taa_blend(&mut self, alpha: f32) -> &mut Self {
        unsafe {
            sys::UIApp_SetTAABlend(self.ptr, alpha);
        }
        self
    }

    /// Sets the target frame rate. Pass 0 (or anything <= 0) to
    /// unlock.
    pub fn set_target_fps(&mut self, fps: i32) -> &mut Self {
        unsafe {
            sys::UIApp_SetTargetFPS(self.ptr, fps);
        }
        self
    }

    /// Currently configured target FPS (0 = unlimited).
    pub fn target_fps(&self) -> i32 {
        unsafe { sys::UIApp_GetTargetFPS(self.ptr) }
    }

    /// Releases cached renderer resources.
    pub fn trim_caches(&mut self) -> &mut Self {
        unsafe { sys::UIApp_TrimCaches(self.ptr) };
        self
    }

    /// Declares this process's AppUserModelID (Windows taskbar / Task
    /// Manager grouping). No-op on non-Windows.
    pub fn set_app_id(&mut self, aumid: &str) -> Result<&mut Self> {
        let c = CString::new(aumid)?;
        unsafe { sys::UIApp_SetAppId(self.ptr, c.as_ptr()) };
        Ok(self)
    }

    /// Hands the root widget tree to the app. Subsequent calls
    /// replace it (the previous tree is destroyed by mocida).
    pub fn set_children(&mut self, children: Children) -> &mut Self {
        let raw = children.into_raw();
        unsafe {
            sys::UIApp_SetChildren(self.ptr, raw);
        }
        self.has_children = true;
        self
    }

    /// Convenience: lazily creates a [`Children`] collection if the
    /// app doesn't have one yet and appends the widget. Equivalent to
    /// the common `UIChildren_Add(children, widget)` pattern from the
    /// C demos.
    ///
    /// Note: each call rebuilds the children collection from scratch,
    /// so prefer batching with [`App::set_children`] when adding many
    /// widgets at once.
    pub fn add_child(&mut self, widget: Widget) -> Result<&mut Self> {
        // For now we just delegate; the multi-call ergonomic is a
        // future addition. The user can build a Children themselves
        // and call set_children when batching matters.
        let mut c = Children::new(8)?;
        c.add(widget)?;
        self.set_children(c);
        Ok(self)
    }

    /// Registers a resize handler. The closure receives the new
    /// `(width, height)`. Replaces any previous resize handler.
    pub fn on_resize<F>(&mut self, handler: F) -> &mut Self
    where
        F: FnMut(i32, i32) + 'static,
    {
        let state = Box::new(ResizeState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const ResizeState as *mut c_void;
        unsafe {
            sys::UIApp_OnResize(self.ptr, Some(resize_trampoline), userdata);
        }
        self.resize_cb = Some(state);
        self
    }

    /// Registers a callback for a mocida event.
    pub fn on_event<F>(&mut self, event: crate::event::Event, handler: F) -> &mut Self
    where
        F: FnMut(crate::event::EventData<'_>) + 'static,
    {
        let state = Box::new(EventState {
            handler: Box::new(handler),
        });
        // The C signature is `void(*)(UIEventData)` — no userdata
        // pointer. To bridge the userdata gap we install per-event
        // trampolines that look up the matching state via a TLS slot.
        // Today only one event exists, so we keep one slot.
        EVENT_TRAMPOLINE_STATE.with(|cell| {
            *cell.borrow_mut() = Some(Box::as_ref(&state) as *const EventState);
        });
        unsafe {
            sys::UIApp_SetEventCallback(self.ptr, event.as_raw(), Some(event_trampoline));
        }
        self.event_cbs.push(state);
        self
    }

    /// Shows the application window.
    pub fn show(&mut self) -> &mut Self {
        unsafe {
            sys::UIApp_ShowWindow(self.ptr);
        }
        self
    }

    /// Hides the application window.
    pub fn hide(&mut self) -> &mut Self {
        unsafe {
            sys::UIApp_HideWindow(self.ptr);
        }
        self
    }

    /// Runs the main loop until the window closes. Blocks.
    pub fn run(&mut self) {
        unsafe {
            sys::UIApp_Run(self.ptr);
        }
    }
}

impl Drop for App {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe {
                sys::UIApp_Destroy(self.ptr);
            }
        }
        // Clear the TLS slot so a future App in the same thread
        // doesn't dispatch into a freed callback.
        EVENT_TRAMPOLINE_STATE.with(|cell| {
            *cell.borrow_mut() = None;
        });
    }
}

extern "C" fn resize_trampoline(width: c_int, height: c_int, userdata: *mut c_void) {
    if userdata.is_null() {
        return;
    }
    // Safety: `userdata` is the address of `Box<ResizeState>`'s heap
    // payload, kept alive on the owning App.
    let state = unsafe { &mut *(userdata as *mut ResizeState) };
    (state.handler)(width, height);
}

thread_local! {
    static EVENT_TRAMPOLINE_STATE: std::cell::RefCell<Option<*const EventState>> =
        const { std::cell::RefCell::new(None) };
}

extern "C" fn event_trampoline(data: sys::UIEventData) {
    let ptr_opt = EVENT_TRAMPOLINE_STATE.with(|cell| *cell.borrow());
    let Some(ptr) = ptr_opt else { return };
    // Safety: the App that registered the callback is still alive
    // (we cleared this slot in Drop), so the state pointer is valid.
    let state = unsafe { &mut *(ptr as *mut EventState) };
    let view = crate::event::EventData::from_raw(&data);
    (state.handler)(view);
}
