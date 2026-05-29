//! `UIWidget_WalkTree` — visit every widget in a subtree.

use std::ffi::c_void;

use mocida_sys as sys;

use crate::widget::Widget;

/// Visitor outcome. Mirrors `UIWalkResult`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WalkResult {
    /// Visit the widget and descend into its children.
    Continue,
    /// Visit the widget but don't descend into its children.
    SkipChildren,
    /// Stop the entire walk immediately.
    Stop,
}

impl WalkResult {
    #[inline]
    fn into_raw(self) -> sys::UIWalkResult {
        sys::UIWalkResult(match self {
            WalkResult::Continue => 0,
            WalkResult::SkipChildren => 1,
            WalkResult::Stop => 2,
        })
    }
}

/// Walks a widget subtree starting at `root`. Returns `true` if the
/// walk stopped early via [`WalkResult::Stop`].
pub fn walk_tree<F>(root: &Widget, mut visitor: F) -> bool
where
    F: FnMut(*mut sys::UIWidget, i32) -> WalkResult,
{
    let mut state: TrampolineState<'_> = TrampolineState {
        handler: &mut visitor as &mut dyn FnMut(*mut sys::UIWidget, i32) -> WalkResult,
    };
    let userdata = &mut state as *mut TrampolineState<'_> as *mut c_void;
    let rc =
        unsafe { sys::UIWidget_WalkTree(root.as_ptr(), Some(walk_trampoline), userdata) };
    rc != 0
}

struct TrampolineState<'a> {
    handler: &'a mut dyn FnMut(*mut sys::UIWidget, i32) -> WalkResult,
}

extern "C" fn walk_trampoline(
    widget: *mut sys::UIWidget,
    depth: i32,
    user: *mut c_void,
) -> sys::UIWalkResult {
    if user.is_null() {
        return WalkResult::Continue.into_raw();
    }
    let state = unsafe { &mut *(user as *mut TrampolineState<'_>) };
    (state.handler)(widget, depth).into_raw()
}
