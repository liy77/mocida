//! HTTP from the native library: reachability, GET and a download that
//! reports progress.

use std::ffi::{CStr, CString};
use std::path::Path;

use mocida_sys as sys;

/// True when `url` answers.
pub fn reachable(url: &str) -> bool {
    let Ok(c) = CString::new(url) else { return false };
    unsafe { sys::UINet_Reachable(c.as_ptr()) != 0 }
}

/// GET body, or the error string from the last call when it fails.
pub fn get(url: &str) -> Result<Vec<u8>, String> {
    let c = CString::new(url).map_err(|e| e.to_string())?;
    let mut data = std::ptr::null_mut();
    let mut size = 0;
    let ok = unsafe { sys::UINet_Get(c.as_ptr(), &mut data, &mut size) };
    if ok == 0 {
        return Err(last_error());
    }
    if data.is_null() {
        return Ok(Vec::new());
    }
    let bytes = unsafe { std::slice::from_raw_parts(data as *const u8, size.max(0) as usize) }.to_vec();
    unsafe { mocida_sys::SDL_free(data as *mut _) };
    Ok(bytes)
}

/// Saves `url` to `path`. Poll [`progress`] while a longer transfer runs
/// from another call site; this call blocks until the file is written.
pub fn download(url: &str, path: &Path) -> Result<(), String> {
    let u = CString::new(url).map_err(|e| e.to_string())?;
    let p = CString::new(path.to_string_lossy().as_bytes()).map_err(|e| e.to_string())?;
    let ok = unsafe { sys::UINet_Download(u.as_ptr(), p.as_ptr()) };
    if ok == 0 { Err(last_error()) } else { Ok(()) }
}

/// 0..1 for the download in progress.
pub fn progress() -> f32 {
    unsafe { sys::UINet_Progress() }
}

/// Bytes received, and the total if the server sent one.
pub fn bytes() -> (i32, i32) {
    unsafe { (sys::UINet_BytesDone(), sys::UINet_BytesTotal()) }
}

fn last_error() -> String {
    unsafe {
        let p = sys::UINet_Error();
        if p.is_null() {
            "network error".into()
        } else {
            CStr::from_ptr(p).to_string_lossy().into_owned()
        }
    }
}
