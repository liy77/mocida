//! `UIArena` — chunked bump allocator.
//!
//! Useful for per-frame transients or for the entire object tree of
//! a popup whose lifetime is "open .. close". After [`Arena::reset`]
//! every pointer previously handed out becomes invalid.

use std::ffi::{c_void, CStr, CString};

use mocida_sys as sys;

use crate::error::{Error, Result};

/// Bump allocator backed by chained chunks.
pub struct Arena {
    ptr: *mut sys::UIArena,
}

impl Arena {
    /// Creates an arena with the given initial chunk capacity in
    /// bytes (values below 1 KiB are clamped up to 16 KiB).
    pub fn new(initial_capacity: usize) -> Result<Self> {
        let ptr = unsafe { sys::UIArena_Create(initial_capacity) };
        if ptr.is_null() {
            return Err(Error::Null("UIArena_Create"));
        }
        Ok(Self { ptr })
    }

    /// Allocates `size` bytes aligned to `alignment` (power of two;
    /// 0 = `align_of::<*mut ()>()`). The block is **uninitialized**.
    ///
    /// # Safety
    /// You must not store the returned pointer past the next
    /// [`Arena::reset`] or [`Arena::destroy`].
    pub unsafe fn alloc(&mut self, size: usize, alignment: usize) -> Option<*mut c_void> {
        let p = unsafe { sys::UIArena_Alloc(self.ptr, size, alignment) };
        if p.is_null() {
            None
        } else {
            Some(p)
        }
    }

    /// Like [`alloc`](Self::alloc) but zero-initialized.
    ///
    /// # Safety
    /// Same contract as [`alloc`](Self::alloc).
    pub unsafe fn alloc_zero(&mut self, size: usize, alignment: usize) -> Option<*mut c_void> {
        let p = unsafe { sys::UIArena_AllocZero(self.ptr, size, alignment) };
        if p.is_null() {
            None
        } else {
            Some(p)
        }
    }

    /// Copies a string into the arena. The lifetime of the returned
    /// `&CStr` is tied to the arena's last reset.
    pub fn strdup(&mut self, s: &str) -> Result<&CStr> {
        let c = CString::new(s)?;
        let raw = unsafe { sys::UIArena_Strdup(self.ptr, c.as_ptr()) };
        if raw.is_null() {
            return Err(Error::Null("UIArena_Strdup"));
        }
        // SAFETY: mocida copies the input into the arena and adds a
        // NUL; the returned pointer is valid until the next reset.
        Ok(unsafe { CStr::from_ptr(raw) })
    }

    /// Resets every chunk to empty without releasing memory. Every
    /// pointer previously returned by the arena becomes dangling.
    pub fn reset(&mut self) {
        unsafe { sys::UIArena_Reset(self.ptr) };
    }

    /// Live bytes currently bumped across all chunks.
    pub fn bytes_used(&self) -> usize {
        unsafe { sys::UIArena_BytesUsed(self.ptr) }
    }

    /// Total bytes reserved by the arena's backing chunks.
    pub fn bytes_reserved(&self) -> usize {
        unsafe { sys::UIArena_BytesReserved(self.ptr) }
    }

    /// Borrow the raw `UIArena*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UIArena {
        self.ptr
    }
}

impl Drop for Arena {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { sys::UIArena_Destroy(self.ptr) };
        }
    }
}
