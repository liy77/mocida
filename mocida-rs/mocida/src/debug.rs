//! `UIDebug_*` — runtime log sink + level control.

use std::ffi::{c_void, CStr, CString};

use mocida_sys as sys;

use crate::error::Result;

/// Log levels mocida cares about (mirrors `UILogLevel`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum LogLevel {
    /// Extremely chatty; gated to debug builds.
    Trace = 0,
    /// Developer-only info; gated to debug builds.
    Debug = 1,
    /// User-relevant lifecycle / config.
    Info = 2,
    /// Something looks wrong but recoverable.
    Warn = 3,
    /// Failed operation; caller will see it.
    Error = 4,
    /// Unrecoverable; usually right before abort.
    Fatal = 5,
    /// Pass to [`set_level`] to suppress everything.
    Silent = 6,
}

impl LogLevel {
    fn from_raw(raw: sys::UILogLevel) -> Self {
        match raw.0 {
            0 => Self::Trace,
            1 => Self::Debug,
            2 => Self::Info,
            3 => Self::Warn,
            4 => Self::Error,
            5 => Self::Fatal,
            _ => Self::Silent,
        }
    }
}

/// Where mocida's log output goes.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum LogSink {
    /// `stderr` (and `stdout` on Windows for MSVC's debugger).
    Terminal = 0,
    /// Appended to an on-disk file.
    File = 1,
    /// Broadcast to clients connected to a TCP port.
    Socket = 2,
    /// Routed to a custom callback installed via [`set_handler`].
    Custom = 3,
    /// Sink disabled.
    None = 4,
}

impl LogSink {
    fn from_raw(raw: sys::UILogSink) -> Self {
        match raw.0 {
            0 => Self::Terminal,
            1 => Self::File,
            2 => Self::Socket,
            3 => Self::Custom,
            _ => Self::None,
        }
    }
}

/// Installs a global level filter.
pub fn set_level(level: LogLevel) {
    unsafe { sys::UIDebug_SetLevel(sys::UILogLevel(level as i32)) };
}

/// Reads the current global level filter.
pub fn level() -> LogLevel {
    LogLevel::from_raw(unsafe { sys::UIDebug_GetLevel() })
}

/// Toggles ANSI color escapes when writing to the terminal sink.
pub fn set_color_enabled(enabled: bool) {
    unsafe { sys::UIDebug_SetColorEnabled(enabled as i32) };
}

/// Reset to the default terminal sink. Returns `true` on success.
pub fn open_terminal() -> bool {
    unsafe { sys::UIDebug_OpenTerminal() != 0 }
}

/// Redirect logs to an append-mode file.
pub fn open_file(path: &str) -> Result<bool> {
    let c = CString::new(path)?;
    let rc = unsafe { sys::UIDebug_OpenFile(c.as_ptr()) };
    Ok(rc != 0)
}

/// Open a TCP listener on `127.0.0.1:port` and broadcast log lines
/// to every connected client. Returns `true` on success.
pub fn open_port(port: u16) -> bool {
    unsafe { sys::UIDebug_OpenPort(port) != 0 }
}

/// Returns the currently active sink.
pub fn sink() -> LogSink {
    LogSink::from_raw(unsafe { sys::UIDebug_GetSink() })
}

/// Returns the port if the socket sink is active, otherwise 0.
pub fn port() -> u16 {
    unsafe { sys::UIDebug_GetPort() }
}

/// Closes the active sink (reverts to terminal).
pub fn close() {
    unsafe { sys::UIDebug_Close() };
}

/// Flushes pending output (only meaningful for file / terminal sinks).
pub fn flush() {
    unsafe { sys::UIDebug_Flush() };
}

/// Stored handler state (kept alive once [`set_handler`] is called).
struct HandlerState {
    handler: Box<dyn FnMut(LogLevel, &str, &str, i32, &str, &str) + 'static>,
}

use std::sync::atomic::{AtomicPtr, Ordering};

static LOG_HANDLER: AtomicPtr<HandlerState> = AtomicPtr::new(std::ptr::null_mut());

/// Installs a custom log handler. Replaces any previous handler.
///
/// The closure receives `(level, category, file, line, func, message)`.
pub fn set_handler<F>(handler: F)
where
    F: FnMut(LogLevel, &str, &str, i32, &str, &str) + 'static,
{
    let state = Box::into_raw(Box::new(HandlerState {
        handler: Box::new(handler),
    }));
    let prev = LOG_HANDLER.swap(state, Ordering::AcqRel);
    if !prev.is_null() {
        // Safety: only this module writes to LOG_HANDLER, and we just
        // detached the previous value.
        unsafe { drop(Box::from_raw(prev)) };
    }
    unsafe {
        sys::UIDebug_SetHandler(Some(log_trampoline), state as *mut c_void);
    }
}

/// Removes the custom log handler installed by [`set_handler`].
pub fn clear_handler() {
    unsafe { sys::UIDebug_SetHandler(None, std::ptr::null_mut()) };
    let prev = LOG_HANDLER.swap(std::ptr::null_mut(), Ordering::AcqRel);
    if !prev.is_null() {
        unsafe { drop(Box::from_raw(prev)) };
    }
}

/// Emits a log line at the given level + category.
pub fn log(level: LogLevel, category: &str, message: &str) -> Result<()> {
    let cat = CString::new(category)?;
    let msg = CString::new(message)?;
    let fmt = CString::new("%s")?;
    let blank = CString::new("")?;
    unsafe {
        sys::UIDebug_Logf(
            sys::UILogLevel(level as i32),
            cat.as_ptr(),
            blank.as_ptr(),
            0,
            blank.as_ptr(),
            fmt.as_ptr(),
            msg.as_ptr(),
        );
    }
    Ok(())
}

/// Bumps the per-category live-resource counter (debug builds only).
pub fn track_alloc(category: &str) -> Result<()> {
    let c = CString::new(category)?;
    unsafe { sys::UIDebug_TrackAlloc(c.as_ptr()) };
    Ok(())
}

/// Decrements the per-category counter (debug builds only).
pub fn track_free(category: &str) -> Result<()> {
    let c = CString::new(category)?;
    unsafe { sys::UIDebug_TrackFree(c.as_ptr()) };
    Ok(())
}

/// Logs a WARN for every category with a non-zero balance.
pub fn report_leaks() {
    unsafe { sys::UIDebug_ReportLeaks() };
}

extern "C" fn log_trampoline(
    level: sys::UILogLevel,
    category: *const std::ffi::c_char,
    file: *const std::ffi::c_char,
    line: i32,
    func: *const std::ffi::c_char,
    message: *const std::ffi::c_char,
    user: *mut c_void,
) {
    if user.is_null() {
        return;
    }
    let state = unsafe { &mut *(user as *mut HandlerState) };
    let lvl = LogLevel::from_raw(level);
    let cat = unsafe_cstr(category);
    let f = unsafe_cstr(file);
    let fun = unsafe_cstr(func);
    let msg = unsafe_cstr(message);
    (state.handler)(lvl, cat, f, line, fun, msg);
}

fn unsafe_cstr<'a>(p: *const std::ffi::c_char) -> &'a str {
    if p.is_null() {
        return "";
    }
    unsafe { CStr::from_ptr(p) }.to_str().unwrap_or("")
}
