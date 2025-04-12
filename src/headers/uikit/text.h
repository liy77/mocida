#ifndef UIKIT_TEXT_H
#define UIKIT_TEXT_H

#include <uikit/color.h>
#include <uikit/widget.h>
#include <uikit/rect.h>
#include <uikit/font.h>
#include <SDL3_ttf/SDL_ttf.h>

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
    UIColor color;
    UIRectangle* background;
    char* text;
    int textLength;

    float marginLeft;
    float marginTop;
    float marginRight;
    float marginBottom;
    float paddingLeft;
    float paddingTop;
    float paddingRight;
    float paddingBottom;
    
    SDL_Texture* __SDL_textTexture; // Pointer to the SDL texture for rendering text
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
UIText* UIText_SetColor(UIText* text, UIColor color);

/**
 * Sets the background of the UIText object.
 * @param text Pointer to the UIText object.
 * @param background Background to be set.
 * @return Pointer to the updated UIText object.
 */
UIText* UIText_SetBackground(UIText* text, UIRectangle* backgroundRect);

/**
 * Sets the text of the UIText object.
 * @param text Pointer to the UIText object.
 * @param newText New text to be set.
 * @return Pointer to the updated UIText object.
 */
UIText* UIText_SetText(UIText* text, char* newText);

/**
 * Sets the margins of the UIText object.
 * @param text Pointer to the UIText object.
 * @param left Left margin.
 * @param top Top margin.
 * @param right Right margin.
 * @param bottom Bottom margin.
 * @return Pointer to the updated UIText object.
 */
UIText* UIText_SetMargins(UIText* text, float left, float top, float right, float bottom);

/**
 * Sets the padding of the UIText object.
 * @param text Pointer to the UIText object.
 * @param left Left padding.
 * @param top Top padding.
 * @param right Right padding.
 * @param bottom Bottom padding.
 * @return Pointer to the updated UIText object.
 */
UIText* UIText_SetPadding(UIText* text, float left, float top, float right, float bottom);

#endif // UIKIT_TEXT_H