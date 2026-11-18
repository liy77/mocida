//! Cookies, cache preference and the device the page is open on.
//! Desktop builds report a desktop with no cookies.

use std::ffi::{CStr, CString};

use mocida_sys as sys;

/// True when this binary is the web build.
pub fn is_web() -> bool {
    unsafe { sys::UIWeb_IsWeb() != 0 }
}

/// `phone`, `tablet` or `desktop`.
pub fn device() -> String {
    unsafe {
        let p = sys::UIWeb_Device();
        if p.is_null() { "desktop".into() } else { CStr::from_ptr(p).to_string_lossy().into_owned() }
    }
}

pub fn is_touch() -> bool {
    unsafe { sys::UIWeb_IsTouch() != 0 }
}

pub fn user_agent() -> String {
    unsafe {
        let p = sys::UIWeb_UserAgent();
        if p.is_null() { String::new() } else { CStr::from_ptr(p).to_string_lossy().into_owned() }
    }
}

/// Stores a cookie. `max_age_seconds` of 0 deletes it.
pub fn set_cookie(name: &str, value: &str, max_age_seconds: i32) -> bool {
    let Ok(n) = CString::new(name) else { return false };
    let Ok(v) = CString::new(value) else { return false };
    unsafe { sys::UIWeb_SetCookie(n.as_ptr(), v.as_ptr(), max_age_seconds) != 0 }
}

pub fn cookie(name: &str) -> String {
    let Ok(n) = CString::new(name) else { return String::new() };
    unsafe {
        let p = sys::UIWeb_GetCookie(n.as_ptr());
        if p.is_null() { String::new() } else { CStr::from_ptr(p).to_string_lossy().into_owned() }
    }
}

pub fn delete_cookie(name: &str) {
    let Ok(n) = CString::new(name) else { return };
    unsafe { sys::UIWeb_DeleteCookie(n.as_ptr()) };
}

/// True unless the visitor turned caching off.
pub fn cache_enabled() -> bool {
    unsafe { sys::UIWeb_CacheEnabled() != 0 }
}

pub fn set_cache_enabled(enabled: bool) {
    unsafe { sys::UIWeb_SetCacheEnabled(if enabled { 1 } else { 0 }) };
}
