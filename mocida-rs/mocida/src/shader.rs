//! Shader program library (`UIShader_*`).
//!
//! Built-ins: `unlit`, `lit_sprite`, `hd2d`. Custom passes register
//! Metal / HLSL / GLSL / WGSL / SPIR-V files; software backends keep the
//! registration and Scene3D approximates lighting in CPU.

use std::ffi::CString;
use std::ptr;

use mocida_sys as sys;

use crate::error::{Error, Result};
use crate::gpu::Gpu;

/// Shader source language hint.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum ShaderLang {
    /// Host picks for the active GPU.
    Auto = 0,
    Metal = 1,
    Hlsl = 2,
    Glsl = 3,
    Wgsl = 4,
    Spirv = 5,
}

/// Named shader library bound to a [`Gpu`].
pub struct ShaderLibrary {
    ptr: *mut sys::UIShaderLibrary,
}

impl ShaderLibrary {
    /// Create a library on `gpu` (or the process default when `None`).
    pub fn new(gpu: Option<&Gpu>) -> Result<Self> {
        let g = gpu.map(|g| g.as_ptr()).unwrap_or(ptr::null_mut());
        let ptr = unsafe { sys::UIShader_CreateLibrary(g) };
        if ptr.is_null() {
            return Err(Error::Null("UIShader_CreateLibrary"));
        }
        Ok(Self { ptr })
    }

    /// Ensure built-ins are present.
    pub fn register_builtins(&mut self) -> bool {
        unsafe { sys::UIShader_RegisterBuiltins(self.ptr) == 1 }
    }

    /// Register a custom pass from source / bytecode paths.
    pub fn register_pass(
        &mut self,
        name: &str,
        vertex: Option<&str>,
        fragment: Option<&str>,
        compute: Option<&str>,
        lang: ShaderLang,
        requires_compute: bool,
        requires_rt: bool,
    ) -> Result<()> {
        let n = CString::new(name)?;
        let v = vertex.map(CString::new).transpose()?;
        let f = fragment.map(CString::new).transpose()?;
        let c = compute.map(CString::new).transpose()?;
        let ok = unsafe {
            sys::UIShader_RegisterPass(
                self.ptr,
                n.as_ptr(),
                v.as_ref().map(|s| s.as_ptr()).unwrap_or(ptr::null()),
                f.as_ref().map(|s| s.as_ptr()).unwrap_or(ptr::null()),
                c.as_ref().map(|s| s.as_ptr()).unwrap_or(ptr::null()),
                sys::UIShaderLang(lang as _),
                requires_compute as i32,
                requires_rt as i32,
            )
        };
        if ok == 1 {
            Ok(())
        } else {
            Err(Error::Null("UIShader_RegisterPass"))
        }
    }

    /// Select the active program.
    pub fn use_shader(&mut self, name: &str) -> bool {
        let Ok(n) = CString::new(name) else {
            return false;
        };
        unsafe { sys::UIShader_Use(self.ptr, n.as_ptr()) == 1 }
    }

    /// Name of the active program.
    pub fn active(&self) -> String {
        let p = unsafe { sys::UIShader_Active(self.ptr) };
        if p.is_null() {
            return "unlit".into();
        }
        unsafe { std::ffi::CStr::from_ptr(p) }
            .to_string_lossy()
            .into_owned()
    }

    /// Whether `name` is registered.
    pub fn has(&self, name: &str) -> bool {
        let Ok(n) = CString::new(name) else {
            return false;
        };
        unsafe { sys::UIShader_Has(self.ptr, n.as_ptr()) == 1 }
    }

    /// Raw pointer.
    pub fn as_ptr(&self) -> *mut sys::UIShaderLibrary {
        self.ptr
    }
}

impl Drop for ShaderLibrary {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { sys::UIShader_DestroyLibrary(self.ptr) };
            self.ptr = ptr::null_mut();
        }
    }
}
