//! `mui-dev` — render a `.mui` / `.crm` file in a mocida window, with
//! **hot-reload**: edit the file and the window updates in place (no close,
//! no restart). This is M2 (live design) on top of the M1/M3 runtime.
//!
//! ```text
//! cargo run -p mui-dev -- path/to/hello.mui
//! ```
//!
//! How hot-reload works (no perf cost on the render path):
//! - A background thread polls the file's modified-time (~every 150 ms) and
//!   flips an `AtomicBool` when it changes. That's off the UI thread.
//! - `App::on_tick` runs once per frame on the UI thread; it does a single
//!   atomic load and, only on an actual change, re-parses the file and swaps
//!   the widget tree via `set_children`. The old reactive state (signals +
//!   subscriptions) is dropped as the new one replaces it.
//!
//! With no argument it renders a small built-in sample (no file to watch).

use std::cell::{Cell, RefCell};
use std::collections::HashMap;
use std::ffi::c_void;
use std::path::PathBuf;
use std::process::ExitCode;
use std::rc::Rc;
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, SystemTime};

use mocida::menu_bar::Menu;
use mocida::text::{get_font, search_fonts};
use mocida::{App, Children, Color};
use mui_runtime::device_frames;
use mui_runtime::device_profiles::{self, DeviceProfile, PROFILE_NAMES};
use mui_runtime::device_simulator::DeviceOrientation;
use mui_runtime::Reactive;
use mui_runtime::Runtime;

// ── Simulator menu item IDs ──────────────────────────────────────────────────
const SIM_ROTATE: u32 = 900;
const SIM_SCALE_100: u32 = 901;
const SIM_SCALE_75: u32 = 902;
const SIM_SCALE_50: u32 = 903;
const SIM_SCALE_FIT: u32 = 904;
const SIM_RESTART: u32 = 905;
const SIM_ZOOM: u32 = 906;
const SIM_SCREENSHOT: u32 = 907;
const SIM_SCREENSHOT_SAVED: u32 = 908;
/// `SIM_DEVICE_BASE + i` switches to `MENU_DEVICES[i]`.
const SIM_DEVICE_BASE: u32 = 920;

/// The Simulator ▸ Device menu (every `--list-devices` id works with --device).
const MENU_DEVICES: &[&str] = &[
    "iphone-17-pro-max", "iphone-17-pro", "iphone-air", "iphone-16", "iphone-13-mini", "iphone-8",
    "pixel-9-pro-xl", "pixel-9-pro", "pixel-8", "galaxy-s21",
    "ipad-pro-13-m4-m5", "ipad-mini-8-3-a17-pro", "pixel-tablet", "galaxy-tab-s11-ultra",
];

/// What the simulator shows: a device profile and the images around it.
#[derive(Clone)]
struct Sim {
    profile: DeviceProfile,
    /// A real device's frame and screen mask (device-frames catalog).
    frames: Option<(PathBuf, PathBuf)>,
    /// A plain frame PNG for the built-in placeholder profiles (--device-frame).
    legacy_frame: Option<String>,
    label: String,
}

/// A catalog device (`iphone-17-pro`, `pixel-9-pro`…), downloading its frame
/// the first time, or one of the placeholder profiles (`iphone15`…).
fn sim_for(name: &str, color: Option<&str>, legacy_frame: Option<&str>) -> Result<Sim, String> {
    let sim = sim_device(name, color, legacy_frame)?;
    // `Platform.*` in the .mui reports the simulated device.
    let id = name.to_lowercase();
    let os = device_frames::lookup(name).map_or(if id.starts_with("galaxy") { "android" } else { "ios" }, |d| d.os);
    std::env::set_var("MOCIDA_PLATFORM", os);
    std::env::set_var("MOCIDA_PLATFORM_DEVICE", device_frames::lookup(name).map_or(id.as_str(), |d| d.id));
    std::env::set_var("MOCIDA_PLATFORM_TOUCH", "1");
    Ok(sim)
}

fn sim_device(name: &str, color: Option<&str>, legacy_frame: Option<&str>) -> Result<Sim, String> {
    if let Some(dev) = device_frames::lookup(name) {
        let v = dev.variant(color);
        println!("mui-dev: preparing the {} ({}) frame…", dev.name, v.name);
        let files = device_frames::ensure_files(dev, v)?;
        return Ok(Sim {
            profile: dev.profile(),
            frames: Some(files),
            legacy_frame: None,
            label: format!("{} — {}", dev.name, v.name),
        });
    }
    let profile = device_profiles::lookup(name)
        .ok_or_else(|| format!("unknown device '{name}' (mui-dev --list-devices shows them)"))?;
    Ok(Sim { profile: *profile, frames: None, legacy_frame: legacy_frame.map(str::to_string), label: profile.name.to_string() })
}

/// The host scale that fits the framed device on the display the window is on.
fn fit_scale(app_ptr: *mut mocida::sys::UIApp, profile: &DeviceProfile, orientation: DeviceOrientation) -> f32 {
    let (fw, fh) = match orientation {
        DeviceOrientation::Portrait => (profile.frame_w, profile.frame_h),
        DeviceOrientation::Landscape => (profile.frame_h, profile.frame_w),
    };
    let (mut sw, mut sh) = (0, 0);
    unsafe { mocida::sys::UIApp_DeviceSimulatorHostDisplaySize(app_ptr, &mut sw, &mut sh) };
    let sh = sh - unsafe { mocida::sys::UIApp_DeviceSimulatorToolbarHeight() };
    if sw <= 0 || sh <= 0 || fw <= 0 || fh <= 0 {
        return 1.0;
    }
    (0.86 * sh as f32 / fh as f32).min(0.92 * sw as f32 / fw as f32).min(1.0)
}

fn print_devices() {
    println!("Device frames (https://github.com/jonnyjackson26/device-frames-media):\n");
    for d in device_frames::FRAME_DEVICES {
        let colors: Vec<&str> = d.variants.iter().map(|v| v.id).collect();
        println!("  {:<30} {:<26} {:>4}×{:<5} {}", d.id, d.name, d.points.0, d.points.1, colors.join(", "));
    }
    println!("\nPlaceholder profiles: {}", PROFILE_NAMES.join(", "));
}

/// `extern "C"` trampoline for menu item activations.
/// `user` is a raw pointer to a `Box<AtomicU32>` owned by `run()`.
unsafe extern "C" fn sim_menu_cb(id: u32, user: *mut c_void) {
    if !user.is_null() {
        (*(user as *const AtomicU32)).store(id, Ordering::Release);
    }
}

/// The simulator toolbar's buttons, routed into the same pending action.
unsafe extern "C" fn sim_toolbar_cb(action: std::os::raw::c_int, user: *mut c_void) {
    let id = match action {
        1 => SIM_ROTATE,
        2 => SIM_RESTART,
        3 => SIM_SCREENSHOT_SAVED,
        4 => SIM_ZOOM,
        _ => return,
    };
    sim_menu_cb(id, user);
}

const DEFAULT_SAMPLE: &str = r#"
view Hello(name: string = "world") {
  Stack(orientation: vertical, gap: 12, padding: 24) {
    Text("Hello, MUI!", size: 28, color: #0f172a)
    Text("Rendered by mui-runtime via mocida-rs.", size: 14, color: #64748b)
  }
}
"#;

#[derive(Debug, Clone)]
struct CliOptions {
    /// When set, minimize the window right after show so it doesn't steal
    /// focus from the desktop session the user is currently using. The
    /// process keeps running (the SDL event loop is alive) and the per-frame
    /// tick / hot-reload watcher continue to work — only the window is
    /// hidden. The user can restore it from the Dock or via `osascript`.
    minimize: bool,
    /// Move the window to the N-th display (0 = primary, 1 = first external,
    /// …). Combined with `minimize`, this lets the user run a validation
    /// build on a secondary monitor they aren't looking at and never see.
    display: Option<u32>,
    /// Device to simulate: a catalog id (`iphone-17-pro`, `pixel-9-pro`…) or a
    /// placeholder profile (`iphone15`, `galaxy_s24`…).
    device: Option<String>,
    /// The device's color (`cosmic-orange`); its first color by default.
    device_color: Option<String>,
    /// Host UI scale factor for the simulator (default: fit the display).
    device_scale: Option<f32>,
    /// Optional PNG frame asset to draw around the simulated screen.
    device_frame: Option<String>,
    /// Start the simulator in landscape orientation.
    landscape: bool,
    /// The `.mui` file to render. Mandatory (positional).
    path: Option<String>,
}

impl Default for CliOptions {
    fn default() -> Self {
        Self {
            minimize: false,
            display: None,
            device: None,
            device_color: None,
            device_scale: None,
            device_frame: None,
            landscape: false,
            path: None,
        }
    }
}

fn parse_args() -> CliOptions {
    let mut opts = CliOptions::default();
    let mut it = std::env::args().skip(1);
    while let Some(a) = it.next() {
        match a.as_str() {
            "--minimized" | "-m" => opts.minimize = true,
            "--display" | "-d" => {
                let v = require_arg(&a, &mut it);
                opts.display = Some(v.parse::<u32>().unwrap_or_else(|_| {
                    eprintln!("mui-dev: --display requires a valid number");
                    std::process::exit(2);
                }));
            }
            "--device" => {
                opts.device = Some(require_arg(&a, &mut it));
            }
            "--device-color" => {
                opts.device_color = Some(require_arg(&a, &mut it));
            }
            "--list-devices" => {
                print_devices();
                std::process::exit(0);
            }
            "--device-scale" => {
                let v = require_arg(&a, &mut it);
                let scale = v.parse::<f32>().unwrap_or_else(|_| {
                    eprintln!("mui-dev: --device-scale requires a valid number");
                    std::process::exit(2);
                });
                if scale <= 0.0 {
                    eprintln!("mui-dev: --device-scale must be positive");
                    std::process::exit(2);
                }
                opts.device_scale = Some(scale);
            }
            "--device-frame" => {
                opts.device_frame = Some(require_arg(&a, &mut it));
            }
            "--landscape" => opts.landscape = true,
            "--help" | "-h" => {
                eprintln!(
                    "usage: mui-dev [options] <file.mui>

                       --minimized, -m       start minimized (don't steal focus)
                       --display N, -d N     place window on display N (0 = primary)
                       --device <name>       simulate a device: iphone-17-pro, iphone-16,
                                             pixel-9-pro, galaxy-s21, ipad-pro-13-m4-m5…
                                             (real frames, downloaded once and cached)
                       --device-color <c>    the device's color (e.g. cosmic-orange)
                       --list-devices        every device and its colors
                       --device-scale <s>    host UI scale factor (default: fit the display)
                       --device-frame <png>  frame PNG for a placeholder profile ({})
                       --landscape           start in landscape orientation

                     hot-reload: edit + save <file.mui> and the window updates in place.

                     simulator controls (when --device is active):
                       CmdOrCtrl+Shift+R     rotate device
                       CmdOrCtrl+1/2/3       scale 100% / 75% / 50%
                       CmdOrCtrl+0           fit the display",
                    PROFILE_NAMES.join(", ")
                );
                std::process::exit(0);
            }
            other if !other.starts_with('-') => opts.path = Some(other.to_string()),
            _ => {
                eprintln!("mui-dev: unknown flag '{a}' (try --help)");
                std::process::exit(2);
            }
        }
    }
    opts
}

fn require_arg(flag: &str, it: &mut impl Iterator<Item = String>) -> String {
    match it.next() {
        Some(v) => v,
        None => {
            eprintln!("mui-dev: '{flag}' requires an argument (try --help)");
            std::process::exit(2);
        }
    }
}

fn main() -> ExitCode {
    let opts = parse_args();
    if opts.path.is_none() {
        eprintln!("mui-dev: missing <file.mui> (try --help)");
        return ExitCode::from(2);
    }
    match run(opts) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("mui-dev: {e}");
            ExitCode::FAILURE
        }
    }
}

/// Build the entry view of `source` into a tree + its reactive state. When
/// `path` is given, `import { … } from "…"` components are resolved relative to
/// it. On a parse error, returns `Err` so the caller (hot-reload) keeps the
/// last good tree instead of swapping in a broken/degraded one.
struct Built {
    children: Children,
    reactive: Reactive,
    /// All files that fed this build (entry + imports) — the hot-reload watch
    /// set, so editing an imported component reloads too.
    sources: Vec<PathBuf>,
}

fn build(source: &str, path: Option<&str>) -> Result<Built, Box<dyn std::error::Error>> {
    build_seeded(source, path, &std::collections::HashMap::new())
}

/// Like [`build`] but seeds signal values from `seed`, so a rebuild triggered by
/// a reactive (structural) change — `if phase == ...` — preserves the live
/// state captured from the previous tree.
fn build_seeded(
    source: &str,
    path: Option<&str>,
    seed: &std::collections::HashMap<String, String>,
) -> Result<Built, Box<dyn std::error::Error>> {
    // Load the entry + its imported components (the loader re-parses the entry,
    // which is cheap; it gives us the registry for instantiating `Card(...)`).
    let entry_path = path
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("<sample>.mui"));
    let loaded = mui_syntax::loader::load_from(source, &entry_path);
    for w in &loaded.warnings {
        eprintln!("mui-dev: import: {w}");
    }
    let doc = &loaded.entry;
    if !doc.errors.is_empty() {
        let first = &doc.errors[0];
        return Err(format!(
            "parse error: {} (+{} more)",
            first.message,
            doc.errors.len() - 1
        )
        .into());
    }
    let view = mui_runtime::entry_view(doc).ok_or("no `view` found to render")?;
    // Instantiate against the full registry (imported components + this file's
    // own sibling views), so a view can render another view defined alongside it.
    let components = loaded.registry();
    let (children, reactive) = mui_runtime::build_view_seeded(view, &components, seed)?;
    Ok(Built {
        children,
        reactive,
        sources: loaded.sources,
    })
}

// ── Simulator helpers ────────────────────────────────────────────────────────

/// Apply window size constraints for a device simulator at the given scale and
/// orientation. All three of current / min / max are locked to the same value
/// so the user cannot resize or maximize.
fn apply_sim_window_size(
    app_ptr: *mut mocida::sys::UIApp,
    profile: &DeviceProfile,
    orientation: DeviceOrientation,
    scale: f32,
) {
    let (fw, fh) = match orientation {
        DeviceOrientation::Portrait => (profile.frame_w, profile.frame_h),
        DeviceOrientation::Landscape => (profile.frame_h, profile.frame_w),
    };
    let w = (fw as f32 * scale) as i32;
    // The toolbar (rotate, screenshot, restart, zoom) sits above the device.
    let h = (fh as f32 * scale) as i32 + unsafe { mocida::sys::UIApp_DeviceSimulatorToolbarHeight() };
    if std::env::var_os("MOCIDA_CONTROL").is_some() {
        println!("mui-dev: simulator window {w}×{h}");
    }
    // Loosen the limits first: a new minimum taller than the old maximum
    // (turning landscape → portrait) is refused otherwise.
    unsafe {
        mocida::sys::UIApp_SetMinSize(app_ptr, 1, 1);
        mocida::sys::UIApp_SetMaxSize(app_ptr, 100_000, 100_000);
        mocida::sys::UIApp_SetWindowSize(app_ptr, w, h);
        mocida::sys::UIApp_SetMinSize(app_ptr, w, h);
        mocida::sys::UIApp_SetMaxSize(app_ptr, w, h);
    }
}

/// Enable (or re-enable) the device simulator after a device, rotation or
/// scale change.
fn enable_sim(app_ptr: *mut mocida::sys::UIApp, sim: &Sim, scale: f32, orientation: DeviceOrientation) {
    // Safety: `app_ptr` is the live app and outlives this temporary Runtime view.
    // `from_raw` sets `drop_app: false`, so the Runtime's Drop is a no-op here.
    let mut rt = unsafe { Runtime::from_raw(app_ptr) };
    let result = match &sim.frames {
        Some((frame, mask)) => mui_runtime::device_simulator::enable_frames(&mut rt, &sim.profile, frame, mask, scale, orientation),
        None => rt.enable_device_simulator(&sim.profile, sim.legacy_frame.as_deref(), scale, orientation),
    };
    if let Err(e) = result {
        eprintln!("mui-dev: simulator enable failed: {e}");
    }
}

/// Window title: the device, its color and the orientation.
fn sim_title(app_ptr: *mut mocida::sys::UIApp, sim: &Sim, orientation: DeviceOrientation) {
    let ori = match orientation {
        DeviceOrientation::Portrait => "Portrait",
        DeviceOrientation::Landscape => "Landscape",
    };
    let title = std::ffi::CString::new(format!("{} — Simulator ({ori})", sim.label)).unwrap_or_default();
    unsafe { mocida::sys::UIApp_SetWindowTitle(app_ptr, title.as_ptr()) };
}

fn run(opts: CliOptions) -> Result<(), Box<dyn std::error::Error>> {
    let path = opts.path;
    let (label, source) = match &path {
        Some(p) => (p.clone(), std::fs::read_to_string(p)?),
        None => ("<built-in sample>".to_string(), DEFAULT_SAMPLE.to_string()),
    };

    // Resolve window + bundle config from the document's `App() { }` block, then
    // create the window FIRST — so the view build (which may read `Screen.*` or
    // call `Screen.alwaysOnTop = …`) sees a live window/display, not zeros.
    let doc = mui_syntax::parse(&source);
    let view_name = mui_runtime::entry_view(&doc)
        .map(|v| v.name.clone())
        .unwrap_or_else(|| "MUI".to_string());
    let cfg = mui_runtime::window_config(&doc, &view_name);

    // Renderer backend must be chosen BEFORE *any* SDL call.
    mui_runtime::prefer_renderer(&cfg);

    // Load an `app.bundle` sitting next to the .mui file.
    if let Some(p) = path.as_deref() {
        if let Some(dir) = std::path::Path::new(p).parent() {
            let bundle = dir.join("app.bundle");
            if bundle.is_file() {
                if mocida::bundle::load_manifest(&bundle.to_string_lossy()) {
                    println!("mui-dev: loaded {}", bundle.display());
                } else {
                    eprintln!("mui-dev: failed to load {}", bundle.display());
                }
            }
        }
    }

    if cfg.backdrop.is_some() {
        use mocida::BackdropMaterial as M;
        let material = M::from_effect(cfg.backdrop.as_deref().unwrap_or("auto"));
        let wants_transparent = if material == M::None {
            false
        } else if cfg!(target_os = "windows") {
            !matches!(material, M::Mica | M::MicaAlt)
        } else {
            true
        };
        if wants_transparent {
            mocida::app::request_transparent(true);
        }
    }

    let mut app = App::new(&cfg.title, cfg.width, cfg.height)?;
    let (br, bg_, bb, _ba) = cfg.background;
    app.set_background_color(Color::rgb(br as i32, bg_ as i32, bb as i32));
    if let Some(name) = &cfg.name {
        mocida::bundle::set_name(name);
        let _ = app.set_name(name);
    }
    if let Some(id) = &cfg.id {
        let _ = app.set_app_id(id);
    }
    mui_runtime::set_default_window_icon(&mut app);
    if cfg.min_width > 0 || cfg.min_height > 0 {
        app.set_min_size(cfg.min_width, cfg.min_height);
    }
    if cfg.max_width > 0 || cfg.max_height > 0 {
        app.set_max_size(cfg.max_width, cfg.max_height);
    }
    mui_runtime::apply_render_config(&mut app, &cfg);
    mui_runtime::apply_backdrop(&cfg);

    // ── Device simulator setup ───────────────────────────────────────────────

    // Simulator state shared between setup and the on_tick closure.
    // `None` when --device was not passed.
    let sim_state: Rc<RefCell<Option<Sim>>> = Rc::new(RefCell::new(None));
    let sim_orientation = Rc::new(Cell::new(DeviceOrientation::Portrait));
    let sim_scale = Rc::new(Cell::new(1.0f32));
    // Explicit --device-scale / Ctrl+1-3 pin the scale; otherwise it fits the display.
    let sim_fit = Rc::new(Cell::new(opts.device_scale.is_none()));

    // Pending menu action from the `extern "C"` callback. Boxed so it has a
    // stable heap address we can hand to the C callback as a raw pointer.
    let sim_action: Box<AtomicU32> = Box::new(AtomicU32::new(0));
    // Raw pointer valid for the entire lifetime of `run()`.
    let sim_action_ptr: *const AtomicU32 = Box::as_ref(&sim_action);

    if let Some(name) = &opts.device {
        let sim = match sim_for(name, opts.device_color.as_deref(), opts.device_frame.as_deref()) {
            Ok(sim) => sim,
            Err(e) => {
                eprintln!("mui-dev: {e}");
                std::process::exit(2);
            }
        };

        let orientation = if opts.landscape {
            DeviceOrientation::Landscape
        } else {
            DeviceOrientation::Portrait
        };
        // The toolbar above the device; its buttons land in sim_action.
        unsafe {
            mocida::sys::UIApp_SetDeviceSimulatorToolbar(app.as_ptr(), 1, Some(sim_toolbar_cb), sim_action_ptr as *mut c_void)
        };
        let scale = opts.device_scale.unwrap_or_else(|| fit_scale(app.as_ptr(), &sim.profile, orientation));

        // Lock the window to device frame dimensions — no resize, no maximize.
        apply_sim_window_size(app.as_ptr(), &sim.profile, orientation, scale);
        enable_sim(app.as_ptr(), &sim, scale, orientation);

        // The simulator window floats above other windows, like iOS Simulator.
        unsafe { mocida::sys::UIApp_SetAlwaysOnTop(1) };
        sim_title(app.as_ptr(), &sim, orientation);

        // ── Menu bar: "Simulator" ──────────────────────────────────────────
        // Works as the macOS app menu on macOS and is tracked (with shortcuts)
        // on all platforms via SDL's accelerator system.
        // iPhones, Android phones, then tablets, split by separators.
        let mut devices = Menu::new("Device");
        let mut group = ("", "");
        for (i, id) in MENU_DEVICES.iter().enumerate() {
            let Some(d) = device_frames::lookup(id) else { continue };
            if !group.0.is_empty() && group != (d.os, d.kind) {
                devices = devices.separator();
            }
            group = (d.os, d.kind);
            devices = devices.item(d.name, None, SIM_DEVICE_BASE + i as u32);
        }
        Menu::new("Simulator")
            .submenu(devices)
            .submenu(
                Menu::new("View")
                    .item("Rotate", Some("CmdOrCtrl+Shift+R"), SIM_ROTATE)
                    .item("Screenshot", Some("CmdOrCtrl+S"), SIM_SCREENSHOT)
                    .item("Restart App", Some("CmdOrCtrl+Shift+K"), SIM_RESTART)
                    .separator()
                    .item("Fit the Display", Some("CmdOrCtrl+0"), SIM_SCALE_FIT)
                    .item("Scale 100%", Some("CmdOrCtrl+1"), SIM_SCALE_100)
                    .item("Scale 75%", Some("CmdOrCtrl+2"), SIM_SCALE_75)
                    .item("Scale 50%", Some("CmdOrCtrl+3"), SIM_SCALE_50),
            )
            .set_as_app_menu_bar();

        // Register the extern "C" callback that writes the activated item's
        // ID into sim_action. Safety: sim_action outlives the run loop.
        unsafe {
            mocida::sys::menu_bar::set_item_callback(
                Some(sim_menu_cb),
                sim_action_ptr as *mut c_void,
            )
        };

        println!(
            "mui-dev: simulating {}  (Simulator menu: devices; Ctrl+Shift+R = rotate, Ctrl+0-3 = scale)",
            sim.label
        );
        // Stash simulator state for the on_tick closure.
        *sim_state.borrow_mut() = Some(sim);
        sim_orientation.set(orientation);
        sim_scale.set(scale);
    }

    // ── Font + first build ───────────────────────────────────────────────────

    search_fonts();
    let _ = get_font("Arial");

    let Built {
        children,
        reactive,
        sources,
    } = build(&source, path.as_deref())?;
    println!("mui-dev: rendering view `{view_name}` from {label}");

    let reactive_slot: Rc<RefCell<Option<Reactive>>> = Rc::new(RefCell::new(Some(reactive)));
    app.set_children(children);

    // ── Hot-reload + simulator on_tick ───────────────────────────────────────

    if let Some(p) = path {
        let watch_path = PathBuf::from(&p);
        let mut initial = sources;
        if !initial.iter().any(|s| same_file(s, &watch_path)) {
            initial.push(watch_path.clone());
        }
        let watch_list: Arc<Mutex<Vec<PathBuf>>> = Arc::new(Mutex::new(initial));
        let dirty = Arc::new(AtomicBool::new(false));
        spawn_watcher(watch_list.clone(), dirty.clone());

        let app_ptr = app.as_ptr();
        let reactive_slot = reactive_slot.clone();
        let watch_list_tick = watch_list.clone();
        let source_cell = Rc::new(RefCell::new(source.clone()));
        let last_size = Rc::new(Cell::new((0i32, 0i32, 0i32, 0i32)));

        // Clone Rc handles for the closure.
        let sim_tick = sim_state.clone();
        let sim_orientation_tick = sim_orientation.clone();
        let sim_scale_tick = sim_scale.clone();
        let sim_fit_tick = sim_fit.clone();

        println!(
            "mui-dev: watching {p} (+{} imported file(s)) for changes — edit + save to hot-reload",
            watch_list
                .lock()
                .map(|l| l.len().saturating_sub(1))
                .unwrap_or(0)
        );

        app.on_tick(move || {
            // (0) Simulator menu action (device / rotate / scale) ───────────
            // Safety: sim_action_ptr is valid for the entire run() lifetime.
            let action = unsafe { (*sim_action_ptr).swap(0, Ordering::AcqRel) };
            if action != 0 && sim_tick.borrow().is_some() {
                let mut ori = sim_orientation_tick.get();
                match action {
                    SIM_ROTATE => ori = ori.toggle(),
                    SIM_SCALE_FIT => sim_fit_tick.set(true),
                    SIM_SCREENSHOT => {
                        unsafe { mocida::sys::UIApp_DeviceSimulatorTakeScreenshot(app_ptr) };
                        return;
                    }
                    SIM_SCREENSHOT_SAVED => {
                        let path = unsafe { std::ffi::CStr::from_ptr(mocida::sys::UIApp_DeviceSimulatorLastScreenshot()) };
                        println!("mui-dev: screenshot saved to {}", path.to_string_lossy());
                        return;
                    }
                    SIM_RESTART => {
                        // Start the app over: a fresh build, every signal back to its initial value.
                        let src = std::fs::read_to_string(&watch_path).unwrap_or_else(|_| source_cell.borrow().clone());
                        match build_seeded(&src, watch_path.to_str(), &HashMap::new()) {
                            Ok(built) => {
                                unsafe { mocida::sys::UIApp_SetChildren(app_ptr, built.children.into_raw()) };
                                *reactive_slot.borrow_mut() = Some(built.reactive);
                                *source_cell.borrow_mut() = src;
                                println!("mui-dev: app restarted");
                            }
                            Err(e) => eprintln!("mui-dev: restart failed: {e}"),
                        }
                        return;
                    }
                    // Zoom cycles through the sizes that fit the display: the
                    // fitted size, then 100% / 75% / 50% when smaller, then back.
                    SIM_ZOOM => {
                        let fit = sim_tick.borrow().as_ref().map_or(1.0, |sim| fit_scale(app_ptr, &sim.profile, ori));
                        let current = if sim_fit_tick.get() { fit } else { sim_scale_tick.get() };
                        let next = [1.0f32, 0.75, 0.5].into_iter().find(|&z| z < current - 0.01 && z <= fit);
                        match next {
                            Some(s) => {
                                sim_fit_tick.set(false);
                                sim_scale_tick.set(s);
                            }
                            None => sim_fit_tick.set(true),
                        }
                    }
                    SIM_SCALE_100 | SIM_SCALE_75 | SIM_SCALE_50 => {
                        sim_fit_tick.set(false);
                        sim_scale_tick.set(match action {
                            SIM_SCALE_100 => 1.0,
                            SIM_SCALE_75 => 0.75,
                            _ => 0.5,
                        });
                    }
                    id if id >= SIM_DEVICE_BASE && ((id - SIM_DEVICE_BASE) as usize) < MENU_DEVICES.len() => {
                        match sim_for(MENU_DEVICES[(id - SIM_DEVICE_BASE) as usize], None, None) {
                            Ok(sim) => {
                                println!("mui-dev: simulating {}", sim.label);
                                *sim_tick.borrow_mut() = Some(sim);
                                // Rebuild so `Platform.*` checks see the new device.
                                let seed = reactive_slot.borrow().as_ref().map(|r| r.values()).unwrap_or_default();
                                let src = source_cell.borrow().clone();
                                if let Ok(built) = build_seeded(&src, watch_path.to_str(), &seed) {
                                    unsafe { mocida::sys::UIApp_SetChildren(app_ptr, built.children.into_raw()) };
                                    *reactive_slot.borrow_mut() = Some(built.reactive);
                                }
                            }
                            Err(e) => eprintln!("mui-dev: {e}"),
                        }
                    }
                    _ => {}
                }
                sim_orientation_tick.set(ori);
                if let Some(sim) = sim_tick.borrow().as_ref() {
                    if sim_fit_tick.get() {
                        sim_scale_tick.set(fit_scale(app_ptr, &sim.profile, ori));
                    }
                    let scale = sim_scale_tick.get();
                    apply_sim_window_size(app_ptr, &sim.profile, ori, scale);
                    enable_sim(app_ptr, sim, scale, ori);
                    sim_title(app_ptr, sim, ori);
                }
            }

            // (1) Window resize / monitor change → reactive rebuild ───────────
            let cur_size = unsafe {
                (
                    mocida::sys::UIApp_GetWidthG(),
                    mocida::sys::UIApp_GetHeightG(),
                    mocida::sys::UIScreen_GetWidth(),
                    mocida::sys::UIScreen_GetHeight(),
                )
            };
            if cur_size.0 > 0 && cur_size.1 > 0 && cur_size != last_size.get() {
                last_size.set(cur_size);
                let seed = reactive_slot
                    .borrow()
                    .as_ref()
                    .map(|r| r.values())
                    .unwrap_or_default();
                let src = source_cell.borrow().clone();
                if let Ok(built) = build_seeded(&src, watch_path.to_str(), &seed) {
                    let raw = built.children.into_raw();
                    unsafe { mocida::sys::UIApp_SetChildren(app_ptr, raw) };
                    *reactive_slot.borrow_mut() = Some(built.reactive);
                }
            }

            // (2) Structural signal change → reactive rebuild ─────────────────
            let dirty_signal = reactive_slot
                .borrow()
                .as_ref()
                .map(|r| r.take_dirty())
                .unwrap_or(false);
            if dirty_signal {
                let seed = reactive_slot
                    .borrow()
                    .as_ref()
                    .map(|r| r.values())
                    .unwrap_or_default();
                let src = source_cell.borrow().clone();
                match build_seeded(&src, watch_path.to_str(), &seed) {
                    Ok(built) => {
                        let raw = built.children.into_raw();
                        unsafe { mocida::sys::UIApp_SetChildren(app_ptr, raw) };
                        *reactive_slot.borrow_mut() = Some(built.reactive);
                    }
                    Err(e) => eprintln!("mui-dev: reactive rebuild failed: {e}"),
                }
            }

            // (3) File change → full hot-reload ──────────────────────────────
            if !dirty.swap(false, Ordering::AcqRel) {
                return;
            }
            let s = match std::fs::read_to_string(&watch_path) {
                Ok(s) => s,
                Err(e) => {
                    eprintln!("mui-dev: reload failed: {e}");
                    return;
                }
            };
            match build(&s, watch_path.to_str()) {
                Ok(built) => {
                    *source_cell.borrow_mut() = s;
                    let raw = built.children.into_raw();
                    unsafe { mocida::sys::UIApp_SetChildren(app_ptr, raw) };
                    *reactive_slot.borrow_mut() = Some(built.reactive);
                    let mut next = built.sources;
                    if !next.iter().any(|s| same_file(s, &watch_path)) {
                        next.push(watch_path.clone());
                    }
                    if let Ok(mut list) = watch_list_tick.lock() {
                        *list = next;
                    }
                    println!("mui-dev: reloaded {}", watch_path.display());
                }
                Err(e) => {
                    eprintln!("mui-dev: reload failed: {e}");
                }
            }
        });
    }

    if opts.minimize && std::env::var_os("MUI_DEV_VISIBLE").is_none() {
        mocida::app::window_minimize();
    }
    app.show().run();
    drop(reactive_slot);
    // Keep sim_action alive through the entire run loop — the menu callback
    // holds a raw pointer into it.
    drop(sim_action);
    Ok(())
}

/// Spawn a background thread that flips `dirty` whenever ANY watched file's
/// modified-time changes. The watched set (`paths`) is the whole import graph
/// and is refreshed by the UI tick after each reload, so newly-added imports
/// start being watched too. A coarse 150 ms poll — cheap, off the UI thread,
/// and plenty responsive for editing. No external crate, no notification API.
fn spawn_watcher(paths: Arc<Mutex<Vec<PathBuf>>>, dirty: Arc<AtomicBool>) {
    std::thread::spawn(move || {
        let mtime = |p: &PathBuf| -> Option<SystemTime> {
            std::fs::metadata(p).and_then(|m| m.modified()).ok()
        };
        let mut last: HashMap<PathBuf, Option<SystemTime>> = HashMap::new();
        if let Ok(list) = paths.lock() {
            for p in list.iter() {
                last.insert(p.clone(), mtime(p));
            }
        }
        loop {
            std::thread::sleep(Duration::from_millis(150));
            let list = match paths.lock() {
                Ok(l) => l.clone(),
                Err(_) => continue,
            };
            let mut changed = false;
            for p in &list {
                let now = mtime(p);
                match last.get(p) {
                    Some(prev) if *prev == now => {}
                    _ => {
                        last.insert(p.clone(), now);
                        changed = true;
                    }
                }
            }
            if changed {
                dirty.store(true, Ordering::Release);
            }
        }
    });
}

/// Best-effort "same file" check across possibly-different path spellings
/// (canonical vs the raw CLI arg). Falls back to a plain path compare.
fn same_file(a: &std::path::Path, b: &std::path::Path) -> bool {
    match (std::fs::canonicalize(a), std::fs::canonicalize(b)) {
        (Ok(ca), Ok(cb)) => ca == cb,
        _ => a == b,
    }
}
