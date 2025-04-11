#ifndef UIKIT_TEXT_H
#define UIKIT_TEXT_H

#include <uikit/color.h>
#include <uikit/widget.h>


/**
 * Font styles for text rendering.
 * These styles can be combined using bitwise OR.
 * Normal is the default style.
 * Bold, Italic, and Underscore can be combined with Normal.
 */
typedef enum FontStyle {
    Normal = 0,
    Bold = 1 << 0,
    Italic = 1 << 1,
    Underscore = 1 << 2
} FontStyle;

/**
 * UIText structure representing a text widget.
 * It contains properties for font size, font family, font style,
 * color, background color, and the text itself.
 */
typedef struct UIText {
    const char* __widget_type;

    float fontSize;
    char* fontFamily;

    int fontStyle;
    UIColor* color;
    UIColor* backgroundColor;
    char* text;
    int textLength;
} UIText;

/**
 * Creates a UIText object with the specified text and font size.
 * @param text Text to be displayed.
 * @param fontSize Font size of the text.
 * @return A pointer to the UIText object.
 */
UIText* UIText_Create(char* text, float fontSize);

/**
 * Sets the font family of the UIText object.
 * @param text Pointer to the UIText object.
 * @param fontFamily Font family to be set.
 * @return Pointer to the updated UIText object.
 */
UIText* UIText_SetFontFamily(UIText* text, char* fontFamily);

/**
 * Sets the font style of the UIText object.
 * @param text Pointer to the UIText object.
 * @param fontStyle Font style to be set.
 * @return Pointer to the updated UIText object.
 */
UIText* UIText_SetFontStyle(UIText* text, int fontStyle);

/**
 * Sets the color of the UIText object.
 * @param text Pointer to the UIText object.
 * @param color Color to be set.
 * @return Pointer to the updated UIText object.
 */
UIText* UIText_SetColor(UIText* text, UIColor* color);

/**
 * Sets the background color of the UIText object.
 * @param text Pointer to the UIText object.
 * @param backgroundColor Background color to be set.
 * @return Pointer to the updated UIText object.
 */
UIText* UIText_SetBackgroundColor(UIText* text, UIColor* backgroundColor);

/**
 * Sets the text of the UIText object.
 * @param text Pointer to the UIText object.
 * @param newText New text to be set.
 * @return Pointer to the updated UIText object.
 */
UIText* UIText_SetText(UIText* text, char* newText);

#endif // UIKIT_TEXT_H