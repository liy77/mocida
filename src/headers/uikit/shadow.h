#ifndef UIKIT_SHADOW_H
#define UIKIT_SHADOW_H

#include <uikit/color.h>

/**
 * Describes a CSS-like drop shadow.
 *
 *   offsetX/offsetY : displacement of the shadow relative to the shape.
 *   blur            : blur radius in pixels (width of the soft halo).
 *                     0 = "hard" shadow (mask of the shape, displaced).
 *   spread          : expands (+) or contracts (-) the shape BEFORE blur.
 *   color           : shadow tint (with alpha).
 *
 * Rendering uses analytic coverage rasterization with the SDF (signed
 * distance function) of a rounded rect and a smoothstep falloff over
 * the [0, blur] range. Result: mathematically correct soft edges in a
 * single pass - no multi-pass CPU/GPU blur required.
 */
typedef struct {
    float offsetX;  /**< Horizontal offset in pixels. Positive = right. */
    float offsetY;  /**< Vertical offset in pixels.   Positive = down.  */
    float blur;     /**< Radius of the soft halo. 0 = hard shadow.      */
    float spread;   /**< Expands (+) or contracts (-) the shape pre-blur. */
    UIColor color;  /**< Shadow color. Alpha controls intensity.         */
} UIShadow;

// Convenience macro for a sensible material-design-ish default.
#define UI_SHADOW_DEFAULT ((UIShadow){ \
    .offsetX = 0.0f, .offsetY = 4.0f, .blur = 12.0f, .spread = 0.0f, \
    .color = { 0, 0, 0, 0.25f } })

// Sentinel "no shadow" value.
#define UI_SHADOW_NONE ((UIShadow){ 0.0f, 0.0f, 0.0f, 0.0f, { 0, 0, 0, 0.0f } })

#endif // UIKIT_SHADOW_H
