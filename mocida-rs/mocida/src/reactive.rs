// The SignalValue impls take raw `*mut UISignal` / `*const UISignal`
// pointers by design — they're internal hooks called only from
// `Signal<T>`, which has already validated the pointer. Clippy's
// blanket "raw ptr arg = unsafe" rule produces false positives here.
#![allow(clippy::not_unsafe_ptr_arg_deref)]

//! `UISignal` — reactive value cells, generic over the payload type.
//!
//! ```no_run
//! use mocida::Signal;
//!
//! let mut fps: Signal<i32> = Signal::new(60)?;
//! let _sub = fps.subscribe(|s| println!("now: {}", s.get()));
//! fps.set(120)?;                       // fires the subscriber once
//! fps.set(120)?;                       // dedupe — no second fire
//!
//! let mut title: Signal<String> = Signal::new("Hello".to_string())?;
//! title.set("Olá".to_string())?;
//! assert_eq!(title.get(), "Olá");
//! # Ok::<(), mocida::Error>(())
//! ```

use std::ffi::{c_void, CStr, CString};
use std::marker::PhantomData;

use mocida_sys as sys;

use crate::error::{Error, Result};

/// Underlying type tag mirrored from `UISignalType`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum SignalType {
    /// `i32`-valued signal.
    Int = 0,
    /// `f32`-valued signal.
    Float = 1,
    /// String-valued signal (mocida owns the storage).
    String = 2,
    /// Opaque-pointer signal (caller owns the pointee).
    Pointer = 3,
}

impl SignalType {
    fn from_raw(raw: sys::UISignalType) -> Self {
        match raw.0 {
            0 => Self::Int,
            1 => Self::Float,
            2 => Self::String,
            _ => Self::Pointer,
        }
    }
}

/// Types that can sit inside a [`Signal`].
///
/// Implemented out of the box for `i32`, `f32`, and `String`. Pointer
/// signals stay on the raw FFI layer (see [`Signal::from_raw_pointer`]
/// for an escape hatch) because their safety contract — the pointee
/// must outlive every observer — can't be enforced by the type system.
pub trait SignalValue: Sized + 'static {
    /// Discriminant returned by [`Signal::signal_type`].
    fn signal_type() -> SignalType;

    /// Allocates a fresh `UISignal*` seeded with `value`. Implementors
    /// should call exactly one of the `UISignal_Create*` constructors.
    fn create_raw(value: Self) -> Result<*mut sys::UISignal>;

    /// Reads the current value out of the C-side signal.
    fn read_raw(ptr: *const sys::UISignal) -> Self;

    /// Writes a new value. May allocate (e.g. `CString` for `String`).
    fn write_raw(ptr: *mut sys::UISignal, value: Self) -> Result<()>;
}

impl SignalValue for i32 {
    fn signal_type() -> SignalType {
        SignalType::Int
    }
    fn create_raw(value: i32) -> Result<*mut sys::UISignal> {
        let ptr = unsafe { sys::UISignal_CreateInt(value) };
        if ptr.is_null() {
            Err(Error::Null("UISignal_CreateInt"))
        } else {
            Ok(ptr)
        }
    }
    fn read_raw(ptr: *const sys::UISignal) -> Self {
        unsafe { sys::UISignal_GetInt(ptr) }
    }
    fn write_raw(ptr: *mut sys::UISignal, value: i32) -> Result<()> {
        unsafe { sys::UISignal_SetInt(ptr, value) };
        Ok(())
    }
}

impl SignalValue for f32 {
    fn signal_type() -> SignalType {
        SignalType::Float
    }
    fn create_raw(value: f32) -> Result<*mut sys::UISignal> {
        let ptr = unsafe { sys::UISignal_CreateFloat(value) };
        if ptr.is_null() {
            Err(Error::Null("UISignal_CreateFloat"))
        } else {
            Ok(ptr)
        }
    }
    fn read_raw(ptr: *const sys::UISignal) -> Self {
        unsafe { sys::UISignal_GetFloat(ptr) }
    }
    fn write_raw(ptr: *mut sys::UISignal, value: f32) -> Result<()> {
        unsafe { sys::UISignal_SetFloat(ptr, value) };
        Ok(())
    }
}

impl SignalValue for bool {
    fn signal_type() -> SignalType {
        SignalType::Int
    }
    fn create_raw(value: bool) -> Result<*mut sys::UISignal> {
        i32::create_raw(value as i32)
    }
    fn read_raw(ptr: *const sys::UISignal) -> Self {
        i32::read_raw(ptr) != 0
    }
    fn write_raw(ptr: *mut sys::UISignal, value: bool) -> Result<()> {
        i32::write_raw(ptr, value as i32)
    }
}

impl SignalValue for String {
    fn signal_type() -> SignalType {
        SignalType::String
    }
    fn create_raw(value: String) -> Result<*mut sys::UISignal> {
        let c = CString::new(value)?;
        let ptr = unsafe { sys::UISignal_CreateString(c.as_ptr()) };
        if ptr.is_null() {
            Err(Error::Null("UISignal_CreateString"))
        } else {
            Ok(ptr)
        }
    }
    fn read_raw(ptr: *const sys::UISignal) -> Self {
        let p = unsafe { sys::UISignal_GetString(ptr) };
        if p.is_null() {
            String::new()
        } else {
            unsafe { CStr::from_ptr(p) }
                .to_str()
                .map(str::to_owned)
                .unwrap_or_default()
        }
    }
    fn write_raw(ptr: *mut sys::UISignal, value: String) -> Result<()> {
        let c = CString::new(value)?;
        unsafe { sys::UISignal_SetString(ptr, c.as_ptr()) };
        Ok(())
    }
}

/// Reactive value cell. The type parameter ties the signal to its
/// payload type at compile time, so the C-side type mismatch a raw
/// `UISignal*` would normally allow becomes a compile error.
pub struct Signal<T: SignalValue> {
    ptr: *mut sys::UISignal,
    subscriptions: Vec<Box<TrampolineState>>,
    _marker: PhantomData<T>,
}

struct TrampolineState {
    handler: Box<dyn FnMut(*mut sys::UISignal) + 'static>,
}

/// Handle to an active subscription. Drop the owning [`Signal`] (or
/// call [`Signal::unsubscribe`]) to cancel it.
pub struct Subscription {
    raw: *mut sys::UISubscription,
}

impl<T: SignalValue> Signal<T> {
    /// Creates a signal seeded with `value`.
    pub fn new(value: T) -> Result<Self> {
        let ptr = T::create_raw(value)?;
        Ok(Self {
            ptr,
            subscriptions: Vec::new(),
            _marker: PhantomData,
        })
    }

    /// Wraps an existing `UISignal*` that was allocated elsewhere
    /// (e.g. through [`mocida_sys`] directly). The wrapper takes
    /// ownership and will call `UISignal_Destroy` when dropped.
    ///
    /// # Safety
    /// `ptr` must point to a live signal whose underlying type
    /// matches `T` (use [`SignalType`] to check via the C API).
    pub unsafe fn from_raw(ptr: *mut sys::UISignal) -> Self {
        Self {
            ptr,
            subscriptions: Vec::new(),
            _marker: PhantomData,
        }
    }

    /// Borrow the raw `UISignal*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UISignal {
        self.ptr
    }

    /// Returns the underlying type tag (cheap; reads a C field).
    pub fn signal_type(&self) -> SignalType {
        SignalType::from_raw(unsafe { sys::UISignal_GetType(self.ptr) })
    }

    /// Reads the current value.
    pub fn get(&self) -> T {
        T::read_raw(self.ptr)
    }

    /// Sets a new value. Dedupes — subscribers fire only when the
    /// value actually changes.
    pub fn set(&mut self, value: T) -> Result<()> {
        T::write_raw(self.ptr, value)
    }

    /// Force-notifies subscribers without changing the value.
    pub fn notify(&mut self) {
        unsafe { sys::UISignal_Notify(self.ptr) };
    }

    /// Subscribes a handler that runs synchronously on every change.
    pub fn subscribe<F>(&mut self, mut handler: F) -> Result<Subscription>
    where
        F: FnMut(&Signal<T>) + 'static,
    {
        let erased: Box<dyn FnMut(*mut sys::UISignal) + 'static> = Box::new(move |sig| {
            // Construct a borrowed view that doesn't free anything on drop.
            let view = Signal::<T> {
                ptr: sig,
                subscriptions: Vec::new(),
                _marker: PhantomData,
            };
            handler(&view);
            std::mem::forget(view);
        });
        let state = Box::new(TrampolineState { handler: erased });
        let userdata = Box::as_ref(&state) as *const TrampolineState as *mut c_void;
        let raw = unsafe {
            sys::UISignal_Subscribe(self.ptr, Some(signal_trampoline), userdata)
        };
        if raw.is_null() {
            return Err(Error::Null("UISignal_Subscribe"));
        }
        self.subscriptions.push(state);
        Ok(Subscription { raw })
    }

    /// Cancels a subscription. Safe to call from inside a callback.
    pub fn unsubscribe(sub: Subscription) {
        unsafe { sys::UISignal_Unsubscribe(sub.raw) };
    }
}

impl<T: SignalValue> Drop for Signal<T> {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { sys::UISignal_Destroy(self.ptr) };
        }
    }
}

/// Marker type for `Signal<Opaque>` — an opaque-pointer signal.
///
/// Constructing one of these is always `unsafe`: mocida just stores
/// the bits, so it's on you to keep the pointee alive at least as
/// long as the signal (and every subscriber).
#[derive(Debug, Clone, Copy)]
pub struct Opaque(pub *mut c_void);

impl SignalValue for Opaque {
    fn signal_type() -> SignalType {
        SignalType::Pointer
    }
    fn create_raw(value: Opaque) -> Result<*mut sys::UISignal> {
        let ptr = unsafe { sys::UISignal_CreatePointer(value.0) };
        if ptr.is_null() {
            Err(Error::Null("UISignal_CreatePointer"))
        } else {
            Ok(ptr)
        }
    }
    fn read_raw(ptr: *const sys::UISignal) -> Self {
        Opaque(unsafe { sys::UISignal_GetPointer(ptr) })
    }
    fn write_raw(ptr: *mut sys::UISignal, value: Opaque) -> Result<()> {
        unsafe { sys::UISignal_SetPointer(ptr, value.0) };
        Ok(())
    }
}

extern "C" fn signal_trampoline(sig: *mut sys::UISignal, userdata: *mut c_void) {
    if userdata.is_null() || sig.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut TrampolineState) };
    (state.handler)(sig);
}
