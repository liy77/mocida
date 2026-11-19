//! Native 2.5D / 3D-aux scene widget (`UIScene3D_*`).
//!
//! Depth-sorted sprites, point lights, camera, optional meshes, and a
//! shader library — the host surface Mocidad games and MUI share.

use std::ffi::CString;
use std::ptr;

use mocida_sys as sys;

use crate::color::Color;
use crate::error::{Error, Result};
use crate::shader::ShaderLibrary;
use crate::widget::Widget;

/// Render mode shared with the Mocidad graphics API.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum GfxMode {
    /// Flat ortho 2D.
    Mode2D = 0,
    /// HD-2D: depth sort, billboards, lights.
    Mode25D = 1,
    /// 3D props inside a 2D game.
    Mode3DAux = 2,
}

/// Billboard facing mode for sprites.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum BillboardMode {
    /// Keep authored rotation.
    None = 0,
    /// Face camera on Y.
    YAxis = 1,
    /// Full spherical billboard.
    Spherical = 2,
}

/// 2.5D / 3D scene as a mocida widget.
pub struct Scene3D {
    ptr: *mut sys::UIScene3D,
    moved: bool,
}

impl Scene3D {
    /// Create a scene of the given logical size.
    pub fn new(width: f32, height: f32) -> Result<Self> {
        let ptr = unsafe { sys::UIScene3D_Create(width, height) };
        if ptr.is_null() {
            return Err(Error::Null("UIScene3D_Create"));
        }
        Ok(Self { ptr, moved: false })
    }

    /// Render mode.
    pub fn mode(self, mode: GfxMode) -> Self {
        unsafe { sys::UIScene3D_SetMode(self.ptr, sys::UIGfxMode(mode as _)) };
        self
    }

    /// Clear / backdrop color (alpha 0 = transparent).
    pub fn background(self, color: Color) -> Self {
        unsafe { sys::UIScene3D_SetBackground(self.ptr, color.into_raw()) };
        self
    }

    /// Attach a shader library (borrowed — caller keeps ownership).
    pub fn shader_library(self, lib: &ShaderLibrary) -> Self {
        unsafe { sys::UIScene3D_SetShaderLibrary(self.ptr, lib.as_ptr()) };
        self
    }

    /// Transfer ownership of a shader library to the scene.
    pub fn own_shader_library(self, lib: ShaderLibrary) -> Self {
        let lib = std::mem::ManuallyDrop::new(lib);
        let p = lib.as_ptr();
        unsafe { sys::UIScene3D_OwnShaderLibrary(self.ptr, p) };
        self
    }

    /// Select active shader program (`hd2d`, `lit_sprite`, `unlit`, …).
    pub fn use_shader(self, name: &str) -> Result<Self> {
        let n = CString::new(name)?;
        unsafe { sys::UIScene3D_UseShader(self.ptr, n.as_ptr()) };
        Ok(self)
    }

    /// Add a textured sprite. Returns the sprite index.
    pub fn add_sprite(
        &mut self,
        source: &str,
        x: f32,
        y: f32,
        z: f32,
        w: f32,
        h: f32,
    ) -> Result<usize> {
        let s = CString::new(source)?;
        let sp = unsafe { sys::UIScene3D_AddSprite(self.ptr, s.as_ptr(), x, y, z, w, h) };
        if sp.is_null() {
            return Err(Error::Null("UIScene3D_AddSprite"));
        }
        Ok(unsafe { sys::UIScene3D_SpriteCount(self.ptr) as usize - 1 })
    }

    /// Configure billboard / shadow / tint on a sprite by index.
    pub fn sprite_billboard(&mut self, index: usize, mode: BillboardMode) -> &mut Self {
        let sp = unsafe { sys::UIScene3D_SpriteAt(self.ptr, index as i32) };
        if !sp.is_null() {
            unsafe { (*sp).billboard = sys::UIBillboardMode(mode as _) };
        }
        self
    }

    /// Sprite casts a soft blob shadow.
    pub fn sprite_casts_shadow(&mut self, index: usize, casts: bool) -> &mut Self {
        let sp = unsafe { sys::UIScene3D_SpriteAt(self.ptr, index as i32) };
        if !sp.is_null() {
            unsafe { (*sp).castsShadow = casts as i32 };
        }
        self
    }

    /// Add a point light.
    pub fn add_point_light(
        &mut self,
        x: f32,
        y: f32,
        z: f32,
        radius: f32,
        intensity: f32,
        color: Color,
    ) -> Result<usize> {
        let L = unsafe {
            sys::UIScene3D_AddPointLight(
                self.ptr,
                x,
                y,
                z,
                radius,
                intensity,
                color.into_raw(),
            )
        };
        if L.is_null() {
            return Err(Error::Null("UIScene3D_AddPointLight"));
        }
        Ok(unsafe { sys::UIScene3D_LightCount(self.ptr) as usize - 1 })
    }

    /// Camera yaw / pitch / zoom / position.
    pub fn camera_look(&mut self, x: f32, y: f32, z: f32, yaw: f32, pitch: f32, zoom: f32) {
        let cam = unsafe { sys::UIScene3D_Camera(self.ptr) };
        if cam.is_null() {
            return;
        }
        unsafe {
            (*cam).x = x;
            (*cam).y = y;
            (*cam).z = z;
            (*cam).yaw = yaw;
            (*cam).pitch = pitch;
            (*cam).zoom = zoom;
        }
    }

    /// Consume into a sized [`Widget`].
    pub fn into_widget(mut self) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::new(self.ptr as *mut _) }
    }

    /// Consume into a [`Widget`] with an explicit size.
    pub fn into_widget_sized(mut self, width: f32, height: f32) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::with_size(self.ptr as *mut _, width, height) }
    }

    /// Raw pointer.
    pub fn as_ptr(&self) -> *mut sys::UIScene3D {
        self.ptr
    }
}

impl Drop for Scene3D {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UIScene3D_Destroy(self.ptr) };
            self.ptr = ptr::null_mut();
        }
    }
}
