//! `UITextArea` wrapper — multi-line editable input.

use std::ffi::{c_void, CStr, CString};

use mocida_sys as sys;

use crate::color::Color;
use crate::cursor::Cursor;
use crate::error::{Error, Result};
use crate::text::WrapMode;
use crate::widget::Widget;

/// Multi-line editable text input. Unlike [`TextField`](crate::TextField),
/// Enter inserts a newline.
pub struct TextArea {
    ptr: *mut sys::UITextArea,
    moved: bool,
    on_change: Option<Box<ChangeState>>,
    on_save: Option<Box<SaveState>>,
    hl: Option<Box<HlState>>,
}

struct ChangeState {
    handler: Box<dyn FnMut(&str) + 'static>,
}

struct SaveState {
    handler: Box<dyn FnMut() + 'static>,
}

/// A syntax-highlight callback: maps the full text to colored byte spans
/// `(start, end, color)`. Called by mocida whenever the line cache rebuilds.
struct HlState {
    handler: Box<dyn FnMut(&str) -> Vec<(usize, usize, Color)> + 'static>,
}

impl TextArea {
    /// Allocates a text area.
    pub fn new(initial: &str, font_size: f32) -> Result<Self> {
        let c = CString::new(initial)?;
        let ptr = unsafe { sys::UITextArea_Create(c.as_ptr(), font_size) };
        if ptr.is_null() {
            return Err(Error::Null("UITextArea_Create"));
        }
        Ok(Self {
            ptr,
            moved: false,
            on_change: None,
            on_save: None,
            hl: None,
        })
    }

    /// Replaces the contents.
    pub fn set_text(&mut self, text: &str) -> Result<()> {
        let c = CString::new(text)?;
        unsafe { sys::UITextArea_SetText(self.ptr, c.as_ptr()) };
        Ok(())
    }

    /// Reads the current contents.
    pub fn text(&self) -> Option<String> {
        let p = unsafe { sys::UITextArea_GetText(self.ptr) };
        if p.is_null() {
            None
        } else {
            unsafe { CStr::from_ptr(p) }
                .to_str()
                .ok()
                .map(str::to_owned)
        }
    }

    /// Sets the placeholder.
    pub fn placeholder(self, text: &str) -> Result<Self> {
        let c = CString::new(text)?;
        unsafe { sys::UITextArea_SetPlaceholder(self.ptr, c.as_ptr()) };
        Ok(self)
    }

    /// Enables placeholder breathing animation.
    pub fn placeholder_animated(self, yes: bool) -> Self {
        unsafe { sys::UITextArea_SetPlaceholderAnimated(self.ptr, yes as i32) };
        self
    }

    /// Sets the caret blink half-period in milliseconds.
    pub fn caret_blink_rate(self, ms: i32) -> Self {
        unsafe { sys::UITextArea_SetCaretBlinkRate(self.ptr, ms) };
        self
    }

    /// Sets the font family path.
    pub fn font_family(self, path: &str) -> Result<Self> {
        let c = CString::new(path)?;
        unsafe { sys::UITextArea_SetFontFamily(self.ptr, c.as_ptr() as *mut _) };
        Ok(self)
    }

    /// Sets the font size in points.
    pub fn font_size(self, size: f32) -> Self {
        unsafe { sys::UITextArea_SetFontSize(self.ptr, size) };
        self
    }

    /// Sets the max byte length (-1 for unlimited).
    pub fn max_length(self, len: i32) -> Self {
        unsafe { sys::UITextArea_SetMaxLength(self.ptr, len) };
        self
    }

    /// Toggles the left line-number gutter (off by default).
    pub fn line_numbers(self, show: bool) -> Self {
        unsafe { sys::UITextArea_SetShowLineNumbers(self.ptr, show as i32) };
        self
    }

    /// Line-height multiplier on the font size (default 1.2).
    pub fn line_spacing(self, spacing: f32) -> Self {
        unsafe { sys::UITextArea_SetLineSpacing(self.ptr, spacing) };
        self
    }

    /// Sets the background fill.
    pub fn bg_color(self, color: Color) -> Self {
        unsafe { sys::UITextArea_SetBgColor(self.ptr, color.into_raw()) };
        self
    }

    /// Sets the highlight color drawn behind selected text (honours alpha, so a
    /// translucent color keeps the glyphs readable).
    pub fn selection_color(self, color: Color) -> Self {
        unsafe { sys::UITextArea_SetSelectionColor(self.ptr, color.into_raw()) };
        self
    }

    /// Sets the glyph color.
    pub fn text_color(self, color: Color) -> Self {
        unsafe { sys::UITextArea_SetTextColor(self.ptr, color.into_raw()) };
        self
    }

    /// Sets the border (normal, focused, width).
    pub fn border(self, normal: Color, focused: Color, width: f32) -> Self {
        unsafe {
            sys::UITextArea_SetBorder(self.ptr, normal.into_raw(), focused.into_raw(), width)
        };
        self
    }

    /// Sets the corner radius.
    pub fn radius(self, radius: f32) -> Self {
        unsafe { sys::UITextArea_SetRadius(self.ptr, radius) };
        self
    }

    /// Symmetric padding (`x` horizontal, `y` vertical).
    pub fn padding(self, x: f32, y: f32) -> Self {
        unsafe { sys::UITextArea_SetPadding(self.ptr, x, y) };
        self
    }

    /// Wrap mode.
    pub fn wrap_mode(self, mode: WrapMode) -> Self {
        unsafe { sys::UITextArea_SetWrapMode(self.ptr, sys::UIWrapMode(mode as _)) };
        self
    }

    /// Cursor displayed while hovering.
    pub fn cursor(self, cursor: Cursor) -> Self {
        unsafe { sys::UITextArea_SetCursor(self.ptr, cursor.into_raw()) };
        self
    }

    /// Registers an on-change handler.
    pub fn on_change<F>(mut self, handler: F) -> Self
    where
        F: FnMut(&str) + 'static,
    {
        let state = Box::new(ChangeState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const ChangeState as *mut c_void;
        unsafe { sys::UITextArea_OnChange(self.ptr, Some(change_trampoline), userdata) };
        self.on_change = Some(state);
        self
    }

    /// Registers a save-request handler, fired when the focused area gets Ctrl+S.
    pub fn on_save<F>(mut self, handler: F) -> Self
    where
        F: FnMut() + 'static,
    {
        let state = Box::new(SaveState {
            handler: Box::new(handler),
        });
        let ud = Box::as_ref(&state) as *const SaveState as *mut c_void;
        unsafe { sys::UITextArea_OnSave(self.ptr, Some(save_trampoline), ud) };
        self.on_save = Some(state);
        self
    }

    /// Installs a syntax-highlight callback. The closure maps the full text to
    /// colored byte spans; mocida calls it when the line cache rebuilds (on
    /// text change), so colors track edits. Replaces any previous highlighter.
    pub fn highlighter<F>(mut self, handler: F) -> Self
    where
        F: FnMut(&str) -> Vec<(usize, usize, Color)> + 'static,
    {
        let state = Box::new(HlState {
            handler: Box::new(handler),
        });
        let ud = Box::as_ref(&state) as *const HlState as *mut c_void;
        unsafe { sys::UITextArea_SetHighlighter(self.ptr, Some(hl_trampoline), ud) };
        self.hl = Some(state);
        self
    }

    /// Programmatic focus.
    pub fn focus(self, focused: bool) -> Self {
        unsafe { sys::UITextArea_SetFocus(self.ptr, focused as i32) };
        self
    }

    /// True when this area has keyboard focus.
    pub fn is_focused(&self) -> bool {
        unsafe { sys::UITextArea_IsFocused(self.ptr) != 0 }
    }

    /// Borrow the raw `UITextArea*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UITextArea {
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

impl Drop for TextArea {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UITextArea_Destroy(self.ptr) };
            return;
        }
        if let Some(state) = self.on_change.take() {
            std::mem::forget(state);
        }
        if let Some(state) = self.on_save.take() {
            std::mem::forget(state);
        }
        if let Some(state) = self.hl.take() {
            std::mem::forget(state);
        }
    }
}

extern "C" fn save_trampoline(_ta: *mut sys::UITextArea, ud: *mut c_void) {
    if ud.is_null() {
        return;
    }
    let state = unsafe { &mut *(ud as *mut SaveState) };
    (state.handler)();
}

extern "C" fn hl_trampoline(
    ud: *mut c_void,
    text: *const std::ffi::c_char,
    len: i32,
    sink: *mut c_void,
    emit: sys::UISpanEmitFn,
) {
    if ud.is_null() {
        return;
    }
    let emit = match emit {
        Some(e) => e,
        None => return,
    };
    let state = unsafe { &mut *(ud as *mut HlState) };
    let bytes: &[u8] = if text.is_null() || len <= 0 {
        &[]
    } else {
        unsafe { std::slice::from_raw_parts(text as *const u8, len as usize) }
    };
    let s = match std::str::from_utf8(bytes) {
        Ok(s) => s,
        Err(_) => return,
    };
    for (start, end, color) in (state.handler)(s) {
        unsafe { emit(sink, start as i32, end as i32, color.into_raw()) };
    }
}

extern "C" fn change_trampoline(
    _ta: *mut sys::UITextArea,
    text: *const std::ffi::c_char,
    userdata: *mut c_void,
) {
    if userdata.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut ChangeState) };
    let s = if text.is_null() {
        ""
    } else {
        match unsafe { CStr::from_ptr(text) }.to_str() {
            Ok(s) => s,
            Err(_) => return,
        }
    };
    (state.handler)(s);
}
