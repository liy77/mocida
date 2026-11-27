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

void UIWeb_GetViewport(int* out_w, int* out_h) {
    /* Match listen_boot.js viewportSize(): prefer layout clientWidth/Height.
     * Using visualViewport alone while the canvas is sized to the layout
     * viewport (or the reverse) shifts every anchored HUD chip — classic
     * "JOGAR bleeds off the right edge" on iPhone Safari. */
    int w = EM_ASM_INT({
        var doc = document.documentElement;
        var n = (doc && doc.clientWidth) || window.innerWidth || 0;
        var vv = window.visualViewport;
        if (vv && vv.width > 0) {
            var scale = vv.scale || 1;
            if (scale > 0.95 && scale < 1.05) n = Math.min(n, vv.width);
        }
        if (window.innerWidth > 0) n = Math.min(n, window.innerWidth);
        return Math.max(1, Math.round(n));
    });
    int h = EM_ASM_INT({
        var doc = document.documentElement;
        var n = (doc && doc.clientHeight) || window.innerHeight || 0;
        var vv = window.visualViewport;
        if (vv && vv.height > 0) {
            var scale = vv.scale || 1;
            if (scale > 0.95 && scale < 1.05) n = Math.min(n, vv.height);
        }
        if (window.innerHeight > 0) n = Math.min(n, window.innerHeight);
        return Math.max(1, Math.round(n));
    });
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}

void UIWeb_GetSafeArea(int* out_top, int* out_right, int* out_bottom, int* out_left) {
    int t = EM_ASM_INT({
        try {
            var d = document.getElementById("__mocida_safe");
            if (!d) {
                d = document.createElement("div");
                d.id = "__mocida_safe";
                d.setAttribute("aria-hidden", "true");
                d.style.cssText = "position:fixed;left:0;top:0;width:0;height:0;visibility:hidden;pointer-events:none;"
                    + "padding-top:env(safe-area-inset-top,0px);"
                    + "padding-right:env(safe-area-inset-right,0px);"
                    + "padding-bottom:env(safe-area-inset-bottom,0px);"
                    + "padding-left:env(safe-area-inset-left,0px);";
                (document.documentElement || document.body).appendChild(d);
            }
            var cs = getComputedStyle(d);
            return Math.max(0, Math.round(parseFloat(cs.paddingTop) || 0));
        } catch (e) { return 0; }
    });
    int r = EM_ASM_INT({
        try {
            var d = document.getElementById("__mocida_safe");
            if (!d) return 0;
            return Math.max(0, Math.round(parseFloat(getComputedStyle(d).paddingRight) || 0));
        } catch (e) { return 0; }
    });
    int b = EM_ASM_INT({
        try {
            var d = document.getElementById("__mocida_safe");
            if (!d) return 0;
            return Math.max(0, Math.round(parseFloat(getComputedStyle(d).paddingBottom) || 0));
        } catch (e) { return 0; }
    });
    int l = EM_ASM_INT({
        try {
            var d = document.getElementById("__mocida_safe");
            if (!d) return 0;
            return Math.max(0, Math.round(parseFloat(getComputedStyle(d).paddingLeft) || 0));
        } catch (e) { return 0; }
    });
    if (out_top) *out_top = t;
    if (out_right) *out_right = r;
    if (out_bottom) *out_bottom = b;
    if (out_left) *out_left = l;
}

float UIWeb_PixelRatio(void) {
    double r = EM_ASM_DOUBLE({
        var dpr = window.devicePixelRatio || 1;
        if (dpr < 1) dpr = 1;
        if (dpr > 3) dpr = 3;
        return dpr;
    });
    return (float)r;
}

const char* UIWeb_Device(void) {
    EM_ASM({
        var ua = navigator.userAgent || "";
        var w = (window.visualViewport && window.visualViewport.width)
            ? window.visualViewport.width : (window.innerWidth || 0);
        var touch = (navigator.maxTouchPoints || 0) > 0
            || (window.matchMedia && window.matchMedia("(pointer: coarse)").matches);
        var kind = "desktop";
        if (/iPad|Tablet/i.test(ua) || (touch && w >= 768 && w < 1200)) kind = "tablet";
        else if (/Mobi|iPhone|Android/i.test(ua) || (touch && w < 768)) kind = "phone";
        stringToUTF8(kind, $0, 32);
    }, g_buf);
    return g_buf;
}

int UIWeb_IsTouch(void) {
    return EM_ASM_INT({
        if ((navigator.maxTouchPoints || 0) > 0) return 1;
        try {
            if (window.matchMedia && window.matchMedia("(pointer: coarse)").matches) return 1;
        } catch (e) {}
        return 0;
    });
}

int UIWeb_IsIOS(void) {
    return EM_ASM_INT({
        var ua = navigator.userAgent || "";
        var plat = navigator.platform || "";
        if (/iPhone|iPad|iPod/i.test(ua)) return 1;
        /* iPadOS 13+ reports as MacIntel with touch */
        if (plat === "MacIntel" && (navigator.maxTouchPoints || 0) > 1) return 1;
        return 0;
    });
}

int UIWeb_IsAndroid(void) {
    return EM_ASM_INT({
        return /Android/i.test(navigator.userAgent || "") ? 1 : 0;
    });
}

int UIWeb_PrefersReducedMotion(void) {
    return EM_ASM_INT({
        try {
            return (window.matchMedia
                && window.matchMedia("(prefers-reduced-motion: reduce)").matches) ? 1 : 0;
        } catch (e) { return 0; }
    });
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
void UIWeb_GetViewport(int* out_w, int* out_h) {
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
}
void UIWeb_GetSafeArea(int* out_top, int* out_right, int* out_bottom, int* out_left) {
    if (out_top) *out_top = 0;
    if (out_right) *out_right = 0;
    if (out_bottom) *out_bottom = 0;
    if (out_left) *out_left = 0;
}
float UIWeb_PixelRatio(void) { return 1.0f; }
const char* UIWeb_Device(void) { return "desktop"; }
int UIWeb_IsTouch(void) { return 0; }
int UIWeb_IsIOS(void) { return 0; }
int UIWeb_IsAndroid(void) { return 0; }
int UIWeb_PrefersReducedMotion(void) { return 0; }
const char* UIWeb_UserAgent(void) { return ""; }
int UIWeb_SetCookie(const char* name, const char* value, int max_age_seconds) {
    (void)name; (void)value; (void)max_age_seconds; return 0;
}
const char* UIWeb_GetCookie(const char* name) { (void)name; g_buf[0] = 0; return g_buf; }
void UIWeb_DeleteCookie(const char* name) { (void)name; }
int UIWeb_CacheEnabled(void) { return 1; }
void UIWeb_SetCacheEnabled(int enabled) { (void)enabled; }

#endif
