//! `Platform.*` in `.mui`: the system the app runs on, or the one a simulator
//! is showing, to show or hide parts of the UI per platform.
//!
//! ```text
//! if Platform.isMobile { TouchHint() }
//! Text("Running on ${Platform.os}")
//! ```
//!
//! Members: `os` (`macos`, `windows`, `linux`, `ios`, `android`, or a console:
//! `playstation`, `xbox`, `switch`), `device` (a simulated device's id, else
//! the os), and `isMobile`, `isTouch`, `isDesktop`, `isConsole`, `isIOS`,
//! `isAndroid`, `isMacOS`, `isWindows`, `isLinux` (`1` / `0`, so both
//! `if Platform.isTouch` and `Platform.isTouch == true` work).
//!
//! A simulator sets `MOCIDA_PLATFORM` (the os), `MOCIDA_PLATFORM_DEVICE` and
//! `MOCIDA_PLATFORM_TOUCH` (`1` / `0`) before the view is built.

fn env(name: &str) -> Option<String> {
    std::env::var(name).ok().map(|v| v.trim().to_lowercase()).filter(|v| !v.is_empty())
}

/// The platform's os, simulated or real.
pub fn os() -> String {
    env("MOCIDA_PLATFORM").unwrap_or_else(|| {
        if cfg!(target_os = "ios") {
            "ios"
        } else if cfg!(target_os = "android") {
            "android"
        } else if cfg!(target_os = "macos") {
            "macos"
        } else if cfg!(windows) {
            "windows"
        } else if cfg!(target_arch = "wasm32") {
            "web"
        } else {
            "linux"
        }
        .to_string()
    })
}

fn flag(on: bool) -> String {
    if on { "1" } else { "0" }.to_string()
}

/// `Platform.<field>`, or None for an unknown member.
pub fn member(field: &str) -> Option<String> {
    let os = os();
    let mobile = matches!(os.as_str(), "ios" | "android");
    let console = matches!(os.as_str(), "playstation" | "xbox" | "switch");
    Some(match field {
        "os" => os,
        "device" => env("MOCIDA_PLATFORM_DEVICE").unwrap_or(os),
        "isMobile" => flag(mobile),
        "isConsole" => flag(console),
        "isDesktop" => flag(!mobile && !console && os != "web"),
        "isTouch" => flag(env("MOCIDA_PLATFORM_TOUCH").map_or(mobile, |v| v == "1" || v == "true")),
        "isIOS" => flag(os == "ios"),
        "isAndroid" => flag(os == "android"),
        "isMacOS" => flag(os == "macos"),
        "isWindows" => flag(os == "windows"),
        "isLinux" => flag(os == "linux"),
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn members() {
        // One test owns the env vars (tests run in parallel).
        std::env::set_var("MOCIDA_PLATFORM", "android");
        std::env::set_var("MOCIDA_PLATFORM_DEVICE", "pixel-9-pro");
        std::env::remove_var("MOCIDA_PLATFORM_TOUCH");
        assert_eq!(member("os").as_deref(), Some("android"));
        assert_eq!(member("device").as_deref(), Some("pixel-9-pro"));
        assert_eq!((member("isMobile").unwrap(), member("isTouch").unwrap(), member("isDesktop").unwrap()), ("1".into(), "1".into(), "0".into()));
        std::env::set_var("MOCIDA_PLATFORM", "switch");
        std::env::set_var("MOCIDA_PLATFORM_TOUCH", "0");
        assert_eq!((member("isConsole").unwrap(), member("isTouch").unwrap()), ("1".into(), "0".into()));
        assert_eq!(member("nope"), None);
        std::env::remove_var("MOCIDA_PLATFORM");
        std::env::remove_var("MOCIDA_PLATFORM_DEVICE");
        std::env::remove_var("MOCIDA_PLATFORM_TOUCH");
    }
}
