//! `UIAsset_*` — file lookup with mocida's CWD/exe-dir fallback chain.

use std::ffi::CString;

use mocida_sys as sys;

use crate::error::Result;

/// Loads an image as an `SDL_Surface*`. Caller owns the surface and
/// must free it with `SDL_DestroySurface` (or the wrapper of their
/// choice). Returns `None` when the file isn't found in any of
/// mocida's fallback directories.
pub fn load_surface(path: &str) -> Result<Option<*mut sys::SDL_Surface>> {
    let c = CString::new(path)?;
    let p = unsafe { sys::UIAsset_LoadSurface(c.as_ptr()) };
    Ok(if p.is_null() { None } else { Some(p) })
}

/// Loads an image as an `SDL_Texture*` bound to the given renderer.
///
/// # Safety
/// `renderer` must be a valid `SDL_Renderer*`. The returned texture
/// is owned by the caller.
pub unsafe fn load_texture(
    renderer: *mut sys::SDL_Renderer,
    path: &str,
) -> Result<Option<*mut sys::SDL_Texture>> {
    let c = CString::new(path)?;
    let p = unsafe { sys::UIAsset_LoadTexture(renderer, c.as_ptr()) };
    Ok(if p.is_null() { None } else { Some(p) })
}
