//! Safe wrappers for the C device simulator.

use crate::device_profiles::DeviceProfile;
use mocida::sys as msys;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DeviceOrientation {
    Portrait,
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

pub struct DeviceSimulator;

impl DeviceSimulator {
    pub fn enable(
        app: &mut crate::Runtime,
        profile: &DeviceProfile,
        frame_png_path: Option<&str>,
        host_scale: f32,
        orientation: DeviceOrientation,
    ) {
        unsafe {
            let p = to_c_profile(profile);
            if let Some(path) = frame_png_path {
                let cpath = std::ffi::CString::new(path).expect("invalid frame path");
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
    }

    pub fn disable(app: &mut crate::Runtime) {
        unsafe { msys::UIApp_DisableDeviceSimulator(app.app.as_ptr()); }
    }

    pub fn is_enabled(app: &crate::Runtime) -> bool {
        unsafe { msys::UIApp_IsDeviceSimulatorEnabled(app.app.as_ptr()) }
    }

    pub fn orientation(app: &crate::Runtime) -> DeviceOrientation {
        let o = unsafe { msys::UIApp_GetCurrentDeviceOrientation(app.app.as_ptr()) };
        if o == msys::UI_DEVICE_ORIENTATION_LANDSCAPE {
            DeviceOrientation::Landscape
        } else {
            DeviceOrientation::Portrait
        }
    }

    pub fn set_orientation(app: &mut crate::Runtime, orientation: DeviceOrientation) {
        unsafe { msys::UIApp_SetDeviceOrientation(app.app.as_ptr(), orientation_to_c(orientation)); }
    }

    pub fn toggle_orientation(app: &mut crate::Runtime) {
        Self::set_orientation(app, Self::orientation(app).toggle());
    }

    pub fn window_to_device(app: &crate::Runtime, wx: f32, wy: f32) -> (f32, f32) {
        let mut dx = 0.0f32;
        let mut dy = 0.0f32;
        unsafe { msys::UIApp_DeviceSimulatorWindowToDevice(app.app.as_ptr(), wx, wy, &mut dx, &mut dy); }
        (dx, dy)
    }

    pub fn device_to_window(app: &crate::Runtime, dx: f32, dy: f32) -> (f32, f32) {
        let mut wx = 0.0f32;
        let mut wy = 0.0f32;
        unsafe { msys::UIApp_DeviceSimulatorDeviceToWindow(app.app.as_ptr(), dx, dy, &mut wx, &mut wy); }
        (wx, wy)
    }
}

fn to_c_profile(p: &DeviceProfile) -> msys::UIDeviceProfile {
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
