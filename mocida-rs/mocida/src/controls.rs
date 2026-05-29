//! Interactive controls (`controls.h`): checkbox, slider, progress bar,
//! spinner, switch, radio button.

use std::ffi::c_void;

use mocida_sys as sys;

use crate::color::Color;
use crate::cursor::Cursor;
use crate::error::{Error, Result};
use crate::widget::Widget;

// =====================================================================
// Checkbox
// =====================================================================

/// Two-state toggle drawn as a rounded box with a checkmark.
pub struct Checkbox {
    ptr: *mut sys::UICheckbox,
    moved: bool,
    on_change: Option<Box<CheckboxState>>,
}

struct CheckboxState {
    handler: Box<dyn FnMut(bool) + 'static>,
}

impl Checkbox {
    /// Creates a checkbox with the given initial state.
    pub fn new(initial: bool) -> Result<Self> {
        let ptr = unsafe { sys::UICheckbox_Create(initial as i32) };
        if ptr.is_null() {
            return Err(Error::Null("UICheckbox_Create"));
        }
        Ok(Self {
            ptr,
            moved: false,
            on_change: None,
        })
    }

    /// Programmatic state setter.
    pub fn set_checked(self, checked: bool) -> Self {
        unsafe { sys::UICheckbox_SetChecked(self.ptr, checked as i32) };
        self
    }

    /// Convenience: sets both the box and check fill colors.
    pub fn colors(self, box_color: Color, check: Color) -> Self {
        unsafe { sys::UICheckbox_SetColors(self.ptr, box_color.into_raw(), check.into_raw()) };
        self
    }

    /// Sets only the box (background) color.
    pub fn box_color(self, color: Color) -> Self {
        unsafe { sys::UICheckbox_SetBoxColor(self.ptr, color.into_raw()) };
        self
    }

    /// Sets only the checkmark color.
    pub fn check_color(self, color: Color) -> Self {
        unsafe { sys::UICheckbox_SetCheckColor(self.ptr, color.into_raw()) };
        self
    }

    /// Sets the border color and thickness.
    pub fn border(self, color: Color, width: f32) -> Self {
        unsafe { sys::UICheckbox_SetBorder(self.ptr, color.into_raw(), width) };
        self
    }

    /// Sets the box corner radius.
    pub fn radius(self, radius: f32) -> Self {
        unsafe { sys::UICheckbox_SetRadius(self.ptr, radius) };
        self
    }

    /// Toggle animation duration (0 = instant).
    pub fn anim_ms(self, ms: i32) -> Self {
        unsafe { sys::UICheckbox_SetAnimMs(self.ptr, ms) };
        self
    }

    /// Cursor shown while hovering.
    pub fn cursor(self, cursor: Cursor) -> Self {
        unsafe { sys::UICheckbox_SetCursor(self.ptr, cursor.into_raw()) };
        self
    }

    /// Registers an `on_change` handler. Receives the new checked state.
    pub fn on_change<F>(mut self, handler: F) -> Self
    where
        F: FnMut(bool) + 'static,
    {
        let state = Box::new(CheckboxState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const CheckboxState as *mut c_void;
        unsafe { sys::UICheckbox_OnChange(self.ptr, Some(checkbox_trampoline), userdata) };
        self.on_change = Some(state);
        self
    }

    /// Borrow the raw `UICheckbox*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UICheckbox {
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

impl Drop for Checkbox {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UICheckbox_Destroy(self.ptr) };
            return;
        }
        if let Some(state) = self.on_change.take() {
            std::mem::forget(state);
        }
    }
}

extern "C" fn checkbox_trampoline(_c: *mut sys::UICheckbox, checked: i32, userdata: *mut c_void) {
    if userdata.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut CheckboxState) };
    (state.handler)(checked != 0);
}

// =====================================================================
// Slider
// =====================================================================

/// Horizontal slider with a draggable knob.
pub struct Slider {
    ptr: *mut sys::UISlider,
    moved: bool,
    on_change: Option<Box<SliderState>>,
}

struct SliderState {
    handler: Box<dyn FnMut(f32) + 'static>,
}

impl Slider {
    /// Creates a slider with the given range and initial value.
    pub fn new(min: f32, max: f32, initial: f32) -> Result<Self> {
        let ptr = unsafe { sys::UISlider_Create(min, max, initial) };
        if ptr.is_null() {
            return Err(Error::Null("UISlider_Create"));
        }
        Ok(Self {
            ptr,
            moved: false,
            on_change: None,
        })
    }

    /// Programmatic value setter (clamps into the configured range).
    pub fn set_value(&mut self, value: f32) {
        unsafe { sys::UISlider_SetValue(self.ptr, value) };
    }

    /// Updates the range.
    pub fn range(self, min: f32, max: f32) -> Self {
        unsafe { sys::UISlider_SetRange(self.ptr, min, max) };
        self
    }

    /// Sets all three slider colors at once.
    pub fn colors(self, track: Color, fill: Color, knob: Color) -> Self {
        unsafe {
            sys::UISlider_SetColors(
                self.ptr,
                track.into_raw(),
                fill.into_raw(),
                knob.into_raw(),
            )
        };
        self
    }

    /// Sets only the track (background rail) color.
    pub fn track_color(self, color: Color) -> Self {
        unsafe { sys::UISlider_SetTrackColor(self.ptr, color.into_raw()) };
        self
    }

    /// Sets only the fill (active portion) color.
    pub fn fill_color(self, color: Color) -> Self {
        unsafe { sys::UISlider_SetFillColor(self.ptr, color.into_raw()) };
        self
    }

    /// Sets only the knob color.
    pub fn knob_color(self, color: Color) -> Self {
        unsafe { sys::UISlider_SetKnobColor(self.ptr, color.into_raw()) };
        self
    }

    /// Sets the track height in pixels.
    pub fn track_height(self, height: f32) -> Self {
        unsafe { sys::UISlider_SetTrackHeight(self.ptr, height) };
        self
    }

    /// Sets the knob radius in pixels.
    pub fn knob_radius(self, radius: f32) -> Self {
        unsafe { sys::UISlider_SetKnobRadius(self.ptr, radius) };
        self
    }

    /// Cursor shown while hovering.
    pub fn cursor(self, cursor: Cursor) -> Self {
        unsafe { sys::UISlider_SetCursor(self.ptr, cursor.into_raw()) };
        self
    }

    /// Registers an `on_change` handler.
    pub fn on_change<F>(mut self, handler: F) -> Self
    where
        F: FnMut(f32) + 'static,
    {
        let state = Box::new(SliderState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const SliderState as *mut c_void;
        unsafe { sys::UISlider_OnChange(self.ptr, Some(slider_trampoline), userdata) };
        self.on_change = Some(state);
        self
    }

    /// Borrow the raw `UISlider*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UISlider {
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

impl Drop for Slider {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UISlider_Destroy(self.ptr) };
            return;
        }
        if let Some(state) = self.on_change.take() {
            std::mem::forget(state);
        }
    }
}

extern "C" fn slider_trampoline(_s: *mut sys::UISlider, value: f32, userdata: *mut c_void) {
    if userdata.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut SliderState) };
    (state.handler)(value);
}

// =====================================================================
// ProgressBar
// =====================================================================

/// Read-only progress indicator.
pub struct ProgressBar {
    ptr: *mut sys::UIProgressBar,
    moved: bool,
}

impl ProgressBar {
    /// Creates a progress bar with the given initial value in `0.0..=1.0`.
    pub fn new(initial: f32) -> Result<Self> {
        let ptr = unsafe { sys::UIProgressBar_Create(initial) };
        if ptr.is_null() {
            return Err(Error::Null("UIProgressBar_Create"));
        }
        Ok(Self { ptr, moved: false })
    }

    /// Programmatic value setter.
    pub fn set_value(&mut self, value: f32) {
        unsafe { sys::UIProgressBar_SetValue(self.ptr, value) };
    }

    /// Toggles the back-and-forth indeterminate animation.
    pub fn indeterminate(self, yes: bool) -> Self {
        unsafe { sys::UIProgressBar_SetIndeterminate(self.ptr, yes as i32) };
        self
    }

    /// Sets both track and fill colors.
    pub fn colors(self, track: Color, fill: Color) -> Self {
        unsafe { sys::UIProgressBar_SetColors(self.ptr, track.into_raw(), fill.into_raw()) };
        self
    }

    /// Sets only the track (background) color.
    pub fn track_color(self, color: Color) -> Self {
        unsafe { sys::UIProgressBar_SetTrackColor(self.ptr, color.into_raw()) };
        self
    }

    /// Sets only the fill (filled portion) color.
    pub fn fill_color(self, color: Color) -> Self {
        unsafe { sys::UIProgressBar_SetFillColor(self.ptr, color.into_raw()) };
        self
    }

    /// Sets the corner radius.
    pub fn radius(self, radius: f32) -> Self {
        unsafe { sys::UIProgressBar_SetRadius(self.ptr, radius) };
        self
    }

    /// Borrow the raw `UIProgressBar*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UIProgressBar {
        self.ptr
    }

    /// Lift into a [`Widget`] with an explicit size.
    pub fn into_widget_sized(mut self, width: f32, height: f32) -> Result<Widget> {
        self.moved = true;
        unsafe { Widget::with_size(self.ptr as *mut _, width, height) }
    }
}

impl Drop for ProgressBar {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UIProgressBar_Destroy(self.ptr) };
        }
    }
}

// =====================================================================
// Spinner
// =====================================================================

/// Circular loading indicator.
pub struct Spinner {
    ptr: *mut sys::UISpinner,
    moved: bool,
}

impl Spinner {
    /// Creates a spinner with the given outer radius.
    pub fn new(radius: f32) -> Result<Self> {
        let ptr = unsafe { sys::UISpinner_Create(radius) };
        if ptr.is_null() {
            return Err(Error::Null("UISpinner_Create"));
        }
        Ok(Self { ptr, moved: false })
    }

    /// Sets the arc color.
    pub fn color(self, color: Color) -> Self {
        unsafe { sys::UISpinner_SetColor(self.ptr, color.into_raw()) };
        self
    }

    /// Sets the arc stroke width.
    pub fn thickness(self, thickness: f32) -> Self {
        unsafe { sys::UISpinner_SetThickness(self.ptr, thickness) };
        self
    }

    /// Updates the outer radius.
    pub fn radius(self, radius: f32) -> Self {
        unsafe { sys::UISpinner_SetRadius(self.ptr, radius) };
        self
    }

    /// Sets rotation speed in radians per second.
    pub fn speed(self, rad_per_sec: f32) -> Self {
        unsafe { sys::UISpinner_SetSpeed(self.ptr, rad_per_sec) };
        self
    }

    /// Borrow the raw `UISpinner*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UISpinner {
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

impl Drop for Spinner {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UISpinner_Destroy(self.ptr) };
        }
    }
}

// =====================================================================
// Switch
// =====================================================================

/// Boolean toggle drawn as a pill with a sliding knob.
pub struct Switch {
    ptr: *mut sys::UISwitch,
    moved: bool,
    on_change: Option<Box<SwitchState>>,
}

struct SwitchState {
    handler: Box<dyn FnMut(bool) + 'static>,
}

impl Switch {
    /// Creates a switch with the given initial state.
    pub fn new(initial_on: bool) -> Result<Self> {
        let ptr = unsafe { sys::UISwitch_Create(initial_on as i32) };
        if ptr.is_null() {
            return Err(Error::Null("UISwitch_Create"));
        }
        Ok(Self {
            ptr,
            moved: false,
            on_change: None,
        })
    }

    /// Programmatic state setter.
    pub fn set_on(&mut self, on: bool) {
        unsafe { sys::UISwitch_SetOn(self.ptr, on as i32) };
    }

    /// Reads the current state.
    pub fn is_on(&self) -> bool {
        unsafe { sys::UISwitch_IsOn(self.ptr) != 0 }
    }

    /// Sets the three pill colors at once.
    pub fn colors(self, off: Color, on: Color, knob: Color) -> Self {
        unsafe {
            sys::UISwitch_SetColors(self.ptr, off.into_raw(), on.into_raw(), knob.into_raw())
        };
        self
    }

    /// Sets only the off-state track color.
    pub fn off_color(self, color: Color) -> Self {
        unsafe { sys::UISwitch_SetOffColor(self.ptr, color.into_raw()) };
        self
    }

    /// Sets only the on-state track color.
    pub fn on_color(self, color: Color) -> Self {
        unsafe { sys::UISwitch_SetOnColor(self.ptr, color.into_raw()) };
        self
    }

    /// Sets only the knob color.
    pub fn knob_color(self, color: Color) -> Self {
        unsafe { sys::UISwitch_SetKnobColor(self.ptr, color.into_raw()) };
        self
    }

    /// Sets border color and width.
    pub fn border(self, color: Color, width: f32) -> Self {
        unsafe { sys::UISwitch_SetBorder(self.ptr, color.into_raw(), width) };
        self
    }

    /// Knob slide animation duration (0 = instant).
    pub fn anim_ms(self, ms: i32) -> Self {
        unsafe { sys::UISwitch_SetAnimMs(self.ptr, ms) };
        self
    }

    /// Cursor shown while hovering.
    pub fn cursor(self, cursor: Cursor) -> Self {
        unsafe { sys::UISwitch_SetCursor(self.ptr, cursor.into_raw()) };
        self
    }

    /// Registers an `on_change` handler. Receives the new state.
    pub fn on_change<F>(mut self, handler: F) -> Self
    where
        F: FnMut(bool) + 'static,
    {
        let state = Box::new(SwitchState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const SwitchState as *mut c_void;
        unsafe { sys::UISwitch_OnChange(self.ptr, Some(switch_trampoline), userdata) };
        self.on_change = Some(state);
        self
    }

    /// Borrow the raw `UISwitch*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UISwitch {
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

impl Drop for Switch {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UISwitch_Destroy(self.ptr) };
            return;
        }
        if let Some(state) = self.on_change.take() {
            std::mem::forget(state);
        }
    }
}

extern "C" fn switch_trampoline(_sw: *mut sys::UISwitch, on: i32, userdata: *mut c_void) {
    if userdata.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut SwitchState) };
    (state.handler)(on != 0);
}

// =====================================================================
// RadioButton
// =====================================================================

/// Mutually-exclusive radio button. Buttons that share the same
/// `group` pointer behave as one choice.
pub struct RadioButton {
    ptr: *mut sys::UIRadioButton,
    moved: bool,
    on_change: Option<Box<RadioState>>,
}

struct RadioState {
    handler: Box<dyn FnMut(bool) + 'static>,
}

impl RadioButton {
    /// Creates a radio button. Use the same `group` pointer for
    /// buttons that should be mutually exclusive.
    ///
    /// # Safety
    /// `group` must be a stable, non-null pointer shared by every
    /// radio in the same group. It is treated as an opaque identity;
    /// the pointee is never read.
    pub unsafe fn new(group: *mut c_void, initial: bool) -> Result<Self> {
        let ptr = unsafe { sys::UIRadio_Create(group, initial as i32) };
        if ptr.is_null() {
            return Err(Error::Null("UIRadio_Create"));
        }
        Ok(Self {
            ptr,
            moved: false,
            on_change: None,
        })
    }

    /// Programmatic state setter.
    pub fn set_selected(self, selected: bool) -> Self {
        unsafe { sys::UIRadio_SetSelected(self.ptr, selected as i32) };
        self
    }

    /// Reads the current state.
    pub fn is_selected(&self) -> bool {
        unsafe { sys::UIRadio_IsSelected(self.ptr) != 0 }
    }

    /// Sets the disc background and inner-dot colors.
    pub fn colors(self, box_color: Color, dot: Color) -> Self {
        unsafe { sys::UIRadio_SetColors(self.ptr, box_color.into_raw(), dot.into_raw()) };
        self
    }

    /// Sets only the disc (background) color.
    pub fn box_color(self, color: Color) -> Self {
        unsafe { sys::UIRadio_SetBoxColor(self.ptr, color.into_raw()) };
        self
    }

    /// Sets only the inner-dot color.
    pub fn dot_color(self, color: Color) -> Self {
        unsafe { sys::UIRadio_SetDotColor(self.ptr, color.into_raw()) };
        self
    }

    /// Sets the border color and thickness.
    pub fn border(self, color: Color, width: f32) -> Self {
        unsafe { sys::UIRadio_SetBorder(self.ptr, color.into_raw(), width) };
        self
    }

    /// Sets the inner dot size as a fraction of the outer radius.
    pub fn dot_scale(self, scale: f32) -> Self {
        unsafe { sys::UIRadio_SetDotScale(self.ptr, scale) };
        self
    }

    /// Toggle animation duration (0 = instant).
    pub fn anim_ms(self, ms: i32) -> Self {
        unsafe { sys::UIRadio_SetAnimMs(self.ptr, ms) };
        self
    }

    /// Cursor shown while hovering.
    pub fn cursor(self, cursor: Cursor) -> Self {
        unsafe { sys::UIRadio_SetCursor(self.ptr, cursor.into_raw()) };
        self
    }

    /// Registers an `on_change` handler.
    pub fn on_change<F>(mut self, handler: F) -> Self
    where
        F: FnMut(bool) + 'static,
    {
        let state = Box::new(RadioState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const RadioState as *mut c_void;
        unsafe { sys::UIRadio_OnChange(self.ptr, Some(radio_trampoline), userdata) };
        self.on_change = Some(state);
        self
    }

    /// Borrow the raw `UIRadioButton*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UIRadioButton {
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

impl Drop for RadioButton {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UIRadio_Destroy(self.ptr) };
            return;
        }
        if let Some(state) = self.on_change.take() {
            std::mem::forget(state);
        }
    }
}

extern "C" fn radio_trampoline(_r: *mut sys::UIRadioButton, selected: i32, userdata: *mut c_void) {
    if userdata.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut RadioState) };
    (state.handler)(selected != 0);
}
