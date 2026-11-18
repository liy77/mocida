//! DLSS, FSR and a custom upscaler. The vendor library is loaded from a
//! path the project installed. Mocidad does not ship those binaries.

use std::ffi::{CStr, CString};
use std::path::Path;

use mocida_sys as sys;

pub const OFF: i32 = 0;
pub const DLSS: i32 = 1;
pub const FSR: i32 = 2;
pub const CUSTOM: i32 = 3;
/// Apple MetalFX. Built into macOS 13 and later. No package to install.
pub const METALFX: i32 = 4;

pub const NATIVE: i32 = 0;
pub const ULTRA_QUALITY: i32 = 1;
pub const QUALITY: i32 = 2;
pub const BALANCED: i32 = 3;
pub const PERFORMANCE: i32 = 4;
pub const ULTRA_PERFORMANCE: i32 = 5;

/// Loads a DLSS, FSR or custom library. `mode` is [`DLSS`], [`FSR`] or [`CUSTOM`].
pub fn load(path: &Path, mode: i32) -> Result<(), String> {
    let c = CString::new(path.to_string_lossy().as_bytes()).map_err(|e| e.to_string())?;
    let ok = unsafe { sys::UIUpscale_Load(c.as_ptr(), mode) };
    if ok == 0 { Err(last_error()) } else { Ok(()) }
}

pub fn unload(mode: i32) {
    unsafe { sys::UIUpscale_Unload(mode) };
}

pub fn available(mode: i32) -> bool {
    unsafe { sys::UIUpscale_Available(mode) != 0 }
}

/// Applies the mode. Returns whether the library is ready (or the mode is off).
pub fn configure(mode: i32, quality: i32, sharpness: f32, display_w: i32, display_h: i32) -> Result<(), String> {
    let ok = unsafe { sys::UIUpscale_Configure(mode, quality, sharpness, display_w, display_h) };
    if ok == 0 { Err(last_error()) } else { Ok(()) }
}

pub fn render_size() -> (i32, i32) {
    let mut w = 0;
    let mut h = 0;
    unsafe { sys::UIUpscale_RenderSize(&mut w, &mut h) };
    (w, h)
}

pub fn scale() -> f32 {
    unsafe { sys::UIUpscale_Scale() }
}

pub fn library(mode: i32) -> String {
    unsafe {
        let p = sys::UIUpscale_Library(mode);
        if p.is_null() { String::new() } else { CStr::from_ptr(p).to_string_lossy().into_owned() }
    }
}

fn last_error() -> String {
    unsafe {
        let p = sys::UIUpscale_Error();
        if p.is_null() || *p == 0 {
            "upscaler failed".into()
        } else {
            CStr::from_ptr(p).to_string_lossy().into_owned()
        }
    }
}
