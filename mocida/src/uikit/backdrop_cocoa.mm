// backdrop_cocoa.mm — macOS vibrancy / Liquid Glass via NSVisualEffectView.
//
// SDL draws into a CAMetalLayer that fills the NSWindow contentView, so to get
// a system blur we insert an NSVisualEffectView as the *bottom* subview of the
// contentView and make the window non-opaque. The Metal layer composites on
// top, transparent wherever our content cleared to alpha 0, and the vibrancy
// material shows through.

#import <TargetConditionals.h>
#if defined(__APPLE__) && TARGET_OS_OSX

#import <Cocoa/Cocoa.h>
#include <uikit/backdrop.h>
#include <SDL3/SDL.h>

// Tag used to find (and reuse) our effect view among the contentView subviews.
static const NSInteger kMocidaBackdropTag = 0x4D4F4342; // 'MOCB'

static NSWindow* backdrop_nswindow(SDL_Window* w) {
    if (!w) return nil;
    SDL_PropertiesID props = SDL_GetWindowProperties(w);
    return (__bridge NSWindow*)SDL_GetPointerProperty(
        props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
}

static NSVisualEffectMaterial material_for(UIBackdropMaterial m) {
    switch (m) {
        case UI_BACKDROP_VIBRANCY_SIDEBAR: return NSVisualEffectMaterialSidebar;
        case UI_BACKDROP_VIBRANCY_HEADER:  return NSVisualEffectMaterialHeaderView;
        case UI_BACKDROP_VIBRANCY_MENU:    return NSVisualEffectMaterialMenu;
        case UI_BACKDROP_VIBRANCY_POPOVER: return NSVisualEffectMaterialPopover;
        case UI_BACKDROP_VIBRANCY_HUD:     return NSVisualEffectMaterialHUDWindow;
        case UI_BACKDROP_LIQUID_GLASS:
            // Liquid Glass (macOS 26) extends the same enum; on older systems
            // the richest under-window material is the closest fallback.
            return NSVisualEffectMaterialUnderWindowBackground;
        case UI_BACKDROP_MICA:
        case UI_BACKDROP_MICA_ALT:
        default:
            return NSVisualEffectMaterialWindowBackground;
    }
}

extern "C" UIBackdropMaterial UIBackdrop_ResolveAuto(void) {
    return UI_BACKDROP_VIBRANCY_SIDEBAR;
}

extern "C" int UIBackdrop_NativeAvailable(void) {
    return 1; // NSVisualEffectView has existed since 10.10.
}

extern "C" int UIBackdrop_Apply(SDL_Window* window, UIBackdropMaterial material,
                                UIColor tint, float tintOpacity) {
    (void)tint; (void)tintOpacity;
    NSWindow* win = backdrop_nswindow(window);
    if (!win) return 0;

    NSView* content = win.contentView;
    if (!content) return 0;

    // Locate an existing effect view (reuse on material change).
    NSVisualEffectView* fx = nil;
    for (NSView* sub in content.subviews) {
        if (sub.tag == kMocidaBackdropTag && [sub isKindOfClass:[NSVisualEffectView class]]) {
            fx = (NSVisualEffectView*)sub;
            break;
        }
    }

    if (material == UI_BACKDROP_NONE) {
        if (fx) [fx removeFromSuperview];
        win.opaque = YES;
        win.backgroundColor = [NSColor windowBackgroundColor];
        return 0;
    }

    if (!fx) {
        fx = [[NSVisualEffectView alloc] initWithFrame:content.bounds];
        fx.tag = kMocidaBackdropTag;
        fx.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        fx.blendingMode = NSVisualEffectBlendingModeBehindWindow;
        // Insert below everything (including the SDL Metal layer's view).
        [content addSubview:fx positioned:NSWindowBelow relativeTo:nil];
    }
    fx.material = material_for(material);
    fx.state    = NSVisualEffectStateActive;

    win.opaque = NO;
    win.backgroundColor = [NSColor clearColor];
    return 1;
}

#endif // __APPLE__ && TARGET_OS_OSX
