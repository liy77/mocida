//! Misc helpers from `extra.h`.

use std::ffi::CString;

use mocida_sys as sys;

/// Returns `true` when `url` parses as a syntactically valid
/// HTTP / HTTPS URL. Performs no I/O.
pub fn is_valid_url(url: &str) -> bool {
    let Ok(c) = CString::new(url) else {
        return false;
    };
    unsafe { sys::MOCIDA_IsValidURL(c.as_ptr()) != 0 }
}
