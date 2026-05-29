//! Clipboard helpers (mirrors `UIClipboard_*`).

use std::ffi::{CStr, CString};

use mocida_sys as sys;

use crate::error::Result;

/// Returns the current clipboard contents as a [`String`], or [`None`]
/// when the clipboard is empty or non-text.
pub fn get_text() -> Option<String> {
    let raw = unsafe { sys::UIClipboard_GetText() };
    if raw.is_null() {
        return None;
    }
    // Safety: mocida hands us an owned buffer; we copy it out then free.
    let s = unsafe { CStr::from_ptr(raw) }.to_str().ok().map(str::to_owned);
    unsafe { sys::UIClipboard_FreeText(raw) };
    s
}

/// Replaces the clipboard contents. Returns `Ok(true)` on success.
pub fn set_text(text: &str) -> Result<bool> {
    let c = CString::new(text)?;
    let rc = unsafe { sys::UIClipboard_SetText(c.as_ptr()) };
    Ok(rc != 0)
}

/// Clears the clipboard.
pub fn clear() {
    unsafe { sys::UIClipboard_SetText(std::ptr::null()) };
}

/// True when the clipboard currently has text content.
pub fn has_text() -> bool {
    unsafe { sys::UIClipboard_HasText() != 0 }
}

