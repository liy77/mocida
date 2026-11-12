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
use mui_runtime::device_profiles::{DeviceProfile, PROFILE_NAMES};
use mui_runtime::device_simulator::DeviceOrientation;
use mui_runtime::Reactive;
use mui_runtime::Runtime;

// ── Simulator menu item IDs ──────────────────────────────────────────────────
const SIM_ROTATE: u32 = 900;
const SIM_SCALE_100: u32 = 901;
const SIM_SCALE_75: u32 = 902;
const SIM_SCALE_50: u32 = 903;

/// `extern "C"` trampoline for menu item activations.
/// `user` is a raw pointer to a `Box<AtomicU32>` owned by `run()`.
unsafe extern "C" fn sim_menu_cb(id: u32, user: *mut c_void) {
    if !user.is_null() {
        (*(user as *const AtomicU32)).store(id, Ordering::Release);
    }
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
    /// Named device profile to simulate (e.g. `iphone15`, `galaxy_s24`).
    device: Option<String>,
    /// Host UI scale factor for the simulator (default 1.0).
    device_scale: f32,
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
            device_scale: 1.0,
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
            "--device-scale" => {
                let v = require_arg(&a, &mut it);
                opts.device_scale = v.parse::<f32>().unwrap_or_else(|_| {
                    eprintln!("mui-dev: --device-scale requires a valid number");
                    std::process::exit(2);
                });
                if opts.device_scale <= 0.0 {
                    eprintln!("mui-dev: --device-scale must be positive");
                    std::process::exit(2);
                }
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
                       --device <name>       enable simulator ({})
                       --device-scale <s>    host UI scale factor (default 1.0)
                       --device-frame <png>  optional PNG frame asset
                       --landscape           start in landscape orientation

                     hot-reload: edit + save <file.mui> and the window updates in place.

                     simulator controls (when --device is active):
                       CmdOrCtrl+Shift+R     rotate device
                       CmdOrCtrl+1/2/3       scale 100% / 75% / 50%",
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
    let h = (fh as f32 * scale) as i32;
    unsafe {
        mocida::sys::UIApp_SetWindowSize(app_ptr, w, h);
        mocida::sys::UIApp_SetMinSize(app_ptr, w, h);
        mocida::sys::UIApp_SetMaxSize(app_ptr, w, h);
    }
}

/// Enable (or re-enable) the device simulator after a rotation or scale change.
fn enable_sim(
    app_ptr: *mut mocida::sys::UIApp,
    profile: &DeviceProfile,
    frame_path: Option<&str>,
    scale: f32,
    orientation: DeviceOrientation,
) {
    // Safety: `app_ptr` is the live app and outlives this temporary Runtime view.
    // `from_raw` sets `drop_app: false`, so the Runtime's Drop is a no-op here.
    let mut rt = unsafe { Runtime::from_raw(app_ptr) };
    if let Err(e) = rt.enable_device_simulator(profile, frame_path, scale, orientation) {
        eprintln!("mui-dev: simulator enable failed: {e}");
    }
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
    let sim_profile: Option<&'static DeviceProfile> = None;
    let sim_frame_path: Option<Option<String>> = None;

    let sim_profile = Rc::new(Cell::new(sim_profile));
    let sim_frame_path = Rc::new(RefCell::new(sim_frame_path));
    let sim_orientation = Rc::new(Cell::new(DeviceOrientation::Portrait));
    let sim_scale = Rc::new(Cell::new(1.0f32));

    // Pending menu action from the `extern "C"` callback. Boxed so it has a
    // stable heap address we can hand to the C callback as a raw pointer.
    let sim_action: Box<AtomicU32> = Box::new(AtomicU32::new(0));
    // Raw pointer valid for the entire lifetime of `run()`.
    let sim_action_ptr: *const AtomicU32 = Box::as_ref(&sim_action);

    if let Some(name) = &opts.device {
        let Some(profile) = mui_runtime::device_profiles::lookup(name) else {
            eprintln!(
                "mui-dev: unknown device profile '{name}'\n\nSupported profiles: {}",
                PROFILE_NAMES.join(", ")
            );
            std::process::exit(2);
        };

        let orientation = if opts.landscape {
            DeviceOrientation::Landscape
        } else {
            DeviceOrientation::Portrait
        };
        let scale = opts.device_scale;

        // Lock the window to device frame dimensions — no resize, no maximize.
        apply_sim_window_size(app.as_ptr(), profile, orientation, scale);

        // Enable the C-side device simulator with the optional frame PNG.
        let frame_path = opts.device_frame.as_deref();
        enable_sim(app.as_ptr(), profile, frame_path, scale, orientation);

        // The simulator window floats above other windows, like iOS Simulator.
        unsafe { mocida::sys::UIApp_SetAlwaysOnTop(1) };

        // Title shows the device family and a hint for the rotate shortcut.
        let _ = app.set_window_title(&format!("{} — Simulator", profile.name));

        // ── Menu bar: "Simulator > Device" ─────────────────────────────────
        // Works as the macOS app menu on macOS and is tracked (with shortcuts)
        // on all platforms via SDL's accelerator system.
        Menu::new("Simulator")
            .submenu(
                Menu::new("Device")
                    .item("Rotate", Some("CmdOrCtrl+Shift+R"), SIM_ROTATE)
                    .separator()
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

        // Stash simulator state for the on_tick closure.
        sim_profile.set(Some(profile));
        *sim_frame_path.borrow_mut() = Some(opts.device_frame.clone());
        sim_orientation.set(orientation);
        sim_scale.set(scale);

        println!(
            "mui-dev: simulator active — {name}  \
             (Ctrl+Shift+R = rotate, Ctrl+1/2/3 = scale)"
        );
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
        let sim_profile_tick = sim_profile.clone();
        let sim_frame_path_tick = sim_frame_path.clone();
        let sim_orientation_tick = sim_orientation.clone();
        let sim_scale_tick = sim_scale.clone();

        println!(
            "mui-dev: watching {p} (+{} imported file(s)) for changes — edit + save to hot-reload",
            watch_list
                .lock()
                .map(|l| l.len().saturating_sub(1))
                .unwrap_or(0)
        );

        app.on_tick(move || {
            // (0) Simulator menu action (rotate / scale) ─────────────────────
            // Safety: sim_action_ptr is valid for the entire run() lifetime.
            let action = unsafe { (*sim_action_ptr).swap(0, Ordering::AcqRel) };
            if action != 0 {
                if let Some(profile) = sim_profile_tick.get() {
                    let fp_guard = sim_frame_path_tick.borrow();
                    let fp = fp_guard.as_ref().and_then(|o| o.as_deref());
                    let mut ori = sim_orientation_tick.get();
                    let mut scale = sim_scale_tick.get();

                    match action {
                        SIM_ROTATE => {
                            ori = ori.toggle();
                            sim_orientation_tick.set(ori);
                        }
                        SIM_SCALE_100 => {
                            scale = 1.00;
                            sim_scale_tick.set(scale);
                        }
                        SIM_SCALE_75 => {
                            scale = 0.75;
                            sim_scale_tick.set(scale);
                        }
                        SIM_SCALE_50 => {
                            scale = 0.50;
                            sim_scale_tick.set(scale);
                        }
                        _ => {}
                    }

                    apply_sim_window_size(app_ptr, profile, ori, scale);
                    enable_sim(app_ptr, profile, fp, scale, ori);

                    // Update title to reflect current orientation.
                    let ori_label = match ori {
                        DeviceOrientation::Portrait => "Portrait",
                        DeviceOrientation::Landscape => "Landscape",
                    };
                    let title = std::ffi::CString::new(format!(
                        "{} — Simulator ({})",
                        profile.name, ori_label
                    ))
                    .unwrap_or_default();
                    unsafe { mocida::sys::UIApp_SetWindowTitle(app_ptr, title.as_ptr()) };
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
