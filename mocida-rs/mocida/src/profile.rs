//! `UIProfile_*` — Chrome-trace-compatible profiler.

use std::ffi::CString;

use mocida_sys as sys;

use crate::error::Result;

/// Per-frame stats sampled by mocida's main loop.
#[derive(Debug, Clone, Copy, Default)]
pub struct FrameStats {
    /// Last frame wall-clock time, in milliseconds.
    pub frame_time_ms: f64,
    /// Accumulated time in `event` scopes.
    pub event_time_ms: f64,
    /// Accumulated time in `layout` scopes.
    pub layout_time_ms: f64,
    /// Accumulated time in `render` scopes.
    pub render_time_ms: f64,
    /// Accumulated time in `present` scopes.
    pub present_time_ms: f64,
    /// Exponentially-smoothed FPS (alpha = 0.1).
    pub fps_smoothed: f64,
    /// Monotonic frame counter since process start.
    pub frame_count: u32,
    /// Draw calls reported via [`add_draw_calls`].
    pub last_draw_calls: u32,
}

/// Enables or disables profiling entirely.
pub fn set_enabled(enabled: bool) {
    unsafe { sys::UIProfile_SetEnabled(enabled as i32) };
}

/// Whether profiling is currently enabled.
pub fn is_enabled() -> bool {
    unsafe { sys::UIProfile_IsEnabled() != 0 }
}

/// Starts a trace recording. `max_events = 0` selects the 65536 default.
/// Returns `true` on success.
pub fn trace_start(max_events: usize) -> bool {
    unsafe { sys::UIProfile_TraceStart(max_events) != 0 }
}

/// Stops the active trace recording.
pub fn trace_stop() {
    unsafe { sys::UIProfile_TraceStop() };
}

/// Saves the current trace to a Chrome-tracing JSON file. Returns
/// `true` on success.
pub fn trace_save(path: &str) -> Result<bool> {
    let c = CString::new(path)?;
    let rc = unsafe { sys::UIProfile_TraceSave(c.as_ptr()) };
    Ok(rc != 0)
}

/// Returns how many events the trace buffer currently holds.
pub fn trace_event_count() -> usize {
    unsafe { sys::UIProfile_TraceEventCount() }
}

/// Clears the trace buffer without stopping recording.
pub fn trace_clear() {
    unsafe { sys::UIProfile_TraceClear() };
}

/// Increments the draw-call counter (read by [`frame_stats`]).
pub fn add_draw_calls(n: u32) {
    unsafe { sys::UIProfile_AddDrawCalls(n) };
}

/// Reads the current frame stats.
pub fn frame_stats() -> FrameStats {
    let mut raw = sys::UIFrameStats::default();
    unsafe { sys::UIProfile_GetFrameStats(&mut raw) };
    FrameStats {
        frame_time_ms: raw.frameTimeMs,
        event_time_ms: raw.eventTimeMs,
        layout_time_ms: raw.layoutTimeMs,
        render_time_ms: raw.renderTimeMs,
        present_time_ms: raw.presentTimeMs,
        fps_smoothed: raw.fpsSmoothed,
        frame_count: raw.frameCount,
        last_draw_calls: raw.lastDrawCalls,
    }
}

/// RAII profiler scope. Records a `(name, category, duration)` event
/// when dropped.
pub struct Scope {
    start_ticks: u64,
    name: CString,
    category: Option<CString>,
}

impl Scope {
    /// Begins a new scope.
    pub fn begin(name: &str) -> Result<Self> {
        let name = CString::new(name)?;
        let start = unsafe { sys::SDL_GetPerformanceCounter() };
        Ok(Self {
            start_ticks: start,
            name,
            category: None,
        })
    }

    /// Begins a new scope tagged with a category (e.g. `"render"`).
    pub fn begin_with_category(name: &str, category: &str) -> Result<Self> {
        let name = CString::new(name)?;
        let category = CString::new(category)?;
        let start = unsafe { sys::SDL_GetPerformanceCounter() };
        Ok(Self {
            start_ticks: start,
            name,
            category: Some(category),
        })
    }
}

impl Drop for Scope {
    fn drop(&mut self) {
        let cat = self
            .category
            .as_ref()
            .map(|c| c.as_ptr())
            .unwrap_or(std::ptr::null());
        unsafe { sys::UIProfile_Record(self.name.as_ptr(), cat, self.start_ticks) };
    }
}
