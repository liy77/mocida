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
use std::path::PathBuf;
use std::process::ExitCode;
use std::rc::Rc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, SystemTime};

use mocida::text::{get_font, search_fonts};
use mocida::{App, Children, Color};
use mui_runtime::Reactive;

const DEFAULT_SAMPLE: &str = r#"
view Hello(name: string = "world") {
  Stack(orientation: vertical, gap: 12, padding: 24) {
    Text("Hello, MUI!", size: 28, color: #0f172a)
    Text("Rendered by mui-runtime via mocida-rs.", size: 14, color: #64748b)
  }
}
"#;

fn main() -> ExitCode {
    let path = std::env::args().nth(1);
    match run(path) {
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
    view_name: String,
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
        view_name: view.name.clone(),
        sources: loaded.sources,
    })
}

fn run(path: Option<String>) -> Result<(), Box<dyn std::error::Error>> {
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

    // Renderer backend must be chosen BEFORE *any* SDL call (the env-var hint is
    // read at SDL init; loading a bundle or creating the window both touch SDL).
    mui_runtime::prefer_renderer(&cfg);

    // Load an `app.bundle` sitting next to the .mui file, so a sibling manifest
    // is read regardless of the CWD (`App::new` only auto-loads ./app.bundle).
    // This registers the app name/id and any `mocida://` assets it declares.
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
    // Give the dev window a real icon (a rounded "block") instead of the SDL
    // default, so the title bar / taskbar look intentional.
    mui_runtime::set_default_window_icon(&mut app);
    // Min/max window size from the App block (desktop; 0 = unconstrained).
    if cfg.min_width > 0 || cfg.min_height > 0 {
        app.set_min_size(cfg.min_width, cfg.min_height);
    }
    if cfg.max_width > 0 || cfg.max_height > 0 {
        app.set_max_size(cfg.max_width, cfg.max_height);
    }
    // Renderer backend + AA/MSAA tuning from the App block.
    mui_runtime::apply_render_config(&mut app, &cfg);
    search_fonts();
    let _ = get_font("Arial");

    // Now build the view (window exists → `Screen.*` resolves).
    let Built {
        children,
        reactive,
        view_name: _,
        sources,
    } = build(&source, path.as_deref())?;
    println!("mui-dev: rendering view `{view_name}` from {label}");

    // Hold the live reactive state in a slot so the tick closure can replace
    // it (dropping the old signals/subscriptions) when the file changes.
    let reactive_slot: Rc<RefCell<Option<Reactive>>> = Rc::new(RefCell::new(Some(reactive)));
    app.set_children(children);

    // Hot-reload only makes sense for a real file. With the built-in sample
    // there's nothing to watch, so just run.
    if let Some(p) = path {
        let watch_path = PathBuf::from(&p);
        // The set of files to watch = the whole import graph (entry + imported
        // components). It's shared with the watcher thread and refreshed by the
        // tick after each reload (an edit can add/remove imports). Seed it with
        // the entry too, in case the loader couldn't canonicalize it.
        let mut initial = sources;
        if !initial.iter().any(|s| same_file(s, &watch_path)) {
            initial.push(watch_path.clone());
        }
        let watch_list: Arc<Mutex<Vec<PathBuf>>> = Arc::new(Mutex::new(initial));
        // `dirty` is flipped by the watcher thread, read by the UI tick.
        let dirty = Arc::new(AtomicBool::new(false));
        spawn_watcher(watch_list.clone(), dirty.clone());

        // The tick closure runs each frame on the UI thread. Capture the raw
        // app pointer so it can swap children without borrowing `app` (which
        // is borrowed by `run()`); single-threaded, so this is sound.
        let app_ptr = app.as_ptr();
        let reactive_slot = reactive_slot.clone();
        let watch_list_tick = watch_list.clone();
        // The current source text, refreshed on each file reload. The reactive
        // rebuild path re-runs the build from this (no file read) so a screen
        // switch doesn't depend on disk.
        let source_cell = Rc::new(RefCell::new(source.clone()));
        // Last (window w, window h, screen w, screen h) we built against —
        // seeded zeros so the first tick rebuilds once against the *realized*
        // size (also catches DPI settle), then on any window resize or monitor
        // change (the display the window sits on changing resolution).
        let last_size = Rc::new(Cell::new((0i32, 0i32, 0i32, 0i32)));
        println!(
            "mui-dev: watching {p} (+{} imported file(s)) for changes — edit + save to hot-reload",
            watch_list.lock().map(|l| l.len().saturating_sub(1)).unwrap_or(0)
        );
        app.on_tick(move || {
            // (0) Window resize / monitor change → reactive rebuild, so
            // `Window.*`/`Screen.*` and any `width: Window.width - N` re-resolve.
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

            // (1) Structural signal change (e.g. `phase` switched, or a `for`
            // list grew) → rebuild + swap, seeding from the live values so the
            // user's typed input / current state survives the swap. Cheap: just
            // a Cell read unless something actually changed.
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

            // (2) File change → full hot-reload (fresh state, re-read imports).
            if !dirty.swap(false, Ordering::AcqRel) {
                return; // no file change — the common case, ~free
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
                    // Drop the OLD reactive state only after the new tree is in
                    // place. Replace children via the raw C call (we hold the
                    // app pointer, not a &mut App).
                    let raw = built.children.into_raw();
                    unsafe { mocida::sys::UIApp_SetChildren(app_ptr, raw) };
                    *reactive_slot.borrow_mut() = Some(built.reactive);
                    // Refresh the watch set — imports may have been added/removed.
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
                    // Keep the last good tree on a bad edit; just report it.
                    eprintln!("mui-dev: reload failed: {e}");
                }
            }
        });
    }

    app.show().run();
    // Keep the reactive state alive across the whole loop.
    drop(reactive_slot);
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
        // Per-file last-seen mtime. Seed it so the first poll doesn't fire a
        // spurious reload for files that already existed at startup.
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
                    // Changed, or a file new to the watch set (added import).
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
