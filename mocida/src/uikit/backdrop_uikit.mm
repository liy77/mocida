// backdrop_uikit.mm — iOS vibrancy / Liquid Glass via UIVisualEffectView.
//
// Symmetric to backdrop_cocoa.mm: insert a UIVisualEffectView below the SDL
// render view in the window's root view hierarchy. UIKit blur effects always
// sample the content behind the view, so there is no "behind window" desktop
// blur as on macOS — the effect reads whatever the app draws under it. Still
// useful for translucent chrome over app content.
//
// On iOS 26+ the genuine "Liquid Glass" material is `UIGlassEffect` (applied
// to a UIVisualEffectView). We try it dynamically through the Objective-C
// runtime so the binary still links against older SDKs; on iOS 13..25 we fall
// back to UIBlurEffect's `SystemUltraThinMaterial` / `SystemMaterial` /
// `SystemChromeMaterial` (the AppKit-style Material hierarchy shipped in 13+).

#import <TargetConditionals.h>
#if defined(__APPLE__) && TARGET_OS_IPHONE

#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#include <uikit/backdrop.h>
#include <SDL3/SDL.h>

static const NSInteger kMocidaBackdropTag = 0x4D4F4342; // 'MOCB'

static UIWindow* backdrop_uiwindow(SDL_Window* w) {
    if (!w) return nil;
    SDL_PropertiesID props = SDL_GetWindowProperties(w);
    return (__bridge UIWindow*)SDL_GetPointerProperty(
        props, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL);
}

// ----- iOS 26+ Liquid Glass runtime probe ---------------------------------

static BOOL uieffect_glass_available(void) {
    static int cached = -1;
    if (cached < 0) cached = (objc_getClass("UIGlassEffect") != NULL) ? 1 : 0;
    return cached == 1;
}

static id uieffect_make_glass(void) {
    Class cls = objc_getClass("UIGlassEffect");
    if (!cls) return nil;
    SEL allocSel = sel_registerName("alloc");
    if (![cls respondsToSelector:allocSel]) return nil;

    // The canonical factories on UIGlassEffect mirror UIBlurEffect's:
    //   +effectWithStyle: (int), +effect, +regular, +clear. The first one
    //   that exists on the host class wins.
    SEL factories[] = {
        sel_registerName("effectWithStyle:"),
        sel_registerName("effect"),
        sel_registerName("regular"),
    };
    for (size_t i = 0; i < sizeof(factories)/sizeof(factories[0]); i++) {
        if (![cls respondsToSelector:factories[i]]) continue;
        id result;
        if (i == 0) {
            // 0 maps to "regular" on iOS 26's UIGlassEffectStyle.
            result = ((id (*)(id, SEL, NSInteger))objc_msgSend)(cls, factories[i], 0);
        } else {
            result = ((id (*)(id, SEL))objc_msgSend)(cls, factories[i]);
        }
        if (result) return result;
    }
    return nil;
}

// ----- Pre-iOS 26 fallback path ------------------------------------------

static UIBlurEffectStyle style_for(UIBackdropMaterial m) {
    switch (m) {
        case UI_BACKDROP_VIBRANCY_HUD:     return UIBlurEffectStyleSystemThickMaterialDark;
        case UI_BACKDROP_VIBRANCY_MENU:    return UIBlurEffectStyleSystemMaterial;
        case UI_BACKDROP_VIBRANCY_POPOVER: return UIBlurEffectStyleSystemChromeMaterial;
        case UI_BACKDROP_LIQUID_GLASS:     return UIBlurEffectStyleSystemUltraThinMaterial;
        default:                           return UIBlurEffectStyleSystemMaterial;
    }
}

extern "C" UIBackdropMaterial UIBackdrop_ResolveAuto(void) {
    return UI_BACKDROP_LIQUID_GLASS;
}

extern "C" int UIBackdrop_NativeAvailable(void) {
    return 1;
}

extern "C" int UIBackdrop_Apply(SDL_Window* window, UIBackdropMaterial material,
                                UIColor tint, float tintOpacity) {
    (void)tint; (void)tintOpacity;
    UIWindow* win = backdrop_uiwindow(window);
    if (!win) return 0;
    UIView* root = win.rootViewController ? win.rootViewController.view : win;
    if (!root) return 0;

    UIVisualEffectView* fx = (UIVisualEffectView*)[root viewWithTag:kMocidaBackdropTag];

    if (material == UI_BACKDROP_NONE) {
        if (fx) [fx removeFromSuperview];
        return 0;
    }

    // Pick the right effect. On iOS 26+ the Liquid Glass family is the
    // real material; for everything else (or on older OS) the old
    // UIBlurEffect-with-style path is good enough.
    UIVisualEffect* effect = nil;
    if (material == UI_BACKDROP_LIQUID_GLASS && uieffect_glass_available()) {
        effect = (UIVisualEffect*)uieffect_make_glass();
    }
    if (!effect) {
        effect = [UIBlurEffect effectWithStyle:style_for(material)];
    }

    if (!fx) {
        fx = [[UIVisualEffectView alloc] initWithEffect:effect];
        fx.tag = kMocidaBackdropTag;
        fx.frame = root.bounds;
        fx.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        [root insertSubview:fx atIndex:0];
    } else {
        fx.effect = effect;
    }
    return 1;
}

#endif // __APPLE__ && TARGET_OS_IPHONE
