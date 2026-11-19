//! Soft GPU device (`UIGpu_*`) for shaders and 2.5D/3D work.
//!
//! Creation never hard-fails: when the platform has no SDL_GPU device the
//! handle still exists with [`Gpu::is_available`] == false and Scene3D
//! paints through the software / renderer path.

use mocida_sys as sys;

use crate::error::{Error, Result};

/// Process GPU handle.
pub struct Gpu {
    ptr: *mut sys::UIGpu,
    owned: bool,
}

impl Gpu {
    /// Open a GPU device for `window` (null window = capability probe only).
    pub fn create(window: *mut sys::SDL_Window) -> Result<Self> {
        let ptr = unsafe { sys::UIGpu_Create(window) };
        if ptr.is_null() {
            return Err(Error::Null("UIGpu_Create"));
        }
        Ok(Self { ptr, owned: true })
    }

    /// Process-wide default (lazily created). Not destroyed on Drop.
    pub fn default_device() -> Result<Self> {
        let ptr = unsafe { sys::UIGpu_Default() };
        if ptr.is_null() {
            return Err(Error::Null("UIGpu_Default"));
        }
        Ok(Self { ptr, owned: false })
    }

    /// Whether an SDL_GPUDevice is live.
    pub fn is_available(&self) -> bool {
        unsafe { sys::UIGpu_IsAvailable(self.ptr) == 1 }
    }

    /// Driver name (`metal`, `vulkan`, `direct3d12`, or `software`).
    pub fn driver_name(&self) -> String {
        let p = unsafe { sys::UIGpu_DriverName(self.ptr) };
        if p.is_null() {
            return "software".into();
        }
        unsafe { std::ffi::CStr::from_ptr(p) }
            .to_string_lossy()
            .into_owned()
    }

    /// Custom shader passes can be compiled on this device.
    pub fn supports_shaders(&self) -> bool {
        unsafe { sys::UIGpu_SupportsShaders(self.ptr) == 1 }
    }

    /// Compute shaders are available.
    pub fn supports_compute(&self) -> bool {
        unsafe { sys::UIGpu_SupportsCompute(self.ptr) == 1 }
    }

    /// Raw `UIGpu*`.
    pub fn as_ptr(&self) -> *mut sys::UIGpu {
        self.ptr
    }
}

impl Drop for Gpu {
    fn drop(&mut self) {
        if self.owned && !self.ptr.is_null() {
            unsafe { sys::UIGpu_Destroy(self.ptr) };
            self.ptr = std::ptr::null_mut();
        }
    }
}
