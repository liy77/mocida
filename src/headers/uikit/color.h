#ifndef UIKIT_COLOR_H
#define UIKIT_COLOR_H

#ifdef WIN32
#include <windows.h>
#endif

typedef struct {
    int r;
    int g;
    int b;
    float a; // alpha
} UIColor;

// Predefined colors
#define UIColorBlack (UIColor){0, 0, 0, 1.0f}
#define UIColorWhite (UIColor){255, 255, 255, 1.0f}
#define UIColorRed (UIColor){255, 0, 0, 1.0f}
#define UIColorGreen (UIColor){0, 255, 0, 1.0f}
#define UIColorBlue (UIColor){0, 0, 255, 1.0f}
#define UIColorYellow (UIColor){255, 255, 0, 1.0f}
#define UIColorCyan (UIColor){0, 255, 255, 1.0f}
#define UIColorMagenta (UIColor){255, 0, 255, 1.0f}
#define UIColorGray (UIColor){128, 128, 128, 1.0f}
#define UIColorLightGray (UIColor){211, 211, 211, 1.0f}
#define UIColorDarkGray (UIColor){169, 169, 169, 1.0f}
#define UIColorOrange (UIColor){255, 165, 0, 1.0f}
#define UIColorPurple (UIColor){128, 0, 128, 1.0f}
#define UIColorPink (UIColor){255, 192, 203, 1.0f}
#define UIColorBrown (UIColor){165, 42, 42, 1.0f}
#define UIColorGold (UIColor){255, 215, 0, 1.0f}
#define UIColorSilver (UIColor){192, 192, 192, 1.0f}
#define UIColorNavy (UIColor){0, 0, 128, 1.0f}
#define UIColorTeal (UIColor){0, 128, 128, 1.0f}
#define UIColorTransparent (UIColor){0, 0, 0, 0.0f} // Fully transparent

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