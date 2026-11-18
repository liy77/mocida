#ifndef UIKIT_WEB_H
#define UIKIT_WEB_H

/**
 * Browser identity, cookies and cache preference.
 *
 * On a desktop build every call is a no-op and `UIWeb_IsWeb` is 0.
 * On the web build the values come from the page that hosts the canvas.
 *
 * A cookie named `mocidad_cache` with value `0` asks the site not to
 * cache. Any other value, or no cookie, leaves caching on.
 */

int UIWeb_IsWeb(void);

/** `phone`, `tablet` or `desktop`. Desktop builds return `desktop`. */
const char* UIWeb_Device(void);

/** 1 when the browser reports a touch screen. */
int UIWeb_IsTouch(void);

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
