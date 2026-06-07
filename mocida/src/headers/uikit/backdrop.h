#ifndef UIKIT_BACKDROP_H
#define UIKIT_BACKDROP_H

#include <uikit/glass.h>
#include <uikit/color.h>
#include <SDL3/SDL.h>

/**
 * OS-level window backdrop controller. The implementation is platform-specific:
 *
 *   backdrop_win32.c  — DWM DWMWA_SYSTEMBACKDROP_TYPE (Mica / Acrylic), with a
 *                       Win10 SetWindowCompositionAttribute / ACCENT_POLICY
 *                       fallback.
 *   backdrop_cocoa.mm — NSVisualEffectView inserted below the SDL layer.
 *   backdrop_uikit.mm — UIVisualEffectView (iOS).
 *   backdrop_linux.c  — KWin org_kde_kwin_blur (Wayland) + X11
 *                       _KDE_NET_WM_BLUR_BEHIND_REGION fallback.
 *
 * Exactly one of these provides the symbols on a given platform (the others
 * preprocess to empty). All four are part of the build; CMake only adds the
 * Objective-C++ ones on Apple.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Resolve UI_BACKDROP_AUTO to the platform's preferred window-wide material. */
UIBackdropMaterial UIBackdrop_ResolveAuto(void);

/** 1 when a native compositor backdrop is available on this OS/runtime. */
int UIBackdrop_NativeAvailable(void);

/**
 * Apply (or update) the OS window backdrop. `material` is expected to be a
 * window-wide family (Mica / Mica Alt / KDE blur-window); region families
 * passed here degrade to the closest window-wide equivalent. Pass
 * UI_BACKDROP_NONE to disable.
 *
 * `tint` / `tintOpacity` are forwarded where the OS supports a window tint
 * (ignored otherwise). Returns 1 if a native effect was applied, 0 if the
 * platform has no compositor backdrop (caller falls back to an opaque window).
 */
int UIBackdrop_Apply(SDL_Window* window, UIBackdropMaterial material,
                     UIColor tint, float tintOpacity);

/**
 * Backdrop "companion" window (Windows only; no-op elsewhere).
 *
 * Some renderers (SDL's D3D11/Vulkan swapchain) can't host a real DWM acrylic
 * blur in their own client — the system backdrop renders sharp or flat there.
 * The workaround used by several SDL/GL apps: a separate borderless helper
 * window that DOES carry a clean DWM acrylic (Explorer-style), kept exactly
 * behind the main window. Where the main window is transparent (alpha-0 holes —
 * e.g. glass sidebars), the companion's acrylic shows through. Where it's opaque
 * it's covered, so the companion is invisible except through the holes.
 *
 * Lifecycle: Enable once (creates + shows the helper behind `owner`), call Sync
 * every frame (it matches the owner's screen rect and parks itself directly
 * below the owner in z-order), Disable to tear it down. Single instance.
 */
int  UIBackdropCompanion_Enable(SDL_Window* owner, UIBackdropMaterial material,
                                UIColor tint, float tintOpacity);
void UIBackdropCompanion_Sync(SDL_Window* owner);
void UIBackdropCompanion_Disable(void);

#ifdef __cplusplus
}
#endif

#endif // UIKIT_BACKDROP_H
