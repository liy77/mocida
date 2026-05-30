//! Rust port of mocida's upstream `src/main.c` showcase.
//!
//! Exercises every component the C demo touches:
//!   - `Rectangle` + drop shadow (the panel and four cards)
//!   - `Text` labels (title, FPS readout, target, AA mode, footer)
//!   - `Button` with click callbacks (cycle FPS target, cycle AA mode, trim caches)
//!   - `MouseArea` + drag target (the orange card is draggable)
//!   - `App::on_event(Event::FramerateChanged, ...)` for the live FPS label
//!   - `App::on_resize` for the fluid layout
//!
//! Compare with `mocida/src/main.c` (~290 lines of business code) to
//! see how the Rust wrappers cut boilerplate.

use std::cell::RefCell;
use std::rc::Rc;

use mocida::mouse_area::MouseAreaEvent;
use mocida::text::{get_font, search_fonts};
use mocida::{
    AAMode, App, Button, Children, Color, Event, MouseArea, Rectangle, RenderQuality, Screen,
    Shadow, Text,
};

const WIN_W: i32 = 1024;
const WIN_H: i32 = 640;

const FPS_TARGETS: &[(i32, &str)] = &[(30, "30"), (60, "60"), (120, "120"), (0, "UNLIMITED")];
const AA_MODES: &[(AAMode, &str)] = &[
    (AAMode::Coverage, "COVERAGE"),
    (AAMode::Ssaa2x, "SSAA 2x"),
    (AAMode::Fxaa, "FXAA"),
    (AAMode::Taa, "TAA"),
];

/// Holds the raw `UI*` pointers the resize callback needs to
/// reposition / mutate. Single-threaded mocida loop, so `RefCell` is
/// enough — no Mutex.
#[derive(Default)]
struct State {
    panel: *mut mocida::sys::UIWidget,
    title: *mut mocida::sys::UIWidget,
    footer: *mut mocida::sys::UIWidget,
    fps_label: *mut mocida::sys::UIText,
    fps_label_w: *mut mocida::sys::UIWidget,
    target_label: *mut mocida::sys::UIText,
    target_label_w: *mut mocida::sys::UIWidget,
    aa_label: *mut mocida::sys::UIText,
    aa_label_w: *mut mocida::sys::UIWidget,
    cards: [*mut mocida::sys::UIWidget; 4],
    drag_hit: *mut mocida::sys::UIWidget,
    fps_btn: *mut mocida::sys::UIWidget,
    aa_btn: *mut mocida::sys::UIWidget,
    trim_btn: *mut mocida::sys::UIWidget,

    fps_idx: usize,
    aa_idx: usize,
    orange_dragged: bool,
}

type SharedState = Rc<RefCell<State>>;

/// Returns `(UIText*, UIWidget*)` so callers can both update the
/// text content and reposition the widget on resize.
fn build_label(
    children: &mut Children,
    text: &str,
    font_size: f32,
    color: Color,
    font: &str,
    x: f32,
    y: f32,
) -> Result<(*mut mocida::sys::UIText, *mut mocida::sys::UIWidget), Box<dyn std::error::Error>> {
    let label = Text::new(text, font_size)?
        .font_family(font)?
        .color(color);
    let text_ptr = label.as_ptr();
    let widget = label.into_widget()?.position(x, y);
    let widget_ptr = widget.as_ptr();
    children.add(widget)?;
    Ok((text_ptr, widget_ptr))
}

fn build_card(
    children: &mut Children,
    x: f32,
    y: f32,
    w: f32,
    h: f32,
    color: Color,
) -> Result<*mut mocida::sys::UIWidget, Box<dyn std::error::Error>> {
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
        .into_widget_sized(w, h)?
        .position(x, y)
        .z_index(5);
    let raw = card.as_ptr();
    children.add(card)?;
    Ok(raw)
}

fn build_action_button<F>(
    children: &mut Children,
    label: &str,
    font: &str,
    color: Color,
    x: f32,
    y: f32,
    w: f32,
    handler: F,
) -> Result<*mut mocida::sys::UIWidget, Box<dyn std::error::Error>>
where
    F: FnMut(&mut Button) + 'static,
{
    let btn = Button::new(label, 18.0)?
        .font_family(font)?
        .radius(8.0)
        .colors(color, Color::WHITE)
        .shadow(Shadow::DEFAULT)
        .on_click(handler)
        .into_widget_sized(w, 52.0)?
        .position(x, y);
    let raw = btn.as_ptr();
    children.add(btn)?;
    Ok(raw)
}

// Layout. Mirrors C `OnResize`: proportional and fills the window, insets
// for the device safe area (notch / Dynamic Island), and uses a width-based
// `narrow` arrangement — a portrait phone stacks the header labels + the
// three buttons full-width, while a wide panel (desktop OR landscape phone)
// lays them out in rows so the buttons never run off a short screen.
fn relayout(state: &mut State, win_w: i32, win_h: i32) {
    if win_w <= 0 || win_h <= 0 {
        return;
    }
    // `compact` only shrinks fonts/padding on small screens.
    let compact = win_w < 700 || win_h < 600;
    let pad = if compact { 14.0 } else { 24.0 };
    // Keep clear of the notch / Dynamic Island / home indicator.
    let safe = Screen::safe_area();
    // The safe-area top already clears the notch, so add only a small extra
    // margin there (a full `pad` on top looked too gappy on a phone).
    let top_gap = if safe.top > 0 { 4.0 } else { pad };
    let panel_x = pad + safe.left as f32;
    let panel_y = top_gap + safe.top as f32;
    let panel_w = win_w as f32 - panel_x - pad - safe.right as f32;
    let panel_h = win_h as f32 - panel_y - pad - safe.bottom as f32;
    if panel_w <= 0.0 || panel_h <= 0.0 {
        return;
    }
    // Width-based arrangement, independent of `compact`.
    let narrow = panel_w < 520.0;

    let set_pos = |w: *mut mocida::sys::UIWidget, x: f32, y: f32| unsafe {
        if !w.is_null() {
            mocida::sys::UIWidget_SetPosition(w, x, y);
        }
    };
    let set_size = |w: *mut mocida::sys::UIWidget, x: f32, y: f32| unsafe {
        if !w.is_null() {
            mocida::sys::UIWidget_SetSize(w, x, y);
        }
    };
    let set_text_font = |w: *mut mocida::sys::UIWidget, sz: f32| unsafe {
        if !w.is_null() {
            let t = (*w).data as *mut mocida::sys::UIText;
            if !t.is_null() {
                mocida::sys::UIText_SetFontSize(t, sz);
            }
        }
    };
    let set_btn_font = |w: *mut mocida::sys::UIWidget, sz: f32| unsafe {
        if !w.is_null() {
            let b = (*w).data as *mut mocida::sys::UIButton;
            if !b.is_null() {
                mocida::sys::UIButton_SetFontSize(b, sz);
            }
        }
    };

    // Readable, responsive font sizes (smaller on compact, never unreadable).
    set_text_font(state.title, if compact { 22.0 } else { 28.0 });
    set_text_font(state.fps_label_w, if compact { 15.0 } else { 18.0 });
    set_text_font(state.target_label_w, if compact { 15.0 } else { 18.0 });
    set_text_font(state.aa_label_w, if compact { 15.0 } else { 18.0 });
    set_text_font(state.footer, if compact { 12.0 } else { 14.0 });
    set_btn_font(state.fps_btn, if compact { 15.0 } else { 18.0 });
    set_btn_font(state.aa_btn, if compact { 15.0 } else { 18.0 });
    set_btn_font(state.trim_btn, if compact { 15.0 } else { 18.0 });

    // White background card.
    set_pos(state.panel, panel_x, panel_y);
    set_size(state.panel, panel_w, panel_h);

    let inset = if compact { 14.0 } else { 24.0 };
    let hx = panel_x + inset;

    // Header: stack the three stat labels on a narrow panel, row otherwise.
    set_pos(state.title, hx, panel_y + if compact { 12.0 } else { 20.0 });
    let stat_y = panel_y + if compact { 44.0 } else { 66.0 };
    let header_bottom;
    if narrow {
        set_pos(state.fps_label_w, hx, stat_y);
        set_pos(state.target_label_w, hx, stat_y + 22.0);
        set_pos(state.aa_label_w, hx, stat_y + 44.0);
        header_bottom = stat_y + 66.0;
    } else {
        set_pos(state.fps_label_w, hx, stat_y);
        set_pos(state.target_label_w, hx + 132.0, stat_y);
        set_pos(state.aa_label_w, hx + 300.0, stat_y);
        header_bottom = stat_y + 34.0;
    }

    // Cards: a row that FILLS the panel width — four equal columns. Height
    // tracks width (3:2-ish), capped so it never dominates a tall phone.
    let gap = if compact { 10.0 } else { 16.0 };
    let cards_row_y = header_bottom + if compact { 12.0 } else { 24.0 };
    let mut card_w = (panel_w - 2.0 * inset - 3.0 * gap) / 4.0;
    if card_w < 1.0 {
        card_w = 1.0;
    }
    let mut cards_row_h = card_w * 0.66;
    if cards_row_h > 180.0 {
        cards_row_h = 180.0;
    }
    let fixed_w = [card_w, card_w, card_w, card_w];
    let mut cursor_x = hx;
    for (i, w) in fixed_w.iter().enumerate() {
        let is_orange = i == 3;
        let card = state.cards[i];
        if !card.is_null() {
            if is_orange && state.orange_dragged {
                // Clamp the user's drop into the new viewport.
                let (cw, ch) = unsafe {
                    let w_ptr = (*card).width;
                    let h_ptr = (*card).height;
                    let cw = if w_ptr.is_null() { *w } else { *w_ptr };
                    let ch = if h_ptr.is_null() { cards_row_h } else { *h_ptr };
                    (cw, ch)
                };
                let (mut cx, mut cy) = unsafe { ((*card).x, (*card).y) };
                if cx + cw > win_w as f32 {
                    cx = win_w as f32 - cw;
                }
                if cy + ch > win_h as f32 {
                    cy = win_h as f32 - ch;
                }
                cx = cx.max(0.0);
                cy = cy.max(0.0);
                set_pos(card, cx, cy);
            } else {
                set_pos(card, cursor_x, cards_row_y);
                set_size(card, *w, cards_row_h);
            }
        }
        if is_orange && !state.drag_hit.is_null() {
            if state.orange_dragged && !state.cards[3].is_null() {
                unsafe {
                    let cx = (*state.cards[3]).x;
                    let cy = (*state.cards[3]).y;
                    set_pos(state.drag_hit, cx, cy);
                }
                set_size(state.drag_hit, *w, cards_row_h);
            } else {
                set_pos(state.drag_hit, cursor_x, cards_row_y);
                set_size(state.drag_hit, *w, cards_row_h);
            }
            unsafe {
                let area = (*state.drag_hit).data as *mut mocida::sys::UIMouseArea;
                if !area.is_null() {
                    mocida::sys::UIMouseArea_SetDragBounds(
                        area,
                        0.0,
                        0.0,
                        win_w as f32,
                        win_h as f32,
                    );
                }
            }
        }
        cursor_x += w + gap;
    }

    // Buttons: stack full-width on a narrow panel, three-across otherwise.
    let btn_row_y = cards_row_y + cards_row_h + if compact { 18.0 } else { 50.0 };
    let btn_h = if compact { 46.0 } else { 52.0 };
    let full_w = panel_w - 2.0 * inset;
    if narrow {
        set_pos(state.fps_btn, hx, btn_row_y);
        set_size(state.fps_btn, full_w, btn_h);
        set_pos(state.aa_btn, hx, btn_row_y + (btn_h + gap));
        set_size(state.aa_btn, full_w, btn_h);
        set_pos(state.trim_btn, hx, btn_row_y + 2.0 * (btn_h + gap));
        set_size(state.trim_btn, full_w, btn_h);
    } else {
        let btn_w = (full_w - 2.0 * gap) / 3.0;
        set_pos(state.fps_btn, hx, btn_row_y);
        set_size(state.fps_btn, btn_w, btn_h);
        set_pos(state.aa_btn, hx + btn_w + gap, btn_row_y);
        set_size(state.aa_btn, btn_w, btn_h);
        set_pos(state.trim_btn, hx + 2.0 * (btn_w + gap), btn_row_y);
        set_size(state.trim_btn, btn_w, btn_h);
    }

    // Footer: wrap the hint to the panel width and anchor near the bottom.
    unsafe {
        if !state.footer.is_null() {
            let t = (*state.footer).data as *mut mocida::sys::UIText;
            if !t.is_null() {
                mocida::sys::UIText_SetWrapWidth(t, full_w as i32);
            }
        }
    }
    set_pos(
        state.footer,
        hx,
        panel_y + panel_h - if compact { 56.0 } else { 40.0 },
    );
}

fn set_label_text(label: *mut mocida::sys::UIText, text: &str) {
    if label.is_null() {
        return;
    }
    if let Ok(c) = std::ffi::CString::new(text) {
        unsafe { mocida::sys::UIText_SetText(label, c.as_ptr() as *mut _) };
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut app = App::new("Mocida - Rust demo", WIN_W, WIN_H)?;
    app.set_target_fps(60)
        .set_render_quality(RenderQuality::High)
        .set_aa_mode(AAMode::Coverage)
        .set_background_color(Color::rgb(226, 232, 240));
    let _ = app.set_window_icon("assets/logo.svg");
    search_fonts();
    let arial = get_font("Arial").unwrap_or_default();

    let state: SharedState = Rc::new(RefCell::new(State {
        fps_idx: 1,
        ..State::default()
    }));

    let mut children = Children::new(32)?;

    // Background panel.
    let panel = Rectangle::new()?
        .color(Color::WHITE)
        .radius(14.0)
        .into_widget_sized((WIN_W - 48) as f32, (WIN_H - 48) as f32)?
        .position(24.0, 24.0);
    state.borrow_mut().panel = panel.as_ptr();
    children.add(panel)?;

    // Header labels.
    {
        let mut s = state.borrow_mut();
        // Title.
        let title_text = Text::new("Mocida - demo", 28.0)?
            .font_family(&arial)?
            .color(Color::rgb(15, 23, 42));
        let title_widget = title_text.into_widget()?.position(48.0, 44.0);
        s.title = title_widget.as_ptr();
        children.add(title_widget)?;

        let (fps_t, fps_w) = build_label(
            &mut children,
            "FPS: 60",
            18.0,
            Color::rgb(71, 85, 105),
            &arial,
            48.0,
            90.0,
        )?;
        s.fps_label = fps_t;
        s.fps_label_w = fps_w;

        // Target + AA both use UI_WRAP_FIT so their longest values fit
        // into a fixed slot. The C demo does the same.
        let target_text = Text::new("Target: 60 FPS", 18.0)?
            .font_family(&arial)?
            .color(Color::rgb(71, 85, 105))
            .wrap_to_bounds(true)
            .wrap_mode(mocida::WrapMode::Fit);
        s.target_label = target_text.as_ptr();
        let target_widget = target_text.into_widget_sized(170.0, 28.0)?.position(180.0, 90.0);
        s.target_label_w = target_widget.as_ptr();
        children.add(target_widget)?;

        let aa_text = Text::new("AA: COVERAGE", 18.0)?
            .font_family(&arial)?
            .color(Color::rgb(71, 85, 105))
            .wrap_to_bounds(true)
            .wrap_mode(mocida::WrapMode::Fit);
        s.aa_label = aa_text.as_ptr();
        let aa_widget = aa_text.into_widget_sized(160.0, 28.0)?.position(360.0, 90.0);
        s.aa_label_w = aa_widget.as_ptr();
        children.add(aa_widget)?;
    }

    // Three static cards + one draggable orange card.
    {
        let mut s = state.borrow_mut();
        s.cards[0] = build_card(&mut children, 48.0, 160.0, 240.0, 160.0, Color::rgb(59, 130, 246))?;
        s.cards[1] = build_card(&mut children, 312.0, 160.0, 240.0, 160.0, Color::rgb(34, 197, 94))?;
        s.cards[2] = build_card(&mut children, 576.0, 160.0, 240.0, 160.0, Color::rgb(168, 85, 247))?;
        let orange = build_card(&mut children, 840.0, 160.0, 140.0, 160.0, Color::rgb(251, 146, 60))?;
        s.cards[3] = orange;
    }

    // Mouse area sized to match the orange card, with dragTarget so
    // both move together.
    let orange_ptr = state.borrow().cards[3];
    let drag_state = state.clone();
    let area = MouseArea::new()?
        .draggable(true)
        .drag_bounds(0.0, 0.0, WIN_W as f32, WIN_H as f32)
        .on(MouseAreaEvent::DragStart, move |_| {
            drag_state.borrow_mut().orange_dragged = true;
        });
    // `MouseArea::drag_target` wants `&Widget`, but the orange card
    // already moved into Children — call the raw setter with the
    // pointer we kept.
    unsafe { mocida::sys::UIMouseArea_SetDragTarget(area.as_ptr(), orange_ptr) };
    let hit = area
        .into_widget_sized(140.0, 160.0)?
        .position(840.0, 160.0)
        .z_index(100);
    state.borrow_mut().drag_hit = hit.as_ptr();
    children.add(hit)?;

    // Action buttons. Each callback rewrites its own label.
    let fps_state = state.clone();
    let fps_app_ptr = app.as_ptr();
    let fps_btn = build_action_button(
        &mut children,
        "FPS Target: 60",
        &arial,
        Color::rgb(59, 130, 246),
        48.0,
        380.0,
        220.0,
        move |btn| {
            let mut s = fps_state.borrow_mut();
            s.fps_idx = (s.fps_idx + 1) % FPS_TARGETS.len();
            let (fps, label) = FPS_TARGETS[s.fps_idx];
            unsafe { mocida::sys::UIApp_SetTargetFPS(fps_app_ptr, fps) };
            set_label_text(s.target_label, &format!("Target: {} FPS", label));
            let _ = btn.set_text(&format!("FPS Target: {}", label));
        },
    )?;
    state.borrow_mut().fps_btn = fps_btn;

    let aa_state = state.clone();
    let aa_app_ptr = app.as_ptr();
    let aa_btn = build_action_button(
        &mut children,
        "AA Mode: COVERAGE",
        &arial,
        Color::rgb(34, 197, 94),
        288.0,
        380.0,
        240.0,
        move |btn| {
            let mut s = aa_state.borrow_mut();
            s.aa_idx = (s.aa_idx + 1) % AA_MODES.len();
            let (mode, label) = AA_MODES[s.aa_idx];
            unsafe {
                mocida::sys::UIApp_SetAAMode(aa_app_ptr, mocida::sys::UIAAMode(mode as u32))
            };
            set_label_text(s.aa_label, &format!("AA: {}", label));
            let _ = btn.set_text(&format!("AA Mode: {}", label));
        },
    )?;
    state.borrow_mut().aa_btn = aa_btn;

    let trim_app_ptr = app.as_ptr();
    let trim_btn = build_action_button(
        &mut children,
        "Trim caches",
        &arial,
        Color::rgb(168, 85, 247),
        548.0,
        380.0,
        180.0,
        move |_btn| {
            unsafe { mocida::sys::UIApp_TrimCaches(trim_app_ptr) };
            eprintln!("[demo] caches trimmed — GPU/CPU memory released");
        },
    )?;
    state.borrow_mut().trim_btn = trim_btn;

    // Footer hint.
    let (_footer_t, footer_w) = build_label(
        &mut children,
        "Drag the orange card. Click the buttons to toggle FPS / AA / trim caches.",
        14.0,
        Color::rgb(100, 116, 139),
        &arial,
        48.0,
        (WIN_H - 64) as f32,
    )?;
    state.borrow_mut().footer = footer_w;

    app.set_children(children);
    app.set_background_color(Color::rgb(226, 232, 240));

    // Live FPS readout.
    let fps_state_event = state.clone();
    app.on_event(Event::FramerateChanged, move |data| {
        let s = fps_state_event.borrow();
        set_label_text(s.fps_label, &format!("FPS: {:.0}", data.fps()));
    });

    // Fluid resize.
    let resize_state = state.clone();
    app.on_resize(move |w, h| {
        relayout(&mut resize_state.borrow_mut(), w, h);
    });

    // Match the C demo: run the resize hook once so the initial
    // layout uses the same path as later resizes.
    relayout(&mut state.borrow_mut(), WIN_W, WIN_H);

    app.show().run();
    Ok(())
}
