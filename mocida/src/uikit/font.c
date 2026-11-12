#include <uikit/font.h>
#include <uikit/debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define strcasecmp _stricmp
#else
#include <sys/stat.h>
#include <dirent.h>
#include <strings.h>   // strcasecmp (POSIX, not in <string.h>)
#endif

#include <SDL3_ttf/SDL_ttf.h>

#if defined(__APPLE__) && !defined(MOCIDA_IOS)
#include <CoreText/CoreText.h>
#define MOCIDA_FONT_CORETEXT 1
#endif

// Resolved family -> path, including misses (path = default), so repeated
// lookups of the same family are a short string compare instead of a scan.
typedef struct { char* family; const char* path; } FontLookup;
static FontLookup* s_lookups = NULL;
static int s_lookupCount = 0;

static const char* LookupCached(const char* family) {
    for (int i = 0; i < s_lookupCount; i++) {
        if (strcmp(s_lookups[i].family, family) == 0) return s_lookups[i].path;
    }
    return NULL;
}

static const char* RememberLookup(const char* family, const char* path) {
    FontLookup* grown = realloc(s_lookups, sizeof(FontLookup) * (size_t)(s_lookupCount + 1));
    if (!grown) return path;
    s_lookups = grown;
    s_lookups[s_lookupCount].family = strdup(family);
    s_lookups[s_lookupCount].path = path;
    if (s_lookups[s_lookupCount].family) s_lookupCount++;
    return path;
}

// Cross-platform stand-ins for families that only exist on one OS, so a UI
// authored on Windows ("Consolas", "Segoe UI") gets the native look elsewhere.
static const char* FamilyAlias(const char* family) {
    static const char* const map[][2] = {
#if defined(__APPLE__)
        {"Consolas", "SF Mono"},      {"Cascadia Code", "SF Mono"},
        {"Cascadia Mono", "SF Mono"}, {"Lucida Console", "SF Mono"},
        {"Courier New", "SF Mono"},
        {"Segoe UI", "SF Pro"},       {"Segoe UI Variable", "SF Pro"},
        {"Tahoma", "SF Pro"},         {"Verdana", "SF Pro"},
#else
        {"Menlo", "Consolas"},        {"SF Mono", "Consolas"},
        {"Helvetica Neue", "Segoe UI"}, {"Helvetica", "Arial"},
        {"San Francisco", "Segoe UI"},  {"SF Pro", "Segoe UI"},
        {"SF Pro Text", "Segoe UI"},    {"SF Pro Display", "Segoe UI"},
        {"system-ui", "Segoe UI"},
#endif
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (strcasecmp(map[i][0], family) == 0) return map[i][1];
    }
    return NULL;
}

#ifdef MOCIDA_FONT_CORETEXT
// Ask CoreText for the regular face of `family` (exact family match, no
// substitution). Returns a malloc'd file path or NULL. ~1 ms, versus opening
// every installed font file to read its family name.
static char* CoreTextFamilyPath(const char* family) {
    CFStringRef name = CFStringCreateWithCString(NULL, family, kCFStringEncodingUTF8);
    if (!name) return NULL;
    const void* keys[] = { kCTFontFamilyNameAttribute };
    const void* vals[] = { name };
    CFDictionaryRef attrs = CFDictionaryCreate(NULL, keys, vals, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CTFontDescriptorRef desc = attrs ? CTFontDescriptorCreateWithAttributes(attrs) : NULL;
    CFSetRef mandatory = CFSetCreate(NULL, keys, 1, &kCFTypeSetCallBacks);
    CFArrayRef faces = (desc && mandatory)
        ? CTFontDescriptorCreateMatchingFontDescriptors(desc, mandatory) : NULL;

    char* path = NULL;
    if (faces && CFArrayGetCount(faces) > 0) {
        CFIndex best = 0;
        double bestScore = 1e9;
        for (CFIndex i = 0; i < CFArrayGetCount(faces); i++) {
            CTFontDescriptorRef f = (CTFontDescriptorRef)CFArrayGetValueAtIndex(faces, i);
            double score = 0;
            CFDictionaryRef traits = CTFontDescriptorCopyAttribute(f, kCTFontTraitsAttribute);
            if (traits) {
                CFNumberRef w = CFDictionaryGetValue(traits, kCTFontWeightTrait);
                CFNumberRef sym = CFDictionaryGetValue(traits, kCTFontSymbolicTrait);
                double wv = 0;
                uint32_t sv = 0;
                if (w) CFNumberGetValue(w, kCFNumberDoubleType, &wv);
                if (sym) CFNumberGetValue(sym, kCFNumberSInt32Type, &sv);
                score = (wv < 0 ? -wv : wv) + ((sv & kCTFontItalicTrait) ? 10 : 0)
                      + ((sv & kCTFontCondensedTrait) ? 5 : 0);
                CFRelease(traits);
            }
            if (score < bestScore) { bestScore = score; best = i; }
        }
        CTFontDescriptorRef f = (CTFontDescriptorRef)CFArrayGetValueAtIndex(faces, best);
        CFURLRef url = CTFontDescriptorCopyAttribute(f, kCTFontURLAttribute);
        if (url) {
            char buf[1024];
            if (CFURLGetFileSystemRepresentation(url, true, (UInt8*)buf, sizeof(buf))) {
                path = strdup(buf);
            }
            CFRelease(url);
        }
    }
    if (faces) CFRelease(faces);
    if (mandatory) CFRelease(mandatory);
    if (desc) CFRelease(desc);
    if (attrs) CFRelease(attrs);
    CFRelease(name);
    return path;
}
#endif

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
            // A single unloadable face in the user's font dir (a broken file, an
            // unsupported variable-font axis table, etc.) is expected and not
            // actionable — the family simply won't be available. Log at DEBUG so
            // it doesn't flood the console at every launch.
            UI_DEBUG(UI_CAT_FONT, "skipping unloadable font '%s': %s", child, SDL_GetError());
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
    // Idempotent: if the registry is already populated, don't rescan. Rescanning
    // clears UIFonts to NULL first (below) and repopulates incrementally, which
    // opens a window where renders see no fonts and text disappears. A host that
    // genuinely needs to refresh must call UIFonts_Destroy() first (which NULLs
    // the registry), after which this repopulates normally.
    if (UIFonts != NULL) return;

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

#elif defined(MOCIDA_IOS) || defined(MOCIDA_WEB)
    // iOS apps are sandboxed and cannot read system fonts; Emscripten web
    // builds also preload bundled fonts into the virtual FS. In both cases
    // the only fonts available are the ones bundled inside the app/package
    // at <base>/fonts. Walk that directory; if nothing is found, fall back
    // to the base path.
    {
        const char* base = SDL_GetBasePath();
        if (base) {
            char fontsDir[1024];
            // SDL_GetBasePath() always returns a path with a trailing
            // separator on both iOS and Emscripten, so concatenating
            // "fonts" directly is safe.
            snprintf(fontsDir, sizeof(fontsDir), "%sfonts", base);
            WalkFontsDir(fontsDir, &fontCount);
            if (fontCount == 0) WalkFontsDir(base, &fontCount);
        }
    }

    FontEntry** temp = realloc(UIFonts, sizeof(FontEntry*) * (fontCount + 1));
    if (!temp) {
        UI_ERROR(UI_CAT_FONT, "out of memory finalizing bundled font list");
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
// On desktop this is a fixed system path (DEFAULT_FONT_PATH). On iOS and web
// there are no readable system fonts, so the default is the first font discovered
// in the app bundle by UISearchFonts (a bundled fallback, or the user's own
// custom font if they shipped one) — call UISearchFonts() first.
const char* UIGetDefaultFontPath(void) {
#if defined(MOCIDA_IOS) || defined(MOCIDA_WEB)
    if (UIFonts && UIFonts[0]) return UIFonts[0]->file_path;
    return NULL;
#else
    // DEFAULT_FONT_PATH probes candidate files with fopen on macOS/Linux; it
    // is asked for per text widget, so resolve it once.
    static const char* cached = NULL;
    if (!cached) cached = DEFAULT_FONT_PATH;
    return cached;
#endif
}

#ifndef MOCIDA_FONT_CORETEXT
// Look `family` up in the scanned registry (UISearchFonts). NULL if absent.
static const char* RegistryFamilyPath(const char* family) {
    // Lazily build the registry: hosts driving mocida directly (mui-runtime)
    // never call UISearchFonts() themselves.
    if (UIFonts == NULL) UISearchFonts();
    if (UIFonts == NULL) return NULL;
    for (int i = 0; UIFonts[i] != NULL; i++) {
        FontEntry* e = UIFonts[i];
        if (e && e->family_name && e->file_path && strcmp(e->family_name, family) == 0) {
            return e->file_path;
        }
    }
    return NULL;
}
#endif

#ifdef MOCIDA_FONT_CORETEXT
// macOS system families live in hidden (dot-prefixed) CoreText families, so a
// lookup by their public names fails; map those names to the files directly.
static const char* AppleSystemFontFile(const char* family) {
    static const char* const map[][2] = {
        {"SF Pro", "/System/Library/Fonts/SFNS.ttf"},
        {"SF Pro Text", "/System/Library/Fonts/SFNS.ttf"},
        {"SF Pro Display", "/System/Library/Fonts/SFNS.ttf"},
        {"San Francisco", "/System/Library/Fonts/SFNS.ttf"},
        {"system-ui", "/System/Library/Fonts/SFNS.ttf"},
        {"-apple-system", "/System/Library/Fonts/SFNS.ttf"},
        {"SF Mono", "/System/Library/Fonts/SFNSMono.ttf"},
        {"SF Pro Rounded", "/System/Library/Fonts/SFNSRounded.ttf"},
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (strcasecmp(map[i][0], family) == 0) {
            struct stat st;
            return stat(map[i][1], &st) == 0 ? map[i][1] : NULL;
        }
    }
    return NULL;
}
#endif

static const char* ResolveFamily(const char* family) {
#ifdef MOCIDA_FONT_CORETEXT
    const char* sys = AppleSystemFontFile(family);
    if (sys) return sys;
    // Owned by the lookup cache for the rest of the run.
    return CoreTextFamilyPath(family);
#else
    return RegistryFamilyPath(family);
#endif
}

char* UIGetFont(const char* family_name) {
    if (family_name == NULL || family_name[0] == '\0') {
        return (char*)UIGetDefaultFontPath();
    }
    const char* hit = LookupCached(family_name);
    if (hit) return (char*)hit;

    const char* path = ResolveFamily(family_name);
    if (!path) {
        const char* alias = FamilyAlias(family_name);
        if (alias) path = ResolveFamily(alias);
    }
    if (!path) {
        // Unknown family: draw with the default font rather than dropping the
        // text. Logged once per family (the miss is cached).
        UI_WARN(UI_CAT_FONT, "font family '%s' not found — falling back to default font", family_name);
        path = UIGetDefaultFontPath();
    }
    return (char*)RememberLookup(family_name, path);
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