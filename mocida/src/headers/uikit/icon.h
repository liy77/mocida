#ifndef UIKIT_ICON_H
#define UIKIT_ICON_H

/**
 * UIIcon — convenience for toolbar / nav icons.
 *
 * Accepts:
 *   - SF Symbol names: `"play.fill"` → `sf://play.fill` **on Apple only**
 *   - Explicit SF URIs: `"sf://folder"` / `"symbol://folder"` (Apple only;
 *     elsewhere → `fallback` or a built-in `mocida://icons/…` map)
 *   - Image paths / `mocida://` / http(s) URLs (passed through on every OS)
 *   - Font glyphs via `UIIcon_CreateGlyph` (SDL_ttf — all platforms)
 *
 * The result is a normal `UIImage` (fit + tint). MUI exposes this as `Icon(...)`.
 */

#include <uikit/image.h>
#include <uikit/color.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * True when `name` looks like an SF Symbol name (no path separators, no
 * image extension, not already a URI).
 */
int UIIcon_LooksLikeSymbolName(const char* name);

/**
 * Resolve `name` to a loadable image source.
 * Returns a heap string the caller must `free`, or NULL on failure.
 *
 * @param name     Symbol name, `sf://…`, path, or `mocida://…`.
 * @param fallback Optional non-Apple / miss image (`mocida://icons/play.png`).
 *                 May be NULL — then a small built-in SF→PNG map is tried.
 *
 * Apple: bare / `sf://` names stay as SF Symbol URIs.
 * Other OS: never emit `sf://`; use `fallback` or the built-in PNG map.
 */
char* UIIcon_ResolveSourceEx(const char* name, const char* fallback);

/** Same as UIIcon_ResolveSourceEx(name, NULL). */
char* UIIcon_ResolveSource(const char* name);

/**
 * Create a tinted icon image (fill mode = FIT, antialiasing on).
 */
UIImage* UIIcon_Create(const char* name, UIColor tint);

/**
 * Like UIIcon_Create, with an explicit image fallback for non-Apple platforms
 * (and for Apple if you ever want a PNG instead of SF).
 */
UIImage* UIIcon_CreateWithFallback(const char* name, const char* fallback, UIColor tint);

/**
 * Create an icon from a single UTF-8 glyph drawn with `fontFamily`.
 * Works on every platform that has the font (not SF-Symbols-specific).
 */
UIImage* UIIcon_CreateGlyph(SDL_Renderer* renderer,
                             const char* glyph,
                             const char* fontFamily,
                             float pointSize,
                             UIColor tint);

#ifdef __cplusplus
}
#endif

#endif /* UIKIT_ICON_H */
