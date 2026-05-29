//! # mocida
//!
//! Safe, idiomatic Rust wrapper over the [Mocida](https://github.com/liy77/mocida)
//! C UI toolkit (`mocida-sys`).
//!
//! Mocida itself is a Windows-first, SDL3-backed UI toolkit written in
//! C. This crate doesn't reimplement any of it — it just wraps the
//! `extern "C"` surface with `Drop`-managed handles, `&str`/`String`
//! input where C wants `char*`, and builder-style chaining.
//!
//! Every public mocida header has a corresponding Rust module — see
//! the workspace `README.md` for the full coverage matrix.
//!
//! ```no_run
//! use mocida::{App, Color, Rectangle, Text};
//!
//! let mut app = App::new("Hello, mocida-rs", 800, 600)?;
//! app.set_background_color(Color::rgb(226, 232, 240));
//!
//! let panel = Rectangle::new()?
//!     .color(Color::WHITE)
//!     .radius(14.0)
//!     .into_widget_sized(752.0, 552.0)?
//!     .position(24.0, 24.0);
//!
//! let title = Text::new("Hello from Rust!", 28.0)?
//!     .color(Color::rgb(15, 23, 42))
//!     .into_widget()?
//!     .position(48.0, 44.0);
//!
//! let mut children = mocida::Children::new(4)?;
//! children.add(panel)?;
//! children.add(title)?;
//! app.set_children(children);
//! app.show().run();
//! # Ok::<(), mocida::Error>(())
//! ```

#![warn(missing_docs)]
#![warn(rust_2018_idioms)]

pub mod alignment;
pub mod anim;
pub mod app;
pub mod arena;
pub mod asset;
pub mod bind;
pub mod button;
pub mod children;
pub mod clipboard;
pub mod color;
pub mod container;
pub mod controls;
pub mod crash;
pub mod cursor;
pub mod debug;
pub mod dialog;
pub mod error;
pub mod event;
pub mod extra;
pub mod file_dialog;
pub mod file_drop;
pub mod image;
pub mod mouse_area;
pub mod overlay;
pub mod popup;
pub mod profile;
pub mod reactive;
pub mod rect;
pub mod shadow;
pub mod sound;
pub mod stack;
pub mod tab;
pub mod text;
pub mod textarea;
pub mod textfield;
pub mod theme;
pub mod video;
pub mod walker;
pub mod webview;
#[cfg(target_os = "windows")]
pub mod webview_dcomp;
pub mod widget;
pub mod window;

pub use alignment::{Align, Alignment, HorizontalAlign, VerticalAlign};
pub use anim::{clear_all as anim_clear_all, Ease};
pub use app::{AAMode, App, RenderDriver, RenderQuality};
pub use arena::Arena;
pub use bind::Binding;
pub use button::{Button, ButtonState, ButtonStyle};
pub use children::Children;
pub use color::Color;
pub use container::{Grid, GridView, ListView, Scroll};
pub use controls::{Checkbox, ProgressBar, RadioButton, Slider, Spinner, Switch};
pub use cursor::Cursor;
pub use debug::{LogLevel, LogSink};
pub use dialog::Dialog;
pub use error::{Error, Result};
pub use event::Event;
pub use file_drop::FileDrop;
pub use image::{FillMode, Image, ImageLoadState};
pub use mouse_area::{MouseArea, MouseAreaEvent, MouseEvent};
pub use overlay::OverlayFlag;
pub use popup::{Dropdown, Menu, Tooltip};
pub use profile::{FrameStats, Scope as ProfileScope};
pub use reactive::{Opaque, Signal, SignalType, SignalValue, Subscription};
pub use rect::Rectangle;
pub use shadow::Shadow;
pub use sound::Sound;
pub use stack::{Stack, StackOrientation};
pub use tab::TabView;
pub use text::{FontStyle, Text, TextHAlign, TextVAlign, WrapMode};
pub use textarea::TextArea;
pub use textfield::TextField;
pub use theme::Theme;
pub use video::Video;
pub use walker::WalkResult;
pub use webview::{RequestDecision, WebView, WebViewProcessKind};
pub use widget::Widget;
pub use window::{Window, WindowDisplayMode};

/// Re-export of the raw `mocida-sys` crate for users who need to
/// reach below the safe layer.
pub use mocida_sys as sys;
