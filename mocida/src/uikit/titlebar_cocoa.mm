// titlebar_cocoa.mm — macOS custom-titlebar setup that KEEPS native chrome.
//
// On macOS we must NOT make the NSWindow borderless: that loses the native
// rounded corners, drop shadow, resize behaviour and the traffic-light buttons.
// Instead we keep a normal titled+resizable window (UIWindow_Create leaves the
// BORDERLESS flag off under __APPLE__) and switch it into the standard macOS
// "custom titlebar" configuration:
//
//   styleMask |= NSWindowStyleMaskFullSizeContentView  → the contentView (the
//       SDL Metal layer) extends up under the title bar, so our painted bar
//       fills the whole top edge.
//   titlebarAppearsTransparent = YES                   → the OS title bar is
//       transparent (no grey caption strip over our content).
//   title = @""                                        → no OS title text.
//       (NSWindowTitleVisibilityHidden is iOS-only; on macOS we clear the
//        title string instead, which produces the same visual result.)
//
// This preserves rounded corners, the system drop shadow, edge-resize and the
// traffic-light (close / minimize / zoom) buttons in the top-left. Because the
// traffic-lights are still there, OndaEngine hides its OWN min/max/close buttons
// on macOS (UIWindow_IsMacOS() drives a host signal) and offsets the top bar so
// menus don't sit under the lights.

#import <TargetConditionals.h>
#if defined(__APPLE__) && TARGET_OS_OSX

#import <Cocoa/Cocoa.h>
#include <uikit/menu_bar.h>
#include <uikit/window.h>
#include <SDL3/SDL.h>

static NSWindow* titlebar_nswindow(SDL_Window* w) {
    if (!w) return nil;
    SDL_PropertiesID props = SDL_GetWindowProperties(w);
    return (__bridge NSWindow*)SDL_GetPointerProperty(
        props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
}

extern "C" void UIWindow_ApplyNativeDecorations(SDL_Window* window) {
    NSWindow* win = titlebar_nswindow(window);
    if (!win) return;

    // Run on the main thread (Cocoa UI must not be touched off-main). SDL is
    // already on the main thread during window creation, but guard anyway in
    // case this is re-invoked from elsewhere.
    void (^configure)(void) = ^{
        win.titlebarAppearsTransparent = YES;
        // NSWindowTitleVisibilityHidden is iOS-only. On macOS, clearing the
        // title string produces the same "no OS title text" result.
        win.title                     = @"";
        win.movableByWindowBackground  = NO;  // dragging is handled by the
                                              // app's hit-test region, not the
                                              // whole background.

        // Extend the content under the (now transparent) title bar so our
        // painted bar fills the top edge — while keeping the native frame,
        // shadow, rounded corners, resize and traffic-lights.
        win.styleMask |= NSWindowStyleMaskFullSizeContentView;

        // (No `setContentBorderThickness:forEdge:` here: AppKit rejects
        // that call with NSMaxYEdge on a non-textured window, and
        // switching the window to textured would defeat the point of
        // the custom titlebar's transparent surface. The 28px top
        // offset is instead expressed as `paddingTop: 28` on the
        // app's toolbar via box_spacing_reactive — see toolbar.mui.)
        // Keep the standard traffic-light buttons visible (default), so the
        // user gets native close/minimize/zoom. OndaEngine suppresses its own
        // window-control buttons on macOS via UIWindow_IsMacOS().
        [[win standardWindowButton:NSWindowCloseButton]       setHidden:NO];
        [[win standardWindowButton:NSWindowMiniaturizeButton] setHidden:NO];
        [[win standardWindowButton:NSWindowZoomButton]        setHidden:NO];
    };

    if ([NSThread isMainThread]) {
        configure();
    } else {
        dispatch_sync(dispatch_get_main_queue(), configure);
    }
}

#endif // __APPLE__ && TARGET_OS_OSX
