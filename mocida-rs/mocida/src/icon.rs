//! `UIIcon` — SF Symbol (Apple) / PNG fallback (elsewhere) / font glyph.

use std::ffi::CString;

use mocida_sys as sys;

use crate::color::Color;
use crate::error::{Error, Result};
use crate::image::Image;

/// High-level icon helper. Produces a regular [`Image`] so it shares the
/// existing render / tint / sizing path.
///
/// On Apple, bare names like `play.fill` become SF Symbols (`sf://…`).
/// On Android / Windows / Linux they resolve to `fallback` or a built-in
/// `mocida://icons/…` map — never SF Symbols.
pub struct Icon;

impl Icon {
    /// Resolve `name` (+ optional `fallback`) the same way C does.
    pub fn resolve_source(name: &str, fallback: Option<&str>) -> Option<String> {
        let c = CString::new(name).ok()?;
        let f = fallback.and_then(|s| CString::new(s).ok());
        let p = unsafe {
            sys::UIIcon_ResolveSourceEx(
                c.as_ptr(),
                f.as_ref().map(|x| x.as_ptr()).unwrap_or(std::ptr::null()),
            )
        };
        if p.is_null() {
            return None;
        }
        let s = unsafe { std::ffi::CStr::from_ptr(p) }
            .to_string_lossy()
            .into_owned();
        unsafe { libc_free(p as *mut _) };
        Some(s)
    }

    /// Create a tinted FIT image (SF on Apple, PNG map elsewhere).
    pub fn new(name: &str, tint: Color) -> Result<Image> {
        Icon::with_fallback(name, None, tint)
    }

    /// Create with an explicit PNG/`mocida://` fallback for non-Apple.
    pub fn with_fallback(name: &str, fallback: Option<&str>, tint: Color) -> Result<Image> {
        let c = CString::new(name)?;
        let f = fallback.map(CString::new).transpose()?;
        let ptr = unsafe {
            sys::UIIcon_CreateWithFallback(
                c.as_ptr(),
                f.as_ref().map(|x| x.as_ptr()).unwrap_or(std::ptr::null()),
                tint.into_raw(),
            )
        };
        if ptr.is_null() {
            return Err(Error::Null("UIIcon_CreateWithFallback"));
        }
        Ok(unsafe { Image::from_raw(ptr) })
    }

    /// Rasterise a UTF-8 glyph with `font_family` (all platforms).
    ///
    /// # Safety
    /// `renderer` must be a live `SDL_Renderer*`.
    pub unsafe fn from_glyph(
        renderer: *mut sys::SDL_Renderer,
        glyph: &str,
        font_family: Option<&str>,
        point_size: f32,
        tint: Color,
    ) -> Result<Image> {
        let g = CString::new(glyph)?;
        let fam = font_family.map(CString::new).transpose()?;
        let ptr = unsafe {
            sys::UIIcon_CreateGlyph(
                renderer,
                g.as_ptr(),
                fam.as_ref().map(|c| c.as_ptr()).unwrap_or(std::ptr::null()),
                point_size,
                tint.into_raw(),
            )
        };
        if ptr.is_null() {
            return Err(Error::Null("UIIcon_CreateGlyph"));
        }
        Ok(unsafe { Image::from_raw(ptr) })
    }

    /// Convenience: icon sized to a square.
    pub fn sized(name: &str, size: f32, tint: Color) -> Result<crate::widget::Widget> {
        Icon::new(name, tint)?.into_widget_sized(size, size)
    }
}

unsafe fn libc_free(p: *mut std::ffi::c_void) {
    extern "C" {
        fn free(p: *mut std::ffi::c_void);
    }
    free(p);
}
