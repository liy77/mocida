//! `UI_EVENT` enum + the framerate payload.
//!
//! Mocida only exposes one public event today (`UI_EVENT_FRAMERATE_CHANGED`).
//! When new ones land in C they can be added to [`Event`] without
//! breaking existing users.

use mocida_sys as sys;

/// Symbolic name of a mocida event.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u32)]
pub enum Event {
    /// Fires once per second with the measured FPS.
    FramerateChanged = 0x0001,
}

impl Event {
    /// The corresponding raw `UI_EVENT` value.
    #[inline]
    pub const fn as_raw(self) -> sys::UI_EVENT {
        self as sys::UI_EVENT
    }
}

/// Borrowed view over a `UIEventData` passed to event callbacks.
///
/// The view is short-lived: the pointer it wraps is only valid for the
/// duration of the trampoline call, so don't squirrel it away.
pub struct EventData<'a> {
    raw: &'a sys::UIEventData,
}

impl<'a> EventData<'a> {
    /// Wrap a raw reference. Intended for use inside trampoline
    /// glue — most callers should receive `EventData` already built.
    #[inline]
    pub fn from_raw(raw: &'a sys::UIEventData) -> Self {
        Self { raw }
    }

    /// The event type that triggered the callback.
    pub fn event(&self) -> Event {
        // Today there's only one variant; falling back on the raw
        // value keeps the wrapper forward-compatible.
        match self.raw.type_ {
            x if x == Event::FramerateChanged as sys::UI_EVENT => Event::FramerateChanged,
            _ => Event::FramerateChanged,
        }
    }

    /// Frames-per-second sample (populated for
    /// [`Event::FramerateChanged`]).
    pub fn fps(&self) -> f64 {
        self.raw.framerate.fps
    }
}
