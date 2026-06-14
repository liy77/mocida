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

/// Identifier for a menu bar item. Each `MenuItemId` corresponds to a
/// `u32` the host receives in the item-activated callback. The host
/// is free to use any `u32`; this enum is provided for convenience
/// (start at `1` to leave `0` as a "null" sentinel).
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq, Hash)]
pub enum MenuItemId {
    // App menu
    About               = 1,
    Preferences         = 2,
    Quit                = 3,

    // File
    NewProject          = 100,
    OpenProject         = 101,
    Save                = 102,
    SaveAs              = 103,
    CloseWindow         = 104,

    // Edit
    Undo                = 200,
    Redo                = 201,
    Cut                 = 202,
    Copy                = 203,
    Paste               = 204,
    SelectAll           = 205,

    // View
    ToggleFiles         = 300,
    ToggleInspector     = 301,
    ToggleWordWrap      = 302,
    ToggleFullscreen    = 303,

    // Window
    Minimize            = 400,
    Zoom                = 401,
}

/// One entry in a menu: either a leaf item (with optional shortcut),
/// a submenu, or a separator.
enum Entry {
    Item { label: String, shortcut: Option<String>, id: u32 },
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
        Self { title: title.into(), entries: Vec::new() }
    }

    /// Append a leaf menu item. `shortcut` is SDL-style
    /// ("CmdOrCtrl+N", "Alt+Shift+Z") or `None` for no key equivalent.
    pub fn item<I: Into<u32>>(
        mut self,
        label: &str,
        shortcut: Option<&str>,
        id: I,
    ) -> Self {
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
        let raw = self.into_raw();
        unsafe { raw.set_as_app_menu_bar() }
    }

    /// Convert the builder tree into the FFI representation. Used
    /// internally and by tests; not usually called directly.
    pub fn into_raw(self) -> RawMenu {
        let title_c = std::ffi::CString::new(self.title.as_str())
            .expect("menu title has no NUL");
        let mut raw = RawMenu::from_cstr(title_c.as_ptr())
            .expect("ui_menu_bar_create returned NULL (OOM)");
        for entry in self.entries {
            match entry {
                Entry::Item { label, shortcut, id } => {
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
    use mocida_sys::menu_bar::{Menu as RawMenu, ui_menu_bar_create};
    use std::os::raw::c_char;
    pub trait RawMenuFromCStr {
        fn from_cstr(title_with_nul: *const c_char) -> Option<RawMenu>;
    }
    impl RawMenuFromCStr for RawMenu {
        fn from_cstr(title_with_nul: *const c_char) -> Option<RawMenu> {
            let ptr = unsafe { ui_menu_bar_create(title_with_nul) };
            if ptr.is_null() { None } else { unsafe { RawMenu::from_raw(ptr) } }
        }
    }
}
use raw_helpers::RawMenuFromCStr;
