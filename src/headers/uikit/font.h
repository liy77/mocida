#ifndef UIKIT_FONT_H
#define UIKIT_FONT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define SYSTEM_FONTS_PATH "C:\\Windows\\Fonts\\"
#define DEFAULT_FONT_PATH "C:\\Windows\\Fonts\\Arial.ttf"

#elif defined(__APPLE__)
#define SYSTEM_FONTS_PATH "/Library/Fonts/"
#define DEFAULT_FONT_PATH "/Library/Fonts/Arial.ttf"

#elif defined(__linux__)
#define SYSTEM_FONTS_PATH "/usr/share/fonts/"

static const char* find_default_linux_font() {
    const char* candidates[] = {
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        NULL
    };

    for (int i = 0; candidates[i] != NULL; i++) {
        FILE* f = fopen(candidates[i], "r");
        if (f) {
            fclose(f);
            return candidates[i];
        }
    }

    return NULL; 
}

#define DEFAULT_FONT_PATH find_default_linux_font()
#endif


#include <stdlib.h>
#include <string.h>

/**
 * FontEntry structure representing a font entry.
 * It contains the font family name and the file path to the font.
 */
typedef struct FontEntry {
    char *family_name;
    char *file_path;
} FontEntry;

static FontEntry **UIFonts = NULL;

/**
 * Searches for available fonts in the system and populates the UIFonts map.
 * The map contains font family names as keys and their file paths as values.
 */
void UISearchFonts();

/**
 * Retrieves the font file path for a given font family name.
 * @param family_name The name of the font family.
 * @return The file path of the font, or NULL if not found.
 */
char* UIGetFont(const char* family_name);

/**
 * Destroys the UIFont object and frees allocated memory.
 * @param font The UIFont object to destroy.
 */
void UIFont_Destroy();

/**
 * Destroys the UIFonts map and frees allocated memory.
 * This function should be called when the application is closing or when fonts are no longer needed.
 */
void UIFonts_Destroy();

#endif // UIKIT_FONT_H