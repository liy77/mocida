//! `UICursor` enum + global cursor switch.
//!
//! Each logical kind (`pointer`, `text`, resize, …) can show a custom
//! image instead of the system shape. Widgets that already advertise a
//! kind pick the override up on the next hover.

use std::ffi::CString;

use mocida_sys as sys;

/// Logical mouse cursor. Mirrors the upstream `UICursor` enum 1:1.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum Cursor {
    /// Plain arrow.
    Default = 0,
    /// Pointing hand (clickables).
    Pointer = 1,
    /// I-beam (text fields).
    Text = 2,
    /// Crosshair (precision picks).
    Crosshair = 3,
    /// Four-arrow move.
    Move = 4,
    /// Forbidden / disabled.
    NotAllowed = 5,
    /// Busy / hourglass.
    Wait = 6,
    /// Working in background.
    Progress = 7,
    /// Left-right resize.
    EwResize = 8,
    /// Up-down resize.
    NsResize = 9,
    /// Diagonal resize (top-left ↔ bottom-right).
    NwseResize = 10,
    /// Diagonal resize (top-right ↔ bottom-left).
    NeswResize = 11,
}

impl Cursor {
    /// Applies the cursor immediately (mirrors `UICursor_Apply`).
    ///
    /// A kind with a custom image installed via [`set_image`](Self::set_image)
    /// shows that image.
    pub fn apply(self) {
        unsafe { sys::UICursor_Apply(sys::UICursor(self as _)) };
    }

    /// Replaces the image used whenever this cursor kind is shown.
    ///
    /// `path` is resolved like other assets (PNG, CUR, SVG, …). `hot_x` /
    /// `hot_y` are the click point inside the image. Widgets that advertise
    /// this kind (a button's pointer, a field's I-beam, a resize handle)
    /// start showing the image on the next hover. Returns whether the image
    /// loaded.
    pub fn set_image(self, path: &str, hot_x: i32, hot_y: i32) -> crate::Result<bool> {
        let c = CString::new(path)?;
        let ok = unsafe { sys::UICursor_SetImage(self.into_raw(), c.as_ptr(), hot_x, hot_y) };
        Ok(ok != 0)
    }

    /// Drops the custom image for this kind and goes back to the system shape.
    pub fn clear_image(self) {
        unsafe { sys::UICursor_SetImage(self.into_raw(), std::ptr::null(), 0, 0) };
    }

    /// Every logical kind, in enum order. Useful when installing a full set.
    pub fn all() -> &'static [Cursor] {
        &ALL
    }

    /// `default`, `pointer`, `text`, `ewresize`, … Empty when `name` is unknown.
    pub fn parse(name: &str) -> Option<Cursor> {
        Some(match name.trim().to_ascii_lowercase().as_str() {
            "default" | "arrow" => Cursor::Default,
            "pointer" | "hand" => Cursor::Pointer,
            "text" | "ibeam" | "i-beam" => Cursor::Text,
            "crosshair" => Cursor::Crosshair,
            "move" | "grab" => Cursor::Move,
            "notallowed" | "not-allowed" | "forbidden" | "no" => Cursor::NotAllowed,
            "wait" | "busy" => Cursor::Wait,
            "progress" => Cursor::Progress,
            "ewresize" | "ew-resize" | "colresize" | "horizontalresize" => Cursor::EwResize,
            "nsresize" | "ns-resize" | "rowresize" | "verticalresize" => Cursor::NsResize,
            "nwseresize" | "nwse-resize" => Cursor::NwseResize,
            "neswresize" | "nesw-resize" => Cursor::NeswResize,
            _ => return None,
        })
    }

    /// Convert to the raw bindgen newtype.
    #[inline]
    pub fn into_raw(self) -> sys::UICursor {
        sys::UICursor(self as _)
    }
}

const ALL: [Cursor; 12] = [
    Cursor::Default,
    Cursor::Pointer,
    Cursor::Text,
    Cursor::Crosshair,
    Cursor::Move,
    Cursor::NotAllowed,
    Cursor::Wait,
    Cursor::Progress,
    Cursor::EwResize,
    Cursor::NsResize,
    Cursor::NwseResize,
    Cursor::NeswResize,
];

/// Drops every custom cursor image. System shapes stay available.
pub fn clear_images() {
    unsafe { sys::UICursor_ClearImages() };
}

/// Frees cached system cursors and custom images. Normally called from
/// [`App::drop`](crate::App), but callable manually.
pub fn shutdown() {
    unsafe { sys::UICursor_Shutdown() };
}
