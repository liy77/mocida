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
// traffic-lights are still there, Mocidad hides its OWN min/max/close buttons
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

// Height of the app's painted top bar (UIWindow_SetTitlebarHeight); 0 keeps
// the traffic lights where AppKit puts them (centered in its 28 pt bar).
static int g_titlebarHeight = 0;

// Centers the traffic lights in a `g_titlebarHeight` bar: the titlebar
// container grows to that height and each button is centered in it. AppKit
// lays the buttons out again on resize, key changes and full screen, so this
// runs after each of those too.
static void place_traffic_lights(NSWindow* win) {
    if (g_titlebarHeight <= 0 || (win.styleMask & NSWindowStyleMaskFullScreen)) return;
    NSButton* close = [win standardWindowButton:NSWindowCloseButton];
    NSView* container = close.superview.superview;
    if (!close || !container) return;
    CGFloat height = (CGFloat)g_titlebarHeight;
    NSRect frame = container.frame;
    frame.size.height = height;
    frame.origin.y = NSHeight(win.frame) - height;
    [container setFrame:frame];
    NSWindowButton kinds[] = { NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton };
    for (NSWindowButton kind : kinds) {
        NSButton* b = [win standardWindowButton:kind];
        if (!b) continue;
        NSRect bf = b.frame;
        bf.origin.y = round((NSHeight(b.superview.frame) - NSHeight(bf)) / 2.0);
        [b setFrameOrigin:bf.origin];
    }
}

static void watch_traffic_lights(NSWindow* win) {
    NSNotificationName names[] = {
        NSWindowDidResizeNotification, NSWindowDidEndLiveResizeNotification,
        NSWindowDidBecomeKeyNotification, NSWindowDidResignKeyNotification,
        NSWindowDidExitFullScreenNotification, NSWindowDidChangeScreenNotification,
    };
    // Unretained (this file is MRC): only `win` posts these, so the block
    // never runs after the window is gone.
    void* raw = (void*)win;
    for (NSNotificationName name : names) {
        [[NSNotificationCenter defaultCenter] addObserverForName:name object:win queue:nil
            usingBlock:^(NSNotification*) { place_traffic_lights((NSWindow*)raw); }];
    }
    place_traffic_lights(win);
}

extern "C" void UIWindow_SetTitlebarHeight(int height) {
    g_titlebarHeight = height > 0 ? height : 0;
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
        // user gets native close/minimize/zoom. Mocidad suppresses its own
        // window-control buttons on macOS via UIWindow_IsMacOS().
        [[win standardWindowButton:NSWindowCloseButton]       setHidden:NO];
        [[win standardWindowButton:NSWindowMiniaturizeButton] setHidden:NO];
        [[win standardWindowButton:NSWindowZoomButton]        setHidden:NO];
        watch_traffic_lights(win);
    };

    if ([NSThread isMainThread]) {
        configure();
    } else {
        dispatch_sync(dispatch_get_main_queue(), configure);
    }
}

#endif // __APPLE__ && TARGET_OS_OSX
