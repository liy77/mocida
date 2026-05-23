#ifndef UIKIT_THEME_H
#define UIKIT_THEME_H

#include <uikit/color.h>

/**
 * Global design tokens. Widgets that don't have explicit colours set
 * fall back to these values during construction. Change at runtime via
 * UITheme_SetGlobal; new widgets pick up the new values, existing ones
 * keep whatever they were configured with.
 */
typedef struct {
    UIColor primary;     /**< Buttons, sliders, focused borders. */
    UIColor onPrimary;   /**< Text on primary surfaces. */
    UIColor surface;     /**< Cards, sheets, dialogs. */
    UIColor onSurface;   /**< Text on surface. */
    UIColor background;  /**< Window / scroll background. */
    UIColor border;      /**< Borders and dividers. */
    UIColor success;         /**< Tint for success / confirmation states. */
    UIColor warning;         /**< Tint for warning states. */
    UIColor danger;          /**< Tint for destructive / error states. */
    UIColor disabled;        /**< Tint for non-interactive (greyed-out) widgets. */
    float   radius;          /**< Default rounded-corner radius. */
    float   spacing;         /**< Default gap between things in a stack. */
    float   fontSizeSmall;   /**< Small font size token. */
    float   fontSizeMedium;  /**< Default font size token. */
    float   fontSizeLarge;   /**< Large font size token. */
} UITheme;

/** Returns the active global theme (always non-NULL). */
const UITheme* UITheme_GetGlobal(void);

/** Replaces the global theme. Copying is shallow (just the floats and
    UIColor primitives). NULL is silently ignored. */
void UITheme_SetGlobal(const UITheme* t);

/** Populates `out` with the default light theme. */
void UITheme_FillLight(UITheme* out);

/** Populates `out` with the default dark theme. */
void UITheme_FillDark(UITheme* out);

#endif // UIKIT_THEME_H
