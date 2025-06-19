#ifndef UIKIT_COLOR_H
#define UIKIT_COLOR_H

#ifdef WIN32
#include <windows.h>
#endif

/**
 * UIColor structure representing a color in RGBA format.
 * It contains red, green, blue, and alpha components.
 * The alpha component is used for transparency.
 * The values are in the range of 0-255 for RGB and 0.0-1.0 for alpha.
 */
typedef struct {
    int r;
    int g;
    int b;
    float a; // alpha
} UIColor;

// Predefined colors
#define UI_COLOR_BLACK (UIColor){0, 0, 0, 1.0f}
#define UI_COLOR_WHITE (UIColor){255, 255, 255, 1.0f}
#define UI_COLOR_RED (UIColor){255, 0, 0, 1.0f}
#define UI_COLOR_GREEN (UIColor){0, 255, 0, 1.0f}
#define UI_COLOR_BLUE (UIColor){0, 0, 255, 1.0f}
#define UI_COLOR_YELLOW (UIColor){255, 255, 0, 1.0f}
#define UI_COLOR_CYAN (UIColor){0, 255, 255, 1.0f}
#define UI_COLOR_MAGENTA (UIColor){255, 0, 255, 1.0f}
#define UI_COLOR_GRAY (UIColor){128, 128, 128, 1.0f}
#define UI_COLOR_LIGHT_GRAY (UIColor){211, 211, 211, 1.0f}
#define UI_COLOR_DARK_GRAY (UIColor){169, 169, 169, 1.0f}
#define UI_COLOR_ORANGE (UIColor){255, 165, 0, 1.0f}
#define UI_COLOR_PURPLE (UIColor){128, 0, 128, 1.0f}
#define UI_COLOR_PINK (UIColor){255, 192, 203, 1.0f}
#define UI_COLOR_BROWN (UIColor){165, 42, 42, 1.0f}
#define UI_COLOR_GOLD (UIColor){255, 215, 0, 1.0f}
#define UI_COLOR_SILVER (UIColor){192, 192, 192, 1.0f}
#define UI_COLOR_NAVY (UIColor){0, 0, 128, 1.0f}
#define UI_COLOR_TEAL (UIColor){0, 128, 128, 1.0f}
#define UI_COLOR_TRANSPARENT (UIColor){0, 0, 0, 0.0f} // Fully transparent

/**
 * Creates a UIColor object with the specified RGBA values.
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @param a Alpha component (0.0-1.0)
 * @return A pointer to the UIColor object.
 */
UIColor* UIColor_RGBA(int r, int g, int b, float a);
/**
 * Creates a UIColor object from a hex string.
 * @param hex Hex string (e.g., "#FF5733")
 * @return A pointer to the UIColor object.
 */
UIColor* UIColor_Hex(const char* hex);

#endif // UIKIT_COLOR_H