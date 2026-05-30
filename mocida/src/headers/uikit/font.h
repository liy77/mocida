#ifndef UIKIT_FONT_H
#define UIKIT_FONT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define SYSTEM_FONTS_PATH "C:\\Windows\\Fonts\\"
#define DEFAULT_FONT_PATH "C:\\Windows\\Fonts\\Arial.ttf"

#elif defined(__APPLE__)
// Modern macOS keeps almost nothing in /Library/Fonts: system faces live
// in /System/Library/Fonts (+ its Supplemental/ subdir, where Arial.ttf
// actually is), and user fonts in ~/Library/Fonts. UISearchFonts walks all
// of those; this define is just the bootstrap root for the recursive walk.
#define SYSTEM_FONTS_PATH "/System/Library/Fonts/"

static const char* find_default_macos_font() {
    const char* candidates[] = {
        "/System/Library/Fonts/Supplemental/Arial.ttf", // family "Arial"
        "/Library/Fonts/Arial Unicode.ttf",
        "/System/Library/Fonts/Helvetica.ttc",          // always present
        "/System/Library/Fonts/SFNS.ttf",               // San Francisco
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

#define DEFAULT_FONT_PATH find_default_macos_font()

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
    char *family_name; /**< Family name reported by the font (e.g. "Arial"). */
    char *file_path;   /**< Absolute path to the .ttf / .otf file on disk. */
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
 * Returns the path of the default font for widgets that don't set a family.
 * Desktop: a fixed system path. iOS: the first font bundled in the .app
 * (call UISearchFonts() first). May return NULL on iOS if no font shipped.
 * The returned string is borrowed — copy if you need to keep it.
 */
const char* UIGetDefaultFontPath(void);

/**
 * Destroys the UIFont object and frees allocated memory.
 * @param font The UIFont object to destroy.
 */
void UIFont_Destroy(FontEntry* font);

/**
 * Destroys the UIFonts map and frees allocated memory.
 * This function should be called when the application is closing or when fonts are no longer needed.
 */
void UIFonts_Destroy();

#endif // UIKIT_FONT_H