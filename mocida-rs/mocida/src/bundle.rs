//! Virtual asset bundling (`mocida://`) and the `app.bundle` manifest.
//!
//! Register assets so the same code finds them on desktop and inside the
//! sandboxed iOS app bundle. Any path-taking API ([`crate::Image`],
//! `Text::font_family`, …) accepts a `mocida://` URI.
//!
//! ```no_run
//! use mocida::bundle;
//! bundle::set("mocida://font.ttf", "assets/Inter.ttf");
//! // ...later, anywhere a path is expected:
//! //   Text::new("hi", 18.0).font_family("mocida://font.ttf")
//! ```

use std::ffi::{CStr, CString};
use std::os::raw::c_char;

use mocida_sys as sys;

/// Registers a virtual asset: maps a `mocida://key` URI to a real file.
/// The scheme is optional in `virtual_uri` ("font.ttf" works too).
pub fn set(virtual_uri: &str, real_path: &str) {
    if let (Ok(v), Ok(r)) = (CString::new(virtual_uri), CString::new(real_path)) {
        unsafe { sys::UIApp_SetBundle(v.as_ptr(), r.as_ptr()) }
    }
}

/// Resolves a possibly-virtual path to a real, openable one. Non-`mocida://`
/// inputs return unchanged; returns `None` if a key can't be resolved.
pub fn resolve(uri: &str) -> Option<String> {
    let c = CString::new(uri).ok()?;
    let p = unsafe { sys::UIApp_ResolveBundle(c.as_ptr()) };
    cstr_to_string(p)
}

/// Loads an `app.bundle` JSON manifest (`{name, id, assets{}}`). Returns
/// `true` on success. `App::new` already auto-loads `./app.bundle`.
pub fn load_manifest(path: &str) -> bool {
    match CString::new(path) {
        Ok(c) => unsafe { sys::UIApp_LoadBundleManifest(c.as_ptr()) != 0 },
        Err(_) => false,
    }
}

/// Sets the app / bundle display name (shown as the app name).
pub fn set_name(name: &str) {
    if let Ok(c) = CString::new(name) {
        unsafe { sys::UIApp_SetBundleName(c.as_ptr()) }
    }
}

/// Current bundle / app name (from the manifest or [`set_name`]).
pub fn name() -> Option<String> {
    cstr_to_string(unsafe { sys::UIApp_GetBundleName() })
}

/// Current bundle identifier.
pub fn id() -> Option<String> {
    cstr_to_string(unsafe { sys::UIApp_GetBundleId() })
}

fn cstr_to_string(p: *const c_char) -> Option<String> {
    if p.is_null() {
        None
    } else {
        Some(unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned())
    }
}
