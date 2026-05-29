//! Demonstrates [`Signal<T>`] + [`TextField`] + [`Dropdown`].
//!
//! Type into the field — every keystroke pushes the new value into a
//! `Signal<String>`. A subscriber on that signal updates a [`Text`]
//! widget below. Picking a color from the dropdown updates a
//! `Signal<i32>` whose subscriber retints the title.

use std::cell::RefCell;
use std::rc::Rc;

use mocida::text::{by_ptr, get_font, search_fonts};
use mocida::{App, Children, Color, Dropdown, Rectangle, Signal, Text, TextField};

const PALETTE: [(&str, Color); 4] = [
    ("Slate", Color::rgb(15, 23, 42)),
    ("Indigo", Color::rgb(67, 56, 202)),
    ("Emerald", Color::rgb(5, 150, 105)),
    ("Crimson", Color::rgb(220, 38, 38)),
];

fn main() -> Result<(), Box<dyn std::error::Error>> {
    const WIN_W: i32 = 720;
    const WIN_H: i32 = 480;

    let mut app = App::new("mocida-rs signals demo", WIN_W, WIN_H)?;
    app.set_background_color(Color::rgb(241, 245, 249));
    search_fonts();
    let arial = get_font("Arial").unwrap_or_default();

    let mut children = Children::new(16)?;

    children.add(
        Rectangle::new()?
            .color(Color::WHITE)
            .radius(14.0)
            .into_widget_sized((WIN_W - 48) as f32, (WIN_H - 48) as f32)?
            .position(24.0, 24.0),
    )?;

    let title = Text::new("Type below", 28.0)?
        .font_family(&arial)?
        .color(PALETTE[0].1)
        .into_widget()?
        .position(48.0, 44.0);
    let title_ptr = title.as_ptr();
    children.add(title)?;

    // Typed reactive signals.
    let mut text_signal: Signal<String> = Signal::new("Type below".to_string())?;
    let mut color_signal: Signal<i32> = Signal::new(0)?;

    // Resolve the UIText* once. The widget owns it; the UI loop is
    // single-threaded so capturing the pointer in the subscribers
    // below is safe.
    let title_text_ptr: *mut mocida::sys::UIText = unsafe {
        let data = (*title_ptr).data as *mut mocida::sys::UIText;
        if data.is_null() {
            return Err("title widget has no UIText payload".into());
        }
        data
    };

    // Subscriber: the title text mirrors text_signal.
    let _text_sub = text_signal.subscribe(move |sig| {
        let _ = unsafe { by_ptr::set_text(title_text_ptr, &sig.get()) };
    })?;

    // Subscriber: the title color mirrors color_signal.
    let _color_sub = color_signal.subscribe(move |sig| {
        let idx = (sig.get() as usize) % PALETTE.len();
        let (_, color) = PALETTE[idx];
        unsafe { by_ptr::set_color(title_text_ptr, color) };
    })?;

    // Keep the signals alive for the duration of the app loop.
    let text_signal = Rc::new(RefCell::new(text_signal));
    let color_signal = Rc::new(RefCell::new(color_signal));

    // Text input — set the typed Signal<String> on every keystroke.
    let text_signal_cb = text_signal.clone();
    children.add(
        TextField::new("Type below", 18.0)?
            .font_family(&arial)?
            .placeholder("Start typing...")?
            .placeholder_animated(true)
            .radius(8.0)
            .padding(12.0, 8.0)
            .border(Color::rgb(203, 213, 225), Color::rgb(59, 130, 246), 1.0)
            .text_color(Color::rgb(30, 41, 59))
            .on_change(move |s| {
                let _ = text_signal_cb.borrow_mut().set(s.to_owned());
            })
            .into_widget_sized((WIN_W - 96) as f32, 40.0)?
            .position(48.0, 110.0),
    )?;

    // Color picker — drives Signal<i32>.
    let color_signal_cb = color_signal.clone();
    let mut dropdown = Dropdown::new()?.font(&arial, 16.0)?;
    for (name, _) in &PALETTE {
        dropdown.add_option(name)?;
    }
    let dropdown = dropdown
        .set_selected(0)
        .on_change(move |idx, _label| {
            let _ = color_signal_cb.borrow_mut().set(idx);
        });
    children.add(dropdown.into_widget_sized(220.0, 36.0)?.position(48.0, 170.0))?;

    let footer = Text::new(
        "Signal<String> drives the title text. Signal<i32> drives the color.",
        14.0,
    )?
    .font_family(&arial)?
    .color(Color::rgb(100, 116, 139))
    .into_widget()?
    .position(48.0, (WIN_H - 64) as f32);
    children.add(footer)?;

    app.set_children(children);
    app.show().run();

    drop(text_signal);
    drop(color_signal);
    Ok(())
}
