//! `UICrash_*` — crash handler installation + manual report.

use std::ffi::{c_void, CStr, CString};

use mocida_sys as sys;

use crate::error::Result;

/// Installs the process-wide crash handler (already called by
/// `UIApp_Create`, but exposed in case you want to install it
/// manually from a non-`App` setup).
pub fn install() {
    unsafe { sys::UICrash_Install() };
}

/// Removes the installed handler.
pub fn uninstall() {
    unsafe { sys::UICrash_Uninstall() };
}

/// Overrides the default `mocida_crash_*.log` path. Pass `None` to
/// revert to the timestamped default.
pub fn set_log_file(path: Option<&str>) -> Result<()> {
    let raw = match path {
        Some(p) => Some(CString::new(p)?),
        None => None,
    };
    let p = raw.as_ref().map(|c| c.as_ptr()).unwrap_or(std::ptr::null());
    unsafe { sys::UICrash_SetLogFile(p) };
    Ok(())
}

/// Reads the current log file path.
pub fn log_file() -> Option<String> {
    let p = unsafe { sys::UICrash_GetLogFile() };
    if p.is_null() {
        None
    } else {
        unsafe { CStr::from_ptr(p) }.to_str().ok().map(str::to_owned)
    }
}

/// Stored callback state for [`set_callback`].
struct CallbackState {
    handler: Box<dyn FnMut(&str) + 'static>,
}

use std::sync::atomic::{AtomicPtr, Ordering};

static CRASH_CALLBACK: AtomicPtr<CallbackState> = AtomicPtr::new(std::ptr::null_mut());

/// Installs a callback invoked with the full report text before the
/// process exits. Keep the implementation minimal — process state is
/// suspect at this point.
pub fn set_callback<F>(handler: F)
where
    F: FnMut(&str) + 'static,
{
    let state = Box::into_raw(Box::new(CallbackState {
        handler: Box::new(handler),
    }));
    let prev = CRASH_CALLBACK.swap(state, Ordering::AcqRel);
    if !prev.is_null() {
        unsafe { drop(Box::from_raw(prev)) };
    }
    unsafe {
        sys::UICrash_SetCallback(Some(callback_trampoline), state as *mut c_void);
    }
}

/// Removes any callback installed via [`set_callback`].
pub fn clear_callback() {
    unsafe { sys::UICrash_SetCallback(None, std::ptr::null_mut()) };
    let prev = CRASH_CALLBACK.swap(std::ptr::null_mut(), Ordering::AcqRel);
    if !prev.is_null() {
        unsafe { drop(Box::from_raw(prev)) };
    }
}

/// Manually triggers a report without crashing. Does NOT terminate
/// the process.
pub fn dump_report(reason: &str) -> Result<()> {
    let c = CString::new(reason)?;
    unsafe { sys::UICrash_DumpReport(c.as_ptr()) };
    Ok(())
}

extern "C" fn callback_trampoline(report: *const std::ffi::c_char, user: *mut c_void) {
    if user.is_null() {
        return;
    }
    let state = unsafe { &mut *(user as *mut CallbackState) };
    let s = if report.is_null() {
        ""
    } else {
        unsafe { CStr::from_ptr(report) }.to_str().unwrap_or("")
    };
    (state.handler)(s);
}
