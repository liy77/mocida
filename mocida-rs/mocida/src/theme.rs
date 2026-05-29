//! `UITheme` — design tokens consumed by widgets on construction.

use mocida_sys as sys;

use crate::color::Color;

/// Snapshot of every design token mocida exposes.
#[derive(Debug, Clone, Copy)]
pub struct Theme {
    /// Primary brand color (buttons, sliders, focused borders).
    pub primary: Color,
    /// Text on primary surfaces.
    pub on_primary: Color,
    /// Card / sheet / dialog background.
    pub surface: Color,
    /// Text on surface.
    pub on_surface: Color,
    /// Window background.
    pub background: Color,
    /// Borders and dividers.
    pub border: Color,
    /// Success tint.
    pub success: Color,
    /// Warning tint.
    pub warning: Color,
    /// Destructive / error tint.
    pub danger: Color,
    /// Disabled / inactive tint.
    pub disabled: Color,
    /// Default rounded-corner radius.
    pub radius: f32,
    /// Default gap between things in a stack.
    pub spacing: f32,
    /// Small font size token.
    pub font_size_small: f32,
    /// Default font size token.
    pub font_size_medium: f32,
    /// Large font size token.
    pub font_size_large: f32,
}

impl Theme {
    /// Returns mocida's default light theme.
    pub fn light() -> Self {
        let mut raw = sys::UITheme::default();
        unsafe { sys::UITheme_FillLight(&mut raw) };
        Self::from_raw(raw)
    }

    /// Returns mocida's default dark theme.
    pub fn dark() -> Self {
        let mut raw = sys::UITheme::default();
        unsafe { sys::UITheme_FillDark(&mut raw) };
        Self::from_raw(raw)
    }

    /// Reads the current global theme.
    pub fn global() -> Self {
        // SAFETY: mocida guarantees the pointer is non-null and lives
        // for the process lifetime.
        let raw = unsafe { *sys::UITheme_GetGlobal() };
        Self::from_raw(raw)
    }

    /// Installs this theme as the new global. Existing widgets keep
    /// their current colors; new ones pick up the new tokens.
    pub fn install(self) {
        let raw = self.into_raw();
        unsafe { sys::UITheme_SetGlobal(&raw) };
    }

    fn from_raw(raw: sys::UITheme) -> Self {
        Self {
            primary: Color::from_raw(raw.primary),
            on_primary: Color::from_raw(raw.onPrimary),
            surface: Color::from_raw(raw.surface),
            on_surface: Color::from_raw(raw.onSurface),
            background: Color::from_raw(raw.background),
            border: Color::from_raw(raw.border),
            success: Color::from_raw(raw.success),
            warning: Color::from_raw(raw.warning),
            danger: Color::from_raw(raw.danger),
            disabled: Color::from_raw(raw.disabled),
            radius: raw.radius,
            spacing: raw.spacing,
            font_size_small: raw.fontSizeSmall,
            font_size_medium: raw.fontSizeMedium,
            font_size_large: raw.fontSizeLarge,
        }
    }

    fn into_raw(self) -> sys::UITheme {
        sys::UITheme {
            primary: self.primary.into_raw(),
            onPrimary: self.on_primary.into_raw(),
            surface: self.surface.into_raw(),
            onSurface: self.on_surface.into_raw(),
            background: self.background.into_raw(),
            border: self.border.into_raw(),
            success: self.success.into_raw(),
            warning: self.warning.into_raw(),
            danger: self.danger.into_raw(),
            disabled: self.disabled.into_raw(),
            radius: self.radius,
            spacing: self.spacing,
            fontSizeSmall: self.font_size_small,
            fontSizeMedium: self.font_size_medium,
            fontSizeLarge: self.font_size_large,
        }
    }
}
