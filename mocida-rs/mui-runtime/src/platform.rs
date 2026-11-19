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
//! `isAndroid`, `isMacOS`, `isWindows`, `isLinux`, `isWeb`,
//! `prefersReducedMotion` (`1` / `0`, so both `if Platform.isTouch` and
//! `Platform.isTouch == true` work). On the web build, `isIOS` / `isAndroid`
//! follow the live User-Agent so a phone visiting the listen site lights up.
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
    // On the web build, prefer the live browser signal (phone/tablet/desktop)
    // so `if Platform.isMobile` works for a phone visiting a listen page.
    let web_device = if cfg!(target_arch = "wasm32") {
        Some(mocida::web::device())
    } else {
        None
    };
    let mobile = match web_device.as_deref() {
        Some("phone") | Some("tablet") => true,
        Some(_) => false,
        None => matches!(os.as_str(), "ios" | "android"),
    };
    let touch = env("MOCIDA_PLATFORM_TOUCH").map_or_else(
        || {
            if cfg!(target_arch = "wasm32") {
                mocida::web::is_touch() || mobile
            } else {
                mobile
            }
        },
        |v| v == "1" || v == "true",
    );
    // On the listen / WASM site, prefer live UA so `Platform.isIOS` /
    // `isAndroid` light up for Safari and Chrome mobile visitors.
    let ios = if cfg!(target_arch = "wasm32") {
        mocida::web::is_ios() || os == "ios"
    } else {
        os == "ios"
    };
    let android = if cfg!(target_arch = "wasm32") {
        mocida::web::is_android() || os == "android"
    } else {
        os == "android"
    };
    let console = matches!(os.as_str(), "playstation" | "xbox" | "switch");
    let reported_os = if ios {
        "ios".to_string()
    } else if android {
        "android".to_string()
    } else {
        os.clone()
    };
    Some(match field {
        "os" => reported_os,
        "isWeb" => flag(cfg!(target_arch = "wasm32")),
        "device" => env("MOCIDA_PLATFORM_DEVICE").unwrap_or_else(|| {
            web_device.unwrap_or(os)
        }),
        "isMobile" => flag(mobile || ios || android),
        "isConsole" => flag(console),
        "isDesktop" => flag(!mobile && !ios && !android && !console && !cfg!(target_arch = "wasm32")),
        "isTouch" => flag(touch),
        "isIOS" => flag(ios),
        "isAndroid" => flag(android),
        "isMacOS" => flag(os == "macos" && !ios),
        "isWindows" => flag(os == "windows"),
        "isLinux" => flag(os == "linux"),
        "prefersReducedMotion" => flag(if cfg!(target_arch = "wasm32") {
            mocida::web::prefers_reduced_motion()
        } else {
            false
        }),
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
