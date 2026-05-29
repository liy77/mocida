//! Mocida hello-world in Rust.
//!
//! Mirrors the spirit of mocida's C `src/main.c` demo: a white card on
//! a slate background with a title, an FPS readout, and a button that
//! cycles the target FPS. Run with:
//!
//! ```text
//! $env:MOCIDA_INCLUDE_DIR = "C:\path\to\mocida\src\headers"
//! $env:MOCIDA_LIB_DIR     = "C:\path\to\mocida\build"
//! cargo run --example hello_world
//! ```

use std::cell::Cell;
use std::rc::Rc;

use mocida::text::{get_font, search_fonts};
use mocida::{AAMode, App, Button, Children, Color, Event, Rectangle, RenderQuality, Shadow, Text};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    const WIN_W: i32 = 1024;
    const WIN_H: i32 = 640;

    let mut app = App::new("Mocida - Rust demo", WIN_W, WIN_H)?;
    app.set_target_fps(60)
        .set_render_quality(RenderQuality::High)
        .set_aa_mode(AAMode::Coverage)
        .set_background_color(Color::rgb(226, 232, 240));

    search_fonts();
    let arial = get_font("Arial").unwrap_or_default();

    // Root container holding everything in this window.
    let mut children = Children::new(16)?;

    // White background panel.
    let panel = Rectangle::new()?
        .color(Color::WHITE)
        .radius(14.0)
        .into_widget_sized((WIN_W - 48) as f32, (WIN_H - 48) as f32)?
        .position(24.0, 24.0);
    children.add(panel)?;

    // Title.
    let title = Text::new("Mocida - Rust port", 28.0)?
        .font_family(&arial)?
        .color(Color::rgb(15, 23, 42))
        .into_widget()?
        .position(48.0, 44.0);
    children.add(title)?;

    // FPS readout. The label's contents get rewritten from inside the
    // FRAMERATE_CHANGED callback, so we keep its widget reachable via
    // an Rc<Cell<*mut UIText>>. Cell is fine here because the entire
    // mocida event loop runs on a single thread.
    let fps_text = Text::new("FPS: 60", 18.0)?
        .font_family(&arial)?
        .color(Color::rgb(71, 85, 105));
    // Snapshot the raw pointer before transferring ownership to a
    // Widget; stays valid because the app owns the widget tree until
    // Drop. The UI loop is single-threaded, so a Send-less raw
    // pointer captured by the closure is fine.
    let fps_text_ptr: *mut mocida::sys::UIText = fps_text.as_ptr();
    let fps_label = fps_text.into_widget()?.position(48.0, 90.0);
    children.add(fps_label)?;

    // Three demo cards.
    for (i, color) in [
        Color::rgb(59, 130, 246),
        Color::rgb(34, 197, 94),
        Color::rgb(168, 85, 247),
    ]
    .into_iter()
    .enumerate()
    {
        let card = Rectangle::new()?
            .color(color)
            .radius(14.0)
            .shadow(Shadow {
                offset_y: 8.0,
                blur: 18.0,
                spread: -2.0,
                color: Color::rgba(0, 0, 0, 0.18),
                ..Shadow::DEFAULT
            })
            .into_widget_sized(240.0, 160.0)?
            .position(48.0 + (i as f32) * 264.0, 160.0);
        children.add(card)?;
    }

    // Cycle button: bumps the FPS target between 30, 60, 120, and
    // unlimited on each click and rewrites its own label.
    let targets: [(i32, &str); 4] = [(30, "30"), (60, "60"), (120, "120"), (0, "UNLIMITED")];
    let cycle_idx = Rc::new(Cell::new(1usize));
    let cycle_idx_cb = cycle_idx.clone();

    // Capturing `app` directly in the click handler would conflict
    // with the &mut borrow on `app.add_child(...)`. We pass the raw
    // pointer through Rc<Cell<_>> instead — single-threaded UI loop,
    // so no aliasing hazard.
    let app_ptr: *mut mocida::sys::UIApp = app.as_ptr();
    let fps_btn = Button::new(&format!("FPS Target: {}", targets[1].1), 18.0)?
        .font_family(&arial)?
        .radius(8.0)
        .colors(Color::rgb(59, 130, 246), Color::WHITE)
        .shadow(Shadow::DEFAULT)
        .on_click(move |btn| {
            let i = (cycle_idx_cb.get() + 1) % targets.len();
            cycle_idx_cb.set(i);
            let (fps, label) = targets[i];
            unsafe { mocida::sys::UIApp_SetTargetFPS(app_ptr, fps) };
            let _ = btn.set_text(&format!("FPS Target: {}", label));
        })
        .into_widget_sized(220.0, 52.0)?
        .position(48.0, 380.0);
    children.add(fps_btn)?;

    // Footer hint.
    let footer = Text::new(
        "Click the button to cycle FPS targets. Close the window to exit.",
        14.0,
    )?
    .font_family(&arial)?
    .color(Color::rgb(100, 116, 139))
    .into_widget()?
    .position(48.0, (WIN_H - 64) as f32);
    children.add(footer)?;

    app.set_children(children);

    // Live FPS readout — capture the raw label pointer; mocida's main
    // loop fires this on the UI thread.
    app.on_event(Event::FramerateChanged, move |data| {
        let text = format!("FPS: {:.0}", data.fps());
        if let Ok(c) = std::ffi::CString::new(text) {
            if !fps_text_ptr.is_null() {
                unsafe { mocida::sys::UIText_SetText(fps_text_ptr, c.as_ptr() as *mut _) };
            }
        }
    });

    app.show().run();
    Ok(())
}
