// backdrop_uikit.mm — iOS vibrancy / Liquid Glass via UIVisualEffectView.
//
// Symmetric to backdrop_cocoa.mm: insert a UIVisualEffectView below the SDL
// render view in the window's root view hierarchy. UIKit blur effects always
// sample the content behind the view, so there is no "behind window" desktop
// blur as on macOS — the effect reads whatever the app draws under it. Still
// useful for translucent chrome over app content.

#import <TargetConditionals.h>
#if defined(__APPLE__) && TARGET_OS_IPHONE

#import <UIKit/UIKit.h>
#include <uikit/backdrop.h>
#include <SDL3/SDL.h>

static const NSInteger kMocidaBackdropTag = 0x4D4F4342; // 'MOCB'

static UIWindow* backdrop_uiwindow(SDL_Window* w) {
    if (!w) return nil;
    SDL_PropertiesID props = SDL_GetWindowProperties(w);
    return (__bridge UIWindow*)SDL_GetPointerProperty(
        props, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL);
}

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

    UIBlurEffect* effect = [UIBlurEffect effectWithStyle:style_for(material)];
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
