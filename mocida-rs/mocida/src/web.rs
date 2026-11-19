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

/// True when the page is running on iOS / iPadOS Safari (or similar).
pub fn is_ios() -> bool {
    unsafe { sys::UIWeb_IsIOS() != 0 }
}

/// True when the page is running on Android.
pub fn is_android() -> bool {
    unsafe { sys::UIWeb_IsAndroid() != 0 }
}

/// True when the visitor asked for reduced motion.
pub fn prefers_reduced_motion() -> bool {
    unsafe { sys::UIWeb_PrefersReducedMotion() != 0 }
}

/// CSS viewport size in CSS pixels (`visualViewport` when available).
pub fn viewport() -> (i32, i32) {
    let mut w = 0i32;
    let mut h = 0i32;
    unsafe { sys::UIWeb_GetViewport(&mut w, &mut h) };
    (w, h)
}

/// CSS `env(safe-area-inset-*)` as (top, right, bottom, left).
pub fn safe_area() -> (i32, i32, i32, i32) {
    let mut t = 0i32;
    let mut r = 0i32;
    let mut b = 0i32;
    let mut l = 0i32;
    unsafe { sys::UIWeb_GetSafeArea(&mut t, &mut r, &mut b, &mut l) };
    (t, r, b, l)
}

/// `devicePixelRatio` clamped to 1..=3.
pub fn pixel_ratio() -> f32 {
    unsafe { sys::UIWeb_PixelRatio() }
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
