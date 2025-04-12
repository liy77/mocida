#include <uikit/font.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <dirent.h>
#endif

#include <SDL3_ttf/SDL_ttf.h>

void UISearchFonts() {
    int fontCount = 0;
    UIFonts = NULL;

    if (TTF_Init() != 1) {
        fprintf(stderr, "Failed to initialize SDL_ttf: %s\n", SDL_GetError());
        return;
    }

#ifdef _WIN32
    char searchPath[512] = {0};
    printf("SYSTEM_FONTS_PATH: '%s'\n", SYSTEM_FONTS_PATH);
    snprintf(searchPath, sizeof(searchPath), "%s\\*.ttf", SYSTEM_FONTS_PATH);
    
    WIN32_FIND_DATA findFileData;
    HANDLE hFind = FindFirstFile(searchPath, &findFileData);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        printf("No fonts found in: %s\n", SYSTEM_FONTS_PATH);
        TTF_Quit();
        return;
    }
    
    do {
        if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
    
        char *fontName = findFileData.cFileName;
        char fontPath[512];
        snprintf(fontPath, sizeof(fontPath), "%s\\%s", SYSTEM_FONTS_PATH, fontName);
        
        // Load the font temporarily to get the family name
        TTF_Font* font = TTF_OpenFont(fontPath, 12); // Arbitrary size, just to load the font
        if (font == NULL) {
            fprintf(stderr, "Failed to load font %s: %s\n", fontPath, SDL_GetError());
            continue;
        }

        // Extract the real family name from the font
        const char* family_name = TTF_GetFontFamilyName(font);
        char* family_name_copy = NULL;
        if (family_name) {
            family_name_copy = _strdup(family_name);
        } else {
            fprintf(stderr, "No family name found in font %s\n", fontPath);
            family_name_copy = _strdup("Unknown Family"); // Fallback
        }

        TTF_CloseFont(font);

        if (!family_name_copy) {
            printf("Failed to duplicate family name for font: %s\n", fontName);
            continue;
        }

        FontEntry** temp = realloc(UIFonts, sizeof(FontEntry *) * (fontCount + 1));
        if (temp == NULL) {
            printf("Memory allocation failed\n");
            free(family_name_copy);
            FindClose(hFind);
            TTF_Quit();
            return;
        }
        UIFonts = temp;
        
        UIFonts[fontCount] = malloc(sizeof(FontEntry));
        if (!UIFonts[fontCount]) {
            printf("Memory allocation failed\n");
            free(family_name_copy);
            FindClose(hFind);
            TTF_Quit();
            return;
        }

        UIFonts[fontCount]->family_name = family_name_copy;
        UIFonts[fontCount]->file_path = _strdup(fontPath);
        if (!UIFonts[fontCount]->family_name || !UIFonts[fontCount]->file_path) {
            printf("String duplication failed\n");
            free(UIFonts[fontCount]->family_name);
            free(UIFonts[fontCount]->file_path);
            free(UIFonts[fontCount]);
            FindClose(hFind);
            TTF_Quit();
            return;
        }
        fontCount++;
        printf("Font loaded: %s, Path: %s\n", family_name_copy, fontPath);
    } while (FindNextFile(hFind, &findFileData) != 0);
    
    FindClose(hFind);

    FontEntry** temp = realloc(UIFonts, sizeof(FontEntry *) * (fontCount + 1));
    if (temp == NULL) {
        printf("Memory allocation failed for NULL termination\n");
        TTF_Quit();
        return;
    }
    UIFonts = temp;
    UIFonts[fontCount] = NULL; // NULL-terminate the array

#elif defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__)
    DIR *dir = opendir(SYSTEM_FONTS_PATH);

    if (!dir) {
        printf("No fonts found in: %s\n", SYSTEM_FONTS_PATH);
        TTF_Quit();
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR)
            continue;

        if (strstr(entry->d_name, ".ttf") == NULL)
            continue;

        char fontPath[512];
        snprintf(fontPath, sizeof(fontPath), "%s/%s", SYSTEM_FONTS_PATH, entry->d_name);

        // Load the font temporarily to get the family name
        TTF_Font* font = TTF_OpenFont(fontPath, 12); // Arbitrary size, just to load the font
        if (!font) {
            fprintf(stderr, "Failed to load font %s: %s\n", fontPath, TTF_GetError());
            continue;
        }

        // Extract the real family name from the font
        const char* family_name = TTF_FontFaceFamilyName(font);
        char* family_name_copy = NULL;
        if (family_name) {
            family_name_copy = strdup(family_name);
        } else {
            fprintf(stderr, "No family name found in font %s\n", fontPath);
            family_name_copy = strdup("Unknown Family"); // Fallback
        }

        TTF_CloseFont(font);

        if (!family_name_copy) {
            printf("Failed to duplicate family name for font: %s\n", entry->d_name);
            continue;
        }

        FontEntry** temp = realloc(UIFonts, sizeof(FontEntry *) * (fontCount + 1));
        if (temp == NULL) {
            printf("Memory allocation failed\n");
            free(family_name_copy);
            closedir(dir);
            TTF_Quit();
            return;
        }
        UIFonts = temp;

        UIFonts[fontCount] = malloc(sizeof(FontEntry));
        if (!UIFonts[fontCount]) {
            printf("Memory allocation failed\n");
            free(family_name_copy);
            closedir(dir);
            TTF_Quit();
            return;
        }

        UIFonts[fontCount]->family_name = family_name_copy;
        UIFonts[fontCount]->file_path = strdup(fontPath);
        if (!UIFonts[fontCount]->family_name || !UIFonts[fontCount]->file_path) {
            printf("String duplication failed\n");
            free(UIFonts[fontCount]->family_name);
            free(UIFonts[fontCount]->file_path);
            free(UIFonts[fontCount]);
            closedir(dir);
            TTF_Quit();
            return;
        }
        fontCount++;
        printf("Font loaded: %s, Path: %s\n", family_name_copy, fontPath);
    }

    closedir(dir);

    FontEntry** temp = realloc(UIFonts, sizeof(FontEntry *) * (fontCount + 1));
    if (temp == NULL) {
        printf("Memory allocation failed for NULL termination\n");
        TTF_Quit();
        return;
    }
    UIFonts = temp;
    UIFonts[fontCount] = NULL; 
#endif

    printf("Fonts found: %d\n", fontCount);
    TTF_Quit(); // Finalizar SDL_ttf
}

char* UIGetFont(const char* family_name) {
    if (UIFonts == NULL) {
        fprintf(stderr, "Fonts not initialized. Call UISearchFonts() first.\n");
        return NULL;
    }

    if (family_name == NULL) {
        fprintf(stderr, "Family name is NULL\n");
        return NULL;
    }

    for (int i = 0; UIFonts[i] != NULL; i++) {
        FontEntry* fontEntry = UIFonts[i];
        if (fontEntry == NULL || fontEntry->family_name == NULL || fontEntry->file_path == NULL) {
            fprintf(stderr, "Invalid font entry at index %d\n", i);
            continue;
        }

        if (strcmp(fontEntry->family_name, family_name) == 0) {
            return fontEntry->file_path;
        }
    }

    return NULL; 
}