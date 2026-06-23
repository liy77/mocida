//! Pre-defined device profiles for the mui-dev simulator.

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct DeviceProfile {
    pub name: &'static str,
    pub width: i32,
    pub height: i32,
    pub safe_top: i32,
    pub safe_bottom: i32,
    pub safe_left: i32,
    pub safe_right: i32,
    pub scale: f32,
    pub frame_screen_x: i32,
    pub frame_screen_y: i32,
    pub frame_screen_w: i32,
    pub frame_screen_h: i32,
    pub frame_w: i32,
    pub frame_h: i32,
}

impl DeviceProfile {
    /// Generic frame geometry: 20 px side bezels, 70 px top bezel for camera.
    const fn framed(
        name: &'static str,
        width: i32,
        height: i32,
        safe_top: i32,
        safe_bottom: i32,
        safe_left: i32,
        safe_right: i32,
        scale: f32,
    ) -> Self {
        let frame_w = width + 40;
        let frame_h = height + 120;
        Self {
            name,
            width,
            height,
            safe_top,
            safe_bottom,
            safe_left,
            safe_right,
            scale,
            frame_screen_x: 20,
            frame_screen_y: 70,
            frame_screen_w: width,
            frame_screen_h: height,
            frame_w,
            frame_h,
        }
    }
}

pub const IPHONE_15: DeviceProfile = DeviceProfile::framed("iphone15", 393, 852, 59, 34, 0, 0, 3.0);
pub const IPHONE_15_PRO: DeviceProfile =
    DeviceProfile::framed("iphone15pro", 393, 852, 59, 34, 0, 0, 3.0);
pub const IPHONE_15_PRO_MAX: DeviceProfile =
    DeviceProfile::framed("iphone15promax", 430, 932, 59, 34, 0, 0, 3.0);
pub const IPHONE_16: DeviceProfile = DeviceProfile::framed("iphone16", 393, 852, 59, 34, 0, 0, 3.0);
pub const GALAXY_S24: DeviceProfile =
    DeviceProfile::framed("galaxy_s24", 360, 780, 24, 24, 0, 0, 3.0);
pub const GALAXY_S24_PLUS: DeviceProfile =
    DeviceProfile::framed("galaxy_s24plus", 384, 832, 24, 24, 0, 0, 3.2);
pub const GALAXY_S25: DeviceProfile =
    DeviceProfile::framed("galaxy_s25", 360, 780, 24, 24, 0, 0, 3.0);
pub const GALAXY_S25_ULTRA: DeviceProfile =
    DeviceProfile::framed("galaxy_s25ultra", 384, 832, 24, 24, 0, 0, 3.2);

pub const PROFILE_NAMES: &[&str] = &[
    "iphone15",
    "iphone15pro",
    "iphone15promax",
    "iphone16",
    "galaxy_s24",
    "galaxy_s24plus",
    "galaxy_s25",
    "galaxy_s25ultra",
];

pub fn lookup(name: &str) -> Option<&'static DeviceProfile> {
    Some(match name {
        "iphone15" => &IPHONE_15,
        "iphone15pro" => &IPHONE_15_PRO,
        "iphone15promax" => &IPHONE_15_PRO_MAX,
        "iphone16" => &IPHONE_16,
        "galaxy_s24" => &GALAXY_S24,
        "galaxy_s24plus" => &GALAXY_S24_PLUS,
        "galaxy_s25" => &GALAXY_S25,
        "galaxy_s25ultra" => &GALAXY_S25_ULTRA,
        _ => return None,
    })
}
