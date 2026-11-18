#include <uikit/web.h>
#include <SDL3/SDL.h>
#include <string.h>

static char g_buf[512];
static char g_ua[512];

static int NameOk(const char* s) {
    if (!s || !s[0] || SDL_strlen(s) > 64) return 0;
    for (const char* p = s; *p; p++) {
        if (*p == ';' || *p == '=' || *p == '\r' || *p == '\n' || *p == ' ') return 0;
    }
    return 1;
}

#if defined(MOCIDA_WEB)
#include <emscripten.h>

int UIWeb_IsWeb(void) { return 1; }

const char* UIWeb_Device(void) {
    EM_ASM({
        var ua = navigator.userAgent || "";
        var w = window.innerWidth || 0;
        var touch = (navigator.maxTouchPoints || 0) > 0;
        var kind = "desktop";
        if (/iPad|Tablet/i.test(ua) || (touch && w >= 768 && w < 1200)) kind = "tablet";
        else if (/Mobi|iPhone|Android/i.test(ua) || (touch && w < 768)) kind = "phone";
        stringToUTF8(kind, $0, 32);
    }, g_buf);
    return g_buf;
}

int UIWeb_IsTouch(void) {
    return EM_ASM_INT({ return (navigator.maxTouchPoints || 0) > 0 ? 1 : 0; });
}

const char* UIWeb_UserAgent(void) {
    EM_ASM({ stringToUTF8(navigator.userAgent || "", $0, 512); }, g_ua);
    return g_ua;
}

int UIWeb_SetCookie(const char* name, const char* value, int max_age_seconds) {
    if (!NameOk(name) || !value) return 0;
    if (SDL_strstr(value, ";") || SDL_strstr(value, "\n") || SDL_strstr(value, "\r")) return 0;
    EM_ASM({
        var name = UTF8ToString($0);
        var value = UTF8ToString($1);
        var age = $2;
        document.cookie = name + "=" + encodeURIComponent(value)
            + "; path=/; max-age=" + age + "; SameSite=Lax";
    }, name, value, max_age_seconds);
    return 1;
}

const char* UIWeb_GetCookie(const char* name) {
    g_buf[0] = 0;
    if (!NameOk(name)) return g_buf;
    EM_ASM({
        var name = UTF8ToString($0);
        var parts = (document.cookie || "").split("; ");
        var found = "";
        for (var i = 0; i < parts.length; i++) {
            var eq = parts[i].indexOf("=");
            if (eq > 0 && parts[i].substring(0, eq) === name) {
                found = decodeURIComponent(parts[i].substring(eq + 1));
                break;
            }
        }
        stringToUTF8(found, $1, $2);
    }, name, g_buf, (int)sizeof(g_buf));
    return g_buf;
}

void UIWeb_DeleteCookie(const char* name) {
    UIWeb_SetCookie(name, "", 0);
}

int UIWeb_CacheEnabled(void) {
    const char* v = UIWeb_GetCookie("mocidad_cache");
    return !(v && v[0] == '0');
}

void UIWeb_SetCacheEnabled(int enabled) {
    UIWeb_SetCookie("mocidad_cache", enabled ? "1" : "0", 60 * 60 * 24 * 365);
}

#else

int UIWeb_IsWeb(void) { return 0; }
const char* UIWeb_Device(void) { return "desktop"; }
int UIWeb_IsTouch(void) { return 0; }
const char* UIWeb_UserAgent(void) { return ""; }
int UIWeb_SetCookie(const char* name, const char* value, int max_age_seconds) {
    (void)name; (void)value; (void)max_age_seconds; return 0;
}
const char* UIWeb_GetCookie(const char* name) { (void)name; g_buf[0] = 0; return g_buf; }
void UIWeb_DeleteCookie(const char* name) { (void)name; }
int UIWeb_CacheEnabled(void) { return 1; }
void UIWeb_SetCacheEnabled(int enabled) { (void)enabled; }

#endif
