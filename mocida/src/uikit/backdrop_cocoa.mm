// backdrop_cocoa.mm — macOS vibrancy / Liquid Glass.
//
// SDL draws into a CAMetalLayer that fills the NSWindow contentView, so to get
// a system blur we insert a native effect view as the *bottom* subview of the
// contentView and make the window non-opaque. The Metal layer composites on
// top, transparent wherever our content cleared to alpha 0, and the OS effect
// shows through.
//
// On macOS 26+ the genuine "Liquid Glass" material is `NSGlassEffectView` with
// an `NSGlassEffectMaterial` (Clear / Regular). On older systems (and the
// generic case) the same role is filled by `NSVisualEffectView`. We pick the
// right class at runtime via @available checks so the binary keeps building
// against the macOS 13 SDK; on a 10.10+ host with no Liquid Glass class the
// code falls back to NSVisualEffectView's `UnderWindowBackground` material
// (the richest under-window effect before macOS 26).

#import <TargetConditionals.h>
#if defined(__APPLE__) && TARGET_OS_OSX

#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#import <objc/message.h>
#include <uikit/backdrop.h>
#include <uikit/debug.h>
#include <SDL3/SDL.h>

// Marker used to find (and reuse) our effect view among the contentView
// subviews. NSView.tag is readonly on macOS, so we attach a private associated
// object instead.
static const void* kMocidaBackdropKey = &kMocidaBackdropKey;

static NSWindow* backdrop_nswindow(SDL_Window* w) {
    if (!w) return nil;
    SDL_PropertiesID props = SDL_GetWindowProperties(w);
    return (__bridge NSWindow*)SDL_GetPointerProperty(
        props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
}

// ----- Liquid Glass (macOS 26+) -------------------------------------------
// NSGlassEffectView + NSGlassEffectMaterial ship with macOS 26. The class is
// private to AppKit; we talk to it through the Objective-C runtime so the
// binary still links against older SDKs and just no-ops on hosts that lack
// the class. This matches what Apple does internally for SwiftUI's
// `.glassEffect()` — the same surface, just invoked dynamically.

static BOOL maceffect_glass_available(void) {
    static int cached = -1;
    if (cached < 0) cached = (objc_getClass("NSGlassEffectView") != NULL) ? 1 : 0;
    return cached == 1;
}

static id maceffect_make_glass_view(NSRect frame) {
    Class cls = objc_getClass("NSGlassEffectView");
    if (!cls) return nil;
    SEL allocSel = sel_registerName("alloc");
    SEL initSel  = sel_registerName("initWithFrame:");
    if (![cls respondsToSelector:allocSel] || ![cls instancesRespondToSelector:initSel])
        return nil;
    id obj = ((id (*)(id, SEL))objc_msgSend)(cls, allocSel);
    return ((id (*)(id, SEL, NSRect))objc_msgSend)(obj, initSel, frame);
}

static void maceffect_set_glass_material(id view, const char* name) {
    if (!view) return;
    Class matCls = objc_getClass("NSGlassEffectMaterial");
    if (!matCls) return;

    // The canonical factories on NSGlassEffectMaterial mirror UIBlurEffect's
    // style enums: `+clear` and `+regular` (NSColor-style). We try each by
    // name; the first selector that exists on the class wins. This keeps
    // the surface tiny and forward-compatible with future Apple renames.
    NSString* want = [NSString stringWithUTF8String:name];
    SEL factories[] = {
        sel_registerName("clear"),
        sel_registerName("regular"),
    };
    for (size_t i = 0; i < sizeof(factories)/sizeof(factories[0]); i++) {
        NSString* selName = NSStringFromSelector(factories[i]);
        if (![want isEqualToString:selName]) continue;
        if (![matCls respondsToSelector:factories[i]]) continue;
        id m = ((id (*)(id, SEL))objc_msgSend)(matCls, factories[i]);
        SEL setSel = sel_registerName("setGlassMaterial:");
        if ([view respondsToSelector:setSel]) {
            ((void (*)(id, SEL, id))objc_msgSend)(view, setSel, m);
        }
        return;
    }
}

static void maceffect_set_glass_state(id view, BOOL active) {
    if (!view) return;
    SEL sel = sel_registerName("setState:");
    if ([view respondsToSelector:sel]) {
        typedef void (*SetterType)(id, SEL, NSInteger);
        ((SetterType)objc_msgSend)(view, sel, active ? 1 : 0);
    }
}

// ----- NSVisualEffectView mapping (fallback path) --------------------------

static NSVisualEffectMaterial maceffect_vibrancy_material(UIBackdropMaterial m) {
    switch (m) {
        case UI_BACKDROP_VIBRANCY_SIDEBAR: return NSVisualEffectMaterialSidebar;
        case UI_BACKDROP_VIBRANCY_HEADER:  return NSVisualEffectMaterialHeaderView;
        case UI_BACKDROP_VIBRANCY_MENU:    return NSVisualEffectMaterialMenu;
        case UI_BACKDROP_VIBRANCY_POPOVER: return NSVisualEffectMaterialPopover;
        case UI_BACKDROP_VIBRANCY_HUD:     return NSVisualEffectMaterialHUDWindow;
        case UI_BACKDROP_LIQUID_GLASS:
            // macOS 26+ picks NSGlassEffectView in maceffect_make_view; this
            // branch is the pre-26 fallback. UnderWindowBackground is the
            // richest desktop-aware material before Liquid Glass.
            return NSVisualEffectMaterialUnderWindowBackground;
        case UI_BACKDROP_MICA:
        case UI_BACKDROP_MICA_ALT:
        default:
            return NSVisualEffectMaterialWindowBackground;
    }
}

// Find the existing mocida effect view (either glass or visual-effect).
static NSView* maceffect_find_existing(NSView* content) {
    for (NSView* sub in content.subviews) {
        if (objc_getAssociatedObject(sub, kMocidaBackdropKey)) {
            return sub;
        }
    }
    return nil;
}

extern "C" UIBackdropMaterial UIBackdrop_ResolveAuto(void) {
    return UI_BACKDROP_VIBRANCY_SIDEBAR;
}

extern "C" int UIBackdrop_NativeAvailable(void) {
    return 1; // NSVisualEffectView has existed since 10.10; on 26+ we use NSGlassEffectView.
}

extern "C" int UIBackdrop_Apply(SDL_Window* window, UIBackdropMaterial material,
                                UIColor tint, float tintOpacity) {
    (void)tint; (void)tintOpacity;
    NSWindow* win = backdrop_nswindow(window);
    if (!win) return 0;

    NSView* content = win.contentView;
    if (!content) return 0;

    // Locate an existing effect view (reuse on material change).
    NSView* existing = maceffect_find_existing(content);
    BOOL isGlass = NO;
    if (existing) {
        isGlass = maceffect_glass_available() &&
                  [existing isKindOfClass:objc_getClass("NSGlassEffectView")];
    }

    if (material == UI_BACKDROP_NONE) {
        if (existing) [existing removeFromSuperview];
        win.opaque = YES;
        win.backgroundColor = [NSColor windowBackgroundColor];
        return 0;
    }

    // Pick the native class: Liquid Glass wins on macOS 26+ for the LIQUID
    // family and the VIBRANCY family (NSGlassEffectView is the modern
    // superset of NSVisualEffectView on Tahoe and later). For Mica/Acrylic
    // we still go through NSVisualEffectView — those are AppKit-defined
    // "background" effects, not glass.
    BOOL wantGlass = maceffect_glass_available() &&
                     (material == UI_BACKDROP_LIQUID_GLASS ||
                      material == UI_BACKDROP_VIBRANCY_SIDEBAR ||
                      material == UI_BACKDROP_VIBRANCY_HEADER ||
                      material == UI_BACKDROP_VIBRANCY_MENU ||
                      material == UI_BACKDROP_VIBRANCY_POPOVER ||
                      material == UI_BACKDROP_VIBRANCY_HUD);

    if (wantGlass) {
        id glass = nil;
        if (existing && isGlass) {
            glass = existing;
        } else {
            if (existing) [existing removeFromSuperview];
            glass = maceffect_make_glass_view(content.bounds);
            if (!glass) {
                // Class flipped off between the cached check and the alloc
                // (extremely unlikely; could happen if a plugin unloaded
                // AppKit). Defer to NSVisualEffectView.
                wantGlass = NO;
            } else {
                objc_setAssociatedObject(glass, kMocidaBackdropKey, @YES,
                                         OBJC_ASSOCIATION_RETAIN_NONATOMIC);
                SEL arSel = sel_registerName("setAutoresizingMask:");
                if ([glass respondsToSelector:arSel]) {
                    typedef void (*SetterType)(id, SEL, NSUInteger);
                    ((SetterType)objc_msgSend)(glass, arSel,
                        NSViewWidthSizable | NSViewHeightSizable);
                }
                [content addSubview:glass positioned:NSWindowBelow relativeTo:nil];
                isGlass = YES;
            }
        }
        if (wantGlass && glass) {
            maceffect_set_glass_material(glass,
                material == UI_BACKDROP_LIQUID_GLASS ? "regular" : "regular");
            maceffect_set_glass_state(glass, YES);
        }
    }

    if (!wantGlass) {
        NSVisualEffectView* fx = nil;
        if (existing && !isGlass) {
            fx = (NSVisualEffectView*)existing;
        } else {
            if (existing) [existing removeFromSuperview];
            fx = [[NSVisualEffectView alloc] initWithFrame:content.bounds];
            objc_setAssociatedObject(fx, kMocidaBackdropKey, @YES,
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            fx.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
            fx.blendingMode = NSVisualEffectBlendingModeBehindWindow;
            [content addSubview:fx positioned:NSWindowBelow relativeTo:nil];
        }
        fx.material = maceffect_vibrancy_material(material);
        fx.state    = NSVisualEffectStateActive;
    }

    win.opaque = NO;
    win.backgroundColor = [NSColor clearColor];

    // Structured log so the headless / minimized validation path can confirm
    // the effect view was actually inserted with the right material, and the
    // window + its layer are configured for per-pixel alpha. Without this
    // there's no way to know "vibrancy is wired" except by visual inspection
    // — and on a CI box / remote / shared screen we have to do it blind.
    NSView* bottomEffect = nil;
    for (NSView* sub in [content.subviews reverseObjectEnumerator]) {
        if (objc_getAssociatedObject(sub, kMocidaBackdropKey)) {
            bottomEffect = sub;
            break;
        }
    }
    // Convert NSString* to UTF-8 C strings so the printf-style UI_INFO
    // formatter (UIDebug_Logf is a C function, not an NSLog) can render
    // them — passing NSString* with %@ would print the literal "@".
    NSString* effectClass = bottomEffect ? NSStringFromClass([bottomEffect class]) : @"<none>";
    NSString* matName = @"<none>";
    if (bottomEffect) {
        if ([bottomEffect isKindOfClass:objc_getClass("NSGlassEffectView")]) {
            matName = @"NSGlassEffectMaterial (Liquid Glass)";
        } else if ([bottomEffect isKindOfClass:[NSVisualEffectView class]]) {
            NSVisualEffectMaterial m = [(NSVisualEffectView*)bottomEffect material];
            switch (m) {
                case NSVisualEffectMaterialSidebar:        matName = @"Sidebar"; break;
                case NSVisualEffectMaterialHeaderView:     matName = @"HeaderView"; break;
                case NSVisualEffectMaterialMenu:           matName = @"Menu"; break;
                case NSVisualEffectMaterialPopover:        matName = @"Popover"; break;
                case NSVisualEffectMaterialHUDWindow:      matName = @"HUDWindow"; break;
                case NSVisualEffectMaterialUnderWindowBackground: matName = @"UnderWindowBackground"; break;
                case NSVisualEffectMaterialWindowBackground: matName = @"WindowBackground"; break;
                default:                                   matName = @"other"; break;
            }
        }
    }
    NSView* metalView = win.contentView;
    BOOL layerIsNonOpaque = (metalView.layer != nil && !metalView.layer.opaque);
    UI_INFO(UI_CAT_WINDOW,
             "vibrancy: class=%s material=%s windowOpaque=%d layerOpaque=%d subviews=%lu",
             [effectClass UTF8String],
             [matName UTF8String],
             win.opaque ? 0 : 1,            // 1 = correctly non-opaque
             layerIsNonOpaque ? 1 : 0,      // 1 = correctly non-opaque
             (unsigned long)content.subviews.count);

    return 1;
}

#endif // __APPLE__ && TARGET_OS_OSX
