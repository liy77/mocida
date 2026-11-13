//! `UIApp` wrapper — the top-level application object.

use std::ffi::{c_int, c_void, CString};

use mocida_sys as sys;

use crate::children::Children;
use crate::color::Color;
use crate::error::{Error, Result};
use crate::widget::Widget;

/// Render backend selection (mirrors `UIRenderDriver`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum RenderDriver {
    /// CPU rasterizer.
    Software = 0,
    /// Hardware OpenGL backend.
    OpenGL = 1,
    /// Hardware Vulkan backend.
    Vulkan = 2,
    /// Direct3D 12 (Windows only).
    #[cfg(target_os = "windows")]
    D3d12 = 3,
    /// Direct3D 11 (Windows only).
    #[cfg(target_os = "windows")]
    D3d11 = 4,
    /// Direct3D 9 (Windows only).
    #[cfg(target_os = "windows")]
    D3d9 = 5,
    /// Apple Metal (macOS only).
    #[cfg(target_os = "macos")]
    Metal = 6,
    /// Picks the best available GPU backend.
    Gpu = 7,
}

/// Device-orientation flags for the iOS rotation lock. Combine with
/// bitwise-OR and pass to [`App::set_orientation`]. Default is
/// [`Orientation::ALL`] (every orientation allowed). No-op on desktop.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u32)]
pub enum Orientation {
    /// Portrait, upright.
    Portrait = 1 << 0,
    /// Portrait, upside-down.
    PortraitUpsideDown = 1 << 1,
    /// Landscape, home button on the left.
    LandscapeLeft = 1 << 2,
    /// Landscape, home button on the right.
    LandscapeRight = 1 << 3,
}

impl Orientation {
    /// Both landscape orientations.
    pub const LANDSCAPE: u32 =
        (Orientation::LandscapeLeft as u32) | (Orientation::LandscapeRight as u32);
    /// Every orientation (the default).
    pub const ALL: u32 = (Orientation::Portrait as u32)
        | (Orientation::PortraitUpsideDown as u32)
        | (Orientation::LandscapeLeft as u32)
        | (Orientation::LandscapeRight as u32);
}

/// Analytic-coverage AA quality preset (samples-per-side).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum RenderQuality {
    /// No AA (aliased edges, max performance).
    Low = 1,
    /// 4 SPP — good for small/fast UI.
    Medium = 2,
    /// 16 SPP — default, strong MSAA-equivalent.
    High = 4,
    /// 64 SPP — for close-ups and screenshots.
    Ultra = 8,
}

/// Anti-aliasing pipeline.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum AAMode {
    /// Coverage AA disabled.
    None = 0,
    /// Analytic-coverage AA (default).
    Coverage = 1,
    /// SSAA 2x — renders at 2x then downscales.
    Ssaa2x = 2,
    /// SSAA 4x — renders at 4x then downscales.
    Ssaa4x = 3,
    /// Post-process FXAA edge-blur.
    Fxaa = 4,
    /// Temporal accumulation; great for static UI.
    Taa = 5,
}

/// Top-level application object.
///
/// Owns the main window, the root widget, and global render tuning
/// knobs. One [`App`] is typically enough; for multi-window apps the
/// same settings apply across windows.
pub struct App {
    ptr: *mut sys::UIApp,
    /// Heap-stored resize callback, kept alive for the app's lifetime.
    resize_cb: Option<Box<ResizeState>>,
    /// Heap-stored per-event callbacks, by raw event ID. Forward
    /// compatibility: future events plug in here without API churn.
    event_cbs: Vec<Box<EventState>>,
    /// Children handed off via [`App::set_children`]; we keep no
    /// extra Rust handle, the app owns them.
    has_children: bool,
    /// Heap-stored tray menu-item callbacks, kept alive for the app's life.
    tray_cbs: Vec<Box<TrayState>>,
    /// Heap-stored per-frame tick callback, kept alive for the app's lifetime.
    tick_cb: Option<Box<TickState>>,
}

struct ResizeState {
    handler: Box<dyn FnMut(i32, i32) + 'static>,
}

struct TickState {
    handler: Box<dyn FnMut() + 'static>,
}

struct TrayState {
    handler: Box<dyn FnMut() + 'static>,
}

struct EventState {
    handler: Box<dyn FnMut(crate::event::EventData<'_>) + 'static>,
}

impl App {
    /// Creates the application window with the given title and size.
    pub fn new(title: &str, width: i32, height: i32) -> Result<Self> {
        let title_c = CString::new(title)?;
        let ptr = unsafe { sys::UIApp_Create(title_c.as_ptr(), width, height) };
        if ptr.is_null() {
            return Err(Error::Null("UIApp_Create"));
        }
        Ok(Self {
            ptr,
            resize_cb: None,
            event_cbs: Vec::new(),
            has_children: false,
            tray_cbs: Vec::new(),
            tick_cb: None,
        })
    }

    /// Borrow the raw `UIApp*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UIApp {
        self.ptr
    }

    /// Sets the window's clear color.
    pub fn set_background_color(&mut self, color: Color) -> &mut Self {
        unsafe {
            sys::UIApp_SetBackgroundColor(self.ptr, color.into_raw());
        }
        self
    }

    /// Updates the window title.
    pub fn set_window_title(&mut self, title: &str) -> Result<&mut Self> {
        let c = CString::new(title)?;
        unsafe {
            sys::UIApp_SetWindowTitle(self.ptr, c.as_ptr());
        }
        Ok(self)
    }

    /// Sets the window icon from a file path. Returns `true` on
    /// success (mirrors the C return code).
    pub fn set_window_icon(&mut self, path: &str) -> Result<bool> {
        let c = CString::new(path)?;
        let rc = unsafe { sys::UIApp_SetWindowIcon(self.ptr, c.as_ptr()) };
        Ok(rc != 0)
    }

    /// Sets the window size.
    pub fn set_window_size(&mut self, width: i32, height: i32) -> &mut Self {
        unsafe {
            sys::UIApp_SetWindowSize(self.ptr, width, height);
        }
        self
    }

    /// Sets the minimum window size the user can resize to (desktop only).
    /// Mirrors `UIApp_SetMinSize` / `SDL_SetWindowMinimumSize`.
    pub fn set_min_size(&mut self, width: i32, height: i32) -> &mut Self {
        unsafe {
            sys::UIApp_SetMinSize(self.ptr, width, height);
        }
        self
    }

    /// Sets the maximum window size the user can resize to (desktop only).
    /// Mirrors `UIApp_SetMaxSize` / `SDL_SetWindowMaximumSize`.
    pub fn set_max_size(&mut self, width: i32, height: i32) -> &mut Self {
        unsafe {
            sys::UIApp_SetMaxSize(self.ptr, width, height);
        }
        self
    }

    /// Sets the window position.
    pub fn set_window_position(&mut self, x: i32, y: i32) -> &mut Self {
        unsafe {
            sys::UIApp_SetWindowPosition(self.ptr, x, y);
        }
        self
    }

    /// Switches the render backend (recreates the renderer).
    pub fn set_render_driver(&mut self, driver: RenderDriver) -> &mut Self {
        unsafe {
            sys::UIApp_SetRenderDriver(self.ptr, driver as sys::UIRenderDriver);
        }
        self
    }

    /// Shortcut for `UIApp_SetMSAASamples`.
    pub fn set_render_quality(&mut self, quality: RenderQuality) -> &mut Self {
        unsafe {
            sys::UIApp_SetRenderQuality(self.ptr, sys::UIRenderQuality(quality as _));
        }
        self
    }

    /// Sets the analytic-coverage samples-per-side.
    pub fn set_msaa_samples(&mut self, samples: i32) -> &mut Self {
        unsafe {
            sys::UIApp_SetMSAASamples(self.ptr, samples);
        }
        self
    }

    /// Selects the AA pipeline.
    pub fn set_aa_mode(&mut self, mode: AAMode) -> &mut Self {
        unsafe {
            sys::UIApp_SetAAMode(self.ptr, sys::UIAAMode(mode as _));
        }
        self
    }

    /// Sets the TAA history blend weight in `0.0..=1.0`.
    pub fn set_taa_blend(&mut self, alpha: f32) -> &mut Self {
        unsafe {
            sys::UIApp_SetTAABlend(self.ptr, alpha);
        }
        self
    }

    /// Sets the target frame rate. Pass 0 (or anything <= 0) to
    /// unlock.
    pub fn set_target_fps(&mut self, fps: i32) -> &mut Self {
        unsafe {
            sys::UIApp_SetTargetFPS(self.ptr, fps);
        }
        self
    }

    /// Currently configured target FPS (0 = unlimited).
    pub fn target_fps(&self) -> i32 {
        unsafe { sys::UIApp_GetTargetFPS(self.ptr) }
    }

    /// Releases cached renderer resources.
    pub fn trim_caches(&mut self) -> &mut Self {
        unsafe { sys::UIApp_TrimCaches(self.ptr) };
        self
    }

    /// Current window width in logical points (the real device screen width
    /// on iOS, not the requested size).
    pub fn width(&self) -> i32 {
        unsafe { sys::UIApp_GetWidth(self.ptr) }
    }

    /// Current window height in logical points.
    pub fn height(&self) -> i32 {
        unsafe { sys::UIApp_GetHeight(self.ptr) }
    }

    /// Sets the app display name: updates both the bundle name (shown as the
    /// app name / on iOS) and the desktop window title.
    pub fn set_name(&mut self, name: &str) -> Result<&mut Self> {
        let c = CString::new(name)?;
        unsafe { sys::UIApp_SetName(self.ptr, c.as_ptr()) };
        Ok(self)
    }

    /// Keeps the run loop alive while the window is hidden (e.g. minimized
    /// to the tray) instead of exiting. Events keep pumping; rendering is
    /// skipped while hidden. Desktop feature.
    pub fn set_run_in_background(&mut self, enabled: bool) -> &mut Self {
        unsafe { sys::UIApp_SetRunInBackground(self.ptr, enabled as c_int) };
        self
    }

    /// Installs a desktop system-tray icon from an image path (a `mocida://`
    /// URI works). Returns `true` on success; no-op (`false`) on iOS.
    pub fn set_tray_icon(&mut self, icon_path: &str, tooltip: Option<&str>) -> Result<bool> {
        let icon = CString::new(icon_path)?;
        let tip = match tooltip {
            Some(t) => Some(CString::new(t)?),
            None => None,
        };
        let tip_ptr = tip.as_ref().map_or(std::ptr::null(), |c| c.as_ptr());
        let rc = unsafe { sys::UIApp_SetTrayIcon(self.ptr, icon.as_ptr(), tip_ptr) };
        Ok(rc != 0)
    }

    /// Appends a clickable item to the tray menu; `handler` fires on click.
    /// Requires [`App::set_tray_icon`] first. No-op on iOS.
    pub fn add_tray_menu_item<F>(&mut self, label: &str, handler: F) -> Result<&mut Self>
    where
        F: FnMut() + 'static,
    {
        let c = CString::new(label)?;
        let state = Box::new(TrayState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const TrayState as *mut c_void;
        unsafe {
            sys::UIApp_AddTrayMenuItem(self.ptr, c.as_ptr(), Some(tray_trampoline), userdata);
        }
        self.tray_cbs.push(state);
        Ok(self)
    }

    /// Declares this process's AppUserModelID (Windows taskbar / Task
    /// Manager grouping). No-op on non-Windows.
    pub fn set_app_id(&mut self, aumid: &str) -> Result<&mut Self> {
        let c = CString::new(aumid)?;
        unsafe { sys::UIApp_SetAppId(self.ptr, c.as_ptr()) };
        Ok(self)
    }

    /// Restricts the device orientations the app may rotate into. Pass a
    /// bitmask of [`Orientation`] flags, e.g.:
    /// - `Orientation::Portrait as u32` — portrait only
    /// - `Orientation::LANDSCAPE` — landscape only
    /// - `Orientation::ALL` — all (default)
    ///
    /// Honored live on iOS; no-op on desktop.
    pub fn set_orientation(&mut self, orientations: u32) -> &mut Self {
        unsafe { sys::UIApp_SetOrientation(self.ptr, orientations) };
        self
    }

    /// Returns the currently configured orientation bitmask, or
    /// [`Orientation::ALL`] if never set.
    pub fn orientation(&self) -> u32 {
        unsafe { sys::UIApp_GetOrientation(self.ptr) }
    }

    /// Hands the root widget tree to the app. Subsequent calls
    /// replace it (the previous tree is destroyed by mocida).
    pub fn set_children(&mut self, children: Children) -> &mut Self {
        let raw = children.into_raw();
        unsafe {
            sys::UIApp_SetChildren(self.ptr, raw);
        }
        self.has_children = true;
        self
    }

    /// Convenience: lazily creates a [`Children`] collection if the
    /// app doesn't have one yet and appends the widget. Equivalent to
    /// the common `UIChildren_Add(children, widget)` pattern from the
    /// C demos.
    ///
    /// Note: each call rebuilds the children collection from scratch,
    /// so prefer batching with [`App::set_children`] when adding many
    /// widgets at once.
    pub fn add_child(&mut self, widget: Widget) -> Result<&mut Self> {
        // For now we just delegate; the multi-call ergonomic is a
        // future addition. The user can build a Children themselves
        // and call set_children when batching matters.
        let mut c = Children::new(8)?;
        c.add(widget)?;
        self.set_children(c);
        Ok(self)
    }

    /// Registers a resize handler. The closure receives the new
    /// `(width, height)`. Replaces any previous resize handler.
    pub fn on_resize<F>(&mut self, handler: F) -> &mut Self
    where
        F: FnMut(i32, i32) + 'static,
    {
        let state = Box::new(ResizeState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const ResizeState as *mut c_void;
        unsafe {
            sys::UIApp_OnResize(self.ptr, Some(resize_trampoline), userdata);
        }
        self.resize_cb = Some(state);
        self
    }

    /// Registers a per-frame tick handler: it runs once at the top of every
    /// main-loop iteration on the UI thread (before events / render). Keep it
    /// light — it fires every frame. Intended for cheap per-frame work like
    /// polling a hot-reload flag and swapping the tree via [`App::set_children`].
    /// Replaces any previous tick handler.
    pub fn on_tick<F>(&mut self, handler: F) -> &mut Self
    where
        F: FnMut() + 'static,
    {
        let state = Box::new(TickState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const TickState as *mut c_void;
        unsafe {
            sys::UIApp_OnTick(self.ptr, Some(tick_trampoline), userdata);
        }
        self.tick_cb = Some(state);
        self
    }

    /// Registers a callback for a mocida event.
    pub fn on_event<F>(&mut self, event: crate::event::Event, handler: F) -> &mut Self
    where
        F: FnMut(crate::event::EventData<'_>) + 'static,
    {
        let state = Box::new(EventState {
            handler: Box::new(handler),
        });
        // The C signature is `void(*)(UIEventData)` — no userdata
        // pointer. To bridge the userdata gap we install per-event
        // trampolines that look up the matching state via a TLS slot.
        // Today only one event exists, so we keep one slot.
        EVENT_TRAMPOLINE_STATE.with(|cell| {
            *cell.borrow_mut() = Some(Box::as_ref(&state) as *const EventState);
        });
        unsafe {
            sys::UIApp_SetEventCallback(self.ptr, event.as_raw(), Some(event_trampoline));
        }
        self.event_cbs.push(state);
        self
    }

    /// Shows the application window.
    pub fn show(&mut self) -> &mut Self {
        unsafe {
            sys::UIApp_ShowWindow(self.ptr);
        }
        self
    }

    /// Hides the application window.
    pub fn hide(&mut self) -> &mut Self {
        unsafe {
            sys::UIApp_HideWindow(self.ptr);
        }
        self
    }

    /// Controls iOS status bar (clock / battery / signal) visibility. The
    /// status bar is visible by default (`hidden = false`). No-op on desktop.
    pub fn set_status_bar_hidden(&mut self, hidden: bool) -> &mut Self {
        unsafe {
            sys::UIApp_SetStatusBarHidden(self.ptr, hidden as c_int);
        }
        self
    }

    /// Controls whether individual OS title-bar chrome buttons are visible.
    ///
    /// - **macOS**: hides/shows the traffic-light buttons (close / minimize /
    ///   zoom). Hidden buttons leave a gap in the title bar, exactly like
    ///   apps such as Preview in presentation mode.
    /// - **Windows**: removes/restores `WS_MINIMIZEBOX` / `WS_MAXIMIZEBOX`
    ///   style bits and grays/enables the close entry in the system menu.
    ///   Full visual removal of the close ×-button is not possible on Windows
    ///   without a custom title bar — this method grays it out instead.
    /// - Other platforms: no-op.
    pub fn set_window_buttons(
        &mut self,
        close: bool,
        minimize: bool,
        maximize: bool,
    ) -> &mut Self {
        let win_ptr = unsafe { mocida_sys::UIApp_GetWindow(self.ptr) } as *mut mocida_sys::UIWindow;
        let sdl_win = if win_ptr.is_null() { std::ptr::null_mut() } else { unsafe { (*win_ptr).sdlWindow } };
        if !sdl_win.is_null() {
            unsafe { platform::set_window_buttons(sdl_win, close, minimize, maximize) };
        }
        self
    }

    /// Enables (or disables) a custom client-side title bar on the
    /// already-created window: drops the native chrome and installs the
    /// drag/resize hit-test. The window must have been created borderless via
    /// [`request_custom_titlebar`] for the bar to be fully frameless.
    pub fn set_custom_titlebar(&mut self, on: bool) -> &mut Self {
        unsafe { sys::UIApp_SetCustomTitlebar(on as c_int) };
        self
    }

    /// Updates the title-bar drag region (window-logical coords). Call every
    /// frame with the live bounds of the widget acting as the title bar. Empty
    /// areas of the region drag the window; interactive widgets under it still
    /// receive clicks. A zero/negative size clears the region.
    pub fn set_drag_region(&mut self, x: f32, y: f32, w: f32, h: f32) -> &mut Self {
        unsafe { sys::UIApp_SetDragRegion(x, y, w, h) };
        self
    }

    /// Runs the main loop until the window closes. Blocks.
    pub fn run(&mut self) {
        unsafe {
            sys::UIApp_Run(self.ptr);
        }
    }
}

/// Requests client-side window decorations (a custom title bar). Must be called
/// **before** [`App::new`] — it controls the borderless window flag at creation
/// time. The app then paints its own bar and feeds the live drag region via
/// [`App::set_drag_region`]. Pass `false` for the default native chrome.
pub fn request_custom_titlebar(on: bool) {
    unsafe { sys::UIWindow_RequestCustomTitlebar(on as c_int) };
}

/// Height of the app's painted top bar: on macOS the native traffic lights
/// are centered vertically in it. Call before [`App::new`]; 0 leaves them
/// where AppKit puts them.
pub fn set_titlebar_height(height: i32) {
    unsafe { sys::UIWindow_SetTitlebarHeight(height as c_int) };
}

/// Requests a transparent (per-pixel alpha) window. Must be called **before**
/// [`App::new`] — it adds the SDL transparent-window flag at creation time so an
/// OS system backdrop (Mica/Acrylic, via [`Window::set_backdrop`]) can composite
/// through the app's transparent pixels. Only the D3D11 renderer honors this on
/// Windows; the Vulkan/GL renderers create an opaque swapchain. The window still
/// clears opaque by default, so it looks identical until a backdrop is enabled.
pub fn request_transparent(on: bool) {
    unsafe { sys::UIWindow_RequestTransparent(on as c_int) };
}

/// A text-editing command (the Edit menu set).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EditCommand {
    /// Undo the last edit.
    Undo,
    /// Redo the last undone edit.
    Redo,
    /// Cut the selection to the clipboard.
    Cut,
    /// Copy the selection to the clipboard.
    Copy,
    /// Paste the clipboard at the caret.
    Paste,
    /// Select all text.
    SelectAll,
}

/// Run `cmd` on whichever TextField / TextArea has focus in the current app,
/// exactly as its keyboard shortcut would. For native menus: on macOS the menu
/// bar consumes ⌘C/⌘V/…, so Edit ▸ Copy has to drive the widget explicitly.
pub fn send_edit_command(cmd: EditCommand) {
    // SDL scancodes: A=4, C=6, V=25, X=27, Z=29.
    let (scancode, shift) = match cmd {
        EditCommand::Undo => (29, 0),
        EditCommand::Redo => (29, 1),
        EditCommand::Cut => (27, 0),
        EditCommand::Copy => (6, 0),
        EditCommand::Paste => (25, 0),
        EditCommand::SelectAll => (4, 0),
    };
    unsafe { sys::UIApp_SendEditKeyG(scancode, shift) };
}

/// `true` while a visible text field / text area has keyboard focus — route
/// Edit ▸ Undo to the text then, and to the host's own history otherwise.
pub fn text_has_focus() -> bool {
    unsafe { sys::UIApp_TextHasFocusG() != 0 }
}

/// Window-space box `(x, y, w, h)` of the first visible widget matching an
/// automation selector (`tip:"Add node"`, `"Run"`, `~"Assets"`), or `None`.
pub fn find_widget_bounds(selector: &str) -> Option<(f32, f32, f32, f32)> {
    let c = std::ffi::CString::new(selector).ok()?;
    let (mut x, mut y, mut w, mut h) = (0.0f32, 0.0f32, 0.0f32, 0.0f32);
    let found = unsafe { sys::UIApp_FindWidgetBoundsG(c.as_ptr(), &mut x, &mut y, &mut w, &mut h) };
    (found != 0).then_some((x, y, w, h))
}

/// Minimizes the current app's window to the taskbar.
pub fn window_minimize() {
    unsafe { sys::UIApp_MinimizeG() };
}

/// Toggles the current window between maximized and restored (respects the
/// desktop work area, so it won't cover the taskbar).
pub fn window_toggle_maximize() {
    unsafe { sys::UIApp_ToggleMaximizeG() };
}

/// Returns `true` if the current window is maximized.
pub fn window_is_maximized() -> bool {
    unsafe { sys::UIApp_IsMaximizedG() != 0 }
}

/// Requests the app to quit (clean main-loop teardown). Used by the custom
/// title bar's close button.
pub fn window_close() {
    unsafe { sys::UIApp_CloseG() };
}

impl Drop for App {
    fn drop(&mut self) {
        // Clear the TLS slot so a future App in the same thread
        // doesn't dispatch into a freed callback.
        EVENT_TRAMPOLINE_STATE.with(|cell| {
            *cell.borrow_mut() = None;
        });
        // The closures (and whatever they captured: sounds, images, widget
        // handles) go before UIApp_Destroy, whose SDL_Quit frees the SDL
        // objects those captures still point at.
        if !self.ptr.is_null() {
            unsafe {
                sys::UIApp_OnTick(self.ptr, None, std::ptr::null_mut());
                sys::UIApp_OnResize(self.ptr, None, std::ptr::null_mut());
            }
        }
        self.tick_cb = None;
        self.resize_cb = None;
        self.event_cbs.clear();
        if !self.ptr.is_null() {
            unsafe {
                sys::UIApp_Destroy(self.ptr);
            }
        }
    }
}

extern "C" fn tray_trampoline(userdata: *mut c_void) {
    if userdata.is_null() {
        return;
    }
    // Safety: `userdata` points at a Box<TrayState> payload kept alive on
    // the owning App's `tray_cbs`.
    let state = unsafe { &mut *(userdata as *mut TrayState) };
    (state.handler)();
}

extern "C" fn resize_trampoline(width: c_int, height: c_int, userdata: *mut c_void) {
    if userdata.is_null() {
        return;
    }
    // Safety: `userdata` is the address of `Box<ResizeState>`'s heap
    // payload, kept alive on the owning App.
    let state = unsafe { &mut *(userdata as *mut ResizeState) };
    (state.handler)(width, height);
}

extern "C" fn tick_trampoline(userdata: *mut c_void) {
    if userdata.is_null() {
        return;
    }
    // Safety: `userdata` is the address of `Box<TickState>`'s heap payload,
    // kept alive on the owning App for its whole lifetime.
    let state = unsafe { &mut *(userdata as *mut TickState) };
    (state.handler)();
}

thread_local! {
    static EVENT_TRAMPOLINE_STATE: std::cell::RefCell<Option<*const EventState>> =
        const { std::cell::RefCell::new(None) };
}

extern "C" fn event_trampoline(data: sys::UIEventData) {
    let ptr_opt = EVENT_TRAMPOLINE_STATE.with(|cell| *cell.borrow());
    let Some(ptr) = ptr_opt else { return };
    // Safety: the App that registered the callback is still alive
    // (we cleared this slot in Drop), so the state pointer is valid.
    let state = unsafe { &mut *(ptr as *mut EventState) };
    let view = crate::event::EventData::from_raw(&data);
    (state.handler)(view);
}

// ── Platform-specific window-button visibility ─────────────────────────────

#[cfg(target_os = "macos")]
mod platform {
    use std::ffi::{c_char, c_void};

    // SDL3 is already linked transitively via mocida; declare only what we need.
    extern "C" {
        fn SDL_GetWindowProperties(window: *mut crate::sys::SDL_Window) -> u32;
        fn SDL_GetPointerProperty(
            props: u32,
            name: *const c_char,
            default_value: *mut c_void,
        ) -> *mut c_void;
    }

    // libobjc is a macOS system library; always available.
    #[link(name = "objc")]
    extern "C" {
        fn sel_registerName(name: *const c_char) -> *mut c_void;
        // Declared without variadic args; cast to the right signature via transmute.
        fn objc_msgSend(obj: *mut c_void, sel: *mut c_void) -> *mut c_void;
    }

    pub(super) unsafe fn set_window_buttons(
        sdl_win: *mut crate::sys::SDL_Window,
        close: bool,
        minimize: bool,
        maximize: bool,
    ) {
        let props = SDL_GetWindowProperties(sdl_win);
        if props == 0 {
            return;
        }
        // SDL_PROP_WINDOW_COCOA_WINDOW_POINTER
        let ns_win = SDL_GetPointerProperty(
            props,
            b"SDL.window.cocoa.window\0".as_ptr() as *const c_char,
            std::ptr::null_mut(),
        );
        if ns_win.is_null() {
            return;
        }

        let sel_btn = sel_registerName(b"standardWindowButton:\0".as_ptr() as *const c_char);
        let sel_hidden = sel_registerName(b"setHidden:\0".as_ptr() as *const c_char);

        // [NSWindow standardWindowButton:idx] → NSButton*
        type GetBtnFn = unsafe extern "C" fn(*mut c_void, *mut c_void, u64) -> *mut c_void;
        // [NSButton setHidden:BOOL] — returns nil (ObjC void methods return nil on arm64/x86_64)
        type SetHiddenFn = unsafe extern "C" fn(*mut c_void, *mut c_void, u8) -> *mut c_void;

        // transmute between fn-pointer types of the same size: valid on all
        // platforms because `objc_msgSend` is variadic at the ABI level.
        let base: unsafe extern "C" fn(*mut c_void, *mut c_void) -> *mut c_void = objc_msgSend;
        let get_btn: GetBtnFn = std::mem::transmute(base);
        let set_hidden: SetHiddenFn = std::mem::transmute(base);

        // NSWindowButton: Close=0, Miniaturize=1, Zoom=2
        for (btn_idx, visible) in [(0u64, close), (1, minimize), (2, maximize)] {
            let btn = get_btn(ns_win, sel_btn, btn_idx);
            if !btn.is_null() {
                let _ = set_hidden(btn, sel_hidden, u8::from(!visible));
            }
        }
    }
}

#[cfg(target_os = "windows")]
mod platform {
    use std::ffi::{c_char, c_void};

    extern "C" {
        fn SDL_GetWindowProperties(window: *mut crate::sys::SDL_Window) -> u32;
        fn SDL_GetPointerProperty(
            props: u32,
            name: *const c_char,
            default_value: *mut c_void,
        ) -> *mut c_void;
    }

    extern "system" {
        fn GetWindowLongPtrW(hwnd: *mut c_void, n_index: i32) -> isize;
        fn SetWindowLongPtrW(hwnd: *mut c_void, n_index: i32, dw_new_long: isize) -> isize;
        fn SetWindowPos(
            hwnd: *mut c_void,
            hwnd_insert_after: *mut c_void,
            x: i32,
            y: i32,
            cx: i32,
            cy: i32,
            u_flags: u32,
        ) -> i32;
        fn GetSystemMenu(hwnd: *mut c_void, b_revert: i32) -> *mut c_void;
        fn EnableMenuItem(h_menu: *mut c_void, u_id_enable_item: u32, u_enable: u32) -> i32;
    }

    const GWL_STYLE: i32 = -16;
    const WS_MINIMIZEBOX: isize = 0x0002_0000;
    const WS_MAXIMIZEBOX: isize = 0x0001_0000;
    const SWP_NOMOVE: u32 = 0x0002;
    const SWP_NOSIZE: u32 = 0x0001;
    const SWP_NOZORDER: u32 = 0x0004;
    const SWP_FRAMECHANGED: u32 = 0x0020;
    const SC_CLOSE: u32 = 0xF060;
    const MF_BYCOMMAND: u32 = 0x0000;
    const MF_GRAYED: u32 = 0x0001;
    const MF_ENABLED: u32 = 0x0000;

    pub(super) unsafe fn set_window_buttons(
        sdl_win: *mut crate::sys::SDL_Window,
        close: bool,
        minimize: bool,
        maximize: bool,
    ) {
        let props = SDL_GetWindowProperties(sdl_win);
        if props == 0 {
            return;
        }
        // SDL_PROP_WINDOW_WIN32_HWND_POINTER
        let hwnd = SDL_GetPointerProperty(
            props,
            b"SDL.window.win32.hwnd\0".as_ptr() as *const c_char,
            std::ptr::null_mut(),
        );
        if hwnd.is_null() {
            return;
        }

        // Minimize / maximize: Win32 style bits
        let mut style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        if minimize {
            style |= WS_MINIMIZEBOX;
        } else {
            style &= !WS_MINIMIZEBOX;
        }
        if maximize {
            style |= WS_MAXIMIZEBOX;
        } else {
            style &= !WS_MAXIMIZEBOX;
        }
        SetWindowLongPtrW(hwnd, GWL_STYLE, style);

        // Close button: Windows can't hide the ×; gray/enable it via system menu.
        let menu = GetSystemMenu(hwnd, 0);
        if !menu.is_null() {
            EnableMenuItem(
                menu,
                SC_CLOSE,
                MF_BYCOMMAND | if close { MF_ENABLED } else { MF_GRAYED },
            );
        }

        // Repaint the non-client frame immediately.
        SetWindowPos(
            hwnd,
            std::ptr::null_mut(),
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED,
        );
    }
}

#[cfg(target_os = "linux")]
mod platform {
    use std::ffi::{c_char, c_int, c_long, c_ulong, c_void};

    extern "C" {
        fn SDL_GetWindowProperties(window: *mut crate::sys::SDL_Window) -> u32;
        fn SDL_GetPointerProperty(
            props: u32,
            name: *const c_char,
            default_value: *mut c_void,
        ) -> *mut c_void;
        fn SDL_GetNumberProperty(props: u32, name: *const c_char, default_value: i64) -> i64;
    }

    // dlsym resolves in already-loaded libraries when handle = RTLD_DEFAULT (null).
    // libdl is linked explicitly for pre-glibc-2.34 systems; on newer glibc it's a no-op.
    #[link(name = "dl")]
    extern "C" {
        fn dlsym(handle: *mut c_void, symbol: *const c_char) -> *mut c_void;
    }

    // Motif window manager hints — honored by virtually all X11 WMs and XWayland.
    #[repr(C)]
    struct MotifWmHints {
        flags: c_ulong,
        functions: c_ulong,
        decorations: c_ulong,
        input_mode: c_long,
        status: c_ulong,
    }

    const MWM_HINTS_FUNCTIONS: c_ulong   = 1 << 0;
    const MWM_HINTS_DECORATIONS: c_ulong = 1 << 1;
    const MWM_FUNC_MOVE: c_ulong         = 1 << 2;
    const MWM_FUNC_MINIMIZE: c_ulong     = 1 << 3;
    const MWM_FUNC_MAXIMIZE: c_ulong     = 1 << 4;
    const MWM_FUNC_CLOSE: c_ulong        = 1 << 5;
    const MWM_DECOR_BORDER: c_ulong      = 1 << 1;
    const MWM_DECOR_RESIZEH: c_ulong     = 1 << 2;
    const MWM_DECOR_TITLE: c_ulong       = 1 << 3;
    const MWM_DECOR_MENU: c_ulong        = 1 << 4;
    const MWM_DECOR_MINIMIZE: c_ulong    = 1 << 5;
    const MWM_DECOR_MAXIMIZE: c_ulong    = 1 << 6;
    const PROP_MODE_REPLACE: c_int       = 0;

    type XInternAtomFn =
        unsafe extern "C" fn(*mut c_void, *const c_char, c_int) -> c_ulong;
    type XChangePropertyFn = unsafe extern "C" fn(
        *mut c_void,
        c_ulong,
        c_ulong,
        c_ulong,
        c_int,
        c_int,
        *const u8,
        c_int,
    ) -> c_int;
    type XFlushFn = unsafe extern "C" fn(*mut c_void) -> c_int;

    pub(super) unsafe fn set_window_buttons(
        sdl_win: *mut crate::sys::SDL_Window,
        close: bool,
        minimize: bool,
        maximize: bool,
    ) {
        let props = SDL_GetWindowProperties(sdl_win);
        if props == 0 {
            return;
        }

        // X11 Display* — null on native Wayland (safe early exit, pure no-op).
        let display = SDL_GetPointerProperty(
            props,
            b"SDL.window.x11.display\0".as_ptr() as *const c_char,
            std::ptr::null_mut(),
        );
        if display.is_null() {
            return;
        }

        let xwin = SDL_GetNumberProperty(
            props,
            b"SDL.window.x11.window\0".as_ptr() as *const c_char,
            0,
        ) as c_ulong;
        if xwin == 0 {
            return;
        }

        // SDL3's X11 backend already loaded libX11 into the process; locate
        // the symbols via RTLD_DEFAULT (null handle) without a hard link dep.
        let intern_sym =
            dlsym(std::ptr::null_mut(), b"XInternAtom\0".as_ptr() as *const c_char);
        let change_sym =
            dlsym(std::ptr::null_mut(), b"XChangeProperty\0".as_ptr() as *const c_char);
        let flush_sym =
            dlsym(std::ptr::null_mut(), b"XFlush\0".as_ptr() as *const c_char);
        if intern_sym.is_null() || change_sym.is_null() || flush_sym.is_null() {
            return;
        }

        let x_intern: XInternAtomFn = std::mem::transmute(intern_sym);
        let x_change: XChangePropertyFn = std::mem::transmute(change_sym);
        let x_flush: XFlushFn = std::mem::transmute(flush_sym);

        let motif_atom = x_intern(
            display,
            b"_MOTIF_WM_HINTS\0".as_ptr() as *const c_char,
            0, // False — create if absent
        );
        if motif_atom == 0 {
            return;
        }

        // Build allowed-functions mask (WM uses this for the menu and button state).
        let mut functions: c_ulong = MWM_FUNC_MOVE;
        if close {
            functions |= MWM_FUNC_CLOSE;
        }
        if minimize {
            functions |= MWM_FUNC_MINIMIZE;
        }
        if maximize {
            functions |= MWM_FUNC_MAXIMIZE;
        }

        // Build decoration mask (controls which buttons are painted in the title bar).
        let mut decorations: c_ulong =
            MWM_DECOR_BORDER | MWM_DECOR_RESIZEH | MWM_DECOR_TITLE | MWM_DECOR_MENU;
        if minimize {
            decorations |= MWM_DECOR_MINIMIZE;
        }
        if maximize {
            decorations |= MWM_DECOR_MAXIMIZE;
        }

        let hints = MotifWmHints {
            flags: MWM_HINTS_FUNCTIONS | MWM_HINTS_DECORATIONS,
            functions,
            decorations,
            input_mode: 0,
            status: 0,
        };

        // XChangeProperty with format=32 reads `nelements` c_ulong-sized values.
        x_change(
            display,
            xwin,
            motif_atom,
            motif_atom, // type = _MOTIF_WM_HINTS (convention)
            32,
            PROP_MODE_REPLACE,
            std::ptr::addr_of!(hints) as *const u8,
            5, // MotifWmHints has 5 fields
        );
        x_flush(display);
    }
}

#[cfg(not(any(target_os = "macos", target_os = "windows", target_os = "linux")))]
mod platform {
    pub(super) unsafe fn set_window_buttons(
        _sdl_win: *mut crate::sys::SDL_Window,
        _close: bool,
        _minimize: bool,
        _maximize: bool,
    ) {
    }
}
