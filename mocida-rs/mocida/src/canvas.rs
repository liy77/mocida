//! `UICanvas` — immediate-mode 2D vector canvas (HTML5/Skia-style context).
//!
//! The C widget records a path of `MoveTo`/`LineTo`/`Arc`/`BezierCurveTo`
//! commands and fills/strokes it each frame. Fills are triangulated with
//! ear-clipping on the C side, so **concave** outlines (e.g. browser-style
//! curved tabs) render correctly.
//!
//! The Rust wrapper exposes a per-frame draw callback. The closure receives a
//! [`CanvasDraw`] borrow of the live canvas; it issues path/fill commands which
//! mocida renders into the canvas surface that frame. This is the idiomatic way
//! to drive custom vector visuals (the runtime's `Canvas` element uses it to
//! paint the editor tab bar).

use std::ffi::c_void;

use mocida_sys as sys;

use crate::color::Color;
use crate::error::{Error, Result};
use crate::widget::Widget;

/// A live drawing context handed to the [`Canvas::on_frame`] closure. Methods
/// mirror the canvas 2D API; calls only have an effect for the duration of the
/// frame callback.
pub struct CanvasDraw {
    ptr: *mut sys::UICanvas,
}

impl CanvasDraw {
    #[inline]
    fn raw(&self) -> *mut sys::UICanvas {
        self.ptr
    }

    /// Canvas width in logical pixels.
    pub fn width(&self) -> f32 {
        unsafe { sys::UICanvas_GetWidth(self.ptr) }
    }

    /// Canvas height in logical pixels.
    pub fn height(&self) -> f32 {
        unsafe { sys::UICanvas_GetHeight(self.ptr) }
    }

    /// Set the fill color used by the next [`fill`](CanvasDraw::fill).
    pub fn set_fill(&mut self, color: Color) -> &mut Self {
        unsafe { sys::UICanvas_SetFillColor(self.ptr, color.into_raw()) };
        self
    }

    /// Set the stroke color used by the next [`stroke`](CanvasDraw::stroke).
    pub fn set_stroke(&mut self, color: Color) -> &mut Self {
        unsafe { sys::UICanvas_SetStrokeColor(self.ptr, color.into_raw()) };
        self
    }

    /// Set the stroke line width.
    pub fn set_line_width(&mut self, w: f32) -> &mut Self {
        unsafe { sys::UICanvas_SetLineWidth(self.ptr, w) };
        self
    }

    /// Begin a fresh path (discards any in-progress one).
    pub fn begin_path(&mut self) -> &mut Self {
        unsafe { sys::UICanvas_BeginPath(self.ptr) };
        self
    }

    /// Move the pen to `(x, y)` without drawing.
    pub fn move_to(&mut self, x: f32, y: f32) -> &mut Self {
        unsafe { sys::UICanvas_MoveTo(self.ptr, x, y) };
        self
    }

    /// Straight line from the pen to `(x, y)`.
    pub fn line_to(&mut self, x: f32, y: f32) -> &mut Self {
        unsafe { sys::UICanvas_LineTo(self.ptr, x, y) };
        self
    }

    /// Cubic Bézier curve from the pen to `(x, y)` via the two control points.
    pub fn bezier_to(
        &mut self,
        cp1x: f32,
        cp1y: f32,
        cp2x: f32,
        cp2y: f32,
        x: f32,
        y: f32,
    ) -> &mut Self {
        unsafe { sys::UICanvas_BezierCurveTo(self.ptr, cp1x, cp1y, cp2x, cp2y, x, y) };
        self
    }

    /// Arc centered at `(x, y)` with `r`, from `start` to `end` radians.
    pub fn arc(&mut self, x: f32, y: f32, r: f32, start: f32, end: f32) -> &mut Self {
        unsafe { sys::UICanvas_Arc(self.ptr, x, y, r, start, end) };
        self
    }

    /// Close the current sub-path back to its start.
    pub fn close_path(&mut self) -> &mut Self {
        unsafe { sys::UICanvas_ClosePath(self.ptr) };
        self
    }

    /// Fill the current path with the active fill color.
    pub fn fill(&mut self) -> &mut Self {
        unsafe { sys::UICanvas_Fill(self.ptr) };
        self
    }

    /// Stroke the current path with the active stroke color/width.
    pub fn stroke(&mut self) -> &mut Self {
        unsafe { sys::UICanvas_Stroke(self.ptr) };
        self
    }

    /// Filled axis-aligned rectangle.
    pub fn fill_rect(&mut self, x: f32, y: f32, w: f32, h: f32) -> &mut Self {
        unsafe { sys::UICanvas_FillRect(self.ptr, x, y, w, h) };
        self
    }

    /// Stroked axis-aligned rectangle (current stroke color / line width).
    pub fn stroke_rect(&mut self, x: f32, y: f32, w: f32, h: f32) -> &mut Self {
        unsafe { sys::UICanvas_StrokeRect(self.ptr, x, y, w, h) };
        self
    }

    /// Paint `(x, y, w, h)` with the current fill color (the canvas draws
    /// straight into the window, so there's no transparent backing to clear to).
    pub fn clear_rect(&mut self, x: f32, y: f32, w: f32, h: f32) -> &mut Self {
        unsafe { sys::UICanvas_ClearRect(self.ptr, x, y, w, h) };
        self
    }

    /// Filled, anti-aliased circle.
    pub fn fill_circle(&mut self, x: f32, y: f32, r: f32) -> &mut Self {
        unsafe { sys::UICanvas_FillCircle(self.ptr, x, y, r) };
        self
    }

    /// Circle outline (current stroke color / line width).
    pub fn stroke_circle(&mut self, x: f32, y: f32, r: f32) -> &mut Self {
        unsafe { sys::UICanvas_StrokeCircle(self.ptr, x, y, r) };
        self
    }

    /// Multiply every subsequent fill / stroke / text / image alpha by `alpha`.
    pub fn set_global_alpha(&mut self, alpha: f32) -> &mut Self {
        unsafe { sys::UICanvas_SetGlobalAlpha(self.ptr, alpha) };
        self
    }

    /// Push the drawing state (colors, line width, alpha, transform).
    pub fn save(&mut self) -> &mut Self {
        unsafe { sys::UICanvas_Save(self.ptr) };
        self
    }

    /// Pop the state pushed by the matching [`save`](CanvasDraw::save).
    pub fn restore(&mut self) -> &mut Self {
        unsafe { sys::UICanvas_Restore(self.ptr) };
        self
    }

    /// Translate subsequent drawing.
    pub fn translate(&mut self, x: f32, y: f32) -> &mut Self {
        unsafe { sys::UICanvas_Translate(self.ptr, x, y) };
        self
    }

    /// Rotate subsequent drawing by `radians` (clockwise on screen).
    pub fn rotate(&mut self, radians: f32) -> &mut Self {
        unsafe { sys::UICanvas_Rotate(self.ptr, radians) };
        self
    }

    /// Scale subsequent drawing.
    pub fn scale(&mut self, x: f32, y: f32) -> &mut Self {
        unsafe { sys::UICanvas_Scale(self.ptr, x, y) };
        self
    }

    /// Pointer position relative to the canvas (last known).
    pub fn mouse_pos(&self) -> (f32, f32) {
        let (mut x, mut y) = (0.0f32, 0.0f32);
        unsafe { sys::UICanvas_GetMousePos(self.ptr, &mut x, &mut y) };
        (x, y)
    }

    /// Whether the pointer is over the canvas.
    pub fn is_mouse_inside(&self) -> bool {
        unsafe { sys::UICanvas_IsMouseInside(self.ptr) != 0 }
    }

    /// Draw text at `(x, y)` (top-left) in the given font family + size, using
    /// the current fill color. Returns silently if the font can't be opened.
    /// The C side caches the rasterised texture per (font, text, color), so
    /// redrawing the same label every frame is cheap.
    pub fn draw_text(&mut self, text: &str, x: f32, y: f32, font_family: &str, size: f32) -> &mut Self {
        // NUL-terminate into a reused thread-local buffer instead of two fresh
        // CStrings per call (this runs every frame).
        thread_local! {
            static BUF: std::cell::RefCell<Vec<u8>> = const { std::cell::RefCell::new(Vec::new()) };
        }
        if text.contains('\0') || font_family.contains('\0') {
            return self;
        }
        let ptr = self.ptr;
        BUF.with(|b| {
            let mut b = b.borrow_mut();
            b.clear();
            b.extend_from_slice(text.as_bytes());
            b.push(0);
            let split = b.len();
            b.extend_from_slice(font_family.as_bytes());
            b.push(0);
            let t = b.as_ptr() as *const std::os::raw::c_char;
            let f = unsafe { t.add(split) };
            unsafe { sys::UICanvas_DrawText(ptr, t, x, y, f, size) };
        });
        self
    }

    /// Draw an [`Image`](crate::image::Image) into the rect `(x, y, w, h)` using
    /// the current global alpha / blend mode. The image's GPU texture must
    /// already be created — load it eagerly via
    /// [`Image::from_memory`](crate::image::Image::from_memory) (which decodes
    /// against a renderer up front); a lazily-loaded [`Image::load`] has no
    /// texture outside the widget tree and draws nothing here.
    pub fn draw_image(&mut self, img: &crate::image::Image, x: f32, y: f32, w: f32, h: f32) -> &mut Self {
        unsafe { sys::UICanvas_DrawImage(self.ptr, img.as_ptr(), x, y, w, h) };
        self
    }

    /// The live `SDL_Renderer*` this canvas paints into. Valid only during the
    /// frame callback; pass it to [`Image::from_memory`](crate::image::Image::from_memory)
    /// to decode a texture eagerly for [`draw_image`](CanvasDraw::draw_image).
    #[inline]
    pub fn renderer(&self) -> *mut sys::SDL_Renderer {
        unsafe { sys::UICanvas_GetRenderer(self.ptr) }
    }

    /// Borrow the raw `UICanvas*` (escape hatch for advanced use).
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UICanvas {
        self.raw()
    }
}

struct FrameState {
    handler: Box<dyn FnMut(&mut CanvasDraw) + 'static>,
}

/// Immediate-mode 2D canvas widget.
pub struct Canvas {
    ptr: *mut sys::UICanvas,
    moved: bool,
    frame_cb: Option<Box<FrameState>>,
}

impl Canvas {
    /// Create a canvas of the given logical size.
    pub fn new(width: f32, height: f32) -> Result<Self> {
        let ptr = unsafe { sys::UICanvas_Create(width, height) };
        if ptr.is_null() {
            return Err(Error::Null("UICanvas_Create"));
        }
        Ok(Self {
            ptr,
            moved: false,
            frame_cb: None,
        })
    }

    /// Register the per-frame draw callback. The closure receives a live
    /// [`CanvasDraw`] each frame to (re)issue its vector commands.
    pub fn on_frame<F>(mut self, handler: F) -> Self
    where
        F: FnMut(&mut CanvasDraw) + 'static,
    {
        let state = Box::new(FrameState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const FrameState as *mut c_void;
        unsafe { sys::UICanvas_OnFrame(self.ptr, Some(frame_trampoline), userdata) };
        self.frame_cb = Some(state);
        self
    }

    /// Borrow the raw `UICanvas*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UICanvas {
        self.ptr
    }

    /// Lift into a [`Widget`] with an explicit size.
    pub fn into_widget_sized(mut self, width: f32, height: f32) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::with_size(self.ptr as *mut _, width, height) }
    }

    /// Lift into a [`Widget`] using the canvas's intrinsic size.
    pub fn into_widget(mut self) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::new(self.ptr as *mut _) }
    }
}

impl Drop for Canvas {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UICanvas_Destroy(self.ptr) };
            return;
        }
        // Ownership transferred to a parent: the trampoline userdata must stay
        // valid for the widget's lifetime, so mocida drops the closure together
        // with the canvas.
        if let Some(b) = self.frame_cb.take() {
            crate::widget::attach_owned(self.ptr as *const c_void, b);
        }
    }
}

extern "C" fn frame_trampoline(
    canvas: *mut sys::UICanvas,
    _renderer: *mut sys::SDL_Renderer,
    userdata: *mut c_void,
) {
    if userdata.is_null() || canvas.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut FrameState) };
    let mut draw = CanvasDraw { ptr: canvas };
    // A panic must not unwind into the C render loop (UB); drop the frame.
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| (state.handler)(&mut draw)));
}
