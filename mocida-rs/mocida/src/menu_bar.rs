//! High-level builder for the application menu bar.
//!
//! Wraps `mocida_sys::menu_bar` (the FFI bindings for
//! `uikit/menu_bar.h`) with a chained-builder API. The host builds the
//! tree once at startup and calls `Menu::set_as_app_menu_bar` to
//! install it as `[NSApp mainMenu]` on Cocoa (no-op on non-Apple).
//!
//! # Example
//!
//! ```no_run
//! use mocida::menu_bar::{Menu, MenuItemId};
//!
//! let bar = Menu::new("OndaEngine")
//!     .submenu(
//!         Menu::new("File")
//!             .item("New Project", Some("CmdOrCtrl+N"), MenuItemId::NewProject)
//!             .item("Open Project\u{2026}", Some("CmdOrCtrl+O"), MenuItemId::OpenProject)
//!             .separator()
//!             .item("Save", Some("CmdOrCtrl+S"), MenuItemId::Save)
//!     )
//!     .submenu(
//!         Menu::new("Edit")
//!             .item("Undo", Some("CmdOrCtrl+Z"), MenuItemId::Undo)
//!             .item("Redo", Some("CmdOrCtrl+Shift+Z"), MenuItemId::Redo)
//!             .separator()
//!             .item("Cut", Some("CmdOrCtrl+X"), MenuItemId::Cut)
//!             .item("Copy", Some("CmdOrCtrl+C"), MenuItemId::Copy)
//!             .item("Paste", Some("CmdOrCtrl+V"), MenuItemId::Paste)
//!             .item("Select All", Some("CmdOrCtrl+A"), MenuItemId::SelectAll)
//!     );
//!
//! bar.set_as_app_menu_bar();
//! ```
//!
//! The host then registers a callback (via
//! `mocida_sys::menu_bar::set_item_callback`) that receives the
//! `MenuItemId` as a `u32` and routes it to whatever action makes
//! sense (e.g. setting a signal in the .mui runtime).

use mocida_sys::menu_bar::{Item as RawItem, Menu as RawMenu};

/// Stock-action item ids (`UI_MENU_BAR_ROLE_*`). An item created with one of
/// these runs the platform's standard action (macOS: the AppKit selector —
/// Hide, Quit, Minimize, Full Screen…) instead of reaching the item callback.
/// Host ids must stay below [`role::FIRST`].
pub mod role {
    /// First reserved id.
    pub const FIRST: u32 = 0xFFFF_0000;
    /// About panel.
    pub const ABOUT: u32 = 0xFFFF_0001;
    /// Hide the app (⌘H).
    pub const HIDE: u32 = 0xFFFF_0002;
    /// Hide other apps (⌥⌘H).
    pub const HIDE_OTHERS: u32 = 0xFFFF_0003;
    /// Show all apps.
    pub const SHOW_ALL: u32 = 0xFFFF_0004;
    /// Quit (⌘Q).
    pub const QUIT: u32 = 0xFFFF_0005;
    /// Minimize the key window (⌘M).
    pub const MINIMIZE: u32 = 0xFFFF_0006;
    /// Zoom the key window.
    pub const ZOOM: u32 = 0xFFFF_0007;
    /// Toggle full screen (⌃⌘F).
    pub const FULLSCREEN: u32 = 0xFFFF_0008;
    /// Bring all windows to front.
    pub const FRONT: u32 = 0xFFFF_0009;
}

/// Identifier for a menu bar item. Each `MenuItemId` corresponds to a
/// `u32` the host receives in the item-activated callback. The host
/// is free to use any `u32`; this enum is provided for convenience
/// (start at `1` to leave `0` as a "null" sentinel).
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq, Hash)]
pub enum MenuItemId {
    // App menu
    /// App menu ▸ About.
    About = 1,
    /// App menu ▸ Settings.
    Preferences = 2,
    /// App menu ▸ Quit.
    Quit = 3,

    // File
    /// File ▸ New.
    NewProject = 100,
    /// File ▸ Open.
    OpenProject = 101,
    /// File ▸ Save.
    Save = 102,
    /// File ▸ Save As.
    SaveAs = 103,
    /// File ▸ Close.
    CloseWindow = 104,

    // Edit
    /// Edit ▸ Undo.
    Undo = 200,
    /// Edit ▸ Redo.
    Redo = 201,
    /// Edit ▸ Cut.
    Cut = 202,
    /// Edit ▸ Copy.
    Copy = 203,
    /// Edit ▸ Paste.
    Paste = 204,
    /// Edit ▸ Select All.
    SelectAll = 205,

    // View
    /// View ▸ Files panel.
    ToggleFiles = 300,
    /// View ▸ Inspector.
    ToggleInspector = 301,
    /// View ▸ Word wrap.
    ToggleWordWrap = 302,
    /// View ▸ Full screen.
    ToggleFullscreen = 303,

    // Window
    /// Window ▸ Minimize.
    Minimize = 400,
    /// Window ▸ Zoom.
    Zoom = 401,
}

impl From<MenuItemId> for u32 {
    fn from(id: MenuItemId) -> u32 {
        id as u32
    }
}

/// One entry in a menu: either a leaf item (with optional shortcut),
/// a submenu, or a separator.
enum Entry {
    Item {
        label: String,
        shortcut: Option<String>,
        id: u32,
    },
    Submenu(Menu),
    Separator,
}

/// Builder for a (sub)menu. `title` is the visible name (e.g. "File");
/// for the root menu, use the app's display name (e.g. "OndaEngine").
pub struct Menu {
    title: String,
    entries: Vec<Entry>,
}

impl Menu {
    /// Create a new menu. Call `.set_as_app_menu_bar()` on the root to
    /// install it; submenus are attached via `.submenu(...)`.
    pub fn new(title: impl Into<String>) -> Self {
        Self {
            title: title.into(),
            entries: Vec::new(),
        }
    }

    /// Append a leaf menu item. `shortcut` is SDL-style
    /// ("CmdOrCtrl+N", "Alt+Shift+Z") or `None` for no key equivalent.
    pub fn item<I: Into<u32>>(mut self, label: &str, shortcut: Option<&str>, id: I) -> Self {
        self.entries.push(Entry::Item {
            label: label.into(),
            shortcut: shortcut.map(String::from),
            id: id.into(),
        });
        self
    }

    /// Append a submenu.
    pub fn submenu(mut self, sub: Menu) -> Self {
        self.entries.push(Entry::Submenu(sub));
        self
    }

    /// Append a separator line.
    pub fn separator(mut self) -> Self {
        self.entries.push(Entry::Separator);
        self
    }

    /// Install this menu as the application's main menu bar. Consumes
    /// the builder. On macOS this calls `[NSApp setMainMenu:]`; on
    /// other platforms it's a no-op (the .c tracks the tree but
    /// nothing is displayed).
    pub fn set_as_app_menu_bar(self) {
        self.into_raw().set_as_app_menu_bar()
    }

    /// Convert the builder tree into the FFI representation. Used
    /// internally and by tests; not usually called directly.
    pub fn into_raw(self) -> RawMenu {
        let title_c = std::ffi::CString::new(self.title.as_str()).expect("menu title has no NUL");
        let mut raw =
            RawMenu::from_cstr(title_c.as_ptr()).expect("ui_menu_bar_create returned NULL (OOM)");
        for entry in self.entries {
            match entry {
                Entry::Item {
                    label,
                    shortcut,
                    id,
                } => {
                    let item = RawItem::new(&label, shortcut.as_deref(), id);
                    raw.append_item(item);
                }
                Entry::Submenu(sub) => {
                    let sub_raw = sub.into_raw();
                    raw.append_submenu(sub_raw);
                }
                Entry::Separator => {
                    raw.append_separator();
                }
            }
        }
        raw
    }
}

// ── Re-export the FFI `from_raw` (private in mocida-sys) so the
//    high-level builder can wrap pointers returned by the C side ────────
mod raw_helpers {
    use mocida_sys::menu_bar::{ui_menu_bar_create, Menu as RawMenu};
    use std::os::raw::c_char;
    pub trait RawMenuFromCStr {
        fn from_cstr(title_with_nul: *const c_char) -> Option<RawMenu>;
    }
    impl RawMenuFromCStr for RawMenu {
        fn from_cstr(title_with_nul: *const c_char) -> Option<RawMenu> {
            let ptr = unsafe { ui_menu_bar_create(title_with_nul) };
            if ptr.is_null() {
                None
            } else {
                RawMenu::from_raw(ptr)
            }
        }
    }
}
use raw_helpers::RawMenuFromCStr;
