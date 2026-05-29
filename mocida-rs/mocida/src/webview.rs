//! `UIWebView` — embedded WebView2 widget (Windows).

use std::ffi::{c_void, CStr, CString};

use mocida_sys as sys;

use crate::color::Color;
use crate::error::{Error, Result};
use crate::widget::Widget;

/// Which WebView2 child process triggered the failure callback.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum WebViewProcessKind {
    /// Fatal: full environment dies.
    Browser = 0,
    /// Page blanked; can reload.
    Renderer = 1,
    /// Hung but not crashed.
    RendererUnresponsive = 2,
    /// Per-frame renderer.
    FrameRenderer = 3,
    /// Utility process.
    Utility = 4,
    /// Sandbox process.
    Sandbox = 5,
    /// GPU process (Chromium will retry).
    Gpu = 6,
    /// Anything else.
    Other = 7,
}

impl WebViewProcessKind {
    fn from_raw(raw: sys::UIWebViewProcessKind) -> Self {
        match raw.0 {
            0 => Self::Browser,
            1 => Self::Renderer,
            2 => Self::RendererUnresponsive,
            3 => Self::FrameRenderer,
            4 => Self::Utility,
            5 => Self::Sandbox,
            6 => Self::Gpu,
            _ => Self::Other,
        }
    }
}

/// Decision returned by [`WebView::on_request`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RequestDecision {
    /// Let the request proceed.
    Allow,
    /// Reject the request (browser sees an aborted response).
    Block,
}

/// Embedded WebView2 widget. Windows-only at runtime — calls compile
/// everywhere but only do anything when linked against a Windows
/// mocida build.
pub struct WebView {
    ptr: *mut sys::UIWebView,
    moved: bool,
    ready_cb: Option<Box<ReadyState>>,
    url_change_cb: Option<Box<UrlChangeState>>,
    loading_cb: Option<Box<LoadingState>>,
    process_failed_cb: Option<Box<ProcessFailedState>>,
    request_cb: Option<Box<RequestState>>,
}

struct ReadyState {
    handler: Box<dyn FnMut() + 'static>,
}

struct UrlChangeState {
    handler: Box<dyn FnMut(&str) + 'static>,
}

struct LoadingState {
    handler: Box<dyn FnMut(bool) + 'static>,
}

struct ProcessFailedState {
    handler: Box<dyn FnMut(WebViewProcessKind) + 'static>,
}

struct RequestState {
    handler: Box<dyn FnMut(&str) -> RequestDecision + 'static>,
}

impl WebView {
    /// Creates a webview pointed at `initial_url`. Pass `None` to
    /// start blank.
    pub fn new(initial_url: Option<&str>) -> Result<Self> {
        let url_c = initial_url.map(CString::new).transpose()?;
        let url_ptr = url_c.as_ref().map(|c| c.as_ptr()).unwrap_or(std::ptr::null());
        let ptr = unsafe { sys::UIWebView_Create(url_ptr) };
        if ptr.is_null() {
            return Err(Error::Null("UIWebView_Create"));
        }
        Ok(Self {
            ptr,
            moved: false,
            ready_cb: None,
            url_change_cb: None,
            loading_cb: None,
            process_failed_cb: None,
            request_cb: None,
        })
    }

    /// Navigates to a new URL.
    pub fn navigate(&mut self, url: &str) -> Result<()> {
        let c = CString::new(url)?;
        unsafe { sys::UIWebView_Navigate(self.ptr, c.as_ptr()) };
        Ok(())
    }

    /// Returns the current URL.
    pub fn url(&self) -> Option<String> {
        let p = unsafe { sys::UIWebView_GetUrl(self.ptr) };
        if p.is_null() {
            None
        } else {
            unsafe { CStr::from_ptr(p) }.to_str().ok().map(str::to_owned)
        }
    }

    /// Reloads the current page.
    pub fn reload(&mut self) {
        unsafe { sys::UIWebView_Reload(self.ptr) };
    }

    /// Navigates back in history.
    pub fn go_back(&mut self) {
        unsafe { sys::UIWebView_GoBack(self.ptr) };
    }

    /// Navigates forward in history.
    pub fn go_forward(&mut self) {
        unsafe { sys::UIWebView_GoForward(self.ptr) };
    }

    /// Spins up a dedicated WebView2 environment for this view.
    pub fn isolated(self, enabled: bool) -> Self {
        unsafe { sys::UIWebView_SetIsolated(self.ptr, enabled as i32) };
        self
    }

    /// Switches to DirectComposition rendering. Must be called before
    /// the first render.
    pub fn composition_mode(self, enabled: bool) -> Self {
        unsafe { sys::UIWebView_SetCompositionMode(self.ptr, enabled as i32) };
        self
    }

    /// Sets the corner radius of the webview surface.
    pub fn radius(self, radius: f32) -> Self {
        unsafe { sys::UIWebView_SetRadius(self.ptr, radius) };
        self
    }

    /// Frames the webview with a colored border.
    pub fn border(self, color: Color, width: f32) -> Self {
        unsafe { sys::UIWebView_SetBorder(self.ptr, color.into_raw(), width) };
        self
    }

    /// Sets the User-Agent header.
    pub fn user_agent(self, ua: &str) -> Result<Self> {
        let c = CString::new(ua)?;
        unsafe { sys::UIWebView_SetUserAgent(self.ptr, c.as_ptr()) };
        Ok(self)
    }

    /// Enables / disables F12 developer tools.
    pub fn devtools_enabled(self, enabled: bool) -> Self {
        unsafe { sys::UIWebView_SetDevToolsEnabled(self.ptr, enabled as i32) };
        self
    }

    /// Enables / disables right-click context menus.
    pub fn context_menus_enabled(self, enabled: bool) -> Self {
        unsafe { sys::UIWebView_SetContextMenusEnabled(self.ptr, enabled as i32) };
        self
    }

    /// Returns the default `additionalBrowserArguments` string mocida
    /// uses.
    pub fn default_browser_arguments() -> Option<String> {
        let p = unsafe { sys::UIWebView_GetDefaultBrowserArguments() };
        if p.is_null() {
            None
        } else {
            unsafe { CStr::from_ptr(p) }.to_str().ok().map(str::to_owned)
        }
    }

    /// Replaces the browser arguments entirely.
    pub fn browser_arguments(self, args: &str) -> Result<Self> {
        let c = CString::new(args)?;
        unsafe { sys::UIWebView_SetBrowserArguments(self.ptr, c.as_ptr()) };
        Ok(self)
    }

    /// Appends extra browser arguments.
    pub fn append_browser_arguments(self, extra: &str) -> Result<Self> {
        let c = CString::new(extra)?;
        unsafe { sys::UIWebView_AppendBrowserArguments(self.ptr, c.as_ptr()) };
        Ok(self)
    }

    /// Registers a JavaScript snippet to execute on every page load.
    pub fn add_init_script(&mut self, js: &str) -> Result<()> {
        let c = CString::new(js)?;
        unsafe { sys::UIWebView_AddInitScript(self.ptr, c.as_ptr()) };
        Ok(())
    }

    /// Runs a JS snippet once in the currently loaded page.
    pub fn execute_script(&mut self, js: &str) -> Result<()> {
        let c = CString::new(js)?;
        unsafe { sys::UIWebView_ExecuteScript(self.ptr, c.as_ptr()) };
        Ok(())
    }

    /// Clears every cookie stored by this profile.
    pub fn clear_cookies(&mut self) {
        unsafe { sys::UIWebView_ClearCookies(self.ptr) };
    }

    /// Registers a ready callback (fires once the browser is up).
    pub fn on_ready<F>(mut self, handler: F) -> Self
    where
        F: FnMut() + 'static,
    {
        let state = Box::new(ReadyState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const ReadyState as *mut c_void;
        unsafe { sys::UIWebView_OnReady(self.ptr, Some(ready_trampoline), userdata) };
        self.ready_cb = Some(state);
        self
    }

    /// Subscribes to URL-change notifications.
    pub fn on_url_change<F>(mut self, handler: F) -> Self
    where
        F: FnMut(&str) + 'static,
    {
        let state = Box::new(UrlChangeState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const UrlChangeState as *mut c_void;
        unsafe { sys::UIWebView_OnUrlChange(self.ptr, Some(url_change_trampoline), userdata) };
        self.url_change_cb = Some(state);
        self
    }

    /// Subscribes to loading-state notifications.
    pub fn on_loading_change<F>(mut self, handler: F) -> Self
    where
        F: FnMut(bool) + 'static,
    {
        let state = Box::new(LoadingState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const LoadingState as *mut c_void;
        unsafe { sys::UIWebView_OnLoadingChange(self.ptr, Some(loading_trampoline), userdata) };
        self.loading_cb = Some(state);
        self
    }

    /// Subscribes to process-failure notifications.
    pub fn on_process_failed<F>(mut self, handler: F) -> Self
    where
        F: FnMut(WebViewProcessKind) + 'static,
    {
        let state = Box::new(ProcessFailedState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const ProcessFailedState as *mut c_void;
        unsafe { sys::UIWebView_OnProcessFailed(self.ptr, Some(process_failed_trampoline), userdata) };
        self.process_failed_cb = Some(state);
        self
    }

    /// Registers a per-request interceptor. The closure decides
    /// whether to allow or block each outgoing request.
    pub fn on_request<F>(mut self, handler: F) -> Self
    where
        F: FnMut(&str) -> RequestDecision + 'static,
    {
        let state = Box::new(RequestState {
            handler: Box::new(handler),
        });
        let userdata = Box::as_ref(&state) as *const RequestState as *mut c_void;
        unsafe { sys::UIWebView_OnRequest(self.ptr, Some(request_trampoline), userdata) };
        self.request_cb = Some(state);
        self
    }

    /// Borrow the raw `UIWebView*`.
    #[inline]
    pub fn as_ptr(&self) -> *mut sys::UIWebView {
        self.ptr
    }

    /// Lift into a [`Widget`]. WebViews **must** have an explicit
    /// size, so [`WebView::into_widget_sized`] is usually what you want.
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

impl Drop for WebView {
    fn drop(&mut self) {
        if !self.moved && !self.ptr.is_null() {
            unsafe { sys::UIWebView_Destroy(self.ptr) };
            return;
        }
        for slot in [
            self.ready_cb.take().map(Box::into_raw).map(|p| p as *mut ()),
            self.url_change_cb.take().map(Box::into_raw).map(|p| p as *mut ()),
            self.loading_cb.take().map(Box::into_raw).map(|p| p as *mut ()),
            self.process_failed_cb.take().map(Box::into_raw).map(|p| p as *mut ()),
            self.request_cb.take().map(Box::into_raw).map(|p| p as *mut ()),
        ]
        .into_iter()
        .flatten()
        {
            let _ = slot; // intentional leak
        }
    }
}

extern "C" fn ready_trampoline(_wv: *mut sys::UIWebView, userdata: *mut c_void) {
    if userdata.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut ReadyState) };
    (state.handler)();
}

extern "C" fn url_change_trampoline(
    _wv: *mut sys::UIWebView,
    url: *const std::ffi::c_char,
    userdata: *mut c_void,
) {
    if userdata.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut UrlChangeState) };
    let s = if url.is_null() {
        ""
    } else {
        match unsafe { CStr::from_ptr(url) }.to_str() {
            Ok(s) => s,
            Err(_) => return,
        }
    };
    (state.handler)(s);
}

extern "C" fn loading_trampoline(_wv: *mut sys::UIWebView, loading: i32, userdata: *mut c_void) {
    if userdata.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut LoadingState) };
    (state.handler)(loading != 0);
}

extern "C" fn process_failed_trampoline(
    _wv: *mut sys::UIWebView,
    kind: sys::UIWebViewProcessKind,
    userdata: *mut c_void,
) {
    if userdata.is_null() {
        return;
    }
    let state = unsafe { &mut *(userdata as *mut ProcessFailedState) };
    (state.handler)(WebViewProcessKind::from_raw(kind));
}

extern "C" fn request_trampoline(
    _wv: *mut sys::UIWebView,
    url: *const std::ffi::c_char,
    userdata: *mut c_void,
) -> i32 {
    if userdata.is_null() {
        return 0;
    }
    let state = unsafe { &mut *(userdata as *mut RequestState) };
    let s = if url.is_null() {
        ""
    } else {
        match unsafe { CStr::from_ptr(url) }.to_str() {
            Ok(s) => s,
            Err(_) => return 0,
        }
    };
    match (state.handler)(s) {
        RequestDecision::Allow => 0,
        RequestDecision::Block => 1,
    }
}
