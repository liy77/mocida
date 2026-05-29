//! Native file dialogs (`UIFileDialog_*`).
//!
//! Both [`open`] and [`save`] are asynchronous: the callback fires
//! from SDL's event pump when the user finishes the dialog.

use std::ffi::{c_void, CStr, CString};
use std::ptr;

use mocida_sys as sys;

use crate::error::Result;

/// Outcome of a file dialog. `None` = the user cancelled.
pub type Outcome = Option<String>;

/// Shows a native "Open file" dialog. The callback is invoked once
/// from the SDL event pump when the user finishes.
///
/// `filter_exts` is a semicolon-separated list of extensions without
/// the leading dot (e.g. `"png;jpg;jpeg"`). Pass `""` to allow all.
///
/// # Safety
/// `window` must be a valid `SDL_Window*` from a live `UIApp`. The
/// safe wrappers don't expose the SDL window directly; users that
/// need this API today must reach through `mocida::sys` for the
/// window pointer.
pub unsafe fn open<F>(
    window: *mut sys::SDL_Window,
    filter_desc: &str,
    filter_exts: &str,
    handler: F,
) -> Result<()>
where
    F: FnOnce(Outcome) + 'static,
{
    let desc = CString::new(filter_desc)?;
    let exts = CString::new(filter_exts)?;
    let state = Box::into_raw(Box::new(HandlerState {
        handler: Some(Box::new(handler)),
    }));
    unsafe {
        sys::UIFileDialog_OpenFile(
            window,
            if filter_desc.is_empty() { ptr::null() } else { desc.as_ptr() },
            if filter_exts.is_empty() { ptr::null() } else { exts.as_ptr() },
            Some(trampoline),
            state as *mut c_void,
        );
    }
    Ok(())
}

/// Shows a native "Save file" dialog. Same shape as [`open`].
///
/// # Safety
/// Same contract as [`open`].
pub unsafe fn save<F>(
    window: *mut sys::SDL_Window,
    filter_desc: &str,
    filter_exts: &str,
    handler: F,
) -> Result<()>
where
    F: FnOnce(Outcome) + 'static,
{
    let desc = CString::new(filter_desc)?;
    let exts = CString::new(filter_exts)?;
    let state = Box::into_raw(Box::new(HandlerState {
        handler: Some(Box::new(handler)),
    }));
    unsafe {
        sys::UIFileDialog_SaveFile(
            window,
            if filter_desc.is_empty() { ptr::null() } else { desc.as_ptr() },
            if filter_exts.is_empty() { ptr::null() } else { exts.as_ptr() },
            Some(trampoline),
            state as *mut c_void,
        );
    }
    Ok(())
}

struct HandlerState {
    handler: Option<Box<dyn FnOnce(Outcome) + 'static>>,
}

extern "C" fn trampoline(path: *const std::ffi::c_char, userdata: *mut c_void) {
    if userdata.is_null() {
        return;
    }
    // Reclaim ownership: the dialog fires exactly once, so this is
    // the right moment to free the handler box.
    let mut state = unsafe { Box::from_raw(userdata as *mut HandlerState) };
    let outcome: Outcome = if path.is_null() {
        None
    } else {
        unsafe { CStr::from_ptr(path) }.to_str().ok().map(str::to_owned)
    };
    if let Some(h) = state.handler.take() {
        h(outcome);
    }
}
