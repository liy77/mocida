#ifndef UIKIT_GLASS_H
#define UIKIT_GLASS_H

#include <uikit/widget.h>
#include <uikit/children.h>
#include <uikit/color.h>
#include <stdint.h>

#define UI_WIDGET_GLASS "@uikit/glass"

/**
 * Backdrop material families. Two subgroups:
 *
 *  - Window-wide (Mica / Mica Alt / KDE blur-window): the effect is a property
 *    of the whole window, drawn by the OS compositor behind the entire surface.
 *    These belong in `app { backdrop: }`, NOT in a per-widget Glass.
 *
 *  - Region (Acrylic / Liquid Glass / Vibrancy* / KDE blur-region): the effect
 *    applies to a sub-area, so it can back a `Glass {}` widget.
 *
 * `UI_BACKDROP_AUTO` asks the runtime to pick the platform default (Mica on
 * Win11, Vibrancy on macOS, KDE blur on Linux, in-app fallback elsewhere).
 */
typedef enum {
    UI_BACKDROP_NONE = 0,          /**< No effect ("off"/"none"). */
    UI_BACKDROP_AUTO,              /**< Platform default, resolved at runtime. */

    /* --- Window-wide --- */
    UI_BACKDROP_MICA,              /**< Win11 DWMSBT_MAINWINDOW. */
    UI_BACKDROP_MICA_ALT,          /**< Win11 DWMSBT_TABBEDWINDOW. */
    UI_BACKDROP_KDE_BLUR_WINDOW,   /**< KWin whole-window blur. */

    /* --- Region --- */
    UI_BACKDROP_ACRYLIC,           /**< Win11 DWMSBT_TRANSIENTWINDOW. */
    UI_BACKDROP_ACRYLIC_LEGACY,    /**< Win10 ACCENT_ENABLE_ACRYLICBLURBEHIND. */
    UI_BACKDROP_LIQUID_GLASS,      /**< macOS/iOS 26 Liquid Glass material. */
    UI_BACKDROP_VIBRANCY_SIDEBAR,  /**< NSVisualEffectMaterialSidebar. */
    UI_BACKDROP_VIBRANCY_HEADER,   /**< NSVisualEffectMaterialHeaderView. */
    UI_BACKDROP_VIBRANCY_MENU,     /**< NSVisualEffectMaterialMenu. */
    UI_BACKDROP_VIBRANCY_POPOVER,  /**< NSVisualEffectMaterialPopover. */
    UI_BACKDROP_VIBRANCY_HUD,      /**< NSVisualEffectMaterialHUDWindow. */
    UI_BACKDROP_KDE_BLUR_REGION    /**< KWin per-region blur. */
} UIBackdropMaterial;

/** Per-effect tunable knobs, as a bitmask (UIBackdropMaterial_SupportedProps). */
typedef enum {
    UI_GLASS_PROP_TINT       = 1 << 0, /**< tint + tintOpacity. */
    UI_GLASS_PROP_BLUR       = 1 << 1, /**< blur radius in px (KDE). */
    UI_GLASS_PROP_REFRACTION = 1 << 2, /**< refraction 0..1 (Liquid Glass). */
    UI_GLASS_PROP_NOISE      = 1 << 3, /**< grain 0..1 (Acrylic / Vibrancy HUD). */
    UI_GLASS_PROP_STATE      = 1 << 4  /**< vibrancy state (AppKit). */
} UIGlassProp;

/** AppKit NSVisualEffectView state, applied only to vibrancy materials. */
typedef enum {
    UI_VIBRANCY_ACTIVE   = 0,
    UI_VIBRANCY_INACTIVE = 1,
    UI_VIBRANCY_PRESSED  = 2
} UIVibrancyState;

/** Material thickness preset (sugar; maps to a per-OS material choice). */
typedef enum {
    UI_GLASS_THIN    = 0,
    UI_GLASS_REGULAR = 1,
    UI_GLASS_THICK   = 2
} UIGlassThickness;

/**
 * A region-backdrop container. Behaves like a vertical UIStack (padding +
 * spacing + alignment), but draws a glass background first: either the OS
 * region effect (Acrylic / Liquid Glass / Vibrancy / KDE region) where
 * available, or an in-app painted approximation (tinted rounded rect + a
 * soft top highlight) as a universal fallback.
 *
 * The effect-specific knobs (blurPx, refraction, noise, vibrancyState) are
 * applied best-effort: only those the active material supports are read; the
 * rest are ignored (with a MOCIDA_DEBUG log).
 */
typedef struct {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_GLASS). */

    float marginLeft;          /**< Left outer margin (pixels). */
    float marginTop;           /**< Top outer margin (pixels). */
    float marginRight;         /**< Right outer margin (pixels). */
    float marginBottom;        /**< Bottom outer margin (pixels). */

    UIBackdropMaterial material; /**< Which glass family to use. */

    /* Universal knobs */
    float            radius;       /**< Corner radius of the glass fill. */
    UIColor          tint;         /**< Overlay tint color. */
    float            tintOpacity;  /**< 0..1 tint strength. Default 0.5. */
    UIGlassThickness thickness;    /**< thin|regular|thick material preset. */

    /* Effect-specific knobs (best-effort) */
    float           blurPx;        /**< KDE blur radius (px). */
    float           refraction;    /**< Liquid Glass refraction 0..1. */
    float           noise;         /**< Acrylic / HUD grain 0..1. */
    UIVibrancyState vibrancyState; /**< AppKit vibrancy state. */

    /* Layout (UIStack-compatible) */
    int   orientation;         /**< 0 = vertical, 1 = horizontal. */
    int   align;               /**< UIStackAlign cross-axis. */
    int   justify;             /**< UIStackJustify main-axis. */
    float spacing;             /**< Gap between items (pixels). */
    float paddingLeft;
    float paddingTop;
    float paddingRight;
    float paddingBottom;

    UIChildren* items;         /**< Owned items. Destroyed with the glass. */

    int freeLayout;            /**< 1 = absolute-position children. */
} UIGlass;

/* ---- material helpers (pure logic; cross-platform, in glass.c) ---- */

/**
 * Parses an `effect:` string into a material. "auto"/NULL -> AUTO,
 * "none"/"off" -> NONE. Recognises mica, mica-alt, acrylic, acrylic-legacy,
 * liquid, vibrancy-sidebar/header/menu/popover/hud, kde-window, kde-region.
 * Unknown strings fall back to AUTO.
 */
UIBackdropMaterial UIBackdrop_FromString(const char* effect);

/** Bitmask of UIGlassProp that `m` actually reads. */
uint32_t UIBackdropMaterial_SupportedProps(UIBackdropMaterial m);

/** 1 when `m` is a window-wide material (Mica / Mica Alt / KDE blur-window). */
int UIBackdropMaterial_IsWindowWide(UIBackdropMaterial m);

/* ---- widget API (mirrors UIStack) ---- */

UIGlass* UIGlass_Create(UIBackdropMaterial material);
UIGlass* UIGlass_SetRadius      (UIGlass* g, float radius);
UIGlass* UIGlass_SetTint        (UIGlass* g, UIColor tint);
UIGlass* UIGlass_SetTintOpacity (UIGlass* g, float opacity);
UIGlass* UIGlass_SetThickness   (UIGlass* g, UIGlassThickness thickness);
UIGlass* UIGlass_SetBlur        (UIGlass* g, float px);
UIGlass* UIGlass_SetRefraction  (UIGlass* g, float refraction);
UIGlass* UIGlass_SetNoise       (UIGlass* g, float noise);
UIGlass* UIGlass_SetVibrancyState(UIGlass* g, UIVibrancyState state);
UIGlass* UIGlass_SetOrientation (UIGlass* g, int horizontal);
UIGlass* UIGlass_SetSpacing     (UIGlass* g, float spacing);
UIGlass* UIGlass_SetAlign       (UIGlass* g, int align);
UIGlass* UIGlass_SetJustify     (UIGlass* g, int justify);
UIGlass* UIGlass_SetPadding     (UIGlass* g, float l, float t, float r, float b);
UIGlass* UIGlass_SetFreeLayout  (UIGlass* g, int enabled);
int      UIGlass_AddItem        (UIGlass* g, UIWidget* item);
void     UIGlass_GetContentSize (UIGlass* g, float* outW, float* outH);
void     UIGlass_Destroy        (UIGlass* g);

#endif // UIKIT_GLASS_H
