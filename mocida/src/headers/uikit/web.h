#ifndef UIKIT_WEB_H
#define UIKIT_WEB_H

/**
 * Browser identity, viewport, safe areas, cookies and cache preference.
 *
 * On a desktop build every call is a no-op and `UIWeb_IsWeb` is 0.
 * On the web build the values come from the page that hosts the canvas
 * (visualViewport, CSS env safe-area insets, User-Agent, …).
 *
 * A cookie named `mocidad_cache` with value `0` asks the site not to
 * cache. Any other value, or no cookie, leaves caching on.
 */

int UIWeb_IsWeb(void);

/**
 * Browser CSS viewport in CSS pixels. Prefer `visualViewport` when
 * present (iOS Safari URL bar / Android chrome), else `innerWidth` /
 * `innerHeight`. Always at least 1×1 on web; 0×0 on desktop.
 */
void UIWeb_GetViewport(int* out_w, int* out_h);

/**
 * CSS `env(safe-area-inset-*)` in CSS pixels (notch, home indicator,
 * status bar). All zeros on desktop or when the page has no
 * `viewport-fit=cover`.
 */
void UIWeb_GetSafeArea(int* out_top, int* out_right, int* out_bottom, int* out_left);

/** `window.devicePixelRatio`, clamped to [1, 3]. Desktop returns 1. */
float UIWeb_PixelRatio(void);

/** `phone`, `tablet` or `desktop`. Desktop builds return `desktop`. */
const char* UIWeb_Device(void);

/** 1 when the browser reports a touch screen (or coarse pointer). */
int UIWeb_IsTouch(void);

/** 1 when User-Agent / platform looks like iOS (iPhone/iPad/iPod). */
int UIWeb_IsIOS(void);

/** 1 when User-Agent looks like Android. */
int UIWeb_IsAndroid(void);

/** 1 when `prefers-reduced-motion: reduce` is set. */
int UIWeb_PrefersReducedMotion(void);

const char* UIWeb_UserAgent(void);

/**
 * Stores a cookie for this origin. `max_age_seconds` of 0 deletes it.
 * The name and value may not contain `;` or a newline. Returns 1 on
 * the web, 0 on desktop.
 */
int UIWeb_SetCookie(const char* name, const char* value, int max_age_seconds);

/** The cookie value, or "" when it is absent. */
const char* UIWeb_GetCookie(const char* name);

void UIWeb_DeleteCookie(const char* name);

/** 1 when the page asked to keep cached responses. */
int UIWeb_CacheEnabled(void);

/** Writes the `mocidad_cache` cookie. 0 turns caching off, 1 turns it on. */
void UIWeb_SetCacheEnabled(int enabled);

#endif
