#include <uikit/text.h>
#include <stdio.h>

UIText* UIText_Create(char* text, float fontSize) {
    UIText* uiText = (UIText*)malloc(sizeof(UIText));
    if (uiText == NULL) {
        fprintf(stderr, "Failed to allocate memory for UIText\n");
        return NULL; // Memory allocation failed
    }

    if (text == NULL) {
        text = _strdup(""); // Default text if NULL
    }

    uiText->fontSize = fontSize;
    uiText->fontFamily = _strdup("Arial"); // Default font family
    uiText->fontStyle = Normal; // Default style
    uiText->color = UIColor_RGBA(0, 0, 0, 1.0f); // Default color (black)
    uiText->backgroundColor = UIColor_RGBA(255, 255, 255, 1.0f); // Default background color (white
    uiText->text = _strdup(text);
    uiText->textLength = strlen(text);
    uiText->__widget_type = UI_WIDGET_TEXT; // Set the widget type

    return uiText;
}

UIText* UIText_SetFontFamily(UIText* text, char* fontFamily) {
    if (text == NULL || fontFamily == NULL) {
        return NULL; // Invalid arguments
    }

    free(text->fontFamily); // Free previous font family
    text->fontFamily = _strdup(fontFamily);
    return text;
}

UIText* UIText_SetFontStyle(UIText* text, int fontStyle) {
    if (text == NULL) {
        return NULL; // Invalid argument
    }

    text->fontStyle = fontStyle;
    return text;
}

UIText* UIText_SetColor(UIText* text, UIColor* color) {
    if (text == NULL || color == NULL) {
        return NULL; // Invalid arguments
    }

    free(text->color); // Free previous color
    text->color = color;
    return text;
}

UIText* UIText_SetBackgroundColor(UIText* text, UIColor* backgroundColor) {
    if (text == NULL || backgroundColor == NULL) {
        return NULL; // Invalid arguments
    }

    free(text->backgroundColor); // Free previous background color
    text->backgroundColor = backgroundColor;
    return text;
}

UIText* UIText_SetText(UIText* text, char* newText) {
    if (text == NULL || newText == NULL) {
        return NULL; // Invalid arguments
    }

    free(text->text); // Free previous text
    text->text = _strdup(newText);
    text->textLength = strlen(newText);
    return text;
}