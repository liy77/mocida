//! `UIGlass` — region backdrop container (Acrylic / Liquid Glass / Vibrancy /
//! KDE blur), plus the [`BackdropMaterial`] family shared with the window-level
//! backdrop ([`Window::set_backdrop`](crate::window::Window::set_backdrop)).

use std::ffi::CString;

use mocida_sys as sys;

use crate::color::Color;
use crate::error::{Error, Result};
use crate::widget::Widget;

/// Backdrop material family. Mirrors the C `UIBackdropMaterial` enum 1:1.
///
/// `Mica` / `MicaAlt` / `KdeBlurWindow` are *window-wide* (use them with
/// [`Window::set_backdrop`](crate::window::Window::set_backdrop)); the rest are
/// *region* materials usable on a [`Glass`] widget.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum BackdropMaterial {
    /// No effect.
    None = 0,
    /// Platform default, resolved at runtime.
    Auto = 1,
    /// Win11 Mica (window-wide).
    Mica = 2,
    /// Win11 Mica Alt (window-wide).
    MicaAlt = 3,
    /// KWin whole-window blur (window-wide).
    KdeBlurWindow = 4,
    /// Win11 Acrylic.
    Acrylic = 5,
    /// Win10 acrylic blur-behind.
    AcrylicLegacy = 6,
    /// macOS/iOS 26 Liquid Glass.
    LiquidGlass = 7,
    /// macOS sidebar vibrancy.
    VibrancySidebar = 8,
    /// macOS header vibrancy.
    VibrancyHeader = 9,
    /// macOS menu vibrancy.
    VibrancyMenu = 10,
    /// macOS popover vibrancy.
    VibrancyPopover = 11,
    /// macOS HUD vibrancy.
    VibrancyHud = 12,
    /// KWin per-region blur.
    KdeBlurRegion = 13,
}

impl BackdropMaterial {
    /// Parse an `effect:` string ("mica", "acrylic", "liquid",
    /// "vibrancy-sidebar", "kde-region", …) using the C resolver, so the
    /// mapping stays single-sourced. Unknown / empty → [`Auto`](Self::Auto).
    pub fn from_effect(effect: &str) -> Self {
        let c = match CString::new(effect) {
            Ok(c) => c,
            Err(_) => return BackdropMaterial::Auto,
        };
        let raw = unsafe { sys::UIBackdrop_FromString(c.as_ptr()) };
        Self::from_raw(raw.0)
    }

    /// 1:1 from the raw enum value (out-of-range → [`Auto`](Self::Auto)).
    pub fn from_raw(v: i32) -> Self {
        match v {
            0 => Self::None,
            1 => Self::Auto,
            2 => Self::Mica,
            3 => Self::MicaAlt,
            4 => Self::KdeBlurWindow,
            5 => Self::Acrylic,
            6 => Self::AcrylicLegacy,
            7 => Self::LiquidGlass,
            8 => Self::VibrancySidebar,
            9 => Self::VibrancyHeader,
            10 => Self::VibrancyMenu,
            11 => Self::VibrancyPopover,
            12 => Self::VibrancyHud,
            13 => Self::KdeBlurRegion,
            _ => Self::Auto,
        }
    }

    #[inline]
    pub(crate) fn raw(self) -> sys::UIBackdropMaterial {
        sys::UIBackdropMaterial(self as i32)
    }

    /// True for window-wide materials (Mica / Mica Alt / KDE blur-window).
    pub fn is_window_wide(self) -> bool {
        matches!(self, Self::Mica | Self::MicaAlt | Self::KdeBlurWindow)
    }
}

/// Material thickness preset (mirrors `UIGlassThickness`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum GlassThickness {
    /// Thin material.
    Thin = 0,
    /// Default material.
    Regular = 1,
    /// Thick material.
    Thick = 2,
}

/// AppKit vibrancy state (mirrors `UIVibrancyState`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum VibrancyState {
    /// Active appearance.
    Active = 0,
    /// Inactive appearance.
    Inactive = 1,
    /// Pressed appearance.
    Pressed = 2,
}

/// Region-backdrop container. Lays children like a vertical [`Stack`](crate::Stack)
/// while drawing a glass background (native OS effect where available, in-app
/// tint otherwise).
pub struct Glass {
    ptr: *mut sys::UIGlass,
    moved: bool,
}

impl Glass {
    /// Creates a glass container with the given material.
    pub fn new(material: BackdropMaterial) -> Result<Self> {
        let ptr = unsafe { sys::UIGlass_Create(material.raw()) };
        if ptr.is_null() {
            return Err(Error::Null("UIGlass_Create"));
        }
        Ok(Self { ptr, moved: false })
    }

    /// Corner radius of the glass fill.
    pub fn radius(self, radius: f32) -> Self {
        unsafe { sys::UIGlass_SetRadius(self.ptr, radius) };
        self
    }

    /// Overlay tint color.
    pub fn tint(self, color: Color) -> Self {
        unsafe { sys::UIGlass_SetTint(self.ptr, color.into_raw()) };
        self
    }

    /// Tint strength, `0.0..=1.0`.
    pub fn tint_opacity(self, opacity: f32) -> Self {
        unsafe { sys::UIGlass_SetTintOpacity(self.ptr, opacity) };
        self
    }

    /// Material thickness preset.
    pub fn thickness(self, thickness: GlassThickness) -> Self {
        unsafe { sys::UIGlass_SetThickness(self.ptr, sys::UIGlassThickness(thickness as i32)) };
        self
    }

    /// KDE blur radius in pixels (ignored by materials that lack a blur knob).
    pub fn blur(self, px: f32) -> Self {
        unsafe { sys::UIGlass_SetBlur(self.ptr, px) };
        self
    }

    /// Liquid Glass refraction, `0.0..=1.0` (ignored by other materials).
    pub fn refraction(self, refraction: f32) -> Self {
        unsafe { sys::UIGlass_SetRefraction(self.ptr, refraction) };
        self
    }

    /// Acrylic / HUD grain, `0.0..=1.0` (ignored by other materials).
    pub fn noise(self, noise: f32) -> Self {
        unsafe { sys::UIGlass_SetNoise(self.ptr, noise) };
        self
    }

    /// AppKit vibrancy state (ignored by non-vibrancy materials).
    pub fn vibrancy_state(self, state: VibrancyState) -> Self {
        unsafe { sys::UIGlass_SetVibrancyState(self.ptr, sys::UIVibrancyState(state as i32)) };
        self
    }

    /// Lay children left-to-right (`true`) instead of top-to-bottom.
    pub fn horizontal(self, horizontal: bool) -> Self {
        unsafe { sys::UIGlass_SetOrientation(self.ptr, horizontal as i32) };
        self
    }

    /// Gap between consecutive items.
    pub fn spacing(self, spacing: f32) -> Self {
        unsafe { sys::UIGlass_SetSpacing(self.ptr, spacing) };
        self
    }

    /// Cross-axis alignment (0 start, 1 center, 2 end).
    pub fn align(self, align: i32) -> Self {
        unsafe { sys::UIGlass_SetAlign(self.ptr, align) };
        self
    }

    /// Main-axis distribution (0 start, 1 center, 2 end, 3 between).
    pub fn justify(self, justify: i32) -> Self {
        unsafe { sys::UIGlass_SetJustify(self.ptr, justify) };
        self
    }

    /// Inner padding (left, top, right, bottom).
    pub fn padding(self, left: f32, top: f32, right: f32, bottom: f32) -> Self {
        unsafe { sys::UIGlass_SetPadding(self.ptr, left, top, right, bottom) };
        self
    }

    /// Absolute-position children instead of flowing them.
    pub fn free_layout(self, enabled: bool) -> Self {
        unsafe { sys::UIGlass_SetFreeLayout(self.ptr, enabled as i32) };
        self
    }

    /// Appends an item. The glass takes ownership of the widget.
    pub fn add(&mut self, item: Widget) -> Result<()> {
        let raw = item.into_raw();
        let rc = unsafe { sys::UIGlass_AddItem(self.ptr, raw) };
        if rc != 1 {
            return Err(Error::Null("UIGlass_AddItem"));
        }
        Ok(())
    }

    /// Borrow the raw `UIGlass*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UIGlass {
        self.ptr
    }

    /// Lift into a [`Widget`] (auto-sized).
    pub fn into_widget(mut self) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::new(self.ptr as *mut _) }
    }

    /// Lift into a [`Widget`] with an explicit size.
    pub fn into_widget_sized(mut self, width: f32, height: f32) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::with_size(self.ptr as *mut _, width, height) }
    }
}

impl Drop for Glass {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UIGlass_Destroy(self.ptr) };
        }
    }
}

/// Global glass switch (live, no rebuild). When `false`, every `Glass` widget
/// renders as a SOLID panel (full-opacity tint, no blur) — so a `Glass{}` can
/// double as a plain panel and the app toggles "translucent" at runtime.
pub fn set_glass_enabled(on: bool) {
    unsafe { sys::UIGlass_SetGlobalEnabled(if on { 1 } else { 0 }) };
}

/// Global blur-strength multiplier applied to every `Glass` widget's `blur`
/// (the intensity / "power" knob). 1.0 = as authored.
pub fn set_glass_blur_scale(scale: f32) {
    unsafe { sys::UIGlass_SetGlobalBlurScale(scale) };
}
