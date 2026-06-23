//! Safe wrappers for the C device simulator.

use crate::device_profiles::DeviceProfile;
use mocida::sys as msys;
use std::ffi::CString;

/// Orientation of a simulated device.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DeviceOrientation {
    /// Portrait orientation (taller than wide).
    Portrait,
    /// Landscape orientation (wider than tall).
    Landscape,
}

impl DeviceOrientation {
    pub fn toggle(self) -> Self {
        match self {
            DeviceOrientation::Portrait => DeviceOrientation::Landscape,
            DeviceOrientation::Landscape => DeviceOrientation::Portrait,
        }
    }
}

/// Enable the device simulator for `app` using `profile`.
///
/// If `frame_png_path` is provided, the simulator loads the device frame from
/// that file; otherwise no frame image is used. Returns an error if the path
/// contains an interior NUL byte.
pub fn enable(
    app: &mut crate::Runtime,
    profile: &DeviceProfile,
    frame_png_path: Option<&str>,
    host_scale: f32,
    orientation: DeviceOrientation,
) -> Result<(), mocida::Error> {
    let p = to_c_profile(profile);
    unsafe {
        if let Some(path) = frame_png_path {
            let cpath = CString::new(path)?;
            msys::UIApp_EnableDeviceSimulatorFromFile(
                app.app.as_ptr(),
                &p,
                cpath.as_ptr(),
                host_scale,
                orientation_to_c(orientation),
            );
        } else {
            msys::UIApp_EnableDeviceSimulator(
                app.app.as_ptr(),
                &p,
                std::ptr::null_mut(),
                host_scale,
                orientation_to_c(orientation),
            );
        }
    }
    Ok(())
}

/// Disable the device simulator for `app`.
pub fn disable(app: &mut crate::Runtime) {
    unsafe { msys::UIApp_DisableDeviceSimulator(app.app.as_ptr()); }
}

/// Returns whether the device simulator is currently enabled for `app`.
pub fn is_enabled(app: &crate::Runtime) -> bool {
    unsafe { msys::UIApp_IsDeviceSimulatorEnabled(app.app.as_ptr()) }
}

/// Returns the current simulator orientation.
///
/// Unknown orientation values returned by the C API fall back to `Portrait`.
pub fn orientation(app: &crate::Runtime) -> DeviceOrientation {
    let o = unsafe { msys::UIApp_GetCurrentDeviceOrientation(app.app.as_ptr()) };
    if o == msys::UI_DEVICE_ORIENTATION_LANDSCAPE {
        DeviceOrientation::Landscape
    } else {
        DeviceOrientation::Portrait
    }
}

/// Set the simulator orientation for `app`.
pub fn set_orientation(app: &mut crate::Runtime, orientation: DeviceOrientation) {
    unsafe { msys::UIApp_SetDeviceOrientation(app.app.as_ptr(), orientation_to_c(orientation)); }
}

/// Toggle the simulator orientation for `app`.
pub fn toggle_orientation(app: &mut crate::Runtime) {
    set_orientation(app, orientation(app).toggle());
}

/// Convert a window-coordinate point to device coordinates.
pub fn window_to_device(app: &crate::Runtime, wx: f32, wy: f32) -> (f32, f32) {
    let mut dx = 0.0f32;
    let mut dy = 0.0f32;
    unsafe { msys::UIApp_DeviceSimulatorWindowToDevice(app.app.as_ptr(), wx, wy, &mut dx, &mut dy); }
    (dx, dy)
}

/// Convert a device-coordinate point to window coordinates.
pub fn device_to_window(app: &crate::Runtime, dx: f32, dy: f32) -> (f32, f32) {
    let mut wx = 0.0f32;
    let mut wy = 0.0f32;
    unsafe { msys::UIApp_DeviceSimulatorDeviceToWindow(app.app.as_ptr(), dx, dy, &mut wx, &mut wy); }
    (wx, wy)
}

fn to_c_profile(p: &DeviceProfile) -> msys::UIDeviceProfile {
    // SAFETY: `DeviceProfile::name` is a `'static` string and the C API copies
    // it synchronously during the enable call, so the pointer only needs to be
    // valid for the duration of that call.
    msys::UIDeviceProfile {
        name: p.name.as_ptr() as *const i8,
        width: p.width,
        height: p.height,
        safeTop: p.safe_top,
        safeBottom: p.safe_bottom,
        safeLeft: p.safe_left,
        safeRight: p.safe_right,
        scale: p.scale,
        frameScreenX: p.frame_screen_x,
        frameScreenY: p.frame_screen_y,
        frameScreenW: p.frame_screen_w,
        frameScreenH: p.frame_screen_h,
        frameW: p.frame_w,
        frameH: p.frame_h,
    }
}

fn orientation_to_c(o: DeviceOrientation) -> msys::UIDeviceOrientation {
    match o {
        DeviceOrientation::Portrait => msys::UI_DEVICE_ORIENTATION_PORTRAIT,
        DeviceOrientation::Landscape => msys::UI_DEVICE_ORIENTATION_LANDSCAPE,
    }
}
