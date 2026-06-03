//! MUI runtime — builds a **reactive** mocida widget tree from the MUI
//! component AST.
//!
//! Covers M1 (static structure) **and** the M3 reactivity core: `mut x =
//! signal(n)` becomes a real mocida [`Signal`], `${x}` interpolation
//! subscribes the text widget to its signals (so it re-renders on change), and
//! event handlers (`onClick: { x += 1 }`) mutate the signal — which fires the
//! subscriptions synchronously. The counter example is fully live.
//!
//! Scope of the live subset (everything else still renders statically):
//! - State: `signal(<int>)` / `signal(<param>)` — integer signals.
//! - Interpolation: `"… ${x} …"` text, one-way reactive.
//! - Handlers: `x += n`, `x -= n`, `x++`, `x--`, `x = x ± n`, `x = <int>`
//!   (see [`mui_syntax::ast::HandlerAction`]).
//! - `Button(label, onClick: {…})` is a real button wired to its handler.
//! Richer expressions / control-flow re-evaluation come later.
//!
//! ```no_run
//! # fn demo() -> Result<(), Box<dyn std::error::Error>> {
//! let source = r#"view Hi() { Text("hello", size: 24) }"#;
//! let doc = mui_syntax::parse(source);
//! let view = &doc.views[0];
//! let mut app = mocida::App::new(&view.name, 800, 600)?;
//! let (children, _reactive) = mui_runtime::build_view(view)?;
//! app.set_children(children);
//! // keep `_reactive` alive for the whole app loop — it owns the signals.
//! app.show().run();
//! # Ok(())
//! # }
//! ```

use std::cell::RefCell;
use std::collections::HashMap;
use std::rc::Rc;

use copper_syntax::expr::{BinOp, Expr, ExprKind, Literal, StrPart, StrTemplate};
use mocida::text::by_ptr;
use mocida::{
    Button, Checkbox, Children, Color, Cursor, FillMode, FontStyle, Grid, GridView, HorizontalAlign,
    Image, ListView, MouseArea, MouseAreaEvent, ProgressBar, RadioButton, Rectangle, Scroll, Shadow,
    Signal, Slider, Spinner, Stack, StackAlign, StackJustify, StackOrientation, Switch, Text, TextArea,
    TextField, TextHAlign, TextVAlign, VerticalAlign, Widget, WrapMode,
};
use mui_syntax::ast::{Element, Handler, HandlerAction, MuiValue, Node, Prop, PropValue, View};
use mui_syntax::loader::Registry;
use mui_syntax::style::{self, Anchor, HAnchor, Rgba, ShadowSpec, VAnchor};

mod layout;
use layout::Layout;

/// Anything that can go wrong while turning the AST into widgets.
#[derive(Debug)]
pub enum BuildError {
    /// A `mocida` wrapper call failed (allocation / FFI).
    Mocida(mocida::Error),
    /// The element/feature isn't implemented in the runtime yet.
    Unsupported(String),
}

impl std::fmt::Display for BuildError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            BuildError::Mocida(e) => write!(f, "mocida error: {e}"),
            BuildError::Unsupported(what) => write!(f, "unsupported in runtime: {what}"),
        }
    }
}

impl std::error::Error for BuildError {}

impl From<mocida::Error> for BuildError {
    fn from(e: mocida::Error) -> Self {
        BuildError::Mocida(e)
    }
}

type Result<T> = std::result::Result<T, BuildError>;

/// Owns the live reactive state for one rendered view: the signals and their
/// subscriptions. It **must outlive** `App::run()` — dropping it tears down the
/// signals (and the text-update closures that capture widget pointers), so keep
/// it bound until the window closes.
pub struct Reactive {
    /// Integer state signals by name. Shared (`Rc`) so button handlers can
    /// mutate the same signal a text widget subscribes to. `RefCell` because
    /// handlers take `&mut` while subscriptions only read.
    signals: HashMap<String, Rc<RefCell<Signal<i32>>>>,
    /// String state signals by name (`signal("config")`). Drives text inputs
    /// (two-way), `${str}` interpolation, and `selected: x == "y"` radios.
    strings: HashMap<String, Rc<RefCell<Signal<String>>>>,
    /// List state by name (`signal([])`). Drives `for item in list { ... }`.
    /// Pushing to one flags the tree dirty (the loop's item count drives layout),
    /// so a host loop rebuilds — e.g. a streaming install log.
    lists: HashMap<String, Rc<RefCell<Vec<String>>>>,
    /// Live subscriptions — kept alive so the text widgets keep updating.
    _subs: Vec<mocida::Subscription>,
    /// Flipped whenever a *structural* signal (one read by an `if`/`for`) changes,
    /// so a host loop knows to rebuild + swap the tree. Shared so the per-signal
    /// subscriptions can set it.
    dirty: Rc<std::cell::Cell<bool>>,
}

impl Reactive {
    fn new() -> Self {
        Reactive {
            signals: HashMap::new(),
            strings: HashMap::new(),
            lists: HashMap::new(),
            _subs: Vec::new(),
            dirty: Rc::new(std::cell::Cell::new(false)),
        }
    }

    /// Read an integer signal's current value (used by tests / introspection).
    pub fn get(&self, name: &str) -> Option<i32> {
        self.signals.get(name).map(|s| s.borrow().get())
    }

    /// Read a string signal's current value (tests / introspection).
    pub fn get_str(&self, name: &str) -> Option<String> {
        self.strings.get(name).map(|s| s.borrow().get())
    }

    /// Set a string signal's value (notifies its subscribers). No-op if the name
    /// isn't a string signal. Used by a host controller to push backend results
    /// into the live tree (e.g. streaming an install log).
    pub fn set_str(&self, name: &str, value: &str) {
        if let Some(sig) = self.strings.get(name) {
            let _ = sig.borrow_mut().set(value.to_string());
        }
    }

    /// Set an integer signal's value (notifies subscribers). No-op if absent.
    pub fn set_int(&self, name: &str, value: i32) {
        if let Some(sig) = self.signals.get(name) {
            let _ = sig.borrow_mut().set(value);
        }
    }

    /// Snapshot every signal's current value as `name → string` (ints stringified).
    /// Used to seed the next build so a rebuild preserves live state.
    pub fn values(&self) -> HashMap<String, String> {
        let mut m = HashMap::new();
        for (n, s) in &self.signals {
            m.insert(n.clone(), s.borrow().get().to_string());
        }
        for (n, s) in &self.strings {
            m.insert(n.clone(), s.borrow().get());
        }
        // Lists snapshot as their items joined by '\n' (log lines never contain
        // newlines), so a rebuild seed can split them back — preserving e.g. the
        // streaming install log across a phase rebuild.
        for (n, l) in &self.lists {
            m.insert(n.clone(), l.borrow().join("\n"));
        }
        m
    }

    /// Take + clear the structural-dirty flag (a structural signal changed since
    /// the last check, so the host should rebuild the tree).
    pub fn take_dirty(&self) -> bool {
        self.dirty.replace(false)
    }

    /// Append a value to a list signal (`for item in list`) and flag the tree
    /// dirty so the host rebuilds with the new item. No-op if absent.
    pub fn push_list(&self, name: &str, value: &str) {
        if let Some(list) = self.lists.get(name) {
            list.borrow_mut().push(value.to_string());
            self.dirty.set(true);
        }
    }

    /// Replace a list signal's contents (e.g. reset the log). Flags dirty.
    pub fn set_list(&self, name: &str, values: Vec<String>) {
        if let Some(list) = self.lists.get(name) {
            *list.borrow_mut() = values;
            self.dirty.set(true);
        }
    }
}

/// Resolved window + bundle settings for a runnable MUI document, merged from
/// its optional `app { ... }` block with sensible defaults. `mui-dev` (and the
/// codegen `main`) use this to create and name the window.
#[derive(Debug, Clone)]
pub struct WindowConfig {
    /// App / bundle display name (also used to set the mocida bundle name).
    pub name: Option<String>,
    /// Bundle identifier.
    pub id: Option<String>,
    /// Window title (resolved: `title` → `name` → entry view name).
    pub title: String,
    pub width: i32,
    pub height: i32,
    /// Optional min/max window size (logical px, desktop only). `0` = unset.
    pub min_width: i32,
    pub min_height: i32,
    pub max_width: i32,
    pub max_height: i32,
    /// Render tuning (desktop). Empty/None = leave the engine default.
    pub renderer: Option<String>,
    pub msaa: Option<i32>,
    pub aa: Option<String>,
    pub render_quality: Option<String>,
    pub taa_blend: Option<f32>,
    /// Window background as 0-255 RGBA.
    pub background: (u8, u8, u8, u8),
}

impl Default for WindowConfig {
    fn default() -> Self {
        WindowConfig {
            name: None,
            id: None,
            title: "MUI".to_string(),
            width: 900,
            height: 600,
            min_width: 0,
            min_height: 0,
            max_width: 0,
            max_height: 0,
            renderer: None,
            msaa: None,
            aa: None,
            render_quality: None,
            taa_blend: None,
            background: (241, 245, 249, 255),
        }
    }
}

/// Resolve the window/bundle settings from a document's `app { }` block (if
/// any), falling back to defaults. `entry_view_name` seeds the default title.
pub fn window_config(doc: &mui_syntax::ast::Document, entry_view_name: &str) -> WindowConfig {
    let mut cfg = WindowConfig {
        title: entry_view_name.to_string(),
        ..Default::default()
    };
    if let Some(app) = &doc.app {
        cfg.name = app.name.clone();
        cfg.id = app.id.clone();
        cfg.title = app
            .title
            .clone()
            .or_else(|| app.name.clone())
            .unwrap_or_else(|| entry_view_name.to_string());
        if let Some(w) = app.width {
            cfg.width = w;
        }
        if let Some(h) = app.height {
            cfg.height = h;
        }
        if let Some(v) = app.min_width {
            cfg.min_width = v;
        }
        if let Some(v) = app.min_height {
            cfg.min_height = v;
        }
        if let Some(v) = app.max_width {
            cfg.max_width = v;
        }
        if let Some(v) = app.max_height {
            cfg.max_height = v;
        }
        cfg.renderer = app.renderer.clone();
        cfg.msaa = app.msaa;
        cfg.aa = app.aa.clone();
        cfg.render_quality = app.render_quality.clone();
        cfg.taa_blend = app.taa_blend;
        if let Some(bg) = app.background {
            cfg.background = bg;
        }
    }
    cfg
}

/// Pick the view a document should mount as its root: the `app { entry: }`
/// named view if present and found, else the first view.
pub fn entry_view(doc: &mui_syntax::ast::Document) -> Option<&View> {
    if let Some(app) = &doc.app {
        if let Some(name) = &app.entry {
            if let Some(v) = doc.views.iter().find(|v| &v.name == name) {
                return Some(v);
            }
        }
    }
    doc.views.first()
}

/// The default MUI window icon (a rounded "block"), embedded so any host can
/// apply it without shipping a separate file.
pub const DEFAULT_ICON_PNG: &[u8] = include_bytes!("../assets/default-icon.png");

/// Give `app`'s window the default MUI icon — a rounded square — so a dev
/// window or a host that didn't set its own isn't stuck with the bare SDL
/// icon. The PNG is embedded and extracted to a temp file (mocida loads the
/// icon from a path). Cosmetic: every failure is ignored.
pub fn set_default_window_icon(app: &mut mocida::App) {
    let path = std::env::temp_dir().join("mui-default-icon.png");
    let _ = std::fs::write(&path, DEFAULT_ICON_PNG);
    if let Some(p) = path.to_str() {
        let _ = app.set_window_icon(p);
    }
}

/// Map a MUI `renderer:` value to the SDL render-driver name SDL expects in
/// `SDL_HINT_RENDER_DRIVER`. `None` for an unknown name.
fn renderer_sdl_name(r: &str) -> Option<&'static str> {
    Some(match r.to_ascii_lowercase().as_str() {
        "software" | "sw" => "software",
        "opengl" | "gl" => "opengl",
        "vulkan" | "vk" => "vulkan",
        "gpu" => "gpu",
        "d3d12" | "direct3d12" => "direct3d12",
        "d3d11" | "direct3d11" | "direct3d" => "direct3d11",
        "d3d9" | "direct3d9" => "direct3d",
        "metal" => "metal",
        _ => return None,
    })
}

/// Select the SDL render backend from `cfg.renderer` — BEFORE the window is
/// created. The driver can't be swapped after the renderer exists (SDL ties one
/// renderer to a window), so this sets the `SDL_RENDER_DRIVER` env var that SDL
/// reads when it creates the renderer. Call before `App::new`.
pub fn prefer_renderer(cfg: &WindowConfig) {
    if let Some(name) = cfg.renderer.as_deref().and_then(renderer_sdl_name) {
        std::env::set_var("SDL_RENDER_DRIVER", name);
    }
}

/// Apply a [`WindowConfig`]'s post-creation render tuning (MSAA samples, AA
/// pipeline, quality preset, TAA blend) to `app`. The renderer BACKEND is not
/// here — use [`prefer_renderer`] before `App::new` for that. Unset fields are
/// left at the engine default; unknown strings are ignored.
pub fn apply_render_config(app: &mut mocida::App, cfg: &WindowConfig) {
    use mocida::{AAMode, RenderQuality};
    if let Some(q) = cfg.render_quality.as_deref() {
        let quality = match q.to_ascii_lowercase().as_str() {
            "low" | "none" => Some(RenderQuality::Low),
            "medium" | "med" => Some(RenderQuality::Medium),
            "high" => Some(RenderQuality::High),
            "ultra" => Some(RenderQuality::Ultra),
            _ => None,
        };
        if let Some(q) = quality {
            app.set_render_quality(q);
        }
    }
    if let Some(m) = cfg.msaa {
        if m > 0 {
            app.set_msaa_samples(m);
        }
    }
    if let Some(a) = cfg.aa.as_deref() {
        let mode = match a.to_ascii_lowercase().replace(['-', '_'], "").as_str() {
            "none" | "off" => Some(AAMode::None),
            "coverage" | "on" => Some(AAMode::Coverage),
            "ssaa2x" | "ssaa2" => Some(AAMode::Ssaa2x),
            "ssaa4x" | "ssaa4" => Some(AAMode::Ssaa4x),
            "fxaa" => Some(AAMode::Fxaa),
            "taa" => Some(AAMode::Taa),
            _ => None,
        };
        if let Some(m) = mode {
            app.set_aa_mode(m);
        }
    }
    if let Some(b) = cfg.taa_blend {
        app.set_taa_blend(b);
    }
}

/// Build a [`View`] into a mocida [`Children`] tree plus its live [`Reactive`]
/// state. Hand the children to `App::set_children` and keep the `Reactive`
/// bound for the app's lifetime.
pub fn build_view(view: &View) -> Result<(Children, Reactive)> {
    build_view_with(view, &Registry::new())
}

/// Build a view with a component [`Registry`] in scope, so elements whose name
/// matches an imported view (`Card(...)`) are instantiated by inlining that
/// view's body with the call's args bound to its params. Use
/// [`mui_syntax::loader::load`] to produce the registry.
pub fn build_view_with(view: &View, components: &Registry) -> Result<(Children, Reactive)> {
    build_view_seeded(view, components, &HashMap::new())
}

/// Like [`build_view_with`] but seeds each signal's initial value from `seed`
/// (`name → value`), overriding the `signal(...)` default. This is what makes a
/// **reactive rebuild** preserve live state: snapshot the old [`Reactive`] via
/// [`Reactive::values`], then rebuild with that snapshot as the seed.
///
/// It also subscribes every *structural* signal (one referenced by an `if`
/// condition or `for` iterator) to the returned [`Reactive`]'s dirty flag, so a
/// host loop can cheaply detect when a screen-switch / list change needs a
/// rebuild — without rebuilding on every keystroke into a text input.
pub fn build_view_seeded(
    view: &View,
    components: &Registry,
    seed: &HashMap<String, String>,
) -> Result<(Children, Reactive)> {
    let mut ctx = Ctx::new(components);
    ctx.env = Env::from_view(view);
    // Seed the static env first so `signal(name)` initialisers and any
    // env-resolved defaults see the preserved value.
    for (k, v) in seed {
        ctx.env.vars.insert(k.clone(), v.clone());
    }
    ctx.declare_signals(&view.body);
    // Apply the seed to the freshly-created signals (override `signal(...)`).
    for (name, val) in seed {
        if let Some(sig) = ctx.reactive.signals.get(name) {
            if let Ok(n) = val.parse::<i32>() {
                let _ = sig.borrow_mut().set(n);
            }
        } else if let Some(sig) = ctx.reactive.strings.get(name) {
            let _ = sig.borrow_mut().set(val.clone());
        } else if let Some(list) = ctx.reactive.lists.get(name) {
            *list.borrow_mut() = if val.is_empty() {
                Vec::new()
            } else {
                val.split('\n').map(String::from).collect()
            };
        }
    }
    // Subscribe structural signals (read by an `if`/`for`) to the dirty flag.
    subscribe_structural(&mut ctx, &view.body);

    // Seed the available content box with the live window size so a top-level
    // stack with `align`/`justify` fills the window and can center its content.
    if let Some(w) = screen_metric("Window", "width") {
        ctx.avail_w = w as f32;
    }
    if let Some(h) = screen_metric("Window", "height") {
        ctx.avail_h = h as f32;
    }

    let mut children = Children::new(16)?;
    let mut layout = Layout::root();
    for node in &view.body {
        if let Some(widget) = build_node(&mut ctx, node, &mut layout)? {
            children.add(widget)?;
        }
    }
    Ok((children, ctx.reactive))
}

/// Subscribe to the signals that drive structure, flagging the reactive tree
/// dirty only when a branch actually flips — not on every change. So a counter's
/// `if count > 10` rebuilds once (at the 10→11 boundary), and `count 0→1` updates
/// only its `${count}` text (fine-grained), keeping rapid clicks responsive.
///
/// Each `if` condition gets ONE updater that re-evaluates the condition against
/// the live signal values (read via raw ptr, re-entrancy-safe) and sets dirty
/// iff the boolean result changed. `for` iterators (rare; no list-signal type
/// yet) fall back to flagging dirty on any change.
fn subscribe_structural(ctx: &mut Ctx, nodes: &[Node]) {
    let mut conds: Vec<String> = Vec::new();
    let mut for_names: Vec<String> = Vec::new();
    collect_structural(nodes, &mut conds, &mut for_names);
    conds.sort();
    conds.dedup();
    for cond in &conds {
        subscribe_condition(ctx, cond);
    }
    for_names.sort();
    for_names.dedup();
    for name in for_names {
        if let Some(sig) = ctx.reactive.signals.get(&name) {
            let dirty = ctx.reactive.dirty.clone();
            if let Ok(sub) = sig.borrow_mut().subscribe(move |_: &Signal<i32>| dirty.set(true)) {
                ctx.reactive._subs.push(sub);
            }
        } else if let Some(sig) = ctx.reactive.strings.get(&name) {
            let dirty = ctx.reactive.dirty.clone();
            if let Ok(sub) = sig.borrow_mut().subscribe(move |_: &Signal<String>| dirty.set(true)) {
                ctx.reactive._subs.push(sub);
            }
        }
    }
}

/// Subscribe one `if` condition: re-evaluate on any of its signals changing, and
/// flag dirty only when the boolean result flips.
fn subscribe_condition(ctx: &mut Ctx, cond_raw: &str) {
    let mut names: Vec<String> = Vec::new();
    push_idents(cond_raw, &mut names);
    names.sort();
    names.dedup();
    let int_ptrs: Vec<(String, *mut mocida::sys::UISignal)> = names
        .iter()
        .filter_map(|n| ctx.signal(n).map(|s| (n.clone(), s.borrow().as_ptr())))
        .collect();
    let str_ptrs: Vec<(String, *mut mocida::sys::UISignal)> = names
        .iter()
        .filter_map(|n| ctx.string_signal(n).map(|s| (n.clone(), s.borrow().as_ptr())))
        .collect();
    if int_ptrs.is_empty() && str_ptrs.is_empty() {
        return;
    }
    let base = ctx.env.clone();
    let cond = cond_raw.to_string();
    let last = Rc::new(std::cell::Cell::new(eval_condition_env(&cond, &ctx.live_env())));
    let dirty = ctx.reactive.dirty.clone();
    // One shared re-evaluator, fanned out to every dependency signal.
    let updater = Rc::new(move || {
        let mut live = base.clone();
        for (n, p) in &int_ptrs {
            live.vars
                .insert(n.clone(), unsafe { mocida::sys::UISignal_GetInt(*p) }.to_string());
        }
        for (n, p) in &str_ptrs {
            live.vars.insert(n.clone(), unsafe { str_from_signal(*p) });
        }
        let now = eval_condition_env(&cond, &live);
        if now != last.get() {
            last.set(now);
            dirty.set(true);
        }
    });
    for n in &names {
        let u = updater.clone();
        if let Some(sig) = ctx.reactive.signals.get(n) {
            if let Ok(sub) = sig.borrow_mut().subscribe(move |_: &Signal<i32>| u()) {
                ctx.reactive._subs.push(sub);
            }
        } else if let Some(sig) = ctx.reactive.strings.get(n) {
            let u = updater.clone();
            if let Ok(sub) = sig.borrow_mut().subscribe(move |_: &Signal<String>| u()) {
                ctx.reactive._subs.push(sub);
            }
        }
    }
}

/// Walk the tree collecting `if` condition strings + `for` iterator names,
/// recursing through branches / bodies / element children.
fn collect_structural(nodes: &[Node], conds: &mut Vec<String>, for_names: &mut Vec<String>) {
    for n in nodes {
        match n {
            Node::If { cond_raw, then, els, .. } => {
                conds.push(cond_raw.clone());
                collect_structural(then, conds, for_names);
                if let Some(els) = els {
                    collect_structural(els, conds, for_names);
                }
            }
            Node::For { iter, body, .. } => {
                collect_idents(iter, for_names);
                collect_structural(body, conds, for_names);
            }
            Node::Element(el) => collect_structural(&el.children, conds, for_names),
            _ => {}
        }
    }
}

/// Pull bare identifiers out of a raw condition string (`scope == "local"` →
/// `scope`), skipping quoted literals and keywords.
fn push_idents(raw: &str, out: &mut Vec<String>) {
    let mut cur = String::new();
    let mut in_str = false;
    for c in raw.chars() {
        if c == '"' {
            in_str = !in_str;
            cur.clear();
            continue;
        }
        if in_str {
            continue;
        }
        if c == '_' || c.is_alphanumeric() {
            cur.push(c);
        } else {
            flush_ident(&mut cur, out);
        }
    }
    flush_ident(&mut cur, out);
}

fn flush_ident(cur: &mut String, out: &mut Vec<String>) {
    if !cur.is_empty() {
        let first = cur.chars().next().unwrap();
        if !first.is_ascii_digit() && !matches!(cur.as_str(), "true" | "false" | "if" | "else") {
            out.push(cur.clone());
        }
        cur.clear();
    }
}

/// Build context threaded through the tree: the static string env (param
/// defaults), the live signals, the component registry, and accumulating
/// subscriptions.
struct Ctx<'a> {
    env: Env,
    reactive: Reactive,
    /// Imported components, instantiable as elements.
    components: &'a Registry,
    /// Recursion guard against a component that (directly or transitively)
    /// instantiates itself — names currently being expanded.
    expanding: Vec<String>,
    /// Available content box (width, height) of the current container, so a
    /// child stack with `align`/`justify` can fill the corresponding axis and
    /// have room to center/distribute. Seeded with the window size at the root;
    /// each sized container narrows it for its children. 0 == unknown (no fill).
    avail_w: f32,
    avail_h: f32,
    /// Orientation of the enclosing stack: `Some(true)` horizontal, `Some(false)`
    /// vertical, `None` at a block container (root / Rectangle). An axis is only
    /// fillable when the parent doesn't pack along it — a vertical stack may fill
    /// width unless its parent is horizontal (where width is the main axis).
    parent_horizontal: Option<bool>,
}

impl<'a> Ctx<'a> {
    fn new(components: &'a Registry) -> Self {
        Ctx {
            env: Env::default(),
            reactive: Reactive::new(),
            components,
            expanding: Vec::new(),
            avail_w: 0.0,
            avail_h: 0.0,
            parent_horizontal: None,
        }
    }

    /// Scan a node list for `mut name = signal(init)` bindings and create a
    /// real `Signal<i32>` for each (initial value resolved through the env, so
    /// `signal(start)` picks up the param default). Idempotent per name.
    fn declare_signals(&mut self, nodes: &[Node]) {
        for n in nodes {
            if let Node::Let {
                name,
                value: Some(expr),
                ..
            } = n
            {
                // Record the binding's initial value (string or int, incl. the
                // value inside `signal(...)`) so `${name}` and `if` conditions
                // can read it — e.g. `mut phase = signal("config")`.
                if let Some(disp) = binding_display(expr, &self.env) {
                    self.env.vars.entry(name.clone()).or_insert(disp);
                }
                if self.reactive.signals.contains_key(name)
                    || self.reactive.strings.contains_key(name)
                    || self.reactive.lists.contains_key(name)
                {
                    continue;
                }
                // A list signal: `signal([])` → `for item in name { ... }`.
                if signal_init_is_list(expr) {
                    self.reactive
                        .lists
                        .insert(name.clone(), Rc::new(RefCell::new(Vec::new())));
                    continue;
                }
                // Integer signal first (`signal(0)`), else a string signal
                // (`signal("config")`).
                if let Some(init) = signal_init_int(expr, &self.env) {
                    if let Ok(sig) = Signal::new(init) {
                        self.reactive
                            .signals
                            .insert(name.clone(), Rc::new(RefCell::new(sig)));
                    }
                } else if let Some(s) = signal_init_str(expr, &self.env) {
                    if let Ok(sig) = Signal::<String>::new(s) {
                        self.reactive
                            .strings
                            .insert(name.clone(), Rc::new(RefCell::new(sig)));
                    }
                }
            }
        }
    }

    fn signal(&self, name: &str) -> Option<&Rc<RefCell<Signal<i32>>>> {
        self.reactive.signals.get(name)
    }

    fn string_signal(&self, name: &str) -> Option<&Rc<RefCell<Signal<String>>>> {
        self.reactive.strings.get(name)
    }

    fn list_signal(&self, name: &str) -> Option<&Rc<RefCell<Vec<String>>>> {
        self.reactive.lists.get(name)
    }

    /// A snapshot env = the static env overlaid with every signal's CURRENT
    /// value (int + string). Used for the initial render of text/conditions so
    /// they reflect live state, not just the recorded binding defaults.
    fn live_env(&self) -> Env {
        let mut env = self.env.clone();
        for (name, sig) in &self.reactive.signals {
            env.vars.insert(name.clone(), sig.borrow().get().to_string());
        }
        for (name, sig) in &self.reactive.strings {
            env.vars.insert(name.clone(), sig.borrow().get());
        }
        env
    }
}

/// Build a single node. Returns `None` for nodes that don't produce a widget
/// directly (state bindings, effects).
fn build_node(ctx: &mut Ctx, node: &Node, layout: &mut Layout) -> Result<Option<Widget>> {
    match node {
        Node::Element(el) => Ok(Some(build_element(ctx, el, layout)?)),
        // Bindings created their signals in `declare_signals`; no widget here.
        Node::Let { .. } => Ok(None),
        // `effect { ... }` — run its interpretable statements once on mount, and
        // re-run when a signal it references changes (the int-signal subset).
        Node::Effect { raw, .. } => {
            run_effect(ctx, raw);
            Ok(None)
        }
        // Evaluate the condition (against current binding values) and render the
        // matching branch — `else if` chains nest in `els`.
        Node::If {
            cond_raw,
            then,
            els,
            ..
        } => {
            if eval_condition(ctx, cond_raw) {
                build_first(ctx, then, layout)
            } else if let Some(els) = els {
                build_first(ctx, els, layout)
            } else {
                Ok(None)
            }
        }
        Node::For {
            pattern,
            iter,
            body,
            ..
        } => build_for(ctx, pattern, iter, body, layout),
        Node::Match { .. } => Ok(None),
        Node::Expr(_) => Ok(None),
    }
}

fn build_first(ctx: &mut Ctx, nodes: &[Node], layout: &mut Layout) -> Result<Option<Widget>> {
    for n in nodes {
        if let Some(w) = build_node(ctx, n, layout)? {
            return Ok(Some(w));
        }
    }
    Ok(None)
}

/// `for item in list { body }` — iterate a list signal, binding `item` to each
/// value (as a string in the env) and rendering the body's first node per item
/// into a vertical Stack. Reactivity comes from `push_list`/`set_list` flagging
/// the tree dirty (the host rebuilds with the new item count).
fn build_for(
    ctx: &mut Ctx,
    pattern: &str,
    iter: &Expr,
    body: &[Node],
    layout: &mut Layout,
) -> Result<Option<Widget>> {
    let ExprKind::Ident(name) = &iter.kind else {
        return Ok(None);
    };
    let items: Vec<String> = ctx
        .list_signal(name)
        .map(|l| l.borrow().clone())
        .unwrap_or_default();
    if items.is_empty() {
        return Ok(None);
    }

    let mut stack = Stack::new(StackOrientation::Vertical)?.spacing(4.0);
    let saved = ctx.env.vars.get(pattern).cloned();
    let mut content_w: f32 = 0.0;
    let mut content_h: f32 = 0.0;
    let mut count: usize = 0;
    for item in &items {
        ctx.env.vars.insert(pattern.to_string(), item.clone());
        let mut item_layout = Layout::root();
        if let Some(w) = build_first(ctx, body, &mut item_layout)? {
            stack.add(w)?;
            content_w = content_w.max(item_layout.content_width());
            content_h += (item_layout.content_height() - layout::ROW_GAP).max(0.0);
            count += 1;
        }
    }
    // Restore the loop variable's prior binding (if any).
    match saved {
        Some(v) => {
            ctx.env.vars.insert(pattern.to_string(), v);
        }
        None => {
            ctx.env.vars.remove(pattern);
        }
    }

    let h = (content_h + 4.0 * (count.saturating_sub(1) as f32)).max(1.0);
    let w = content_w.max(1.0);
    let (x, y) = layout.next_sized(w, h);
    let widget = stack.into_widget_sized(w, h)?.position(x, y);
    Ok(Some(widget))
}

/// A child widget's own cross-axis alignment from its `align` prop, overriding
/// the parent Stack's `align` for that child (1=start, 2=center, 3=end). Works
/// for ANY widget. The `align` value is the enum member, so `align: center`,
/// `align: left`, and `align: header.center` (anchor-style — the `header.` part
/// names the intended container, the member is the position) all resolve here.
/// A container still aligns its OWN children via its `align` too; this only adds
/// self-positioning.
fn child_self_align(node: &Node) -> Option<i32> {
    let Node::Element(el) = node else {
        return None;
    };
    match style::enum_member(el, "align")?.as_str() {
        "left" | "start" | "top" => Some(1),
        "center" | "middle" => Some(2),
        "right" | "end" | "bottom" => Some(3),
        _ => None,
    }
}

/// A child's outer margins `(left, top, right, bottom)` from `margin:` /
/// `marginTop:` / `marginX:` … — honoured by the parent Stack's layout (it
/// offsets the item and advances its cursor past the margin). `(0,0,0,0)` for
/// a node without margins.
fn child_margin(node: &Node) -> (f32, f32, f32, f32) {
    match node {
        Node::Element(el) => style::box_spacing(el, "margin").unwrap_or((0.0, 0.0, 0.0, 0.0)),
        _ => (0.0, 0.0, 0.0, 0.0),
    }
}

/// Build one element, recursing into children. An imported component (a view
/// in the registry) is instantiated; built-ins map to mocida widgets.
fn build_element(ctx: &mut Ctx, el: &Element, layout: &mut Layout) -> Result<Widget> {
    // A user/imported component takes precedence over the built-in fallbacks
    // (but not over a core widget name, which can't be shadowed).
    let widget = if !is_builtin(&el.name)
        && ctx.components.contains_key(&el.name)
        && !ctx.expanding.contains(&el.name)
    {
        build_component(ctx, el, layout)?
    } else {
        match el.name.as_str() {
            "Rectangle" | "Rect" | "Box" => build_rectangle(ctx, el, layout)?,
            "Stack" => build_stack(ctx, el, layout)?,
            "Grid" => build_grid(ctx, el, layout)?,
            "Scroll" => build_scroll(ctx, el, layout)?,
            "ListView" => build_listview(ctx, el, layout)?,
            "GridView" => build_gridview(ctx, el, layout)?,
            "Text" => build_text(ctx, el, layout)?,
            "Button" => build_button(ctx, el, layout)?,
            "TextField" | "Input" | "TextInput" => build_textfield(ctx, el, layout)?,
            "TextArea" => build_textarea(ctx, el, layout)?,
            "Checkbox" => build_checkbox(ctx, el, layout)?,
            "RadioButton" | "Radio" => build_radio(ctx, el, layout)?,
            "Switch" => build_switch(ctx, el, layout)?,
            "Slider" => build_slider(ctx, el, layout)?,
            "ProgressBar" => build_progressbar(ctx, el, layout)?,
            "Spinner" => build_spinner(ctx, el, layout)?,
            "Image" => build_image(ctx, el, layout)?,
            "MouseArea" => build_mouse_area(ctx, el, layout)?,
            _ if !el.children.is_empty() => build_stack(ctx, el, layout)?,
            other => build_placeholder(ctx, other, el, layout)?,
        }
    };
    // `id: name` (a bare ident or string) sets the widget's mocida lookup id so
    // `UIWidget_GetById("name")` finds it (and it's the handle the id-as-variable
    // sugar binds to).
    let widget = match id_name(el) {
        Some(id) => widget.id(&id)?,
        None => widget,
    };
    // `onKeyInput: { |event| if event.key == "A" { … } }` — a key-down handler
    // on ANY widget. Fires for every key press (keyboard isn't spatial); the
    // body decides which key matters. Wired here so it works on every element.
    let widget = match key_handler_closure(ctx, el, "onKeyInput") {
        Some(mut f) => widget.on_key_down(move |k, _mods| f(k)),
        None => widget,
    };
    Ok(widget)
}

/// The `id:` value as a name — a bare ident (`id: rect`) or a string
/// (`id: "rect"`). `None` when there's no `id` prop.
fn id_name(el: &Element) -> Option<String> {
    match &find_prop(el, "id")?.value {
        PropValue::Expr(e) => match &e.kind {
            ExprKind::Ident(n) => Some(n.clone()),
            ExprKind::Literal(Literal::Str(t)) => Some(render_template_raw(t).replace("{}", "")),
            _ => None,
        },
        _ => None,
    }
}

/// Names that map to a built-in mocida widget (can't be shadowed by a
/// component import).
fn is_builtin(name: &str) -> bool {
    matches!(
        name,
        "Rectangle"
            | "Rect"
            | "Box"
            | "Stack"
            | "Grid"
            | "Scroll"
            | "ListView"
            | "GridView"
            | "Text"
            | "Button"
            | "TextField"
            | "Input"
            | "TextInput"
            | "TextArea"
            | "Checkbox"
            | "RadioButton"
            | "Radio"
            | "Switch"
            | "Slider"
            | "ProgressBar"
            | "Spinner"
            | "Image"
            | "MouseArea"
    )
}

/// Instantiate an imported component: bind the call's args to the component
/// view's params, then build its body as a sub-tree (wrapped in a Stack so a
/// multi-node component lays out and sizes cleanly). String/int param values
/// from the call flow into the component's `${param}` interpolation.
fn build_component(ctx: &mut Ctx, el: &Element, layout: &mut Layout) -> Result<Widget> {
    let view = ctx.components.get(&el.name).cloned().unwrap();

    // Seed an env for the component: each param = the call's arg (a prop or the
    // positional), else the param default.
    let mut comp_env = Env::default();
    for (i, p) in view.params.iter().enumerate() {
        let val = call_arg_string(el, &p.name, i, &ctx.env)
            .or_else(|| p.default.as_ref().and_then(literal_display));
        if let Some(v) = val {
            comp_env.vars.insert(p.name.clone(), v);
        }
    }

    // Build the component's body under its own env, guarding against recursion.
    // A component's body is the root element(s) of its view — typically a single
    // root Stack that already sizes itself (build_stack measures + advances the
    // parent `layout`). So build the body's first renderable node DIRECTLY into
    // the parent layout, rather than re-wrapping in another Stack (an extra
    // wrapper with no real size is exactly what made sibling components overlap).
    let saved_env = std::mem::replace(&mut ctx.env, comp_env);
    ctx.expanding.push(el.name.clone());
    ctx.declare_signals(&view.body);

    // Render the first renderable root node into the parent layout. (A view with
    // multiple top-level roots is uncommon; the first is the component's root.)
    let mut result = None;
    for node in &view.body {
        if let Some(widget) = build_node(ctx, node, layout)? {
            result = Some(widget);
            break;
        }
    }

    ctx.expanding.pop();
    ctx.env = saved_env;

    match result {
        Some(widget) => Ok(apply_anchor(widget, style::anchor(el))),
        // Empty component (no renderable root) — emit a zero-ish spacer so the
        // call still produces a widget.
        None => sized_text("", 1.0, Color::rgb(0, 0, 0), layout),
    }
}

/// Resolve the call-site value for a component param: a matching named prop, or
/// the positional arg for the first param. Returns the display string.
fn call_arg_string(el: &Element, param: &str, index: usize, outer: &Env) -> Option<String> {
    if let Some(p) = el.props.iter().find(|p| p.name == param) {
        return prop_value_string(&p.value, outer);
    }
    if index == 0 {
        if let Some(e) = &el.positional {
            return Some(render_text_expr_env(e, outer));
        }
    }
    None
}

/// Display string for a prop value at a component call site, resolving names
/// through the caller's env (so `Card(title: name)` passes the caller's
/// `name`).
fn prop_value_string(value: &PropValue, outer: &Env) -> Option<String> {
    match value {
        // A boolean expression passed as a component arg (`ok: source_dir != ""`)
        // must be evaluated to `"true"`/`"false"`, not rendered as text (which
        // would yield an empty string → the param reads as falsy). A plain string
        // / interpolation still renders normally.
        PropValue::Expr(e) if is_bool_expr(e) => {
            Some(eval_bool_expr_env(e, outer).unwrap_or(false).to_string())
        }
        PropValue::Expr(e) => Some(render_text_expr_env(e, outer)),
        PropValue::Mui(_) | PropValue::Handler(_) => None,
    }
}

/// True when `e` is a boolean-shaped expression (a comparison / logical op or a
/// bool literal) — i.e. should be evaluated with [`eval_bool_expr_env`] rather
/// than rendered as text.
fn is_bool_expr(e: &Expr) -> bool {
    match &e.kind {
        ExprKind::Literal(Literal::Bool(_)) => true,
        ExprKind::Binary { op, .. } => matches!(
            op,
            BinOp::Eq
                | BinOp::Ne
                | BinOp::And
                | BinOp::Or
                | BinOp::Lt
                | BinOp::Le
                | BinOp::Gt
                | BinOp::Ge
        ),
        _ => false,
    }
}

/// `Stack(orientation:, gap:, padding:) { children }` → `mocida::Stack`.
/// Map a parsed [`ShadowSpec`] onto a mocida [`Shadow`].
fn to_shadow(s: ShadowSpec) -> Shadow {
    Shadow {
        offset_x: s.dx,
        offset_y: s.dy,
        blur: s.blur,
        spread: s.spread,
        color: to_color(s.color),
    }
}

/// `Rectangle(...)` → `mocida::Rectangle`. The most basic widget and the most
/// basic container: a filled, optionally rounded/bordered/shadowed box that can
/// also hold children (laid out top-to-bottom inside its padding box). Accepts
/// every visual knob the C rectangle exposes: `fill`/`background`/`bg`/`color`,
/// `radius`, `borderWidth`, `borderColor`, `shadow`, `padding`, `gap`,
/// `opacity`, `rotation`, explicit `width`/`height`, and `anchor`.
fn build_rectangle(ctx: &mut Ctx, el: &Element, layout: &mut Layout) -> Result<Widget> {
    let mut rect = Rectangle::new()?;

    // Fill color: `fill` is the natural name on a rectangle; `background`/`bg`/
    // `color` also accepted for consistency with the other widgets.
    let fill = style::color_prop(el, "fill")
        .or_else(|| style::background(el))
        .unwrap_or(Rgba {
            r: 255,
            g: 255,
            b: 255,
            a: 255,
        });
    rect = rect.color(to_color(fill));

    if let Some(r) = style::f32_prop(el, "radius") {
        rect = rect.radius(r);
    }
    if let Some(bw) = style::f32_prop(el, "borderWidth") {
        rect = rect.border_width(bw);
    }
    if let Some(bc) = style::color_prop(el, "borderColor") {
        rect = rect.border_color(to_color(bc));
    }
    if let Some(sh) = style::shadow(el) {
        rect = rect.shadow(to_shadow(sh));
    }
    let (pl, pt, pr, pb) = style::box_spacing(el, "padding").unwrap_or((0.0, 0.0, 0.0, 0.0));
    if pl != 0.0 || pt != 0.0 || pr != 0.0 || pb != 0.0 {
        rect = rect.padding(pl, pt, pr, pb);
    }
    let gap = style::f32_prop(el, "gap").unwrap_or(0.0);
    if gap > 0.0 {
        rect = rect.gap(gap);
    }

    // Build children into the rect, measuring them so we can auto-size when no
    // explicit width/height is given (mirrors build_stack's measurement). An
    // explicit size also becomes the children's available box (− padding) so a
    // child stack with `align`/`justify` can fill the rect and center.
    ctx.declare_signals(&el.children);
    let explicit_w = dim_prop(ctx, el, "width");
    let explicit_h = dim_prop(ctx, el, "height");
    let saved_avail = (ctx.avail_w, ctx.avail_h);
    let saved_orient = ctx.parent_horizontal;
    ctx.avail_w = (explicit_w.unwrap_or(ctx.avail_w) - pl - pr).max(0.0);
    ctx.avail_h = (explicit_h.unwrap_or(ctx.avail_h) - pt - pb).max(0.0);
    ctx.parent_horizontal = None; // a Rectangle is a block container
    let mut child_layout = Layout::root();
    let mut content_w: f32 = 0.0;
    let mut content_h: f32 = 0.0;
    let mut count: usize = 0;
    for node in &el.children {
        let before_h = child_layout.content_height();
        if let Some(mut widget) = build_node(ctx, node, &mut child_layout)? {
            // Outer margins push the child away from the rect's edges/neighbours
            // (the C rect render honours them); count them toward the size.
            let (ml, mt, mr, mb) = child_margin(node);
            if ml != 0.0 || mt != 0.0 || mr != 0.0 || mb != 0.0 {
                widget = widget.margin(ml, mt, mr, mb);
            }
            rect.add_child(widget);
            content_w = content_w.max(child_layout.content_width() + ml + mr);
            content_h += (child_layout.content_height() - before_h).max(0.0) + mt + mb;
            count += 1;
        }
    }
    ctx.avail_w = saved_avail.0;
    ctx.avail_h = saved_avail.1;
    ctx.parent_horizontal = saved_orient;
    let gaps = gap * (count.saturating_sub(1) as f32);

    // Explicit size wins; otherwise size to content + padding (+ gaps). A
    // childless rect with no size falls back to a small visible square.
    let w = explicit_w.unwrap_or_else(|| {
        if count > 0 {
            (content_w + pl + pr).max(1.0)
        } else {
            100.0
        }
    });
    let h = explicit_h.unwrap_or_else(|| {
        if count > 0 {
            (content_h + gaps + pt + pb).max(1.0)
        } else {
            100.0
        }
    });

    let (x, y) = layout.next_sized(w, h);
    let mut widget = rect.into_widget_sized(w, h)?.position(x, y);
    if let Some(op) = style::f32_prop(el, "opacity") {
        widget = widget.opacity(op);
    }
    if let Some(rot) = style::f32_prop(el, "rotation") {
        widget = widget.rotation(rot);
    }
    Ok(apply_anchor(widget, style::anchor(el)))
}

fn build_stack(ctx: &mut Ctx, el: &Element, layout: &mut Layout) -> Result<Widget> {
    let horizontal = matches!(
        style::enum_member(el, "orientation").as_deref(),
        Some("horizontal")
    );
    let orientation = if horizontal {
        StackOrientation::Horizontal
    } else {
        StackOrientation::Vertical
    };
    let gap = style::f32_prop(el, "gap").unwrap_or(8.0);
    // Per-side padding: `padding: N` | `[t,r,b,l]` | `paddingTop:`/`paddingX:` …
    let (pl, pt, pr, pb) = style::box_spacing(el, "padding").unwrap_or((0.0, 0.0, 0.0, 0.0));

    let mut stack = Stack::new(orientation)?.spacing(gap);
    if pl != 0.0 || pt != 0.0 || pr != 0.0 || pb != 0.0 {
        stack = stack.padding(pl, pt, pr, pb);
    }
    // Cross-axis alignment of children (`align: center` / `end` / `start`).
    let align = style::enum_member(el, "align").and_then(|a| match a.as_str() {
        "center" | "middle" => Some(StackAlign::Center),
        "end" | "right" | "bottom" => Some(StackAlign::End),
        "start" | "left" | "top" => Some(StackAlign::Start),
        _ => None,
    });
    if let Some(al) = align {
        stack = stack.align(al);
    }
    // Main-axis distribution of children (`justify: center`/`end`/`spaceBetween`).
    let justify = style::enum_member(el, "justify").and_then(|a| match a.as_str() {
        "center" | "middle" => Some(StackJustify::Center),
        "end" => Some(StackJustify::End),
        "spaceBetween" | "between" | "space-between" => Some(StackJustify::SpaceBetween),
        "start" => Some(StackJustify::Start),
        _ => None,
    });
    if let Some(j) = justify {
        stack = stack.justify(j);
    }
    // A non-start `align` fills the cross axis; a non-start `justify` fills the
    // main axis — so the stack is bigger than its content and has room to
    // center/distribute. Cross axis: width for a vertical stack, height for a
    // horizontal one. Main axis is the opposite. But an axis may only fill when
    // the *parent* doesn't pack along it (else a vertical stack inside a
    // horizontal one would wrongly stretch to the full width): width is free
    // unless the parent is horizontal; height is free unless the parent is
    // vertical (a block parent — root/Rectangle — leaves both free).
    let cross_fill = matches!(align, Some(StackAlign::Center) | Some(StackAlign::End));
    let main_fill = matches!(
        justify,
        Some(StackJustify::Center) | Some(StackJustify::End) | Some(StackJustify::SpaceBetween)
    );
    let width_free = ctx.parent_horizontal != Some(true);
    let height_free = ctx.parent_horizontal != Some(false);
    let fill_w = (if horizontal { main_fill } else { cross_fill }) && width_free;
    let fill_h = (if horizontal { cross_fill } else { main_fill }) && height_free;
    // Native background / border drawn behind the items (mocida's Stack paints
    // these itself — no Rectangle wrapper, so nested children still render).
    if let Some(bg) = style::fill_only(el) {
        stack = stack.background(to_color(bg));
    }
    if let Some(r) = style::f32_prop(el, "radius") {
        stack = stack.radius(r);
    }
    if let Some(bc) = style::color_prop(el, "borderColor") {
        stack = stack.border(to_color(bc), style::f32_prop(el, "borderWidth").unwrap_or(1.0));
    } else if let Some(bw) = style::f32_prop(el, "borderWidth") {
        stack = stack.border(Color::rgb(0, 0, 0), bw);
    }
    // Signals declared inside this block are visible to its children.
    ctx.declare_signals(&el.children);

    // Resolve the stack's own size on each axis *before* building children when
    // it's knowable up front (an explicit `width`/`height`, or a fill driven by
    // align/justify against the available box). The children then see this box
    // as their available space, so nested fills resolve correctly.
    let explicit_w = dim_prop(ctx, el, "width");
    let explicit_h = dim_prop(ctx, el, "height");
    let self_w_known = explicit_w.or(if fill_w && ctx.avail_w > 0.0 {
        Some(ctx.avail_w)
    } else {
        None
    });
    let self_h_known = explicit_h.or(if fill_h && ctx.avail_h > 0.0 {
        Some(ctx.avail_h)
    } else {
        None
    });

    // Narrow the available box for the children to this stack's inner content
    // box (known size − padding, else inherit the parent's box − padding), and
    // tell them this stack's orientation so their own fill decisions are correct.
    let saved_avail = (ctx.avail_w, ctx.avail_h);
    let saved_orient = ctx.parent_horizontal;
    ctx.avail_w = (self_w_known.unwrap_or(ctx.avail_w) - pl - pr).max(0.0);
    ctx.avail_h = (self_h_known.unwrap_or(ctx.avail_h) - pt - pb).max(0.0);
    ctx.parent_horizontal = Some(horizontal);

    // Measure each child in isolation (fresh cursor per child), dropping the
    // trailing ROW_GAP so the box matches what mocida draws (spacing comes from
    // the stack's own `gap`).
    let mut content_w: f32 = 0.0;
    let mut content_h: f32 = 0.0;
    let mut count: usize = 0;
    for node in &el.children {
        let mut child_layout = Layout::root();
        if let Some(mut widget) = build_node(ctx, node, &mut child_layout)? {
            // A child with its own `align` positions itself on the cross axis,
            // overriding the stack's `align` for that child.
            if let Some(a) = child_self_align(node) {
                widget = widget.self_align(a);
            }
            // Outer margins push the child away from its neighbours/edges; the
            // C stack honours them (and they count toward the content size).
            let (ml, mt, mr, mb) = child_margin(node);
            if ml != 0.0 || mt != 0.0 || mr != 0.0 || mb != 0.0 {
                widget = widget.margin(ml, mt, mr, mb);
            }
            stack.add(widget)?;
            let cw = child_layout.content_width();
            let ch = (child_layout.content_height() - layout::ROW_GAP).max(0.0);
            if horizontal {
                content_w += cw + ml + mr;
                content_h = content_h.max(ch + mt + mb);
            } else {
                content_h += ch + mt + mb;
                content_w = content_w.max(cw + ml + mr);
            }
            count += 1;
        }
    }
    ctx.avail_w = saved_avail.0;
    ctx.avail_h = saved_avail.1;
    ctx.parent_horizontal = saved_orient;

    let gaps = gap * (count.saturating_sub(1) as f32);
    let (inner_w, inner_h) = if horizontal {
        (content_w + gaps, content_h)
    } else {
        (content_w, content_h + gaps)
    };
    // A filled axis takes the available size; otherwise size to content +
    // padding (explicit width/height already folded into `self_*_known`).
    let w = self_w_known.unwrap_or((inner_w + pl + pr).max(1.0));
    let h = self_h_known.unwrap_or((inner_h + pt + pb).max(1.0));
    let (x, y) = layout.next_sized(w, h);
    let mut widget = stack.into_widget_sized(w, h)?.position(x, y);
    if let Some(op) = style::f32_prop(el, "opacity") {
        widget = widget.opacity(op);
    }
    Ok(apply_anchor(widget, style::anchor(el)))
}

/// `Text(label, size:, color:)` → `mocida::Text`. When the label interpolates
/// signals (`${count}`), subscribe the widget so it re-renders on change.
fn build_text(ctx: &mut Ctx, el: &Element, layout: &mut Layout) -> Result<Widget> {
    let size = style::f32_prop(el, "size").unwrap_or(16.0);
    let color = color_eval(ctx, el, "color")
        .map(to_color)
        .unwrap_or(Color::rgb(15, 23, 42));

    // Names this text reads that are live signals (int OR string) → reactive.
    let reads: Vec<String> = el
        .positional
        .as_ref()
        .map(names_read)
        .unwrap_or_default()
        .into_iter()
        .filter(|n| ctx.signal(n).is_some() || ctx.string_signal(n).is_some())
        .collect();

    let label = el
        .positional
        .as_ref()
        .map(|e| render_text_expr(e, ctx))
        .unwrap_or_default();

    // Size from the *current* text; mocida re-measures on update. An explicit
    // `width:` bounds the line; with `wrap:` set, a label wider than that box
    // wraps, so estimate the wrapped height (line count × line height).
    let (natural_w, line_h) = text_extent(&label, size);
    let wrap_mode = style::enum_member(el, "wrap").and_then(|s| wrap_from(&s));
    let w = dim_prop(ctx, el, "width").unwrap_or(natural_w);
    let h = dim_prop(ctx, el, "height").unwrap_or_else(|| {
        if wrap_mode.is_some() && w > 0.0 && natural_w > w {
            line_h * (natural_w / w).ceil().max(1.0)
        } else {
            line_h
        }
    });
    let mut text = Text::new(&label, size)?.color(color);
    // Typography: weight / fontStyle / font family.
    text = apply_text_font(text, el)?;
    // Alignment + wrapping within the widget bounds.
    if let Some(a) = style::enum_member(el, "align")
        .or_else(|| style::enum_member(el, "hAlign"))
        .and_then(|s| text_h_align(&s))
    {
        text = text.h_align(a);
    }
    if let Some(a) = style::enum_member(el, "vAlign").and_then(|s| text_v_align(&s)) {
        text = text.v_align(a);
    }
    if let Some(wm) = style::enum_member(el, "wrap").and_then(|s| wrap_from(&s)) {
        // `wrap_to_bounds` makes the wrap width follow the widget's own `width:`
        // box; without it mocida wraps at `wrapWidth` (0 by default = no wrap),
        // so a `wrap: word` text wouldn't actually break at the width given.
        text = text.wrap_mode(wm).wrap_to_bounds(true);
    }
    if let Some(c) = style::enum_member(el, "cursor") {
        text = text.cursor(cursor_from(&c));
    }
    if let Some((l, t, r, b)) = style::box_spacing(el, "padding") {
        text = text.padding(l, t, r, b);
    }
    if let Some((l, t, r, b)) = style::box_spacing(el, "margin") {
        text = text.margins(l, t, r, b);
    }
    let text_ptr = text.as_ptr();
    let (x, y) = layout.next_sized(w, h);
    let widget = text.into_widget_sized(w, h)?.position(x, y);

    if !reads.is_empty() {
        // Snapshot what we need to recompute the label inside the subscription:
        // the positional expr + each dependency signal's *raw pointer* (name +
        // `*mut UISignal`). We read values through the raw pointer, NOT by
        // re-borrowing the `Rc<RefCell<Signal>>` — the subscription fires
        // *synchronously inside* a button handler's `borrow_mut().set(...)`, so
        // re-borrowing would panic with "already mutably borrowed". The signal
        // value is already updated in C by the time the subscription runs, and
        // the owning Rc keeps the pointer alive (it lives in `Reactive`).
        let tmpl_expr = el.positional.clone();
        let env = ctx.env.clone();
        // Raw `*mut UISignal` of each dependency, split by value kind so the
        // updater reads int via `UISignal_GetInt` and string via
        // `UISignal_GetString` (the re-entrancy-safe path — never re-borrows the
        // RefCell, which the synchronous subscription would deadlock).
        let int_ptrs: Vec<(String, *mut mocida::sys::UISignal)> = reads
            .iter()
            .filter_map(|n| ctx.signal(n).map(|s| (n.clone(), s.borrow().as_ptr())))
            .collect();
        let str_ptrs: Vec<(String, *mut mocida::sys::UISignal)> = reads
            .iter()
            .filter_map(|n| ctx.string_signal(n).map(|s| (n.clone(), s.borrow().as_ptr())))
            .collect();

        // The updater reads current values via raw ptr, re-renders, sets text.
        let updater = move || {
            let mut live = env.clone();
            for (name, ptr) in &int_ptrs {
                // SAFETY: `*ptr` is a live UISignal owned by a signal kept in
                // `Reactive` (outlives these subscriptions). UI loop is single-
                // threaded. We only read — no aliasing with the writer's &mut.
                let val = unsafe { mocida::sys::UISignal_GetInt(*ptr) };
                live.vars.insert(name.clone(), val.to_string());
            }
            for (name, ptr) in &str_ptrs {
                let val = unsafe { str_from_signal(*ptr) };
                live.vars.insert(name.clone(), val);
            }
            if let Some(e) = &tmpl_expr {
                let s = render_text_expr_env(e, &live);
                // SAFETY: text_ptr points at a live UIText owned by the widget
                // tree, which outlives these subscriptions. Single-threaded.
                let _ = unsafe { by_ptr::set_text(text_ptr, &s) };
            }
        };

        // Subscribe to every dependency signal (int + string). The `borrow_mut()`
        // here is at build time (no handler running), so it can't conflict.
        let mut new_subs = Vec::new();
        for n in &reads {
            let updater = updater.clone();
            if let Some(sig) = ctx.signal(n) {
                if let Ok(sub) = sig.borrow_mut().subscribe(move |_| updater()) {
                    new_subs.push(sub);
                }
            } else if let Some(sig) = ctx.string_signal(n) {
                if let Ok(sub) = sig.borrow_mut().subscribe(move |_| updater()) {
                    new_subs.push(sub);
                }
            }
        }
        ctx.reactive._subs.extend(new_subs);
    }

    Ok(apply_anchor(widget, style::anchor(el)))
}

/// `Button(label, onClick:, ...)` → a real styled `mocida::Button`. Honors the
/// common styling props: `size`, `radius`, `borderWidth`, `cursor`, the
/// `background`/`bg`/`colors` fill + `textColor`, and `anchor:`. If the handler
/// is a recognised action over a signal, wires the click to mutate it.
fn build_button(ctx: &mut Ctx, el: &Element, layout: &mut Layout) -> Result<Widget> {
    let label = el
        .positional
        .as_ref()
        .map(|e| render_text_expr(e, ctx))
        .unwrap_or_else(|| "Button".to_string());
    let size = style::f32_prop(el, "size").unwrap_or(16.0);

    let mut button = Button::new(&label, size)?;

    // Fill + text colors. `background`/`bg`/`color` set the fill; `textColor`
    // sets the label. Defaults match the previous blue button.
    let bg = style::background(el).unwrap_or(Rgba {
        r: 59,
        g: 130,
        b: 246,
        a: 255,
    });
    let text_col = style::color_prop(el, "textColor").unwrap_or(Rgba {
        r: 255,
        g: 255,
        b: 255,
        a: 255,
    });
    button = button.colors(to_color(bg), to_color(text_col));

    button = button.radius(style::f32_prop(el, "radius").unwrap_or(8.0));
    if let Some(bw) = style::f32_prop(el, "borderWidth") {
        button = button.border_width(bw);
    }
    if let Some(c) = style::enum_member(el, "cursor") {
        button = button.cursor(cursor_from(&c));
    }
    // Label typography: weight / fontStyle / font family.
    let bits = style::font_style_bits(el);
    if bits != 0 {
        button = button.font_style(FontStyle::from_bits(bits));
    }
    if let Some(fam) = style::font_family(el) {
        if let Some(path) = mocida::text::get_font(&fam) {
            button = button.font_family(&path)?;
        }
    }
    if let Some(sh) = style::shadow(el) {
        button = button.shadow(to_shadow(sh));
    }
    // `enabled:` accepts a literal OR an expression resolved against live state
    // (`enabled: has_cargo && source_dir != ""`) — so the button greys out until
    // the prerequisites are met. Absent → enabled.
    if eval_bool_prop(ctx, el, "enabled") == Some(false) {
        button = button.enabled(false);
    }

    // Wire onClick to the recognised state mutations (int `x += 1` + string
    // sets `x = "lit"` / `x = otherSignal`).
    if let Some(mut f) = handler_apply_closure(ctx, el, "onClick") {
        button = button.on_click(move |_| f());
    }

    // Auto-size to the label (with comfortable padding) unless an explicit
    // `width:`/`height:` is given — those win so a button can be sized exactly.
    let (tw, th) = text_extent(&label, size);
    let bw = dim_prop(ctx, el, "width").unwrap_or((tw + 24.0).max(48.0));
    let bh = dim_prop(ctx, el, "height").unwrap_or((th + 12.0).max(32.0));
    let (x, y) = layout.next_sized(bw, bh);
    let widget = button.into_widget_sized(bw, bh)?.position(x, y);
    Ok(apply_anchor(widget, style::anchor(el)))
}

/// `TextField` / `Input` — single-line text input, full style surface. Honors
/// `value:`/`text:` (initial, resolved through the env), `placeholder:`,
/// `background`/`bg`, `textColor`/`color`, `placeholderColor`, `caretColor`,
/// `selectionColor`, `borderColor`/`borderColorFocused`/`borderWidth` (or the
/// combined border), `radius`, `padding`, `size`/`fontSize`, `weight`/
/// `fontStyle`, `font`/`fontFamily`, `password`, `maxLength`, `caretBlink`,
/// `placeholderAnimated`, `cursor`, `width`/`height`, `anchor`. Two-way signal
/// binding (writing the bound signal on input) is still M3+.
fn build_textfield(ctx: &mut Ctx, el: &Element, layout: &mut Layout) -> Result<Widget> {
    let initial = prop_string_value(ctx, el, "value")
        .or_else(|| prop_string_value(ctx, el, "text"))
        .unwrap_or_default();
    let size = style::f32_prop(el, "size")
        .or_else(|| style::f32_prop(el, "fontSize"))
        .unwrap_or(16.0);
    let mut tf = TextField::new(&initial, size)?;
    if let Some(ph) = prop_string_value(ctx, el, "placeholder") {
        tf = tf.placeholder(&ph)?;
    }
    tf = tf.radius(style::f32_prop(el, "radius").unwrap_or(8.0));

    // Colors: fill (background/bg), text (textColor/color), and accents.
    if let Some(bg) = style::fill_only(el) {
        tf = tf.bg_color(to_color(bg));
    }
    if let Some(c) = style::color_prop(el, "textColor").or_else(|| style::color_prop(el, "color")) {
        tf = tf.text_color(to_color(c));
    }
    if let Some(c) = style::color_prop(el, "placeholderColor") {
        tf = tf.placeholder_color(to_color(c));
    }
    if let Some(c) = style::color_prop(el, "caretColor") {
        tf = tf.caret_color(to_color(c));
    }
    if let Some(c) = style::color_prop(el, "selectionColor") {
        tf = tf.selection_color(to_color(c));
    }

    // Border: granular normal / focused / width.
    if let Some(c) = style::color_prop(el, "borderColor") {
        tf = tf.border_color(to_color(c));
    }
    if let Some(c) = style::color_prop(el, "borderColorFocused")
        .or_else(|| style::color_prop(el, "focusBorderColor"))
    {
        tf = tf.border_color_focused(to_color(c));
    }
    if let Some(bw) = style::f32_prop(el, "borderWidth") {
        tf = tf.border_width(bw);
    }

    if let Some((l, t, _r, _b)) = style::box_spacing(el, "padding") {
        tf = tf.padding(l, t);
    }

    // Typography.
    let bits = style::font_style_bits(el);
    if bits != 0 {
        tf = tf.font_style(FontStyle::from_bits(bits));
    }
    if let Some(fam) = style::font_family(el) {
        if let Some(path) = mocida::text::get_font(&fam) {
            tf = tf.font_family(&path)?;
        }
    }

    // Behavior.
    if prop_bool(el, "password").unwrap_or(false) {
        tf = tf.password(true);
    }
    if let Some(ml) = style::f32_prop(el, "maxLength") {
        tf = tf.max_length(ml as i32);
    }
    if let Some(ms) = style::f32_prop(el, "caretBlink").or_else(|| style::f32_prop(el, "caretBlinkRate"))
    {
        tf = tf.caret_blink_rate(ms as i32);
    }
    if prop_bool(el, "placeholderAnimated").unwrap_or(false) {
        tf = tf.placeholder_animated(true);
    }
    if let Some(c) = style::enum_member(el, "cursor") {
        tf = tf.cursor(cursor_from(&c));
    }

    // Two-way binding: `onChange: { |v| name = v }` writes the field's text into
    // the bound string signal on every edit (so `${name}` elsewhere updates).
    if let Some(PropValue::Handler(h)) = find_prop(el, "onChange").map(|p| &p.value) {
        let two_way = parse_str_actions(h).into_iter().find_map(|a| match a {
            StrAction::TwoWay { name } => Some(name),
            _ => None,
        });
        if let Some(name) = two_way {
            if let Some(sig) = ctx.string_signal(&name).cloned() {
                tf = tf.on_change(move |s| {
                    // `try_borrow_mut`: a programmatic `set_text` (from the
                    // reactive-display subscription below, fired while another
                    // handler holds this signal's `borrow_mut`) re-enters here —
                    // skip then, so we don't panic with "already borrowed".
                    if let Ok(mut g) = sig.try_borrow_mut() {
                        let _ = g.set(s.to_string());
                    }
                });
            }
        }
    }

    // Reactive display: when the bound `value:` string signal changes
    // EXTERNALLY (e.g. a radio sets `install_dir = default_glob`), push the new
    // value into the field. Guarded by a text compare so the field's own
    // onChange (the user typing) doesn't reset the caret. Reads via raw ptr
    // (re-entrancy-safe — the radio's `set` fires this synchronously).
    let value_name: Option<String> = find_prop(el, "value")
        .and_then(|p| match &p.value {
            PropValue::Expr(e) => Some(names_read(e)),
            _ => None,
        })
        .unwrap_or_default()
        .into_iter()
        .find(|n| ctx.string_signal(n).is_some());
    let tf_ptr = tf.as_ptr();
    if let Some(name) = &value_name {
        if let Some(sig) = ctx.reactive.strings.get(name) {
            let sptr = sig.borrow().as_ptr();
            if let Ok(sub) = sig.borrow_mut().subscribe(move |_: &Signal<String>| {
                let want = unsafe { str_from_signal(sptr) };
                let cur = unsafe {
                    let p = mocida::sys::UITextField_GetText(tf_ptr);
                    if p.is_null() {
                        String::new()
                    } else {
                        std::ffi::CStr::from_ptr(p).to_string_lossy().into_owned()
                    }
                };
                if cur != want {
                    if let Ok(c) = std::ffi::CString::new(want) {
                        unsafe { mocida::sys::UITextField_SetText(tf_ptr, c.as_ptr()) };
                    }
                }
            }) {
                ctx.reactive._subs.push(sub);
            }
        }
    }

    let w = dim_prop(ctx, el, "width").unwrap_or(240.0);
    let h = dim_prop(ctx, el, "height").unwrap_or(size + 20.0);
    let (x, y) = layout.next_sized(w, h);
    let widget = tf.into_widget_sized(w, h)?.position(x, y);
    Ok(apply_anchor(widget, style::anchor(el)))
}

/// `TextArea` — multi-line editable input (Enter inserts a newline). Uses the
/// dedicated `UITextArea` widget (NOT a stretched `TextField`). Honors
/// `value:`/`text:`, `placeholder:`, `background`/`bg`, `textColor`/`color`,
/// `borderColor`/`borderWidth`, `radius`, `padding`, `lineSpacing`, `wrap`,
/// `size`/`fontSize`, `font`/`fontFamily`, `maxLength`, `cursor`,
/// `width`/`height`, `anchor`.
fn build_textarea(ctx: &mut Ctx, el: &Element, layout: &mut Layout) -> Result<Widget> {
    let initial = prop_string_value(ctx, el, "value")
        .or_else(|| prop_string_value(ctx, el, "text"))
        .unwrap_or_default();
    let size = style::f32_prop(el, "size")
        .or_else(|| style::f32_prop(el, "fontSize"))
        .unwrap_or(16.0);
    let mut ta = TextArea::new(&initial, size)?;
    if let Some(ph) = prop_string_value(ctx, el, "placeholder") {
        ta = ta.placeholder(&ph)?;
    }
    ta = ta.radius(style::f32_prop(el, "radius").unwrap_or(8.0));
    if let Some(bg) = style::fill_only(el) {
        ta = ta.bg_color(to_color(bg));
    }
    if let Some(c) = style::color_prop(el, "textColor").or_else(|| style::color_prop(el, "color")) {
        ta = ta.text_color(to_color(c));
    }
    // TextArea's border takes (normal, focused, width) together; synthesize from
    // whichever of borderColor/borderWidth was given.
    let border_c = style::color_prop(el, "borderColor").map(to_color);
    let border_w = style::f32_prop(el, "borderWidth");
    if border_c.is_some() || border_w.is_some() {
        let c = border_c.unwrap_or(Color::rgb(203, 213, 225));
        ta = ta.border(c, c, border_w.unwrap_or(1.0));
    }
    if let Some((l, t, _r, _b)) = style::box_spacing(el, "padding") {
        ta = ta.padding(l, t);
    }
    if let Some(ls) = style::f32_prop(el, "lineSpacing") {
        ta = ta.line_spacing(ls);
    }
    if let Some(wm) = style::enum_member(el, "wrap").and_then(|s| wrap_from(&s)) {
        ta = ta.wrap_mode(wm);
    }
    if let Some(fam) = style::font_family(el) {
        if let Some(path) = mocida::text::get_font(&fam) {
            ta = ta.font_family(&path)?;
        }
    }
    if let Some(ml) = style::f32_prop(el, "maxLength") {
        ta = ta.max_length(ml as i32);
    }
    if let Some(c) = style::enum_member(el, "cursor") {
        ta = ta.cursor(cursor_from(&c));
    }
    let w = dim_prop(ctx, el, "width").unwrap_or(280.0);
    let h = dim_prop(ctx, el, "height").unwrap_or(120.0);
    let (x, y) = layout.next_sized(w, h);
    let widget = ta.into_widget_sized(w, h)?.position(x, y);
    Ok(apply_anchor(widget, style::anchor(el)))
}

/// `Checkbox(value:, label:, …)` — two-state checkbox with the full C style
/// surface: `value`/`checked` (initial), box fill (`background`/`bg`/
/// `boxColor`) + check mark (`checkColor`/`color`), `borderColor`/`borderWidth`,
/// `radius`, `animMs`, `cursor`, `size`. An optional `label:` (or positional)
/// renders a styled caption beside it (`textColor`/`labelColor`, `labelSize`,
/// `weight`/`fontStyle`, `font`).
fn build_checkbox(ctx: &mut Ctx, el: &Element, layout: &mut Layout) -> Result<Widget> {
    let on = eval_bool_prop(ctx, el, "value")
        .or_else(|| eval_bool_prop(ctx, el, "checked"))
        .unwrap_or(false);
    let mut cb = Checkbox::new(on)?;

    let box_c = style::fill_only(el).or_else(|| style::color_prop(el, "boxColor"));
    let check_c = style::color_prop(el, "checkColor").or_else(|| style::color_prop(el, "color"));
    match (box_c, check_c) {
        (Some(b), Some(c)) => cb = cb.colors(to_color(b), to_color(c)),
        (Some(b), None) => cb = cb.box_color(to_color(b)),
        (None, Some(c)) => cb = cb.check_color(to_color(c)),
        (None, None) => {}
    }
    if let Some(bc) = style::color_prop(el, "borderColor") {
        cb = cb.border(to_color(bc), style::f32_prop(el, "borderWidth").unwrap_or(1.0));
    } else if let Some(bw) = style::f32_prop(el, "borderWidth") {
        cb = cb.border(Color::rgb(148, 163, 184), bw);
    }
    if let Some(r) = style::f32_prop(el, "radius") {
        cb = cb.radius(r);
    }
    if let Some(ms) = style::f32_prop(el, "animMs") {
        cb = cb.anim_ms(ms as i32);
    }
    if let Some(c) = style::enum_member(el, "cursor") {
        cb = cb.cursor(cursor_from(&c));
    }
    if let Some(mut f) = handler_apply_closure(ctx, el, "onChange")
        .or_else(|| handler_apply_closure(ctx, el, "onClick"))
    {
        cb = cb.on_change(move |_| f());
    }

    let s = style::f32_prop(el, "size").unwrap_or(24.0);
    // Reactive `value: <signal expr>` tracks the signal.
    let cptr = cb.as_ptr();
    subscribe_bool_prop(ctx, el, "value", move |b| unsafe {
        mocida::sys::UICheckbox_SetChecked(cptr, b as i32);
    });
    let control = cb.into_widget_sized(s, s)?;
    control_with_caption(ctx, el, control, s, s, 14.0, layout)
}

/// A stable, opaque group identity for a `RadioButton`. Radios that pass the
/// same `group` name share a pointer (and so are mutually exclusive in mocida);
/// radios with no `group` each get a unique one (exclusivity then comes from the
/// app's own signal, the common reactive pattern).
fn radio_group_ptr(group: Option<&str>) -> *mut std::ffi::c_void {
    thread_local! {
        static GROUPS: std::cell::RefCell<(HashMap<String, usize>, usize)> =
            std::cell::RefCell::new((HashMap::new(), 0));
    }
    GROUPS.with(|g| {
        let mut g = g.borrow_mut();
        let id = match group {
            Some(name) if !name.is_empty() => {
                let next = g.0.len() + 1;
                *g.0.entry(name.to_string()).or_insert(next)
            }
            // Anonymous groups use a high, ever-incrementing id so they never
            // collide with the named ones above.
            _ => {
                g.1 += 1;
                1_000_000 + g.1
            }
        };
        id as *mut std::ffi::c_void
    })
}

/// `RadioButton(label:, selected:, …)` — a circular radio with the full C style
/// surface: `selected`/`value`/`checked`, `group`, ring/dot colors
/// (`color`/`boxColor` + `dotColor`), `borderColor`/`borderWidth`, `dotScale`,
/// `animMs`, `cursor`, `size`, and `onClick`/`onChange` (int-signal subset). An
/// optional `label:` (or positional) renders a styled caption beside the dial.
fn build_radio(ctx: &mut Ctx, el: &Element, layout: &mut Layout) -> Result<Widget> {
    let selected = eval_bool_prop(ctx, el, "selected")
        .or_else(|| eval_bool_prop(ctx, el, "value"))
        .or_else(|| eval_bool_prop(ctx, el, "checked"))
        .unwrap_or(false);

    let group = style::enum_member(el, "group").or_else(|| prop_string_value(ctx, el, "group"));
    let group_ptr = radio_group_ptr(group.as_deref());

    // SAFETY: the group pointer is a stable, non-null, never-dereferenced
    // identity (see radio_group_ptr).
    let mut radio = unsafe { RadioButton::new(group_ptr, selected) }?;

    // `color`/`dotColor` set the accent DOT (NOT the disc — painting the disc
    // with the accent makes every radio look filled/selected). `boxColor`/`bg`
    // sets the disc; the C default disc is white.
    let dot = style::color_prop(el, "dotColor").or_else(|| style::color_prop(el, "color"));
    let box_c = style::color_prop(el, "boxColor").or_else(|| style::fill_only(el));
    match (box_c, dot) {
        (Some(b), Some(d)) => radio = radio.colors(to_color(b), to_color(d)),
        (Some(b), None) => radio = radio.box_color(to_color(b)),
        (None, Some(d)) => radio = radio.dot_color(to_color(d)),
        (None, None) => {}
    }
    if let Some(bc) = style::color_prop(el, "borderColor") {
        radio = radio.border(to_color(bc), style::f32_prop(el, "borderWidth").unwrap_or(1.0));
    } else if let Some(bw) = style::f32_prop(el, "borderWidth") {
        radio = radio.border(Color::rgb(148, 163, 184), bw);
    }
    if let Some(ds) = style::f32_prop(el, "dotScale") {
        radio = radio.dot_scale(ds);
    }
    if let Some(ms) = style::f32_prop(el, "animMs") {
        radio = radio.anim_ms(ms as i32);
    }
    if let Some(cur) = style::enum_member(el, "cursor") {
        radio = radio.cursor(cursor_from(&cur));
    }
    // `enabled: false` (or an expr that resolves false, e.g. `enabled: is_admin`)
    // makes the radio non-interactive; we also dim the whole row below.
    let enabled = eval_bool_prop(ctx, el, "enabled").unwrap_or(true);
    if !enabled {
        radio = radio.enabled(false);
    }
    // onClick/onChange → recognised state mutations (e.g. `scope = "local"`).
    if let Some(mut f) = handler_apply_closure(ctx, el, "onClick")
        .or_else(|| handler_apply_closure(ctx, el, "onChange"))
    {
        radio = radio.on_change(move |_| f());
    }

    let dial = style::f32_prop(el, "size").unwrap_or(20.0);
    // Reactive `selected: scope == "x"`: re-eval + set when the signal changes,
    // so clicking one radio (which sets the signal) updates the whole group.
    let rptr = radio.as_ptr();
    subscribe_bool_prop(ctx, el, "selected", move |b| unsafe {
        mocida::sys::UIRadio_SetSelected(rptr, b as i32);
    });
    let control = radio.into_widget_sized(dial, dial)?;
    let row = control_with_caption(ctx, el, control, dial, dial, 14.0, layout)?;
    Ok(if enabled { row } else { row.opacity(0.55) })
}

/// `Switch(value:, label:, …)` — boolean toggle (pill). Full style: track
/// colors (`offColor`/`onColor`/`color` + `knobColor`), `borderColor`/
/// `borderWidth`, `animMs`, `cursor`, `width`/`height`, and an optional caption.
fn build_switch(ctx: &mut Ctx, el: &Element, layout: &mut Layout) -> Result<Widget> {
    let on = eval_bool_prop(ctx, el, "value")
        .or_else(|| eval_bool_prop(ctx, el, "checked"))
        .unwrap_or(false);
    let mut sw = Switch::new(on)?;

    let off_c = style::color_prop(el, "offColor");
    let on_c = style::color_prop(el, "onColor").or_else(|| style::color_prop(el, "color"));
    let knob_c = style::color_prop(el, "knobColor");
    if let (Some(o), Some(n), Some(k)) = (off_c, on_c, knob_c) {
        sw = sw.colors(to_color(o), to_color(n), to_color(k));
    } else {
        if let Some(o) = off_c {
            sw = sw.off_color(to_color(o));
        }
        if let Some(n) = on_c {
            sw = sw.on_color(to_color(n));
        }
        if let Some(k) = knob_c {
            sw = sw.knob_color(to_color(k));
        }
    }
    if let Some(bc) = style::color_prop(el, "borderColor") {
        sw = sw.border(to_color(bc), style::f32_prop(el, "borderWidth").unwrap_or(1.0));
    } else if let Some(bw) = style::f32_prop(el, "borderWidth") {
        sw = sw.border(Color::rgb(148, 163, 184), bw);
    }
    if let Some(ms) = style::f32_prop(el, "animMs") {
        sw = sw.anim_ms(ms as i32);
    }
    if let Some(c) = style::enum_member(el, "cursor") {
        sw = sw.cursor(cursor_from(&c));
    }
    if let Some(mut f) = handler_apply_closure(ctx, el, "onChange")
        .or_else(|| handler_apply_closure(ctx, el, "onClick"))
    {
        sw = sw.on_change(move |_| f());
    }

    let w = dim_prop(ctx, el, "width").unwrap_or(48.0);
    let h = dim_prop(ctx, el, "height").unwrap_or(28.0);
    // Reactive `value: <signal expr>` tracks the signal.
    let sptr = sw.as_ptr();
    subscribe_bool_prop(ctx, el, "value", move |b| unsafe {
        mocida::sys::UISwitch_SetOn(sptr, b as i32);
    });
    let control = sw.into_widget_sized(w, h)?;
    control_with_caption(ctx, el, control, w, h, 14.0, layout)
}

/// `Slider(min:, max:, value:, …)` — draggable value slider. Full style: track/
/// fill/knob colors (`trackColor`/`fillColor`+`color`/`knobColor`),
/// `trackHeight`, `knobRadius`, `cursor`, `width`/`height`.
fn build_slider(ctx: &mut Ctx, el: &Element, layout: &mut Layout) -> Result<Widget> {
    let min = style::f32_prop(el, "min").unwrap_or(0.0);
    let max = style::f32_prop(el, "max").unwrap_or(100.0);
    let val = style::f32_prop(el, "value").unwrap_or(min);
    let mut sl = Slider::new(min, max, val)?;

    let track = style::color_prop(el, "trackColor");
    let fill = style::color_prop(el, "fillColor").or_else(|| style::color_prop(el, "color"));
    let knob = style::color_prop(el, "knobColor");
    if let (Some(t), Some(f), Some(k)) = (track, fill, knob) {
        sl = sl.colors(to_color(t), to_color(f), to_color(k));
    } else {
        if let Some(t) = track {
            sl = sl.track_color(to_color(t));
        }
        if let Some(f) = fill {
            sl = sl.fill_color(to_color(f));
        }
        if let Some(k) = knob {
            sl = sl.knob_color(to_color(k));
        }
    }
    if let Some(th) = style::f32_prop(el, "trackHeight") {
        sl = sl.track_height(th);
    }
    if let Some(kr) = style::f32_prop(el, "knobRadius") {
        sl = sl.knob_radius(kr);
    }
    if let Some(c) = style::enum_member(el, "cursor") {
        sl = sl.cursor(cursor_from(&c));
    }

    let w = dim_prop(ctx, el, "width").unwrap_or(200.0);
    let h = dim_prop(ctx, el, "height").unwrap_or(24.0);
    let (x, y) = layout.next_sized(w, h);
    let widget = sl.into_widget_sized(w, h)?.position(x, y);
    Ok(apply_anchor(widget, style::anchor(el)))
}

/// `ProgressBar(value:, …)` — progress indicator (0..1). Full style: track/fill
/// colors (`trackColor`/`fillColor`+`color`), `radius`, and `indeterminate`/
/// `animated` for the moving sweep.
fn build_progressbar(ctx: &mut Ctx, el: &Element, layout: &mut Layout) -> Result<Widget> {
    // `value:` may be a literal OR an int signal (`value: progress`). With a
    // `max:` (default 1) the bar fills `value / max` — so a host can drive it
    // with a 0..100 progress signal and the bar tracks it live (no rebuild).
    let max = style::f32_prop(el, "max").unwrap_or(1.0).max(0.0001);
    let value_signal: Option<String> = find_prop(el, "value")
        .and_then(|p| match &p.value {
            PropValue::Expr(e) => Some(names_read(e)),
            _ => None,
        })
        .unwrap_or_default()
        .into_iter()
        .find(|n| ctx.signal(n).is_some());
    let raw_val = match &value_signal {
        Some(n) => ctx.signal(n).map(|s| s.borrow().get() as f32).unwrap_or(0.0),
        None => style::f32_prop(el, "value").unwrap_or(0.0),
    };
    let mut pb = ProgressBar::new((raw_val / max).clamp(0.0, 1.0))?;

    let track = style::color_prop(el, "trackColor");
    let fill = style::color_prop(el, "fillColor").or_else(|| style::color_prop(el, "color"));
    if let (Some(t), Some(f)) = (track, fill) {
        pb = pb.colors(to_color(t), to_color(f));
    } else {
        if let Some(t) = track {
            pb = pb.track_color(to_color(t));
        }
        if let Some(f) = fill {
            pb = pb.fill_color(to_color(f));
        }
    }
    if let Some(r) = style::f32_prop(el, "radius") {
        pb = pb.radius(r);
    }
    // Indeterminate sweep only when there's no driving value (a real value + a
    // sweep would fight each other).
    if value_signal.is_none()
        && (prop_bool(el, "indeterminate").unwrap_or(false)
            || prop_bool(el, "animated").unwrap_or(false))
    {
        pb = pb.indeterminate(true);
    }

    // Reactive value: update the bar's fill via raw ptr whenever the signal
    // changes (re-entrancy-safe — read the C value, set the C bar; no RefCell
    // re-borrow, mirroring build_text).
    let pb_ptr = pb.as_ptr();
    if let Some(name) = &value_signal {
        // Disjoint field access (`signals` vs `_subs`) so the subscribe + push
        // don't conflict (the `ctx.signal()` method would borrow all of reactive).
        if let Some(sig) = ctx.reactive.signals.get(name) {
            let sig_ptr = sig.borrow().as_ptr();
            if let Ok(sub) = sig.borrow_mut().subscribe(move |_: &Signal<i32>| {
                let v = unsafe { mocida::sys::UISignal_GetInt(sig_ptr) } as f32;
                unsafe { mocida::sys::UIProgressBar_SetValue(pb_ptr, (v / max).clamp(0.0, 1.0)) };
            }) {
                ctx.reactive._subs.push(sub);
            }
        }
    }

    let w = dim_prop(ctx, el, "width").unwrap_or(200.0);
    let h = dim_prop(ctx, el, "height").unwrap_or(8.0);
    let (x, y) = layout.next_sized(w, h);
    let widget = pb.into_widget_sized(w, h)?.position(x, y);
    Ok(apply_anchor(widget, style::anchor(el)))
}

/// `Spinner(radius:, …)` — loading spinner. Full style: `color`, `thickness`,
/// `speed` (rad/s), `radius`.
fn build_spinner(_ctx: &mut Ctx, el: &Element, layout: &mut Layout) -> Result<Widget> {
    let r = style::f32_prop(el, "radius").unwrap_or(16.0);
    let mut sp = Spinner::new(r)?;
    if let Some(c) = style::color_prop(el, "color").or_else(|| style::background(el)) {
        sp = sp.color(to_color(c));
    }
    if let Some(t) = style::f32_prop(el, "thickness") {
        sp = sp.thickness(t);
    }
    if let Some(s) = style::f32_prop(el, "speed") {
        sp = sp.speed(s);
    }
    let d = r * 2.0;
    let (x, y) = layout.next_sized(d, d);
    let widget = sp.into_widget_sized(d, d)?.position(x, y);
    Ok(apply_anchor(widget, style::anchor(el)))
}

/// Map a `fillMode:` keyword to mocida's [`FillMode`] (full set).
fn fill_mode_from(name: Option<&str>) -> FillMode {
    match name {
        Some("stretch") => FillMode::Stretch,
        Some("scale") => FillMode::Scale,
        Some("tile") => FillMode::Tile,
        Some("center") => FillMode::Center,
        Some("fit") => FillMode::Fit,
        Some("fitwidth") => FillMode::FitWidth,
        Some("fitheight") => FillMode::FitHeight,
        Some("cover") => FillMode::Cover,
        _ => FillMode::None,
    }
}

/// `Image(source, fillMode:, tint:, animated:)` — image widget. The source is
/// the positional arg or `source:`/`src:` (a `mocida://` URI resolves through
/// the bundle). `fillMode` accepts the full set (none/stretch/scale/tile/
/// center/fit/fitWidth/fitHeight/cover), `tint` recolors, `animated` plays GIFs.
fn build_image(ctx: &mut Ctx, el: &Element, layout: &mut Layout) -> Result<Widget> {
    let source = el
        .positional
        .as_ref()
        .map(|e| render_text_expr(e, ctx))
        .filter(|s| !s.is_empty())
        .or_else(|| prop_string_value(ctx, el, "source"))
        .or_else(|| prop_string_value(ctx, el, "src"))
        .unwrap_or_default();
    let fill = fill_mode_from(style::enum_member(el, "fillMode").as_deref());
    let tint = style::color_prop(el, "tint")
        .map(to_color)
        .unwrap_or(Color::WHITE);
    let animated = prop_bool(el, "animated").unwrap_or(false);
    let img = Image::new(&source, animated, fill, tint)?;
    let w = dim_prop(ctx, el, "width").unwrap_or(120.0);
    let h = dim_prop(ctx, el, "height").unwrap_or(120.0);
    let (x, y) = layout.next_sized(w, h);
    let widget = img.into_widget_sized(w, h)?.position(x, y);
    Ok(apply_anchor(widget, style::anchor(el)))
}

/// `MouseArea(onClick:, onEnter:, …) { children }` — a transparent interaction
/// surface laid over its content. The children render inside a container that
/// also defines the hit bounds; the area itself draws nothing (unless given a
/// `background`) and captures mouse events anywhere over that box. Handler
/// props: `onClick`/`onRelease`/`onMouseUp` (release), `onPress`/`onMouseDown`,
/// `onEnter`/`onHover`, `onLeave`/`onExit`, `onMove`, `onDoubleClick`, and the
/// drag trio `onDragStart`/`onDrag`/`onDragEnd` (with `draggable: true`). Also
/// `cursor:`, `enabled:`, `background:`/`radius:`/`border*`, `width`/`height`.
fn build_mouse_area(ctx: &mut Ctx, el: &Element, layout: &mut Layout) -> Result<Widget> {
    // The visible container that renders the content (transparent by default,
    // but `background:` makes the area a clickable card).
    let mut rect = Rectangle::new()?.color(to_color(style::background(el).unwrap_or(Rgba {
        r: 0,
        g: 0,
        b: 0,
        a: 0,
    })));
    if let Some(r) = style::f32_prop(el, "radius") {
        rect = rect.radius(r);
    }
    if let Some(bw) = style::f32_prop(el, "borderWidth") {
        rect = rect.border_width(bw);
    }
    if let Some(bc) = style::color_prop(el, "borderColor") {
        rect = rect.border_color(to_color(bc));
    }

    // Build children + measure (avail threaded like build_rectangle).
    ctx.declare_signals(&el.children);
    let explicit_w = dim_prop(ctx, el, "width");
    let explicit_h = dim_prop(ctx, el, "height");
    let saved_avail = (ctx.avail_w, ctx.avail_h);
    let saved_orient = ctx.parent_horizontal;
    ctx.avail_w = explicit_w.unwrap_or(ctx.avail_w).max(0.0);
    ctx.avail_h = explicit_h.unwrap_or(ctx.avail_h).max(0.0);
    ctx.parent_horizontal = None;
    let mut child_layout = Layout::root();
    let mut content_w: f32 = 0.0;
    let mut content_h: f32 = 0.0;
    for node in &el.children {
        let before_h = child_layout.content_height();
        if let Some(widget) = build_node(ctx, node, &mut child_layout)? {
            rect.add_child(widget);
            content_w = content_w.max(child_layout.content_width());
            content_h += (child_layout.content_height() - before_h).max(0.0);
        }
    }
    ctx.avail_w = saved_avail.0;
    ctx.avail_h = saved_avail.1;
    ctx.parent_horizontal = saved_orient;

    let w = explicit_w.unwrap_or(content_w.max(1.0));
    let h = explicit_h.unwrap_or(content_h.max(1.0));

    // The interaction surface: sized to the whole box and overlaid back to the
    // top (it sits AFTER the content in the rect's vertical flow, so a negative
    // top margin of the content height returns it to the origin, covering it).
    let mut area = MouseArea::new()?;
    if let Some(c) = style::enum_member(el, "cursor") {
        area = area.cursor(cursor_from(&c));
    }
    if eval_bool_prop(ctx, el, "enabled") == Some(false) {
        area = area.enabled(false);
    }
    if prop_bool(el, "draggable").unwrap_or(false) {
        area = area.draggable(true);
    }
    const EVENTS: &[(&str, MouseAreaEvent)] = &[
        ("onClick", MouseAreaEvent::MouseUp),
        ("onRelease", MouseAreaEvent::MouseUp),
        ("onMouseUp", MouseAreaEvent::MouseUp),
        ("onPress", MouseAreaEvent::MouseDown),
        ("onMouseDown", MouseAreaEvent::MouseDown),
        ("onEnter", MouseAreaEvent::HoverEnter),
        ("onHover", MouseAreaEvent::HoverEnter),
        ("onMouseEnter", MouseAreaEvent::HoverEnter),
        ("onLeave", MouseAreaEvent::HoverExit),
        ("onExit", MouseAreaEvent::HoverExit),
        ("onMouseLeave", MouseAreaEvent::HoverExit),
        ("onMove", MouseAreaEvent::MouseMove),
        ("onMouseMove", MouseAreaEvent::MouseMove),
        ("onDoubleClick", MouseAreaEvent::DoubleClick),
        ("onDragStart", MouseAreaEvent::DragStart),
        ("onDrag", MouseAreaEvent::Drag),
        ("onDragEnd", MouseAreaEvent::DragEnd),
    ];
    for (prop, ev) in EVENTS {
        if let Some(mut f) = handler_apply_closure(ctx, el, prop) {
            area = area.on(*ev, move |_| f());
        }
    }
    let area_widget = area
        .into_widget_sized(w, h)?
        .margin(0.0, -content_h, 0.0, 0.0);
    rect.add_child(area_widget);

    let (x, y) = layout.next_sized(w, h);
    Ok(apply_anchor(
        rect.into_widget_sized(w, h)?.position(x, y),
        style::anchor(el),
    ))
}

/// `Grid(columns:, gap:) { children }` — fixed-column grid container.
fn build_grid(ctx: &mut Ctx, el: &Element, layout: &mut Layout) -> Result<Widget> {
    let columns = style::f32_prop(el, "columns").map(|c| c as i32).unwrap_or(2).max(1);
    let gap = style::f32_prop(el, "gap").unwrap_or(8.0);
    let mut grid = Grid::new(columns)?.gap(gap, gap);
    ctx.declare_signals(&el.children);

    let mut child_layout = Layout::root();
    let mut content_h: f32 = 0.0;
    let mut content_w: f32 = 0.0;
    for node in &el.children {
        let before = child_layout.content_height();
        if let Some(widget) = build_node(ctx, node, &mut child_layout)? {
            grid.add(widget)?;
            content_h += (child_layout.content_height() - before).max(0.0);
            content_w = content_w.max(child_layout.content_width());
        }
    }
    // A grid arranges into `columns`; reserve roughly content_h / columns rows.
    let w = (content_w * columns as f32 + gap * (columns as f32)).max(1.0);
    let h = (content_h / columns as f32 + gap).max(1.0);
    let (x, y) = layout.next_sized(w, h);
    let widget = grid.into_widget_sized(w, h)?.position(x, y);
    Ok(apply_anchor(widget, style::anchor(el)))
}

/// `Scroll(direction:) { children }` — scrolling viewport. Children fill an
/// inner vertical stack that becomes the content. `direction`
/// (vertical|horizontal|both) picks the axes; `gap`, `wheelSpeed`, `dragScroll`,
/// `width`/`height` tune it.
fn build_scroll(ctx: &mut Ctx, el: &Element, layout: &mut Layout) -> Result<Widget> {
    let mut scroll = Scroll::new()?;
    let (v, h) = match style::enum_member(el, "direction").as_deref() {
        Some("horizontal") => (false, true),
        Some("both") => (true, true),
        _ => (true, false),
    };
    scroll = scroll.axes(v, h);
    if let Some(ws) = style::f32_prop(el, "wheelSpeed") {
        scroll = scroll.wheel_speed(ws);
    }
    if prop_bool(el, "dragScroll").unwrap_or(false) {
        scroll = scroll.drag_scroll(true);
    }

    let gap = style::f32_prop(el, "gap").unwrap_or(8.0);
    let mut inner = Stack::new(StackOrientation::Vertical)?.spacing(gap);
    ctx.declare_signals(&el.children);
    let mut child_layout = Layout::root();
    let mut content_w: f32 = 0.0;
    let mut content_h: f32 = 0.0;
    let mut count: usize = 0;
    for node in &el.children {
        let before = child_layout.content_height();
        if let Some(widget) = build_node(ctx, node, &mut child_layout)? {
            inner.add(widget)?;
            content_w = content_w.max(child_layout.content_width());
            content_h += (child_layout.content_height() - before).max(0.0);
            count += 1;
        }
    }
    let gaps = gap * (count.saturating_sub(1) as f32);
    let iw = content_w.max(1.0);
    let ih = (content_h + gaps).max(1.0);
    scroll = scroll.content(inner.into_widget_sized(iw, ih)?);

    let w = dim_prop(ctx, el, "width").unwrap_or_else(|| iw.max(120.0));
    let h = dim_prop(ctx, el, "height").unwrap_or(200.0);
    let (x, y) = layout.next_sized(w, h);
    let widget = scroll.into_widget_sized(w, h)?.position(x, y);
    Ok(apply_anchor(widget, style::anchor(el)))
}

/// `ListView(itemHeight:) { children }` — vertical scrolling list; each child is
/// a row of `itemHeight` px.
fn build_listview(ctx: &mut Ctx, el: &Element, layout: &mut Layout) -> Result<Widget> {
    let item_h = style::f32_prop(el, "itemHeight").unwrap_or(40.0);
    let mut lv = ListView::new(item_h)?;
    ctx.declare_signals(&el.children);
    let mut child_layout = Layout::root();
    let mut content_w: f32 = 0.0;
    let mut count: usize = 0;
    for node in &el.children {
        if let Some(widget) = build_node(ctx, node, &mut child_layout)? {
            lv.add(widget)?;
            content_w = content_w.max(child_layout.content_width());
            count += 1;
        }
    }
    let w = dim_prop(ctx, el, "width").unwrap_or_else(|| content_w.max(200.0));
    let h = dim_prop(ctx, el, "height")
        .unwrap_or_else(|| (item_h * count as f32).clamp(item_h, 360.0));
    let (x, y) = layout.next_sized(w, h);
    let widget = lv.into_widget_sized(w, h)?.position(x, y);
    Ok(apply_anchor(widget, style::anchor(el)))
}

/// `GridView(columns:, cellWidth:, cellHeight:) { children }` — scrolling grid
/// of fixed cells.
fn build_gridview(ctx: &mut Ctx, el: &Element, layout: &mut Layout) -> Result<Widget> {
    let columns = style::f32_prop(el, "columns")
        .map(|c| c as i32)
        .unwrap_or(2)
        .max(1);
    let cell_w = style::f32_prop(el, "cellWidth")
        .or_else(|| style::f32_prop(el, "cellSize"))
        .unwrap_or(120.0);
    let cell_h = style::f32_prop(el, "cellHeight")
        .or_else(|| style::f32_prop(el, "cellSize"))
        .unwrap_or(120.0);
    let mut gv = GridView::new(columns, cell_w, cell_h)?;
    ctx.declare_signals(&el.children);
    let mut child_layout = Layout::root();
    let mut count: i32 = 0;
    for node in &el.children {
        if let Some(widget) = build_node(ctx, node, &mut child_layout)? {
            gv.add(widget)?;
            count += 1;
        }
    }
    let rows = ((count + columns - 1) / columns).max(1);
    let gap = 8.0;
    let w = dim_prop(ctx, el, "width")
        .unwrap_or_else(|| (cell_w * columns as f32 + gap * columns as f32).max(1.0));
    let h = dim_prop(ctx, el, "height")
        .unwrap_or_else(|| (cell_h * rows as f32 + gap * rows as f32).min(420.0).max(cell_h));
    let (x, y) = layout.next_sized(w, h);
    let widget = gv.into_widget_sized(w, h)?.position(x, y);
    Ok(apply_anchor(widget, style::anchor(el)))
}

/// Convert a parsed [`Rgba`] to a mocida [`Color`] (alpha 0–1).
fn to_color(c: Rgba) -> Color {
    Color::rgba(c.r as i32, c.g as i32, c.b as i32, c.alpha_f32())
}

/// Decode a `__mui_color_RRGGBBAA` placeholder ident (the form a `#rrggbb`
/// literal takes inside a nested expression, e.g. an `if` branch) into [`Rgba`].
fn decode_color_ident(name: &str) -> Option<Rgba> {
    let hex = name.strip_prefix("__mui_color_")?;
    if hex.len() != 8 {
        return None;
    }
    let byte = |i: usize| u8::from_str_radix(&hex[i..i + 2], 16).ok();
    Some(Rgba {
        r: byte(0)?,
        g: byte(2)?,
        b: byte(4)?,
        a: byte(6)?,
    })
}

/// Resolve an expression to a color: a color-literal ident, or a conditional
/// (`if cond { #a } else { #b }` / `cond ? #a : #b`) evaluated against `env`.
fn color_from_expr(e: &Expr, env: &Env) -> Option<Rgba> {
    match &e.kind {
        ExprKind::Ident(n) => decode_color_ident(n),
        ExprKind::If { cond, then, els } => {
            if eval_bool_expr_env(cond, env).unwrap_or(false) {
                color_from_expr(then, env)
            } else {
                els.as_ref().and_then(|e| color_from_expr(e, env))
            }
        }
        ExprKind::Ternary { cond, then, els } => {
            if eval_bool_expr_env(cond, env).unwrap_or(false) {
                color_from_expr(then, env)
            } else {
                color_from_expr(els, env)
            }
        }
        ExprKind::Block(b) => b.tail.as_ref().and_then(|t| color_from_expr(t, env)),
        _ => None,
    }
}

/// Context-aware color prop: a literal color (`style::color_prop`) OR a
/// conditional color expression resolved against the live env. Use this where a
/// color may be `if`/ternary (e.g. a status pill's `color:`).
fn color_eval(ctx: &Ctx, el: &Element, name: &str) -> Option<Rgba> {
    if let Some(c) = style::color_prop(el, name) {
        return Some(c);
    }
    match &find_prop(el, name)?.value {
        PropValue::Expr(e) => color_from_expr(e, &ctx.live_env()),
        _ => None,
    }
}

/// Map an `anchor:` keyword's edges onto a widget via mocida's parent
/// alignment. Only applies when at least one axis is set; a missing axis falls
/// back to Center on that axis (mocida needs both masks).
fn apply_anchor(widget: Widget, a: Anchor) -> Widget {
    if !a.is_set() {
        return widget;
    }
    let v = match a.vertical {
        Some(VAnchor::Top) => VerticalAlign::Top,
        Some(VAnchor::Bottom) => VerticalAlign::Bottom,
        _ => VerticalAlign::Center,
    };
    let h = match a.horizontal {
        Some(HAnchor::Left) => HorizontalAlign::Left,
        Some(HAnchor::Right) => HorizontalAlign::Right,
        _ => HorizontalAlign::Center,
    };
    widget.align_to_parent(v, h);
    widget
}

/// Map a `cursor:` enum member to mocida's [`Cursor`].
fn cursor_from(name: &str) -> Cursor {
    match name {
        "pointer" | "hand" => Cursor::Pointer,
        "text" => Cursor::Text,
        _ => Cursor::Default,
    }
}

/// Apply shared typography props (`weight`/`fontStyle`/bold/italic/underline/
/// strikethrough and `font`/`fontFamily`) to a [`Text`]. Used by `Text` and by
/// the captions of labeled controls so every label honors the same font knobs.
fn apply_text_font(mut text: Text, el: &Element) -> Result<Text> {
    let bits = style::font_style_bits(el);
    if bits != 0 {
        text = text.font_style(FontStyle::from_bits(bits));
    }
    if let Some(fam) = style::font_family(el) {
        if let Some(path) = mocida::text::get_font(&fam) {
            text = text.font_family(&path)?;
        }
    }
    Ok(text)
}

/// The caption color for a labeled control: `textColor`, then `labelColor`.
/// Deliberately NOT `color` — on most controls `color` is the accent (the
/// radio ring, the slider fill…), not the label.
fn label_color(el: &Element) -> Option<Rgba> {
    style::color_prop(el, "textColor").or_else(|| style::color_prop(el, "labelColor"))
}

/// Build a styled caption [`Text`] (color + size + font) for a control's label,
/// sized to `box_h` and **vertically centered** so the text lines up with the
/// control's center (the control and caption share the row height). Returns the
/// widget and its measured width.
fn build_caption(el: &Element, label: &str, default_size: f32, box_h: f32) -> Result<(Widget, f32)> {
    let size = style::f32_prop(el, "labelSize")
        .or_else(|| style::f32_prop(el, "size"))
        .unwrap_or(default_size);
    let mut text = Text::new(label, size)?.v_align(TextVAlign::Center);
    if let Some(c) = label_color(el) {
        text = text.color(to_color(c));
    }
    text = apply_text_font(text, el)?;
    let (tw, _th) = text_extent(label, size);
    Ok((text.into_widget_sized(tw, box_h)?, tw))
}

/// Lay a control widget out with an optional caption to its right (the common
/// checkbox/radio/switch pattern). The label comes from `label:` or the
/// positional arg; with none, the control is returned alone. `cw`/`ch` are the
/// control's size, used to measure the row.
fn control_with_caption(
    ctx: &Ctx,
    el: &Element,
    control: Widget,
    cw: f32,
    ch: f32,
    default_label_size: f32,
    layout: &mut Layout,
) -> Result<Widget> {
    let label = prop_string_value(ctx, el, "label")
        .or_else(|| el.positional.as_ref().map(|e| render_text_expr(e, ctx)))
        .filter(|s| !s.is_empty());
    let Some(label) = label else {
        let (x, y) = layout.next_sized(cw, ch);
        return Ok(apply_anchor(control.position(x, y), style::anchor(el)));
    };
    // Caption shares the control's height + is vertically centered, so the text
    // lines up with the control's center rather than sitting at the top.
    let (caption, tw) = build_caption(el, &label, default_label_size, ch)?;
    let mut row = Stack::new(StackOrientation::Horizontal)?.spacing(8.0);
    row.add(control)?;
    row.add(caption)?;
    let w = cw + 8.0 + tw;
    let h = ch;
    let (x, y) = layout.next_sized(w, h);
    Ok(apply_anchor(
        row.into_widget_sized(w, h)?.position(x, y),
        style::anchor(el),
    ))
}

/// `align:`/`hAlign:` → horizontal text alignment.
fn text_h_align(name: &str) -> Option<TextHAlign> {
    match name {
        "left" | "start" => Some(TextHAlign::Left),
        "center" | "middle" => Some(TextHAlign::Center),
        "right" | "end" => Some(TextHAlign::Right),
        _ => None,
    }
}

/// `vAlign:` → vertical text alignment.
fn text_v_align(name: &str) -> Option<TextVAlign> {
    match name {
        "top" => Some(TextVAlign::Top),
        "center" | "middle" => Some(TextVAlign::Center),
        "bottom" => Some(TextVAlign::Bottom),
        _ => None,
    }
}

/// `wrap:` → wrap strategy.
fn wrap_from(name: &str) -> Option<WrapMode> {
    match name {
        "none" | "false" => Some(WrapMode::None),
        "word" | "true" => Some(WrapMode::Word),
        "char" | "character" => Some(WrapMode::Char),
        "fit" | "shrink" => Some(WrapMode::Fit),
        _ => None,
    }
}

/// Render an unmapped leaf element as a dim `[Name]` label.
fn build_placeholder(
    ctx: &mut Ctx,
    name: &str,
    el: &Element,
    layout: &mut Layout,
) -> Result<Widget> {
    let inner = el
        .positional
        .as_ref()
        .map(|e| render_text_expr(e, ctx))
        .filter(|s| !s.is_empty())
        .map(|s| format!("{name}: {s}"))
        .unwrap_or_else(|| format!("[{name}]"));
    sized_text(&inner, 14.0, Color::rgb(148, 163, 184), layout)
}

/// Create a `Text` widget with an explicit size and advance the cursor.
fn sized_text(label: &str, size: f32, color: Color, layout: &mut Layout) -> Result<Widget> {
    let (w, h) = text_extent(label, size);
    let text = Text::new(label, size)?.color(color);
    let (x, y) = layout.next_sized(w, h);
    Ok(text.into_widget_sized(w, h)?.position(x, y))
}

/// Rough text bounds in logical px (mocida re-measures the real glyph run).
fn text_extent(label: &str, size: f32) -> (f32, f32) {
    let glyphs = label.chars().count().max(1) as f32;
    let w = (glyphs * size * 0.6).ceil().max(size);
    let h = (size * 1.25).ceil() + 4.0;
    (w, h)
}

// ---------------------------------------------------------------------------
// Handler actions
// ---------------------------------------------------------------------------

/// A string-signal mutation parsed from a handler body — the forms the
/// installer uses: `name = "literal"`, `name = otherSignal` (copy), and the
/// two-way `|v| name = v` (set `name` to the input's new text).
#[derive(Clone)]
enum StrAction {
    SetLit { name: String, value: String },
    SetFrom { name: String, from: String },
    TwoWay { name: String },
}

/// Index of a top-level *assignment* `=` (not `==`/`!=`/`<=`/`>=`/`+=`/`-=`).
fn find_assign_eq(s: &str) -> Option<usize> {
    let b = s.as_bytes();
    for i in 0..b.len() {
        if b[i] != b'=' {
            continue;
        }
        let prev = if i > 0 { b[i - 1] } else { 0 };
        let next = if i + 1 < b.len() { b[i + 1] } else { 0 };
        if next != b'=' && !matches!(prev, b'=' | b'!' | b'<' | b'>' | b'+' | b'-' | b'*' | b'/') {
            return Some(i);
        }
    }
    None
}

fn is_simple_ident(s: &str) -> bool {
    !s.is_empty()
        && s.chars().all(|c| c.is_alphanumeric() || c == '_')
        && !s.chars().next().unwrap().is_ascii_digit()
}

/// Parse a handler body into the string mutations it performs (one per `;`/line
/// statement). Recognises the literal / copy / two-way forms above.
fn parse_str_actions(handler: &Handler) -> Vec<StrAction> {
    let mut out = Vec::new();
    for stmt in handler.raw.split([';', '\n']) {
        let s = stmt.trim();
        if s.is_empty() {
            continue;
        }
        let Some(eq) = find_assign_eq(s) else {
            continue;
        };
        let name = s[..eq].trim().to_string();
        let rhs = s[eq + 1..].trim();
        if !is_simple_ident(&name) {
            continue;
        }
        if rhs.len() >= 2 && rhs.starts_with('"') && rhs.ends_with('"') {
            out.push(StrAction::SetLit {
                name,
                value: rhs[1..rhs.len() - 1].to_string(),
            });
        } else if is_simple_ident(rhs) {
            if handler.params.iter().any(|p| p == rhs) {
                out.push(StrAction::TwoWay { name });
            } else {
                out.push(StrAction::SetFrom {
                    name,
                    from: rhs.to_string(),
                });
            }
        }
    }
    out
}

/// Build a closure applying a handler's recognised state mutations: the int
/// action (`x += 1`) AND string sets (`x = "lit"`, `x = otherSignal`). The
/// two-way form is skipped here (it needs the input's value — see
/// `build_textfield`). Returns `None` when nothing is wired.
fn handler_apply_closure(ctx: &Ctx, el: &Element, prop: &str) -> Option<Box<dyn FnMut()>> {
    let handler = match &find_prop(el, prop)?.value {
        PropValue::Handler(h) => h,
        _ => return None,
    };
    let int_pair: Option<(HandlerAction, Rc<RefCell<Signal<i32>>>)> = handler
        .action()
        .and_then(|a| ctx.signal(a.name()).cloned().map(|s| (a, s)));
    type StrSet = (StrAction, Rc<RefCell<Signal<String>>>, Option<Rc<RefCell<Signal<String>>>>);
    let mut str_sets: Vec<StrSet> = Vec::new();
    for a in parse_str_actions(handler) {
        match &a {
            StrAction::SetLit { name, .. } => {
                if let Some(t) = ctx.string_signal(name).cloned() {
                    str_sets.push((a, t, None));
                }
            }
            StrAction::SetFrom { name, from } => {
                if let Some(t) = ctx.string_signal(name).cloned() {
                    if let Some(src) = ctx.string_signal(from).cloned() {
                        str_sets.push((a, t, Some(src)));
                    } else if let Some(v) = ctx.env.get(from) {
                        // `from` isn't a signal but resolves in the env (e.g. a
                        // `for` loop variable, `source_dir = suggestion`) — bake
                        // its current value in as a literal set.
                        str_sets.push((
                            StrAction::SetLit {
                                name: name.clone(),
                                value: v.to_string(),
                            },
                            t,
                            None,
                        ));
                    }
                }
            }
            StrAction::TwoWay { .. } => {}
        }
    }
    // `App.setTitle(…)` / `Screen.alwaysOnTop = …` etc. (a global side effect,
    // not a signal set).
    let screen_raw = if handler.raw.contains("App.")
        || handler.raw.contains("Window.")
        || handler.raw.contains("Screen.")
    {
        Some(handler.raw.clone())
    } else {
        None
    };
    if int_pair.is_none() && str_sets.is_empty() && screen_raw.is_none() {
        return None;
    }
    Some(Box::new(move || {
        if let Some(raw) = &screen_raw {
            apply_screen_actions(raw);
        }
        if let Some((a, sig)) = &int_pair {
            let next = {
                let cur = sig.borrow().get();
                a.apply(cur)
            };
            let _ = sig.borrow_mut().set(next);
        }
        for (a, target, src) in &str_sets {
            match a {
                StrAction::SetLit { value, .. } => {
                    let _ = target.borrow_mut().set(value.clone());
                }
                StrAction::SetFrom { .. } => {
                    if let Some(s) = src {
                        let v = s.borrow().get();
                        let _ = target.borrow_mut().set(v);
                    }
                }
                StrAction::TwoWay { .. } => {}
            }
        }
    }))
}

// ===========================================================================
// onKeyInput — a tiny interpreter for `{ |event| if event.key == "A" { ... } }`
// ===========================================================================

/// Shared state the key-handler interpreter reads each press: the bound param
/// name, the pressed key, and the live signals it may mutate.
struct KeyEvalCtx<'a> {
    param: &'a str,
    key: &'a str,
    ints: &'a HashMap<String, Rc<RefCell<Signal<i32>>>>,
    strs: &'a HashMap<String, Rc<RefCell<Signal<String>>>>,
}

/// Build a per-key-press handler from `onKeyInput: { |event| ... }`. The body is
/// interpreted on each key down with the param (e.g. `event`) and `event.key`
/// bound to the pressed key name, so `if event.key == "A" { count += 1 }` works.
/// Applies signal mutations (`= += -= *= /=`, `++`/`--`) inside matching
/// branches; anything it doesn't recognise is ignored.
fn key_handler_closure(ctx: &Ctx, el: &Element, prop: &str) -> Option<Box<dyn FnMut(&str)>> {
    let handler = match &find_prop(el, prop)?.value {
        PropValue::Handler(h) => h,
        _ => return None,
    };
    let param = handler
        .params
        .first()
        .cloned()
        .unwrap_or_else(|| "key".to_string());
    let (block, _) = copper_syntax::expr::parse_stmts(&handler.raw);
    let ints: HashMap<String, Rc<RefCell<Signal<i32>>>> = ctx
        .reactive
        .signals
        .iter()
        .map(|(k, v)| (k.clone(), v.clone()))
        .collect();
    let strs: HashMap<String, Rc<RefCell<Signal<String>>>> = ctx
        .reactive
        .strings
        .iter()
        .map(|(k, v)| (k.clone(), v.clone()))
        .collect();
    Some(Box::new(move |key: &str| {
        let kc = KeyEvalCtx {
            param: &param,
            key,
            ints: &ints,
            strs: &strs,
        };
        for s in &block.stmts {
            run_key_stmt(s, &kc);
        }
    }))
}

/// Run a block: each statement, then its tail expression (a block's last
/// expression with no trailing `;` lands in `tail`, not `stmts` — that's where
/// `if k == "A" { count += 1 }`'s `count += 1` lives).
fn run_key_block(b: &copper_syntax::expr::Block, kc: &KeyEvalCtx) {
    for st in &b.stmts {
        run_key_stmt(st, kc);
    }
    if let Some(tail) = &b.tail {
        run_key_expr(tail, kc);
    }
}

fn run_key_stmt(s: &copper_syntax::expr::Stmt, kc: &KeyEvalCtx) {
    use copper_syntax::expr::Stmt;
    match s {
        Stmt::If {
            cond,
            let_pattern: None,
            then,
            els,
            ..
        } => {
            if key_eval_cond(cond, kc) {
                run_key_block(then, kc);
            } else if let Some(e) = els {
                run_key_stmt(e, kc);
            }
        }
        Stmt::BlockStmt(b) => run_key_block(b, kc),
        Stmt::IncDec { target, inc, .. } => {
            if let ExprKind::Ident(name) = &target.kind {
                if let Some(sig) = kc.ints.get(name) {
                    let cur = sig.borrow().get();
                    let _ = sig.borrow_mut().set(if *inc { cur + 1 } else { cur - 1 });
                }
            }
        }
        Stmt::Expr(e) => run_key_expr(e, kc),
        _ => {}
    }
}

/// Run an expression in statement position — the mutation kinds a key handler
/// supports: assignment, an `if`-as-value (its branches are block-exprs), or a
/// bare block.
fn run_key_expr(e: &Expr, kc: &KeyEvalCtx) {
    match &e.kind {
        ExprKind::Assign { target, op, value } => run_key_assign(target, *op, value, kc),
        ExprKind::If { cond, then, els } => {
            if key_eval_cond(cond, kc) {
                run_key_expr(then, kc);
            } else if let Some(e) = els {
                run_key_expr(e, kc);
            }
        }
        ExprKind::Block(b) => run_key_block(b, kc),
        _ => {}
    }
}

fn run_key_assign(
    target: &Expr,
    op: copper_syntax::expr::AssignOp,
    value: &Expr,
    kc: &KeyEvalCtx,
) {
    use copper_syntax::expr::AssignOp;
    let ExprKind::Ident(name) = &target.kind else {
        return;
    };
    if let Some(sig) = kc.ints.get(name) {
        let rhs = key_eval_str(value, kc).parse::<i32>().unwrap_or(0);
        let cur = sig.borrow().get();
        let next = match op {
            AssignOp::Plain => rhs,
            AssignOp::Add => cur + rhs,
            AssignOp::Sub => cur - rhs,
            AssignOp::Mul => cur * rhs,
            AssignOp::Div if rhs != 0 => cur / rhs,
            AssignOp::Rem if rhs != 0 => cur % rhs,
            _ => return,
        };
        let _ = sig.borrow_mut().set(next);
    } else if let Some(sig) = kc.strs.get(name) {
        if matches!(op, AssignOp::Plain) {
            let _ = sig.borrow_mut().set(key_eval_str(value, kc));
        }
    }
}

fn key_eval_cond(e: &Expr, kc: &KeyEvalCtx) -> bool {
    use copper_syntax::expr::UnOp;
    match &e.kind {
        ExprKind::Binary { op, lhs, rhs } => match op {
            BinOp::And => key_eval_cond(lhs, kc) && key_eval_cond(rhs, kc),
            BinOp::Or => key_eval_cond(lhs, kc) || key_eval_cond(rhs, kc),
            BinOp::Eq => key_eval_str(lhs, kc) == key_eval_str(rhs, kc),
            BinOp::Ne => key_eval_str(lhs, kc) != key_eval_str(rhs, kc),
            BinOp::Gt | BinOp::Lt | BinOp::Ge | BinOp::Le => {
                match (
                    key_eval_str(lhs, kc).parse::<f64>(),
                    key_eval_str(rhs, kc).parse::<f64>(),
                ) {
                    (Ok(a), Ok(b)) => match op {
                        BinOp::Gt => a > b,
                        BinOp::Lt => a < b,
                        BinOp::Ge => a >= b,
                        _ => a <= b,
                    },
                    _ => false,
                }
            }
            _ => false,
        },
        ExprKind::Unary {
            op: UnOp::Not,
            expr,
        } => !key_eval_cond(expr, kc),
        ExprKind::Literal(Literal::Bool(b)) => *b,
        _ => {
            let v = key_eval_str(e, kc);
            !(v.is_empty() || v == "false" || v == "0")
        }
    }
}

/// Resolve an expression to a string for comparison/assignment. `event` (the
/// param) and `event.key` both resolve to the pressed key; idents resolve to
/// their signal's current value.
fn key_eval_str(e: &Expr, kc: &KeyEvalCtx) -> String {
    match &e.kind {
        ExprKind::Literal(Literal::Int(i)) => i.to_string(),
        ExprKind::Literal(Literal::Float(f)) => f.to_string(),
        ExprKind::Literal(Literal::Bool(b)) => if *b { "1" } else { "0" }.to_string(),
        ExprKind::Literal(Literal::Str(t)) => str_template_lit(t),
        ExprKind::Ident(name) => {
            if name == kc.param {
                kc.key.to_string()
            } else if let Some(s) = kc.ints.get(name) {
                s.borrow().get().to_string()
            } else if let Some(s) = kc.strs.get(name) {
                s.borrow().get()
            } else {
                String::new()
            }
        }
        ExprKind::Member { base, field, .. } => {
            if let ExprKind::Ident(b) = &base.kind {
                if b == kc.param && field == "key" {
                    return kc.key.to_string();
                }
            }
            String::new()
        }
        ExprKind::Binary { op, lhs, rhs } => {
            match (
                key_eval_str(lhs, kc).parse::<f64>(),
                key_eval_str(rhs, kc).parse::<f64>(),
            ) {
                (Ok(a), Ok(b)) => {
                    let r = match op {
                        BinOp::Add => a + b,
                        BinOp::Sub => a - b,
                        BinOp::Mul => a * b,
                        BinOp::Div if b != 0.0 => a / b,
                        _ => return String::new(),
                    };
                    if r.fract() == 0.0 {
                        (r as i64).to_string()
                    } else {
                        r.to_string()
                    }
                }
                _ => String::new(),
            }
        }
        _ => String::new(),
    }
}

/// Concatenate a string template's literal parts (interpolated `${}` parts are
/// dropped — key handlers compare against plain literals like `"A"`).
fn str_template_lit(t: &StrTemplate) -> String {
    let mut s = String::new();
    for p in &t.parts {
        if let StrPart::Lit(l) = p {
            s.push_str(l);
        }
    }
    s
}

/// Subscribe a control's boolean prop (`selected`/`value`/`checked`) to the
/// signals it reads, re-evaluating on change and calling `set(new)` through the
/// widget's raw ptr — so `selected: scope == "x"` tracks the signal. The raw-ptr
/// reads keep it re-entrancy-safe (the write fires this synchronously).
fn subscribe_bool_prop<F>(ctx: &mut Ctx, el: &Element, prop: &str, set: F)
where
    F: FnMut(bool) + 'static,
{
    let Some(PropValue::Expr(e)) = find_prop(el, prop).map(|p| &p.value) else {
        return;
    };
    let e = e.clone();
    let names = names_read(&e);
    let int_ptrs: Vec<(String, *mut mocida::sys::UISignal)> = names
        .iter()
        .filter_map(|n| ctx.signal(n).map(|s| (n.clone(), s.borrow().as_ptr())))
        .collect();
    let str_ptrs: Vec<(String, *mut mocida::sys::UISignal)> = names
        .iter()
        .filter_map(|n| ctx.string_signal(n).map(|s| (n.clone(), s.borrow().as_ptr())))
        .collect();
    if int_ptrs.is_empty() && str_ptrs.is_empty() {
        return;
    }
    let base = ctx.env.clone();
    let set = Rc::new(RefCell::new(set));
    let updater = move || {
        let mut live = base.clone();
        for (n, p) in &int_ptrs {
            live.vars
                .insert(n.clone(), unsafe { mocida::sys::UISignal_GetInt(*p) }.to_string());
        }
        for (n, p) in &str_ptrs {
            live.vars.insert(n.clone(), unsafe { str_from_signal(*p) });
        }
        if let Some(b) = eval_bool_expr_env(&e, &live) {
            (set.borrow_mut())(b);
        }
    };
    let mut new_subs = Vec::new();
    for n in &names {
        let updater = updater.clone();
        if let Some(sig) = ctx.signal(n) {
            if let Ok(sub) = sig.borrow_mut().subscribe(move |_| updater()) {
                new_subs.push(sub);
            }
        } else if let Some(sig) = ctx.string_signal(n) {
            if let Ok(sub) = sig.borrow_mut().subscribe(move |_| updater()) {
                new_subs.push(sub);
            }
        }
    }
    ctx.reactive._subs.extend(new_subs);
}

/// Run an `effect { ... }` body: apply each interpretable statement to its
/// signal, once, at mount. Statements the int-signal interpreter doesn't
/// understand (conditionals, string assignments, cross-signal expressions) are
/// skipped — those await the fuller evaluator. Effects don't re-run yet: the
/// recognised actions are self-referential (`x = x + 1`), so subscribing them to
/// their own signal would loop; reactive re-run lands with the real evaluator.
fn run_effect(ctx: &mut Ctx, raw: &str) {
    for action in mui_syntax::ast::parse_actions(raw) {
        if let Some(sig) = ctx.signal(action.name()).cloned() {
            let next = {
                let cur = sig.borrow().get();
                action.apply(cur)
            };
            let _ = sig.borrow_mut().set(next);
        }
    }
    // `Screen.alwaysOnTop = true` etc. in an effect (runs once on mount).
    apply_screen_actions(raw);
}

trait ActionExt {
    fn name(&self) -> &str;
    fn apply(&self, current: i32) -> i32;
}

impl ActionExt for HandlerAction {
    fn name(&self) -> &str {
        match self {
            HandlerAction::AddAssign { name, .. } | HandlerAction::SetInt { name, .. } => name,
        }
    }
    fn apply(&self, current: i32) -> i32 {
        match self {
            HandlerAction::AddAssign { delta, .. } => current + (*delta as i32),
            HandlerAction::SetInt { value, .. } => *value as i32,
        }
    }
}

// ---------------------------------------------------------------------------
// Static name environment (param defaults + signal initial values, for the
// initial render and for names that aren't live signals)
// ---------------------------------------------------------------------------

#[derive(Default, Clone)]
struct Env {
    vars: HashMap<String, String>,
}

impl Env {
    fn from_view(view: &View) -> Self {
        let mut env = Env::default();
        for p in &view.params {
            if let Some(text) = p.default.as_ref().and_then(literal_display) {
                env.vars.insert(p.name.clone(), text);
            }
        }
        env
    }
    fn get(&self, name: &str) -> Option<&str> {
        self.vars.get(name).map(String::as_str)
    }
}

// ---------------------------------------------------------------------------
// Prop extraction helpers
// ---------------------------------------------------------------------------

/// Find a prop by name. (Typed extraction lives in `mui_syntax::style`; this
/// is only for props the runtime inspects structurally, like `onClick`.)
fn find_prop<'a>(el: &'a Element, name: &str) -> Option<&'a Prop> {
    el.props.iter().find(|p| p.name == name)
}

/// A string prop (`value: "hi"`, `placeholder: "..."`), resolved through the
/// context so `value: name` picks up a param/signal value.
fn prop_string_value(ctx: &Ctx, el: &Element, name: &str) -> Option<String> {
    match &find_prop(el, name)?.value {
        PropValue::Expr(e) => Some(render_text_expr(e, ctx)),
        _ => None,
    }
}

/// A boolean prop (`value: true` / `checked: false`).
fn prop_bool(el: &Element, name: &str) -> Option<bool> {
    match &find_prop(el, name)?.value {
        PropValue::Expr(e) => match &e.kind {
            ExprKind::Literal(Literal::Bool(b)) => Some(*b),
            _ => None,
        },
        _ => None,
    }
}

/// A boolean prop that may be a literal (`true`) OR an expression resolved
/// against the current state: `scope == "local"`, `a && b`, `a || b`, `!x`, or a
/// bare truthy name. This is what lets `selected: scope == "local"` pick the
/// right radio on the initial render. Returns `None` if absent / not boolean.
fn eval_bool_prop(ctx: &Ctx, el: &Element, name: &str) -> Option<bool> {
    match &find_prop(el, name)?.value {
        PropValue::Expr(e) => eval_bool_expr_env(e, &ctx.live_env()),
        _ => None,
    }
}

/// Evaluate a boolean expression against a resolved string env (the `if`-cond
/// subset): `&&`/`||`, `==`/`!=`, a bool literal, or a bare name's truthiness.
/// Pure — used for the initial render (env = live snapshot) AND reactive re-eval
/// (env = fresh raw-ptr reads), so a radio's `selected:` tracks its signal.
fn eval_bool_expr_env(e: &Expr, env: &Env) -> Option<bool> {
    match &e.kind {
        ExprKind::Literal(Literal::Bool(b)) => Some(*b),
        ExprKind::Ident(_) => {
            let v = operand_value_env(e, env);
            Some(!(v.is_empty() || v == "false" || v == "0"))
        }
        ExprKind::Binary { op, lhs, rhs } => match op {
            BinOp::And => Some(eval_bool_expr_env(lhs, env)? && eval_bool_expr_env(rhs, env)?),
            BinOp::Or => Some(eval_bool_expr_env(lhs, env)? || eval_bool_expr_env(rhs, env)?),
            BinOp::Eq => Some(operand_value_env(lhs, env) == operand_value_env(rhs, env)),
            BinOp::Ne => Some(operand_value_env(lhs, env) != operand_value_env(rhs, env)),
            BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge => {
                let a = operand_value_env(lhs, env).parse::<f64>().ok()?;
                let b = operand_value_env(rhs, env).parse::<f64>().ok()?;
                Some(match op {
                    BinOp::Lt => a < b,
                    BinOp::Le => a <= b,
                    BinOp::Gt => a > b,
                    _ => a >= b,
                })
            }
            _ => None,
        },
        ExprKind::Block(b) => b.tail.as_ref().and_then(|t| eval_bool_expr_env(t, env)),
        _ => None,
    }
}

/// Resolve an expression to its value string against `env` (for `==`/`!=` and
/// truthiness): a literal renders itself; an identifier resolves through env.
fn operand_value_env(e: &Expr, env: &Env) -> String {
    if let Some(s) = literal_display(e) {
        return s;
    }
    match &e.kind {
        ExprKind::Ident(n) => env.get(n).map(str::to_string).unwrap_or_default(),
        // `level(line)` / `msg(line)` so `if level(line) == "ok"` works.
        ExprKind::Call { callee, args, .. } if helper_call_name(callee).is_some() => {
            eval_helper_call(helper_call_name(callee).unwrap(), args, env)
        }
        // `Screen.width` / `Screen.height` in a condition.
        ExprKind::Member { base, field, .. } => screen_member(base, field).unwrap_or_default(),
        ExprKind::Block(b) => b
            .tail
            .as_ref()
            .map(|t| operand_value_env(t, env))
            .unwrap_or_default(),
        _ => String::new(),
    }
}

// ---------------------------------------------------------------------------
// Text rendering (`${...}` interpolation)
// ---------------------------------------------------------------------------

/// Render a text expression for the initial draw, resolving names through the
/// context: a live signal → its current value, else the static env, else a
/// `{name}` placeholder.
fn render_text_expr(e: &Expr, ctx: &Ctx) -> String {
    render_text_expr_env(e, &ctx.live_env())
}

/// Pure version: render against a fully-resolved string env (used by the
/// reactive updater, which fills the env with live signal values first).
fn render_text_expr_env(e: &Expr, env: &Env) -> String {
    match &e.kind {
        ExprKind::Literal(Literal::Str(t)) => render_template(t, env),
        ExprKind::Literal(Literal::Int(i)) => i.to_string(),
        ExprKind::Literal(Literal::Float(f)) => f.to_string(),
        ExprKind::Literal(Literal::Bool(b)) => b.to_string(),
        ExprKind::Ident(n) => resolve_name(n, env),
        ExprKind::Call { callee, args, .. } if is_format_call(callee) => {
            render_format_call(args, env)
        }
        // Log helpers `level(line)` / `msg(line)`: split a "level: text" entry so
        // a `for line in log_lines` body can show `msg(line)` colored by
        // `level(line)`. Recognised in text, conditions, and colors.
        ExprKind::Call { callee, args, .. } if helper_call_name(callee).is_some() => {
            eval_helper_call(helper_call_name(callee).unwrap(), args, env)
        }
        // Conditional text: `if cond { a } else { b }` / `cond ? a : b`. Evaluate
        // the condition against the env and render the chosen branch (so the
        // installer's status pills, `if ok { "[ok]" } else { "[x]" }`, render).
        ExprKind::If { cond, then, els } => {
            if eval_bool_expr_env(cond, env).unwrap_or(false) {
                render_text_expr_env(then, env)
            } else if let Some(e) = els {
                render_text_expr_env(e, env)
            } else {
                String::new()
            }
        }
        ExprKind::Ternary { cond, then, els } => {
            if eval_bool_expr_env(cond, env).unwrap_or(false) {
                render_text_expr_env(then, env)
            } else {
                render_text_expr_env(els, env)
            }
        }
        // A braced branch (`{ "[ok]" }`) is a block — render its tail value.
        ExprKind::Block(b) => b
            .tail
            .as_ref()
            .map(|t| render_text_expr_env(t, env))
            .unwrap_or_default(),
        // `Screen.width` / `Screen.height` — live screen metrics.
        ExprKind::Member { base, field, .. } => screen_member(base, field).unwrap_or_default(),
        _ => String::new(),
    }
}

/// A numeric `Screen.*` / `Window.*` metric (display vs this window). `None` for
/// non-numeric / unknown members.
fn screen_metric(ns: &str, field: &str) -> Option<i32> {
    Some(match (ns, field) {
        ("Screen", "width") => unsafe { mocida::sys::UIScreen_GetWidth() },
        ("Screen", "height") => unsafe { mocida::sys::UIScreen_GetHeight() },
        ("Window", "width") => unsafe { mocida::sys::UIApp_GetWidthG() },
        ("Window", "height") => unsafe { mocida::sys::UIApp_GetHeightG() },
        _ => return None,
    })
}

/// A `Screen.*` / `Window.*` member read → its live value as a string (from an
/// `Expr::Member`). `None` for anything else.
fn screen_member(base: &Expr, field: &str) -> Option<String> {
    let ExprKind::Ident(ns) = &base.kind else {
        return None;
    };
    if let Some(n) = screen_metric(ns, field) {
        return Some(n.to_string());
    }
    match (ns.as_str(), field) {
        ("Window", "title") | ("App", "title") => Some(unsafe {
            let p = mocida::sys::UIApp_GetTitleG();
            if p.is_null() {
                String::new()
            } else {
                std::ffi::CStr::from_ptr(p).to_string_lossy().into_owned()
            }
        }),
        _ => None,
    }
}

/// Evaluate a numeric expression at build time: literals, `Screen.width/height`,
/// `+ - * /` arithmetic (so `Screen.width - 80` works), and int-signal / env
/// idents. `None` if it isn't numeric.
fn eval_f32(e: &Expr, ctx: &Ctx) -> Option<f32> {
    match &e.kind {
        ExprKind::Literal(Literal::Int(i)) => Some(*i as f32),
        ExprKind::Literal(Literal::Float(f)) => Some(*f as f32),
        ExprKind::Member { base, field, .. } => {
            screen_member(base, field).and_then(|s| s.parse::<f32>().ok())
        }
        ExprKind::Binary { op, lhs, rhs } => {
            let a = eval_f32(lhs, ctx)?;
            let b = eval_f32(rhs, ctx)?;
            match op {
                BinOp::Add => Some(a + b),
                BinOp::Sub => Some(a - b),
                BinOp::Mul => Some(a * b),
                BinOp::Div if b != 0.0 => Some(a / b),
                _ => None,
            }
        }
        ExprKind::Ident(n) => ctx
            .signal(n)
            .map(|s| s.borrow().get() as f32)
            .or_else(|| ctx.env.get(n).and_then(|v| v.parse::<f32>().ok())),
        _ => None,
    }
}

/// A numeric prop (width/height/size/…) that may be a literal OR a runtime
/// expression (`Screen.width - 80`). Tries the static literal first, then the
/// runtime evaluator.
fn dim_prop(ctx: &Ctx, el: &Element, name: &str) -> Option<f32> {
    if let Some(v) = style::f32_prop(el, name) {
        return Some(v);
    }
    match &find_prop(el, name)?.value {
        PropValue::Expr(e) => eval_f32(e, ctx),
        // `width: Window.width` (no arithmetic) parses as a `Type.Member` enum,
        // not an expression — resolve it as a screen/window metric.
        PropValue::Mui(MuiValue::Enum { ty: Some(ty), member }) => {
            screen_metric(ty, member).map(|n| n as f32)
        }
        _ => None,
    }
}

/// Run the `App.*` / `Window.*` / `Screen.*` side effects in a handler/effect
/// body (`App.setTitle("x")`, `App.setMaxFps(60)`, `App.alwaysOnTop = true`,
/// `App.setSize(w, h)`, …). Parsed with the real Copper statement parser so
/// string args survive intact. Callable from anywhere a body runs.
fn apply_screen_actions(raw: &str) {
    let (block, _) = copper_syntax::expr::parse_stmts(raw);
    apply_host_block(&block);
}

fn apply_host_block(b: &copper_syntax::expr::Block) {
    for s in &b.stmts {
        match s {
            copper_syntax::expr::Stmt::Expr(e) => apply_host_expr(e),
            copper_syntax::expr::Stmt::BlockStmt(inner) => apply_host_block(inner),
            _ => {}
        }
    }
    if let Some(t) = &b.tail {
        apply_host_expr(t);
    }
}

fn apply_host_expr(e: &Expr) {
    match &e.kind {
        // `App.method(args)` / `Window.method(args)`
        ExprKind::Call { callee, args, .. } => {
            if let ExprKind::Member { base, field, .. } = &callee.kind {
                if matches!(&base.kind, ExprKind::Ident(n) if n == "App" || n == "Window") {
                    dispatch_host_call(field, args);
                }
            }
        }
        // `App.alwaysOnTop = true` / `Window.title = "x"` / `Screen.alwaysOnTop = …`
        ExprKind::Assign { target, value, .. } => {
            if let ExprKind::Member { base, field, .. } = &target.kind {
                if matches!(&base.kind, ExprKind::Ident(n) if n == "App" || n == "Window" || n == "Screen")
                {
                    dispatch_host_assign(field, value);
                }
            }
        }
        ExprKind::Block(b) => apply_host_block(b),
        _ => {}
    }
}

fn lit_i32(e: &Expr) -> Option<i32> {
    match &e.kind {
        ExprKind::Literal(Literal::Int(i)) => Some(*i as i32),
        ExprKind::Literal(Literal::Float(f)) => Some(*f as i32),
        _ => None,
    }
}
fn lit_bool(e: &Expr) -> Option<bool> {
    match &e.kind {
        ExprKind::Literal(Literal::Bool(b)) => Some(*b),
        _ => None,
    }
}
fn set_window_title(e: &Expr) {
    let t = render_text_expr_env(e, &Env::default());
    if let Ok(c) = std::ffi::CString::new(t) {
        unsafe { mocida::sys::UIApp_SetTitleG(c.as_ptr()) };
    }
}

fn dispatch_host_call(field: &str, args: &[Expr]) {
    match field {
        "setTitle" => {
            if let Some(a) = args.first() {
                set_window_title(a);
            }
        }
        "setMaxFps" | "setTargetFps" => {
            if let Some(n) = args.first().and_then(lit_i32) {
                unsafe { mocida::sys::UIApp_SetMaxFpsG(n) };
            }
        }
        "setSize" => {
            if let (Some(w), Some(h)) =
                (args.first().and_then(lit_i32), args.get(1).and_then(lit_i32))
            {
                unsafe { mocida::sys::UIApp_SetSizeG(w, h) };
            }
        }
        "setAlwaysOnTop" => {
            if let Some(b) = args.first().and_then(lit_bool) {
                unsafe { mocida::sys::UIApp_SetAlwaysOnTop(b as i32) };
            }
        }
        _ => {}
    }
}

fn dispatch_host_assign(field: &str, value: &Expr) {
    match field {
        "alwaysOnTop" => unsafe {
            mocida::sys::UIApp_SetAlwaysOnTop(lit_bool(value).unwrap_or(false) as i32)
        },
        "title" => set_window_title(value),
        "maxFps" => {
            if let Some(n) = lit_i32(value) {
                unsafe { mocida::sys::UIApp_SetMaxFpsG(n) };
            }
        }
        _ => {}
    }
}

/// If `callee` is one of the built-in log helpers (`level`/`msg`), its name.
fn helper_call_name(callee: &Expr) -> Option<&str> {
    if let ExprKind::Ident(n) = &callee.kind {
        if n == "level" || n == "msg" {
            return Some(n);
        }
    }
    None
}

/// Evaluate `level(line)` / `msg(line)` — split the (resolved) argument on its
/// first `:`. `level` → the part before (trimmed), `msg` → the part after
/// (trimmed); with no `:`, `level` is empty and `msg` is the whole string.
fn eval_helper_call(name: &str, args: &[Expr], env: &Env) -> String {
    let arg = args
        .first()
        .map(|a| render_text_expr_env(a, env))
        .unwrap_or_default();
    match name {
        "level" => arg
            .split_once(':')
            .map(|(l, _)| l.trim().to_string())
            .unwrap_or_default(),
        "msg" => arg
            .split_once(':')
            .map(|(_, r)| r.trim().to_string())
            .unwrap_or(arg),
        _ => String::new(),
    }
}

/// Names an expression reads (idents), so the runtime knows which signals a
/// text/label depends on. Walks the `format(tmpl, args...)` shape Copper lowers
/// interpolation to, plus bare idents.
fn names_read(e: &Expr) -> Vec<String> {
    let mut out = Vec::new();
    collect_idents(e, &mut out);
    out
}

fn collect_idents(e: &Expr, out: &mut Vec<String>) {
    match &e.kind {
        ExprKind::Ident(n) => {
            if !out.contains(n) {
                out.push(n.clone());
            }
        }
        ExprKind::Call { callee, args, .. } => {
            // Skip the `format` callee itself; collect from args.
            if !is_format_call(callee) {
                collect_idents(callee, out);
            }
            for a in args {
                collect_idents(a, out);
            }
        }
        ExprKind::Binary { lhs, rhs, .. } => {
            collect_idents(lhs, out);
            collect_idents(rhs, out);
        }
        ExprKind::If { cond, then, els } => {
            collect_idents(cond, out);
            collect_idents(then, out);
            if let Some(e) = els {
                collect_idents(e, out);
            }
        }
        ExprKind::Ternary { cond, then, els } => {
            collect_idents(cond, out);
            collect_idents(then, out);
            collect_idents(els, out);
        }
        ExprKind::Block(b) => {
            if let Some(t) = &b.tail {
                collect_idents(t, out);
            }
        }
        _ => {}
    }
}

fn resolve_name(name: &str, env: &Env) -> String {
    env.get(name)
        .map(str::to_string)
        .unwrap_or_else(|| format!("{{{name}}}"))
}

fn is_format_call(callee: &Expr) -> bool {
    matches!(&callee.kind, ExprKind::Ident(n) if n == "format" || n == "format!")
}

fn render_format_call(args: &[Expr], env: &Env) -> String {
    let Some((template, rest)) = args.split_first() else {
        return String::new();
    };
    let tmpl = match &template.kind {
        ExprKind::Literal(Literal::Str(t)) => render_template_raw(t),
        _ => return String::new(),
    };
    let mut fills = rest.iter().map(|a| render_text_expr_env(a, env));
    let mut out = String::with_capacity(tmpl.len());
    let mut chars = tmpl.chars().peekable();
    while let Some(c) = chars.next() {
        if c == '{' && chars.peek() == Some(&'}') {
            chars.next();
            out.push_str(&fills.next().unwrap_or_else(|| "{...}".to_string()));
        } else {
            out.push(c);
        }
    }
    out
}

fn render_template_raw(t: &StrTemplate) -> String {
    t.parts
        .iter()
        .map(|p| match p {
            StrPart::Lit(s) => s.clone(),
            StrPart::Expr(_) => "{}".to_string(),
        })
        .collect()
}

fn render_template(t: &StrTemplate, env: &Env) -> String {
    let mut out = String::new();
    for part in &t.parts {
        match part {
            StrPart::Lit(s) => out.push_str(s),
            StrPart::Expr(e) => match &e.kind {
                ExprKind::Ident(n) => out.push_str(&resolve_name(n, env)),
                _ => out.push_str("{...}"),
            },
        }
    }
    out
}

fn literal_display(e: &Expr) -> Option<String> {
    match &e.kind {
        ExprKind::Literal(Literal::Str(t)) => Some(render_template_raw(t).replace("{}", "")),
        ExprKind::Literal(Literal::Int(i)) => Some(i.to_string()),
        ExprKind::Literal(Literal::Float(f)) => Some(f.to_string()),
        ExprKind::Literal(Literal::Bool(b)) => Some(b.to_string()),
        _ => None,
    }
}

/// The display value of a binding initializer — the literal, or the value inside
/// `signal(...)` — so `mut phase = signal("config")` records `phase = "config"`.
fn binding_display(e: &Expr, env: &Env) -> Option<String> {
    if let ExprKind::Call { callee, args, .. } = &e.kind {
        if matches!(&callee.kind, ExprKind::Ident(n) if n == "signal") {
            let arg = args.first()?;
            return literal_display(arg).or_else(|| match &arg.kind {
                ExprKind::Ident(n) => env.get(n).map(str::to_string),
                _ => None,
            });
        }
    }
    literal_display(e)
}

/// Evaluate an `if` condition against current binding/signal values. Handles
/// `&&` / `||`, `==` / `!=`, `!atom`, `true`/`false`, and a bare name (truthy
/// when its value isn't empty / `false` / `0`). Unparseable conditions default
/// to `true` so content isn't hidden. This is the build-time subset; full
/// reactive re-evaluation awaits the evaluator.
fn eval_condition(ctx: &Ctx, raw: &str) -> bool {
    eval_condition_env(raw, &ctx.live_env())
}

/// Pure condition evaluator against a resolved string env (signal values folded
/// in). Used both for the build-time render (env = live snapshot) and for the
/// structural-change subscriptions, which re-eval against fresh raw-ptr reads to
/// decide whether a branch actually flipped.
fn eval_condition_env(raw: &str, env: &Env) -> bool {
    let s = strip_parens(raw.trim());
    if s.is_empty() {
        return true;
    }
    if let Some(parts) = split_top(s, "||") {
        return parts.iter().any(|p| eval_condition_env(p, env));
    }
    if let Some(parts) = split_top(s, "&&") {
        return parts.iter().all(|p| eval_condition_env(p, env));
    }
    for (op, want_eq) in [("==", true), ("!=", false)] {
        if let Some(i) = find_top(s, op) {
            let l = resolve_operand_env(s[..i].trim(), env);
            let r = resolve_operand_env(s[i + op.len()..].trim(), env);
            return if want_eq { l == r } else { l != r };
        }
    }
    // Numeric comparisons (`count > 10`, `n <= max`). `>=`/`<=` are checked
    // before `>`/`<` so the two-char ops aren't split mid-operator. Non-numeric
    // operands fall through (treated as not-matching).
    for op in [">=", "<=", ">", "<"] {
        if let Some(i) = find_top(s, op) {
            let l = resolve_operand_env(s[..i].trim(), env);
            let r = resolve_operand_env(s[i + op.len()..].trim(), env);
            if let (Ok(a), Ok(b)) = (l.parse::<f64>(), r.parse::<f64>()) {
                return match op {
                    ">=" => a >= b,
                    "<=" => a <= b,
                    ">" => a > b,
                    _ => a < b,
                };
            }
        }
    }
    if let Some(rest) = s.strip_prefix('!') {
        return !eval_condition_env(rest.trim(), env);
    }
    match s {
        "true" => true,
        "false" => false,
        _ => {
            let v = resolve_operand_env(s, env);
            !(v.is_empty() || v == "false" || v == "0")
        }
    }
}

/// Resolve one operand to its current value string against `env`: a quoted
/// string → its contents; a number/bool literal → itself; an identifier → its
/// env value (the live snapshot already folds in signal values).
fn resolve_operand_env(s: &str, env: &Env) -> String {
    let s = s.trim();
    if s.len() >= 2 && s.starts_with('"') && s.ends_with('"') {
        return s[1..s.len() - 1].to_string();
    }
    // Bool literals normalize to 0/1 so `is_admin == false` matches a bool
    // signal — which rides an INT signal (false=0, true=1), and so resolves to
    // "0"/"1", not "false"/"true". Without this, "0" == "false" is never true.
    if s == "false" {
        return "0".to_string();
    }
    if s == "true" {
        return "1".to_string();
    }
    if s.parse::<f64>().is_ok() {
        return s.to_string();
    }
    env.get(s).map(str::to_string).unwrap_or_default()
}

/// Resolve one operand to its current value string: a quoted string → its
/// contents; a number/bool literal → itself; an identifier → its live int-signal
/// value, else its recorded binding value.
#[allow(dead_code)]
fn resolve_operand(ctx: &Ctx, s: &str) -> String {
    let s = s.trim();
    if s.len() >= 2 && s.starts_with('"') && s.ends_with('"') {
        return s[1..s.len() - 1].to_string();
    }
    if s == "true" || s == "false" || s.parse::<f64>().is_ok() {
        return s.to_string();
    }
    if let Some(sig) = ctx.signal(s) {
        return sig.borrow().get().to_string();
    }
    // A live STRING signal (e.g. `phase == "config"`) — read its current value,
    // not the stale binding default recorded in `env`.
    if let Some(sig) = ctx.string_signal(s) {
        return sig.borrow().get();
    }
    ctx.env.get(s).map(str::to_string).unwrap_or_default()
}

/// Strip one layer of wrapping `( ... )` if it spans the whole string.
fn strip_parens(s: &str) -> &str {
    let s = s.trim();
    if s.starts_with('(') && s.ends_with(')') && find_top(&s[1..s.len() - 1], ")").is_none() {
        return s[1..s.len() - 1].trim();
    }
    s
}

/// Index of `op` at the top level (outside quotes/parens), or `None`.
fn find_top(s: &str, op: &str) -> Option<usize> {
    let bytes = s.as_bytes();
    let mut depth = 0i32;
    let mut in_str = false;
    let mut i = 0;
    while i < bytes.len() {
        let c = bytes[i] as char;
        if in_str {
            if c == '"' {
                in_str = false;
            }
        } else if c == '"' {
            in_str = true;
        } else if c == '(' || c == '[' {
            depth += 1;
        } else if c == ')' || c == ']' {
            depth -= 1;
        } else if depth == 0 && s[i..].starts_with(op) {
            return Some(i);
        }
        i += 1;
    }
    None
}

/// Split `s` on every top-level occurrence of `op`; `None` if `op` is absent.
fn split_top<'a>(s: &'a str, op: &str) -> Option<Vec<&'a str>> {
    if find_top(s, op).is_none() {
        return None;
    }
    let mut parts = Vec::new();
    let mut rest = s;
    while let Some(i) = find_top(rest, op) {
        parts.push(rest[..i].trim());
        rest = &rest[i + op.len()..];
    }
    parts.push(rest.trim());
    Some(parts)
}

/// Initial integer value of a `signal(...)` binding, resolving a param-ident
/// initializer through the env (`signal(start)` where `start = 0`).
fn signal_init_int(e: &Expr, env: &Env) -> Option<i32> {
    let ExprKind::Call { callee, args, .. } = &e.kind else {
        return None;
    };
    if !matches!(&callee.kind, ExprKind::Ident(n) if n == "signal") {
        return None;
    }
    match &args.first()?.kind {
        ExprKind::Literal(Literal::Int(i)) => Some(*i as i32),
        // Bool signals (`signal(false)`) ride on the int channel as 0/1, so
        // `enabled: has_cargo` and `ok: is_admin` work and a host can seed them.
        ExprKind::Literal(Literal::Bool(b)) => Some(*b as i32),
        ExprKind::Ident(n) => env.get(n).and_then(|v| match v {
            "true" => Some(1),
            "false" => Some(0),
            _ => v.parse::<i32>().ok(),
        }),
        _ => None,
    }
}

/// True when the binding is a list signal: `signal([])` (an array initializer).
fn signal_init_is_list(e: &Expr) -> bool {
    if let ExprKind::Call { callee, args, .. } = &e.kind {
        if matches!(&callee.kind, ExprKind::Ident(n) if n == "signal") {
            return matches!(args.first().map(|a| &a.kind), Some(ExprKind::Array(_)));
        }
    }
    false
}

/// Initial string value of a `signal("...")` binding (or `signal(param)` whose
/// param resolves to a string). `None` when it isn't a string signal.
fn signal_init_str(e: &Expr, env: &Env) -> Option<String> {
    let ExprKind::Call { callee, args, .. } = &e.kind else {
        return None;
    };
    if !matches!(&callee.kind, ExprKind::Ident(n) if n == "signal") {
        return None;
    }
    match &args.first()?.kind {
        ExprKind::Literal(Literal::Str(t)) => Some(render_template_raw(t).replace("{}", "")),
        ExprKind::Ident(n) => env.get(n).map(str::to_string),
        _ => None,
    }
}

/// Read a string signal's current value through its raw `*mut UISignal` — the
/// re-entrancy-safe path used inside subscriptions (see `build_text`), mirroring
/// the `UISignal_GetInt` int path.
unsafe fn str_from_signal(ptr: *mut mocida::sys::UISignal) -> String {
    let p = unsafe { mocida::sys::UISignal_GetString(ptr) };
    if p.is_null() {
        String::new()
    } else {
        unsafe { std::ffi::CStr::from_ptr(p) }
            .to_string_lossy()
            .into_owned()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn if_condition_evaluation() {
        let reg = Registry::new();
        let mut ctx = Ctx::new(&reg);
        ctx.env.vars.insert("phase".into(), "config".into());
        ctx.env.vars.insert("source_dir".into(), "".into());
        assert!(eval_condition(&ctx, "phase == \"config\""));
        assert!(!eval_condition(&ctx, "phase == \"prereqs\""));
        assert!(eval_condition(&ctx, "phase != \"prereqs\""));
        assert!(!eval_condition(&ctx, "source_dir != \"\""));
        assert!(eval_condition(&ctx, "phase == \"config\" && source_dir == \"\""));
        assert!(eval_condition(&ctx, "phase == \"x\" || phase == \"config\""));
        assert!(eval_condition(&ctx, "true"));
        assert!(!eval_condition(&ctx, "false"));
    }

    #[test]
    fn string_binding_recorded_for_conditions() {
        // `mut phase = signal("config")` must make `phase` readable as "config".
        let v = view0("view M() { mut phase = signal(\"config\")\n Text(\"x\") }");
        let reg = Registry::new();
        let mut ctx = Ctx::new(&reg);
        ctx.env = Env::from_view(&v);
        ctx.declare_signals(&v.body);
        assert_eq!(ctx.env.get("phase"), Some("config"));
        assert!(eval_condition(&ctx, "phase == \"config\""));
    }

    fn view0(src: &str) -> View {
        let doc = mui_syntax::parse(src);
        assert!(doc.errors.is_empty(), "parse errors: {:?}", doc.errors);
        doc.views.into_iter().next().expect("a view")
    }

    #[test]
    fn handler_action_increments_signal() {
        // The reactive core: a `+`/`-` handler maps to an int update.
        use mui_syntax::ast::HandlerAction;
        let inc = HandlerAction::AddAssign {
            name: "count".into(),
            delta: 1,
        };
        assert_eq!(inc.apply(0), 1);
        let dec = HandlerAction::AddAssign {
            name: "count".into(),
            delta: -1,
        };
        assert_eq!(dec.apply(5), 4);
        let set = HandlerAction::SetInt {
            name: "count".into(),
            value: 42,
        };
        assert_eq!(set.apply(7), 42);
    }

    #[test]
    fn names_read_finds_signal_in_interpolation() {
        let v = view0("view V() { Text(\"Count: ${count}\") }");
        let Node::Element(text) = &v.body[0] else {
            panic!("Text");
        };
        let reads = names_read(text.positional.as_ref().unwrap());
        assert!(reads.contains(&"count".to_string()), "reads: {reads:?}");
        // `format` itself is not a read.
        assert!(!reads.contains(&"format".to_string()));
    }

    #[test]
    fn signal_init_resolves_param_default() {
        let v = view0("view C(start: int = 7) {\n  mut count = signal(start)\n}");
        let env = Env::from_view(&v);
        let Node::Let { value: Some(e), .. } = &v.body[0] else {
            panic!("Let");
        };
        assert_eq!(signal_init_int(e, &env), Some(7));
    }

    #[test]
    fn unbound_interpolation_shows_placeholder() {
        let v = view0("view V() { Text(\"Hi, ${name}!\") }");
        let registry = Registry::new();
        let ctx = Ctx::new(&registry);
        let Node::Element(text) = &v.body[0] else {
            panic!("Text");
        };
        let label = render_text_expr(text.positional.as_ref().unwrap(), &ctx);
        assert_eq!(label, "Hi, {name}!");
    }

    /// The full click → recompute pipeline, in pure logic (no live mocida
    /// `Signal`, which needs an initialized App + window — exercised by the
    /// `mui-dev` run, not unit tests). This drives the exact code the runtime
    /// uses: parse the Button's `onClick` action, apply it to a value, then
    /// re-render the Text's `${count}` label against the new value.
    #[test]
    fn click_action_recomputes_label() {
        let v = view0(
            "view C(start: int = 0) {\n  mut count = signal(start)\n  Stack {\n    Text(\"Count: ${count}\")\n    Button(\"+\", onClick: { count = count + 1 })\n  }\n}",
        );
        let env = Env::from_view(&v);

        let Some(Node::Element(stack)) = v.body.iter().find(|n| matches!(n, Node::Element(_)))
        else {
            panic!("Stack");
        };
        let text = stack
            .children
            .iter()
            .find_map(|n| match n {
                Node::Element(e) if e.name == "Text" => Some(e),
                _ => None,
            })
            .expect("Text");
        let button = stack
            .children
            .iter()
            .find_map(|n| match n {
                Node::Element(e) if e.name == "Button" => Some(e),
                _ => None,
            })
            .expect("Button");
        let tmpl = text.positional.as_ref().expect("positional");

        // Helper: render the label for a given count value.
        let render_at = |count: i32| {
            let mut live = env.clone();
            live.vars.insert("count".into(), count.to_string());
            render_text_expr_env(tmpl, &live)
        };

        // The `+` button's action, applied like clicks (pure i32 logic).
        let PropValue::Handler(h) = &button.props.iter().find(|p| p.name == "onClick").unwrap().value
        else {
            panic!("onClick handler");
        };
        let action = h.action().expect("onClick action");
        assert_eq!(action.name(), "count");
        let mut count = 0; // signal(start), start default 0
        assert_eq!(render_at(count), "Count: 0");
        count = action.apply(count);
        count = action.apply(count);
        assert_eq!(count, 2, "two +1 clicks → 2");
        assert_eq!(render_at(count), "Count: 2", "label tracks the value");
    }

    #[test]
    fn conditional_text_renders_chosen_branch() {
        let v = view0("view V() { Text(if true { \"YES\" } else { \"no\" }) }");
        let Node::Element(t) = &v.body[0] else {
            panic!("Text element");
        };
        let pos = t.positional.as_ref().expect("positional");
        let s = render_text_expr_env(pos, &Env::default());
        assert_eq!(s, "YES", "conditional text; positional kind = {:?}", pos.kind);
    }

    #[test]
    fn str_actions_parse_set_copy_and_twoway() {
        // `onClick: { scope = "local"; install_dir = default_loc }`
        let v = view0(
            "view V() { Button(\"x\", onClick: { scope = \"local\"; install_dir = default_loc }) }",
        );
        let Node::Element(b) = &v.body[0] else {
            panic!("Button");
        };
        let PropValue::Handler(h) = &b.props.iter().find(|p| p.name == "onClick").unwrap().value
        else {
            panic!("handler");
        };
        let acts = parse_str_actions(h);
        assert!(
            matches!(acts.first(), Some(StrAction::SetLit { name, value }) if name == "scope" && value == "local")
        );
        assert!(
            matches!(acts.get(1), Some(StrAction::SetFrom { name, from }) if name == "install_dir" && from == "default_loc")
        );

        // `onChange: { |v| source_dir = v }` — the two-way form.
        let v2 = view0("view V() { Input(onChange: { |v| source_dir = v }) }");
        let Node::Element(inp) = &v2.body[0] else {
            panic!("Input");
        };
        let PropValue::Handler(h2) =
            &inp.props.iter().find(|p| p.name == "onChange").unwrap().value
        else {
            panic!("handler");
        };
        assert!(matches!(parse_str_actions(h2).as_slice(), [StrAction::TwoWay { name }] if name == "source_dir"));
    }

    #[test]
    fn bool_expr_eq_resolves_against_env() {
        let mut env = Env::default();
        env.vars.insert("scope".into(), "local".into());
        let cond = |src: &str| {
            let v = view0(&format!("view V() {{ RadioButton(selected: {src}) }}"));
            let Node::Element(r) = &v.body[0] else {
                panic!()
            };
            let PropValue::Expr(e) = &r.props.iter().find(|p| p.name == "selected").unwrap().value
            else {
                panic!()
            };
            eval_bool_expr_env(e, &env)
        };
        assert_eq!(cond("scope == \"local\""), Some(true));
        assert_eq!(cond("scope == \"global\""), Some(false));
    }

    #[test]
    fn component_call_binds_args_to_params() {
        // `Card(title: "Revenue", subtitle: "$12k")` — the call's args bind to
        // the component's params; positional binds to the first param.
        let v = view0("view D() { Card(title: \"Revenue\", subtitle: \"$12k\") }");
        let Node::Element(card) = &v.body[0] else {
            panic!("Card element");
        };
        let outer = Env::default();
        // Named props resolve by name.
        assert_eq!(
            call_arg_string(card, "title", 0, &outer).as_deref(),
            Some("Revenue")
        );
        assert_eq!(
            call_arg_string(card, "subtitle", 1, &outer).as_deref(),
            Some("$12k")
        );

        // Positional binds to the first param when no named prop matches.
        let v2 = view0("view D() { Card(\"Inline\") }");
        let Node::Element(card2) = &v2.body[0] else {
            panic!("Card element");
        };
        assert_eq!(
            call_arg_string(card2, "label", 0, &outer).as_deref(),
            Some("Inline")
        );
        // A param with no matching arg → None (caller falls back to default).
        assert_eq!(call_arg_string(card2, "other", 1, &outer), None);
    }
}
