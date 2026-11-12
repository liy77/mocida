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
    /// iPhone frame geometry: 20 px bezel on left/right/top, 28 px on bottom.
    /// Must match BEZELS["iphone"] in generate_placeholders.py.
    const fn iphone(
        name: &'static str,
        width: i32,
        height: i32,
        safe_top: i32,
        safe_bottom: i32,
        scale: f32,
    ) -> Self {
        let left = 20;
        let right = 20;
        let top = 20;
        let bottom = 28;
        Self {
            name,
            width,
            height,
            safe_top,
            safe_bottom,
            safe_left: 0,
            safe_right: 0,
            scale,
            frame_screen_x: left,
            frame_screen_y: top,
            frame_screen_w: width,
            frame_screen_h: height,
            frame_w: width + left + right,
            frame_h: height + top + bottom,
        }
    }

    /// Galaxy frame geometry: 14 px bezel on left/right/top, 18 px on bottom.
    /// Must match BEZELS["galaxy"] in generate_placeholders.py.
    const fn galaxy(
        name: &'static str,
        width: i32,
        height: i32,
        safe_top: i32,
        safe_bottom: i32,
        scale: f32,
    ) -> Self {
        let left = 14;
        let right = 14;
        let top = 14;
        let bottom = 18;
        Self {
            name,
            width,
            height,
            safe_top,
            safe_bottom,
            safe_left: 0,
            safe_right: 0,
            scale,
            frame_screen_x: left,
            frame_screen_y: top,
            frame_screen_w: width,
            frame_screen_h: height,
            frame_w: width + left + right,
            frame_h: height + top + bottom,
        }
    }
}

// iPhone profiles — frame_w = screen_w + 40, frame_h = screen_h + 48
pub const IPHONE_15: DeviceProfile =
    DeviceProfile::iphone("iphone15", 393, 852, 59, 34, 3.0);
pub const IPHONE_15_PRO: DeviceProfile =
    DeviceProfile::iphone("iphone15pro", 393, 852, 59, 34, 3.0);
pub const IPHONE_15_PRO_MAX: DeviceProfile =
    DeviceProfile::iphone("iphone15promax", 430, 932, 59, 34, 3.0);
pub const IPHONE_16: DeviceProfile =
    DeviceProfile::iphone("iphone16", 393, 852, 59, 34, 3.0);

// Galaxy profiles — frame_w = screen_w + 28, frame_h = screen_h + 32
pub const GALAXY_S24: DeviceProfile =
    DeviceProfile::galaxy("galaxy_s24", 360, 780, 24, 24, 3.0);
pub const GALAXY_S24_PLUS: DeviceProfile =
    DeviceProfile::galaxy("galaxy_s24plus", 384, 832, 24, 24, 3.2);
pub const GALAXY_S25: DeviceProfile =
    DeviceProfile::galaxy("galaxy_s25", 360, 780, 24, 24, 3.0);
pub const GALAXY_S25_ULTRA: DeviceProfile =
    DeviceProfile::galaxy("galaxy_s25ultra", 384, 832, 24, 24, 3.2);

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
