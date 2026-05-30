#include <uikit/font.h>
#include <uikit/debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <dirent.h>
#include <strings.h>   // strcasecmp (POSIX, not in <string.h>)
#endif

#include <SDL3_ttf/SDL_ttf.h>

#ifndef _WIN32
// Recursive walker for UISearchFonts on Linux/BSD/macOS. Returns
// silently on any I/O error so a half-populated font tree never
// crashes the host app — we just end up with whichever fonts were
// reachable before the failure.
//
// Pass the bootstrap dir (SYSTEM_FONTS_PATH) as `dirPath`; the
// function recurses into every subdirectory it discovers. Both .ttf
// and .otf are accepted because SDL_ttf 3 opens both via the same
// TTF_OpenFont entry point.
static void WalkFontsDir(const char* dirPath, int* count) {
    if (!dirPath || !count) return;
    DIR* dir = opendir(dirPath);
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue; // skip "." / ".." / hidden

        char child[1024];
        snprintf(child, sizeof(child), "%s/%s", dirPath, entry->d_name);

        // d_type can be DT_UNKNOWN on some filesystems (e.g. XFS,
        // overlayfs in containers). Fall back to stat() to learn the
        // real type rather than skipping a valid entry.
        int isDir = 0, isReg = 0;
        if (entry->d_type == DT_DIR)      isDir = 1;
        else if (entry->d_type == DT_REG) isReg = 1;
        else if (entry->d_type == DT_UNKNOWN || entry->d_type == DT_LNK) {
            struct stat st;
            if (stat(child, &st) == 0) {
                if (S_ISDIR(st.st_mode)) isDir = 1;
                else if (S_ISREG(st.st_mode)) isReg = 1;
            }
        }

        if (isDir) { WalkFontsDir(child, count); continue; }
        if (!isReg) continue;

        const char* ext = strrchr(entry->d_name, '.');
        if (!ext) continue;
        // .ttc (TrueType Collection) is how macOS ships most system faces
        // (Helvetica.ttc, etc.); TTF_OpenFont opens face 0 of a collection.
        if (strcasecmp(ext, ".ttf") != 0 && strcasecmp(ext, ".otf") != 0 &&
            strcasecmp(ext, ".ttc") != 0)
            continue;

        TTF_Font* font = TTF_OpenFont(child, 12);
        if (!font) {
            UI_WARN(UI_CAT_FONT, "failed to load font '%s': %s", child, SDL_GetError());
            continue;
        }

        const char* familyName = TTF_GetFontFamilyName(font);
        char* familyDup = strdup(familyName ? familyName : "Unknown Family");
        TTF_CloseFont(font);
        if (!familyDup) continue;

        FontEntry** grown = realloc(UIFonts, sizeof(FontEntry*) * ((*count) + 1));
        if (!grown) { free(familyDup); continue; }
        UIFonts = grown;
        UIFonts[*count] = malloc(sizeof(FontEntry));
        if (!UIFonts[*count]) { free(familyDup); continue; }
        UIFonts[*count]->family_name = familyDup;
        UIFonts[*count]->file_path   = strdup(child);
        if (!UIFonts[*count]->file_path) {
            free(UIFonts[*count]->family_name);
            free(UIFonts[*count]);
            continue;
        }
        (*count)++;
    }

    closedir(dir);
}
#endif // !_WIN32

void UISearchFonts() {
    int fontCount = 0;
    UIFonts = NULL;

    if (TTF_Init() != 1) {
        UI_ERROR(UI_CAT_FONT, "TTF_Init failed: %s", SDL_GetError());
        return;
    }

#ifdef _WIN32
    char searchPath[512] = {0};
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
            UI_WARN(UI_CAT_FONT, "failed to load font '%s': %s", fontPath, SDL_GetError());
            continue;
        }

        // Extract the real family name from the font
        const char* family_name = TTF_GetFontFamilyName(font);
        char* family_name_copy = NULL;
        if (family_name) {
            family_name_copy = _strdup(family_name);
        } else {
            UI_WARN(UI_CAT_FONT, "no family name found in font '%s'", fontPath);
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
        // printf("Font loaded: %s, Path: %s\n", family_name_copy, fontPath);
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

#elif defined(MOCIDA_IOS)
    // iOS apps are sandboxed and cannot read /System/Library/Fonts, so the
    // only fonts available are the ones bundled INSIDE the .app. The build
    // copies them (custom fonts from the repo's fonts/ dir + a default
    // fallback) into <app>/fonts, and SDL_GetBasePath() returns the bundle
    // resource root at runtime. Walk that fonts/ dir; everything found —
    // including the user's custom faces — is registered for UIGetFont().
    {
        const char* base = SDL_GetBasePath();   // bundle root, owned by SDL
        if (base) {
            char fontsDir[1024];
            snprintf(fontsDir, sizeof(fontsDir), "%sfonts", base);
            WalkFontsDir(fontsDir, &fontCount);
            // Fallback: some fonts may sit at the bundle root directly.
            if (fontCount == 0) WalkFontsDir(base, &fontCount);
        }
    }

    FontEntry** temp = realloc(UIFonts, sizeof(FontEntry *) * (fontCount + 1));
    if (temp == NULL) {
        printf("Memory allocation failed for NULL termination\n");
        TTF_Quit();
        return;
    }
    UIFonts = temp;
    UIFonts[fontCount] = NULL;
#elif defined(__APPLE__)
    // macOS spreads fonts across several roots, and the family the host app
    // asks for (e.g. "Arial") is usually NOT in /Library/Fonts — it lives in
    // /System/Library/Fonts/Supplemental. Walk every root recursively so the
    // family lookup in UIGetFont can actually find them. Without this, text
    // widgets silently render nothing because their font path never resolves.
    WalkFontsDir("/System/Library/Fonts", &fontCount);  // incl. Supplemental/
    WalkFontsDir("/Library/Fonts", &fontCount);         // third-party / Office
    const char* home = getenv("HOME");
    if (home) {
        char userFonts[1024];
        snprintf(userFonts, sizeof(userFonts), "%s/Library/Fonts", home);
        WalkFontsDir(userFonts, &fontCount);
    }

    FontEntry** temp = realloc(UIFonts, sizeof(FontEntry *) * (fontCount + 1));
    if (temp == NULL) {
        printf("Memory allocation failed for NULL termination\n");
        TTF_Quit();
        return;
    }
    UIFonts = temp;
    UIFonts[fontCount] = NULL;
#elif defined(__linux__) || defined(__FreeBSD__)
    // Linux distros nest fonts under category subdirs (truetype/, opentype/,
    // X11/, vendor-specific dirs, ...), so a single-level opendir of
    // /usr/share/fonts returns ZERO .ttf entries — only those subdirs.
    // Walk the tree recursively. .otf is accepted alongside .ttf because
    // SDL_ttf 3 opens both via the same path.
    WalkFontsDir(SYSTEM_FONTS_PATH, &fontCount);

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
    TTF_Quit();
}

// Default font path used by widgets that don't set an explicit family.
// On desktop this is a fixed system path (DEFAULT_FONT_PATH). On iOS there
// are no readable system fonts, so the default is the first font discovered
// in the app bundle by UISearchFonts (a bundled fallback, or the user's own
// custom font if they shipped one) — call UISearchFonts() first.
const char* UIGetDefaultFontPath(void) {
#ifdef MOCIDA_IOS
    if (UIFonts && UIFonts[0]) return UIFonts[0]->file_path;
    return NULL;
#else
    return DEFAULT_FONT_PATH;
#endif
}

char* UIGetFont(const char* family_name) {
    if (UIFonts == NULL || sizeof(UIFonts) == 0) {
        UI_WARN(UI_CAT_FONT, "fonts not initialized — call UISearchFonts() first");
        return NULL;
    }

    if (family_name == NULL) {
        UI_WARN(UI_CAT_FONT, "family name is NULL");
        return NULL;
    }

    for (int i = 0; UIFonts[i] != NULL; i++) {
        FontEntry* fontEntry = UIFonts[i];
        if (fontEntry == NULL || fontEntry->family_name == NULL || fontEntry->file_path == NULL) {
            UI_WARN(UI_CAT_FONT, "invalid font entry at index %d", i);
            continue;
        }

        if (strcmp(fontEntry->family_name, family_name) == 0) {
            return fontEntry->file_path;
        }
    }

    UI_WARN(UI_CAT_FONT, "font family '%s' not found", family_name);
    return NULL; 
}

void UIFont_Destroy(FontEntry* font) {
    if (font == NULL) return;
    free(font->family_name);
    free(font->file_path);
    free(font);
}

void UIFonts_Destroy() {
    if (UIFonts == NULL) return;

    for (int i = 0; UIFonts[i] != NULL; i++) {
        FontEntry* fontEntry = UIFonts[i];
        if (fontEntry == NULL) continue;
        UIFont_Destroy(fontEntry);
    }
    free(UIFonts);
    UIFonts = NULL;
}