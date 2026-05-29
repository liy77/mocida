//! `UIWebViewDComp_*` — DirectComposition shim for WebView2 (Windows).
//!
//! This is mocida's lowest-level WebView2 helper: a C wrapper around
//! DirectComposition + Direct2D that the C++ side uses to compose
//! WebView2 as a DComp visual. Almost every method takes
//! window-pixel-space coordinates and an opaque `UIWebViewDComp`
//! handle returned by [`WebViewDComp::create`].

use std::ffi::{c_void, CString};

use mocida_sys as sys;

use crate::color::Color;
use crate::error::{Error, Result};

/// Opaque DirectComposition pipeline tied to a top-level HWND.
pub struct WebViewDComp {
    ptr: *mut sys::UIWebViewDComp,
}

impl WebViewDComp {
    /// Builds a DComp pipeline targeted at `hwnd`.
    ///
    /// # Safety
    /// `hwnd` must be a valid top-level HWND. The returned pipeline
    /// borrows the window — destroy it before the window goes away.
    #[cfg(target_os = "windows")]
    pub unsafe fn create(hwnd: sys::HWND) -> Result<Self> {
        let ptr = unsafe { sys::UIWebViewDComp_Create(hwnd) };
        if ptr.is_null() {
            return Err(Error::Null("UIWebViewDComp_Create"));
        }
        Ok(Self { ptr })
    }

    /// Returns the `IDCompositionVisual*` (cast to `IUnknown*`) you
    /// hand to WebView2's `put_RootVisualTarget`.
    pub fn root_visual_as_iunknown(&self) -> *mut c_void {
        unsafe { sys::UIWebViewDComp_GetRootVisualAsIUnknown(self.ptr) }
    }

    /// Re-positions / resizes the webview visual and (optionally)
    /// paints rounded corners by filling the four corner triangles
    /// with `corner_color`.
    pub fn set_bounds(
        &mut self,
        x: i32,
        y: i32,
        w: i32,
        h: i32,
        border_w: i32,
        radius: f32,
        corner_color: Color,
    ) {
        let raw = sys::UIWVDCompColor {
            r: corner_color.r,
            g: corner_color.g,
            b: corner_color.b,
            a: corner_color.a,
        };
        unsafe {
            sys::UIWebViewDComp_SetBounds(self.ptr, x, y, w, h, border_w, radius, raw)
        };
    }

    /// Forces a DComp commit.
    pub fn commit(&mut self) {
        unsafe { sys::UIWebViewDComp_Commit(self.ptr) };
    }

    /// Adds a child overlay drawn by D2D inside the DComp tree.
    /// Returns the handle used by the other overlay methods, or
    /// [`None`] on failure.
    pub fn add_overlay(
        &mut self,
        x: i32,
        y: i32,
        w: i32,
        h: i32,
        radius: f32,
        fill: Color,
    ) -> Option<i32> {
        let raw = sys::UIWVDCompColor {
            r: fill.r,
            g: fill.g,
            b: fill.b,
            a: fill.a,
        };
        let h = unsafe { sys::UIWebViewDComp_AddOverlay(self.ptr, x, y, w, h, radius, raw) };
        if h < 0 {
            None
        } else {
            Some(h)
        }
    }

    /// Sets the text drawn inside an overlay (pass an empty string
    /// to clear).
    pub fn set_overlay_text(
        &mut self,
        handle: i32,
        text: &str,
        font_family: Option<&str>,
        font_size: f32,
        color: Color,
        padding: f32,
    ) -> Result<()> {
        let text_c = CString::new(text)?;
        let family_c = font_family.map(CString::new).transpose()?;
        let raw = sys::UIWVDCompColor {
            r: color.r,
            g: color.g,
            b: color.b,
            a: color.a,
        };
        unsafe {
            sys::UIWebViewDComp_SetOverlayText(
                self.ptr,
                handle,
                text_c.as_ptr(),
                family_c.as_ref().map(|c| c.as_ptr()).unwrap_or(std::ptr::null()),
                font_size,
                raw,
                padding,
            )
        };
        Ok(())
    }

    /// Re-positions / resizes an existing overlay.
    pub fn move_overlay(&mut self, handle: i32, x: i32, y: i32, w: i32, h: i32) {
        unsafe { sys::UIWebViewDComp_MoveOverlay(self.ptr, handle, x, y, w, h) };
    }

    /// Removes an overlay.
    pub fn remove_overlay(&mut self, handle: i32) {
        unsafe { sys::UIWebViewDComp_RemoveOverlay(self.ptr, handle) };
    }

    /// Borrow the raw `UIWebViewDComp*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UIWebViewDComp {
        self.ptr
    }
}

impl Drop for WebViewDComp {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { sys::UIWebViewDComp_Destroy(self.ptr) };
        }
    }
}

/// Owns a WebView2 `ICoreWebView2EnvironmentOptions*` returned by
/// `UIWebViewOptions_Create`. Releases automatically on [`Drop`].
pub struct WebViewOptions {
    ptr: *mut c_void,
}

impl WebViewOptions {
    /// Builds environment options with the given
    /// `additionalBrowserArguments` (UTF-8).
    pub fn new(additional_args: &str) -> Result<Self> {
        let c = CString::new(additional_args)?;
        let ptr = unsafe { sys::UIWebViewOptions_Create(c.as_ptr()) };
        if ptr.is_null() {
            return Err(Error::Null("UIWebViewOptions_Create"));
        }
        Ok(Self { ptr })
    }

    /// Borrow the raw `IUnknown*`. Hand it to
    /// `CreateCoreWebView2EnvironmentWithOptions`.
    #[inline]
    pub fn as_ptr(&self) -> *mut c_void {
        self.ptr
    }
}

impl Drop for WebViewOptions {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { sys::UIWebViewOptions_Release(self.ptr) };
        }
    }
}
