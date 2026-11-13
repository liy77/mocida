// ---------------------------------------------------------------------
// UI automation: drive a running app through HandleEvent (the same path
// real input takes) without touching the OS cursor, keyboard focus or
// window order — pair with MOCIDA_WINDOW_HIDDEN=1 to test in the
// background. Included by app.c.
//
// Two front ends share one command set:
//   MOCIDA_SCRIPT=file     replay a script (one command per line, `#` = comment)
//   MOCIDA_CONTROL=path    live Unix-socket control: send a command line, read
//                          the reply lines up to a line holding only "."
//                          (tools/mocida-ctl is a ready-made client)
//
// Selectors pick visible, on-screen widgets instead of coordinates:
//   "Run"                text, button label, placeholder, tooltip or id == Run
//   ~"Assets"            same fields, contains (case-insensitive)
//   text:"..."  tip:"..."  id:name  type:button  img:~"logo"
//                        restrict to one field (type/img match the widget
//                        type suffix / image source)
//   tip:*                any widget with a tooltip (`*` = any non-empty value)
//   ... #2               the 2nd match (tree order; default the 1st)
// Matching is case-insensitive and treats whitespace runs as one space.
//
// Commands (X Y = window points; SEL = selector):
//   click SEL | click X Y [right|middle]     dclick / rclick / hover likewise
//   scroll SEL DY                            wheel over the widget (+ = up)
//   drag X1 Y1 X2 Y2
//   key [ctrl+][shift+][alt+][cmd+]NAME      e.g. key cmd+s, key Escape
//   keydown NAME / keyup NAME                hold / release a key (UIInput state)
//   text STRING                              text input (rest of the line)
//   find SEL                                 list every match with bounds
//   tree [~"filter"]                         list labeled visible widgets
//   expect SEL / expect-not SEL              assert; failures are counted
//   waitfor SEL [timeoutMs]                  wait until SEL shows (3000 ms)
//   waitfor-not SEL [timeoutMs]              wait until SEL is gone
//   wait MS | shot PATH | menu ID | log MSG | quit
// ---------------------------------------------------------------------

#include <stdarg.h>
#include <uikit/glass.h>
#include <uikit/image.h>

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(MOCIDA_WEB)
#define MOCIDA_AUTOMATION_SOCKET 1
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

typedef struct {
    UIWidget*   w;
    const char* type;    // type suffix after "@uikit/"
    const char* text;    // text / button label / field text
    const char* alt;     // placeholder / image source
    const char* tip;     // tooltip attached to this widget
    int         depth;
    SDL_FRect   clip;    // visible region (enclosing Scroll viewports)
} AutoNode;

static AutoNode* g_autoNodes;
static int       g_autoCount, g_autoCap;

static UIChildren* Auto_Children(UIWidgetBase* base) {
    const char* t = base->__widget_type;
    if (!strcmp(t, UI_WIDGET_STACK))     return ((UIStack*)base)->items;
    if (!strcmp(t, UI_WIDGET_GLASS))     return ((UIGlass*)base)->items;
    if (!strcmp(t, UI_WIDGET_GRID))      return ((UIGrid*)base)->items;
    if (!strcmp(t, UI_WIDGET_RECTANGLE)) return (UIChildren*)((UIRectangle*)base)->children;
    if (!strcmp(t, UI_WIDGET_DIALOG))    return ((UIDialog*)base)->content;
    return NULL;
}

static void Auto_Push(UIWidget* w, UIWidgetBase* base, int depth, SDL_FRect clip) {
    if (g_autoCount == g_autoCap) {
        g_autoCap = g_autoCap ? g_autoCap * 2 : 256;
        g_autoNodes = (AutoNode*)realloc(g_autoNodes, sizeof(AutoNode) * (size_t)g_autoCap);
    }
    AutoNode* n = &g_autoNodes[g_autoCount++];
    SDL_zerop(n);
    n->w = w;
    n->depth = depth;
    n->clip = clip;
    const char* t = base->__widget_type;
    n->type = !strncmp(t, "@uikit/", 7) ? t + 7 : t;
    if (!strcmp(t, UI_WIDGET_TEXT)) {
        n->text = ((UIText*)base)->text;
    } else if (!strcmp(t, UI_WIDGET_BUTTON)) {
        UIButton* b = (UIButton*)base;
        n->text = b->label ? b->label->text : NULL;
    } else if (!strcmp(t, UI_WIDGET_TEXTFIELD)) {
        UITextField* tf = (UITextField*)base;
        n->text = tf->text;
        n->alt = tf->placeholder;
    } else if (!strcmp(t, UI_WIDGET_IMAGE)) {
        n->alt = ((UIImage*)base)->source;
    }
}

static void Auto_Walk(UIChildren* c, int depth, SDL_FRect clip) {
    if (!c) return;
    for (int i = 0; i < c->count; i++) {
        UIWidget* w = c->children[i];
        if (!w || !w->data || !w->visible) continue;
        UIWidgetBase* base = (UIWidgetBase*)w->data;
        if (!base->__widget_type) continue;
        if (!strcmp(base->__widget_type, UI_WIDGET_TOOLTIP)) continue;
        Auto_Push(w, base, depth, clip);
        if (!strcmp(base->__widget_type, UI_WIDGET_SCROLL)) {
            // Content outside the viewport is scrolled out of view.
            const SDL_FRect vp = { w->x, w->y, w->width ? *w->width : 0.0f, w->height ? *w->height : 0.0f };
            SDL_FRect inner;
            if (!SDL_GetRectIntersectionFloat(&clip, &vp, &inner)) inner = (SDL_FRect){ 0, 0, 0, 0 };
            UIWidget* content = ((UIScroll*)base)->content;
            if (content && content->data && content->visible) {
                Auto_Push(content, (UIWidgetBase*)content->data, depth + 1, inner);
                Auto_Walk(Auto_Children((UIWidgetBase*)content->data), depth + 2, inner);
            }
        } else {
            Auto_Walk(Auto_Children(base), depth + 1, clip);
        }
    }
}

// Snapshot of the visible tree; tooltips attach their text to their target.
static void Auto_Collect(UIApp* app) {
    g_autoCount = 0;
    UIChildren* root = app->window ? app->window->children : NULL;
    const SDL_FRect screen = { 0, 0, (float)app->window->width, (float)app->window->height };
    Auto_Walk(root, 0, screen);
    if (!root) return;
    for (int i = 0; i < root->count; i++) {
        UIWidget* w = root->children[i];
        if (!w || !w->data) continue;
        UIWidgetBase* base = (UIWidgetBase*)w->data;
        if (!base->__widget_type || strcmp(base->__widget_type, UI_WIDGET_TOOLTIP)) continue;
        UITooltip* tt = (UITooltip*)base;
        for (int k = 0; k < g_autoCount; k++) {
            if (g_autoNodes[k].w == tt->target) { g_autoNodes[k].tip = tt->text; break; }
        }
    }
}

static void Auto_Bounds(const UIWidget* w, float* x, float* y, float* bw, float* bh) {
    *x = w->x;
    *y = w->y;
    *bw = w->width ? *w->width : 0.0f;
    *bh = w->height ? *w->height : 0.0f;
}

static int Auto_OnScreen(const AutoNode* n) {
    float x, y, bw, bh;
    Auto_Bounds(n->w, &x, &y, &bw, &bh);
    if (bw <= 0.0f || bh <= 0.0f) return 0;
    const float cx = x + bw * 0.5f, cy = y + bh * 0.5f;
    return cx >= n->clip.x && cy >= n->clip.y &&
           cx < n->clip.x + n->clip.w && cy < n->clip.y + n->clip.h;
}

typedef struct {
    char field[8];       // "", text, tip, id, type, img
    int  contains;
    char value[256];
    int  index;          // 1-based
} AutoSelector;

// Returns 1 if `s` parses as a selector. Accepts quoted or bare values.
static int Auto_ParseSelector(const char* s, AutoSelector* sel, const char** rest) {
    SDL_zerop(sel);
    sel->index = 1;
    while (*s == ' ') s++;
    const char* colon = NULL;
    for (const char* p = s; *p && *p != ' ' && *p != '"' && *p != '~'; p++) {
        if (*p == ':') { colon = p; break; }
    }
    if (colon && colon - s < (long)sizeof(sel->field)) {
        SDL_memcpy(sel->field, s, (size_t)(colon - s));
        s = colon + 1;
    }
    if (*s == '~') { sel->contains = 1; s++; }
    size_t n = 0;
    if (*s == '"') {
        s++;
        while (*s && *s != '"' && n + 1 < sizeof(sel->value)) sel->value[n++] = *s++;
        if (*s == '"') s++;
    } else {
        while (*s && *s != ' ' && n + 1 < sizeof(sel->value)) sel->value[n++] = *s++;
    }
    sel->value[n] = '\0';
    while (*s == ' ') s++;
    if (*s == '#') {
        sel->index = SDL_max(1, SDL_atoi(s + 1));
        while (*s && *s != ' ') s++;
        while (*s == ' ') s++;
    }
    if (rest) *rest = s;
    return n > 0;
}

// Copies `s` trimmed, with whitespace runs collapsed to one space, so a label
// laid out as "Textures  5" matches "Textures 5".
static void Auto_Squash(const char* s, char* out, size_t cap) {
    size_t n = 0;
    int space = 1;
    for (; *s && n + 1 < cap; s++) {
        const int ws = (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r');
        if (ws) { if (!space) out[n++] = ' '; space = 1; }
        else    { out[n++] = *s; space = 0; }
    }
    if (n && out[n - 1] == ' ') n--;
    out[n] = '\0';
}

static int Auto_StrMatch(const char* have, const AutoSelector* sel) {
    if (!have || !*have) return 0;
    if (!SDL_strcmp(sel->value, "*")) return 1;
    char h[512], v[256];
    Auto_Squash(have, h, sizeof(h));
    Auto_Squash(sel->value, v, sizeof(v));
    return sel->contains ? SDL_strcasestr(h, v) != NULL : SDL_strcasecmp(h, v) == 0;
}

static int Auto_NodeMatches(const AutoNode* n, const AutoSelector* sel) {
    const char* f = sel->field;
    if (!*f) {
        return Auto_StrMatch(n->text, sel) || Auto_StrMatch(n->tip, sel) ||
               Auto_StrMatch(n->alt, sel) || Auto_StrMatch(n->w->id, sel);
    }
    if (!strcmp(f, "text")) return Auto_StrMatch(n->text, sel) || Auto_StrMatch(n->alt, sel);
    if (!strcmp(f, "tip"))  return Auto_StrMatch(n->tip, sel);
    if (!strcmp(f, "id"))   return Auto_StrMatch(n->w->id, sel);
    if (!strcmp(f, "type")) return Auto_StrMatch(n->type, sel);
    if (!strcmp(f, "img"))  return !strcmp(n->type, "image") && Auto_StrMatch(n->alt, sel);
    return 0;
}

// The sel->index-th on-screen match, or NULL. `total` gets the match count.
static AutoNode* Auto_Find(UIApp* app, const AutoSelector* sel, int* total) {
    Auto_Collect(app);
    AutoNode* hit = NULL;
    int count = 0;
    for (int i = 0; i < g_autoCount; i++) {
        AutoNode* n = &g_autoNodes[i];
        if (!Auto_OnScreen(n) || !Auto_NodeMatches(n, sel)) continue;
        if (++count == sel->index) hit = n;
    }
    if (total) *total = count;
    return hit;
}

// ---- output -----------------------------------------------------------

typedef struct {
    const char* name;      // "script" / "control"
    int         fd;        // control client, -1 = stderr
    Uint64      waitUntil;
    int         inFlight;  // a command ran; its reply terminator is owed
    int         pending;   // waitfor in progress
    int         pendingGone; // waitfor-not: wait for the selector to vanish
    AutoSelector pendingSel;
    Uint64      pendingDeadline;
    int         failures;
} AutoCtx;

static void Auto_Out(AutoCtx* ctx, const char* fmt, ...) {
    char buf[1026];
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(buf, 1024, fmt, ap);
    va_end(ap);
#ifdef MOCIDA_AUTOMATION_SOCKET
    if (ctx->fd >= 0) {
        size_t len = strlen(buf);
        buf[len] = '\n';
        (void)!write(ctx->fd, buf, len + 1);
        return;
    }
#endif
    fprintf(stderr, "[mocida.%s] %s\n", ctx->name, buf);
}

static void Auto_Describe(AutoCtx* ctx, int idx, const AutoNode* n) {
    float x, y, bw, bh;
    Auto_Bounds(n->w, &x, &y, &bw, &bh);
    char label[200] = "";
    if (n->text && *n->text) SDL_snprintf(label, sizeof(label), " \"%.80s\"", n->text);
    else if (n->alt && *n->alt) SDL_snprintf(label, sizeof(label), " [%.80s]", n->alt);
    char extra[200] = "";
    if (n->tip) SDL_snprintf(extra, sizeof(extra), " tip=\"%.80s\"", n->tip);
    if (n->w->id) SDL_snprintf(extra + strlen(extra), sizeof(extra) - strlen(extra), " id=%s", n->w->id);
    Auto_Out(ctx, "%d %s%s%s @ %.0f,%.0f %.0fx%.0f", idx, n->type, label, extra, x, y, bw, bh);
}

// ---- input injection ---------------------------------------------------

static void Auto_Mouse(UIApp* app, Uint32 type, float x, float y, Uint8 button, Uint8 clicks) {
    SDL_Event e;
    SDL_zero(e);
    e.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        e.motion.x = x; e.motion.y = y;
    } else {
        e.button.x = x; e.button.y = y;
        e.button.button = button;
        e.button.down = (type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        e.button.clicks = clicks;
    }
    HandleEvent(app, &e);
}

typedef struct { SDL_Keycode key; SDL_Keymod mod; uint32_t id; int found; } AutoChord;

// Menu shortcuts look like "Cmd+s", "Shift+Cmd+l", "CmdOrCtrl+=", "Cmd++".
static void Auto_MatchShortcut(const char* sc, uint32_t id, void* user) {
    AutoChord* c = (AutoChord*)user;
    if (c->found || !sc || !*sc || id >= UI_MENU_BAR_ROLE_FIRST) return;
    char tmp[64];
    SDL_strlcpy(tmp, sc, sizeof(tmp));
    SDL_Keymod want = SDL_KMOD_NONE;
    char* p = tmp;
    for (char* plus = SDL_strchr(p, '+'); plus && plus != p; plus = SDL_strchr(p, '+')) {
        *plus = '\0';
        if (!SDL_strcasecmp(p, "cmd") || !SDL_strcasecmp(p, "command") ||
            !SDL_strcasecmp(p, "cmdorctrl") || !SDL_strcasecmp(p, "super")) want |= SDL_KMOD_GUI;
        else if (!SDL_strcasecmp(p, "ctrl") || !SDL_strcasecmp(p, "control")) want |= SDL_KMOD_CTRL;
        else if (!SDL_strcasecmp(p, "shift")) want |= SDL_KMOD_SHIFT;
        else if (!SDL_strcasecmp(p, "alt") || !SDL_strcasecmp(p, "option")) want |= SDL_KMOD_ALT;
        p = plus + 1;
    }
    const SDL_Keycode k = SDL_strlen(p) == 1 ? (SDL_Keycode)SDL_tolower((unsigned char)p[0])
                                             : SDL_GetKeyFromName(p);
    SDL_Keymod have = SDL_KMOD_NONE;
    if (c->mod & SDL_KMOD_GUI)   have |= SDL_KMOD_GUI;
    if (c->mod & SDL_KMOD_CTRL)  have |= SDL_KMOD_CTRL;
    if (c->mod & SDL_KMOD_SHIFT) have |= SDL_KMOD_SHIFT;
    if (c->mod & SDL_KMOD_ALT)   have |= SDL_KMOD_ALT;
    if (k == c->key && have == want) {
        c->id = id;
        c->found = 1;
    }
}

static int Auto_Key(UIApp* app, const char* spec) {
    char tmp[128];
    SDL_strlcpy(tmp, spec, sizeof(tmp));
    SDL_Keymod mod = SDL_KMOD_NONE;
    char* name = tmp;
    for (char* plus = SDL_strchr(name, '+'); plus && plus[1]; plus = SDL_strchr(name, '+')) {
        *plus = '\0';
        if      (!SDL_strcasecmp(name, "ctrl"))  mod |= SDL_KMOD_LCTRL;
        else if (!SDL_strcasecmp(name, "shift")) mod |= SDL_KMOD_LSHIFT;
        else if (!SDL_strcasecmp(name, "alt"))   mod |= SDL_KMOD_LALT;
        else if (!SDL_strcasecmp(name, "cmd"))   mod |= SDL_KMOD_LGUI;
        name = plus + 1;
    }
    const SDL_Keycode key = SDL_GetKeyFromName(name);
    if (key == SDLK_UNKNOWN) return 0;
#ifdef __APPLE__
    // AppKit hands Cmd chords to the menu bar before the window sees them
    // (Cmd+A/C/V/Z reach text fields as the Edit menu's actions), so do the same.
    if (mod & SDL_KMOD_GUI) {
        AutoChord chord = { key, mod, 0, 0 };
        ui_menu_bar_foreach_item(Auto_MatchShortcut, &chord);
        if (chord.found) {
            ui_menu_bar_trigger(chord.id);
            return 1;
        }
    }
#endif
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = key;
    e.key.scancode = SDL_GetScancodeFromKey(key, NULL);
    e.key.mod = mod;
    e.key.down = true;
    HandleEvent(app, &e);
    return 1;
}

// Resolves "X Y ..." or a selector to a point. Returns 1 on success.
static int Auto_Target(AutoCtx* ctx, UIApp* app, const char* args, float* x, float* y,
                       const char** rest) {
    int consumed = 0;
    if (SDL_sscanf(args, "%f %f %n", x, y, &consumed) >= 2) {
        if (rest) *rest = args + consumed;
        return 1;
    }
    AutoSelector sel;
    if (!Auto_ParseSelector(args, &sel, rest)) {
        Auto_Out(ctx, "error: expected X Y or a selector");
        return 0;
    }
    int total = 0;
    AutoNode* n = Auto_Find(app, &sel, &total);
    if (!n) {
        Auto_Out(ctx, "FAIL no match for %s%s%s\"%s\" #%d (%d found)", sel.field,
                 *sel.field ? ":" : "", sel.contains ? "~" : "", sel.value, sel.index, total);
        ctx->failures++;
        return 0;
    }
    float bx, by, bw, bh;
    Auto_Bounds(n->w, &bx, &by, &bw, &bh);
    *x = bx + bw * 0.5f;
    *y = by + bh * 0.5f;
    return 1;
}

// ---- command execution -------------------------------------------------

static void Auto_Exec(AutoCtx* ctx, UIApp* app, const char* l) {
    const Uint64 now = SDL_GetTicks();
    char cmd[16] = {0};
    int consumed = 0;
    if (SDL_sscanf(l, "%15s %n", cmd, &consumed) < 1) return;
    const char* rest = l + consumed;
    float x = 0, y = 0, x2 = 0, y2 = 0;

    if (!SDL_strcmp(cmd, "wait")) {
        ctx->waitUntil = now + (Uint64)SDL_atoi(rest);
    } else if (!SDL_strcmp(cmd, "move") || !SDL_strcmp(cmd, "hover")) {
        if (Auto_Target(ctx, app, rest, &x, &y, NULL)) {
            Auto_Mouse(app, SDL_EVENT_MOUSE_MOTION, x, y, 0, 0);
            Auto_Out(ctx, "ok %.0f,%.0f", x, y);
        }
    } else if (!SDL_strcmp(cmd, "click") || !SDL_strcmp(cmd, "dclick") ||
               !SDL_strcmp(cmd, "rclick")) {
        const char* opt = "";
        if (Auto_Target(ctx, app, rest, &x, &y, &opt)) {
            Uint8 b = SDL_BUTTON_LEFT;
            if (cmd[0] == 'r' || !SDL_strncmp(opt, "right", 5)) b = SDL_BUTTON_RIGHT;
            if (!SDL_strncmp(opt, "middle", 6)) b = SDL_BUTTON_MIDDLE;
            const int times = cmd[0] == 'd' ? 2 : 1;
            Auto_Mouse(app, SDL_EVENT_MOUSE_MOTION, x, y, 0, 0);
            for (int i = 1; i <= times; i++) {
                Auto_Mouse(app, SDL_EVENT_MOUSE_BUTTON_DOWN, x, y, b, (Uint8)i);
                Auto_Mouse(app, SDL_EVENT_MOUSE_BUTTON_UP, x, y, b, (Uint8)i);
            }
            Auto_Out(ctx, "ok %.0f,%.0f", x, y);
            ctx->waitUntil = now + 50;   // let the host tick react
        }
    } else if (!SDL_strcmp(cmd, "drag") &&
               SDL_sscanf(rest, "%f %f %f %f", &x, &y, &x2, &y2) == 4) {
        Auto_Mouse(app, SDL_EVENT_MOUSE_MOTION, x, y, 0, 0);
        Auto_Mouse(app, SDL_EVENT_MOUSE_BUTTON_DOWN, x, y, SDL_BUTTON_LEFT, 1);
        for (int i = 1; i <= 8; i++) {
            Auto_Mouse(app, SDL_EVENT_MOUSE_MOTION, x + (x2 - x) * (float)i / 8.0f,
                       y + (y2 - y) * (float)i / 8.0f, 0, 0);
        }
        Auto_Mouse(app, SDL_EVENT_MOUSE_BUTTON_UP, x2, y2, SDL_BUTTON_LEFT, 1);
        ctx->waitUntil = now + 50;
    } else if (!SDL_strcmp(cmd, "wheel") && SDL_sscanf(rest, "%f %f %f", &x, &y, &x2) == 3) {
        SDL_Event e;
        SDL_zero(e);
        e.type = SDL_EVENT_MOUSE_WHEEL;
        e.wheel.mouse_x = x; e.wheel.mouse_y = y;
        e.wheel.y = x2;
        HandleEvent(app, &e);
    } else if (!SDL_strcmp(cmd, "scroll")) {
        const char* after = "";
        if (Auto_Target(ctx, app, rest, &x, &y, &after)) {
            SDL_Event e;
            SDL_zero(e);
            e.type = SDL_EVENT_MOUSE_WHEEL;
            e.wheel.mouse_x = x; e.wheel.mouse_y = y;
            e.wheel.y = (float)SDL_atof(after);
            Auto_Mouse(app, SDL_EVENT_MOUSE_MOTION, x, y, 0, 0);
            HandleEvent(app, &e);
            Auto_Out(ctx, "ok");
        }
    } else if ((!SDL_strcmp(cmd, "keydown") || !SDL_strcmp(cmd, "keyup")) && *rest) {
        UIInput_SetKey(rest, cmd[3] == 'd');
        Auto_Out(ctx, "ok");
    } else if (!SDL_strcmp(cmd, "key") && *rest) {
        if (Auto_Key(app, rest)) Auto_Out(ctx, "ok");
        else Auto_Out(ctx, "error: unknown key '%s'", rest);
        ctx->waitUntil = now + 30;
    } else if (!SDL_strcmp(cmd, "text") && *rest) {
        SDL_Event e;
        SDL_zero(e);
        e.type = SDL_EVENT_TEXT_INPUT;
        e.text.text = rest;
        HandleEvent(app, &e);
        Auto_Out(ctx, "ok");
    } else if (!SDL_strcmp(cmd, "find") || !SDL_strcmp(cmd, "tree")) {
        AutoSelector sel;
        const int filtered = Auto_ParseSelector(rest, &sel, NULL);
        const int all = !SDL_strcmp(cmd, "tree");
        if (!all && !filtered) { Auto_Out(ctx, "error: find needs a selector"); return; }
        Auto_Collect(app);
        int shown = 0;
        for (int i = 0; i < g_autoCount; i++) {
            AutoNode* n = &g_autoNodes[i];
            if (!Auto_OnScreen(n)) continue;
            if (filtered ? !Auto_NodeMatches(n, &sel)
                         : !((n->text && *n->text) || n->tip || n->w->id ||
                             (n->alt && *n->alt))) continue;
            Auto_Describe(ctx, ++shown, n);
        }
        if (!shown) Auto_Out(ctx, filtered ? "no match" : "empty");
    } else if (!SDL_strcmp(cmd, "expect") || !SDL_strcmp(cmd, "expect-not")) {
        AutoSelector sel;
        if (!Auto_ParseSelector(rest, &sel, NULL)) { Auto_Out(ctx, "error: expect needs a selector"); return; }
        int total = 0;
        const int found = Auto_Find(app, &sel, &total) != NULL;
        const int want = cmd[6] != '-';
        if (found == want) {
            Auto_Out(ctx, "ok %s", rest);
        } else {
            ctx->failures++;
            Auto_Out(ctx, "FAIL %s %s (%d found)", cmd, rest, total);
        }
    } else if (!SDL_strcmp(cmd, "waitfor") || !SDL_strcmp(cmd, "waitfor-not")) {
        const char* after = "";
        if (!Auto_ParseSelector(rest, &ctx->pendingSel, &after)) {
            Auto_Out(ctx, "error: waitfor needs a selector");
            return;
        }
        const int ms = *after ? SDL_atoi(after) : 3000;
        ctx->pending = 1;
        ctx->pendingGone = cmd[7] == '-';
        ctx->pendingDeadline = now + (Uint64)(ms > 0 ? ms : 3000);
    } else if (!SDL_strcmp(cmd, "shot") && *rest) {
        UIWindow_RequestShot(rest);
        Auto_Out(ctx, "ok %s", rest);
        ctx->waitUntil = now + 60;
    } else if (!SDL_strcmp(cmd, "menu") && *rest) {
        ui_menu_bar_trigger((uint32_t)SDL_atoi(rest));
        Auto_Out(ctx, "ok");
        ctx->waitUntil = now + 30;
    } else if (!SDL_strcmp(cmd, "log")) {
        Auto_Out(ctx, "%s", rest);
    } else if (!SDL_strcmp(cmd, "quit")) {
        if (ctx->failures) Auto_Out(ctx, "%d failure(s)", ctx->failures);
        Auto_Out(ctx, "bye");
        app->window->visible = 0;
        app->runInBackground = 0;
    } else {
        Auto_Out(ctx, "error: cannot parse '%s'", l);
    }
}

// Advances a pending waitfor / timed wait. Returns 1 while the context is busy.
static int Auto_Busy(AutoCtx* ctx, UIApp* app) {
    const Uint64 now = SDL_GetTicks();
    if (ctx->pending) {
        int total = 0;
        const int found = Auto_Find(app, &ctx->pendingSel, &total) != NULL;
        if (found != ctx->pendingGone) {
            ctx->pending = 0;
            Auto_Out(ctx, "ok \"%s\" %s", ctx->pendingSel.value,
                     ctx->pendingGone ? "gone" : "shown");
        } else if (now >= ctx->pendingDeadline) {
            ctx->pending = 0;
            ctx->failures++;
            Auto_Out(ctx, "FAIL %s \"%s\" timed out",
                     ctx->pendingGone ? "waitfor-not" : "waitfor", ctx->pendingSel.value);
        } else {
            return 1;
        }
    }
    return now < ctx->waitUntil;
}

// ---- script front end --------------------------------------------------

static struct {
    char**  lines;
    int     count;
    int     pc;
    int     loaded;
    AutoCtx ctx;
} g_script;

static void Script_Load(void) {
    g_script.loaded = 1;
    g_script.ctx.name = "script";
    g_script.ctx.fd = -1;
    const char* path = SDL_getenv("MOCIDA_SCRIPT");
    if (!path) return;
    size_t size = 0;
    char* buf = (char*)SDL_LoadFile(path, &size);
    if (!buf) {
        fprintf(stderr, "[mocida.script] cannot read %s: %s\n", path, SDL_GetError());
        return;
    }
    int cap = 64;
    g_script.lines = (char**)malloc(sizeof(char*) * (size_t)cap);
    char* save = NULL;
    for (char* l = SDL_strtok_r(buf, "\n", &save); l; l = SDL_strtok_r(NULL, "\n", &save)) {
        while (*l == ' ' || *l == '\t') l++;
        size_t n = strlen(l);
        while (n && (l[n - 1] == '\r' || l[n - 1] == ' ' || l[n - 1] == '\t')) l[--n] = '\0';
        if (!n || l[0] == '#') continue;
        if (g_script.count == cap) {
            cap *= 2;
            g_script.lines = (char**)realloc(g_script.lines, sizeof(char*) * (size_t)cap);
        }
        g_script.lines[g_script.count++] = SDL_strdup(l);
    }
    SDL_free(buf);
    fprintf(stderr, "[mocida.script] %s: %d commands\n", path, g_script.count);
}

static void Script_Step(UIApp* app) {
    if (!g_script.loaded) Script_Load();
    if (!g_script.lines) return;
    while (g_script.pc < g_script.count && !Auto_Busy(&g_script.ctx, app)) {
        Auto_Exec(&g_script.ctx, app, g_script.lines[g_script.pc++]);
    }
}

// ---- control socket front end -----------------------------------------

#ifdef MOCIDA_AUTOMATION_SOCKET
static struct {
    int     init;
    int     listenFd;
    char    path[108];
    char    buf[4096];
    size_t  len;
    AutoCtx ctx;
} g_control = { 0, -1 };

static void Control_Init(void) {
    g_control.init = 1;
    g_control.ctx.name = "control";
    g_control.ctx.fd = -1;
    const char* path = SDL_getenv("MOCIDA_CONTROL");
    if (!path || !*path) return;
    struct sockaddr_un addr;
    SDL_zero(addr);
    addr.sun_family = AF_UNIX;
    SDL_strlcpy(addr.sun_path, path, sizeof(addr.sun_path));
    SDL_strlcpy(g_control.path, path, sizeof(g_control.path));
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return;
    unlink(path);
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0 || listen(fd, 4) != 0) {
        fprintf(stderr, "[mocida.control] cannot listen on %s: %s\n", path, strerror(errno));
        close(fd);
        return;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    g_control.listenFd = fd;
    fprintf(stderr, "[mocida.control] listening on %s\n", path);
}

static void Control_Drop(void) {
    if (g_control.ctx.fd >= 0) close(g_control.ctx.fd);
    g_control.ctx.fd = -1;
    g_control.len = 0;
    g_control.ctx.inFlight = 0;
    g_control.ctx.pending = 0;
}

static void Control_Step(UIApp* app) {
    if (!g_control.init) Control_Init();
    if (g_control.listenFd < 0) return;
    AutoCtx* ctx = &g_control.ctx;

    if (ctx->fd < 0) {
        const int c = accept(g_control.listenFd, NULL, NULL);
        if (c < 0) return;
        fcntl(c, F_SETFL, fcntl(c, F_GETFL, 0) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
        const int one = 1;
        setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
        ctx->fd = c;
        ctx->failures = 0;
    }

    if (Auto_Busy(ctx, app)) return;
    if (ctx->inFlight) {
        ctx->inFlight = 0;
        (void)!write(ctx->fd, ".\n", 2);
    }

    if (g_control.len < sizeof(g_control.buf) - 1) {
        const ssize_t r = read(ctx->fd, g_control.buf + g_control.len,
                               sizeof(g_control.buf) - 1 - g_control.len);
        if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            Control_Drop();
            return;
        }
        if (r > 0) g_control.len += (size_t)r;
    }

    // One command per frame so each one sees the UI its predecessor produced.
    char* nl = memchr(g_control.buf, '\n', g_control.len);
    if (!nl) return;
    *nl = '\0';
    char line[4096];
    SDL_strlcpy(line, g_control.buf, sizeof(line));
    const size_t used = (size_t)(nl - g_control.buf) + 1;
    memmove(g_control.buf, g_control.buf + used, g_control.len - used);
    g_control.len -= used;

    char* l = line;
    while (*l == ' ' || *l == '\t') l++;
    size_t n = strlen(l);
    while (n && (l[n - 1] == '\r' || l[n - 1] == ' ')) l[--n] = '\0';
    ctx->inFlight = 1;
    if (n && l[0] != '#') Auto_Exec(ctx, app, l);
    if (!SDL_strncmp(l, "quit", 4)) {
        (void)!write(ctx->fd, ".\n", 2);
        Control_Drop();
        close(g_control.listenFd);
        g_control.listenFd = -1;
        unlink(g_control.path);
    }
}
#else
static void Control_Step(UIApp* app) { (void)app; }
#endif

static void Automation_Step(UIApp* app) {
    Script_Step(app);
    Control_Step(app);
}

// ---- text focus (used by hosts to route Edit ▸ Undo) ---------------------

static int Auto_TextFocused(UIChildren* c) {
    if (!c) return 0;
    for (int i = 0; i < c->count; i++) {
        UIWidget* w = c->children[i];
        if (!w || !w->data || !w->visible) continue;
        UIWidgetBase* base = (UIWidgetBase*)w->data;
        if (!base->__widget_type) continue;
        if (!strcmp(base->__widget_type, UI_WIDGET_TEXTFIELD) && ((UITextField*)base)->focused) return 1;
        if (!strcmp(base->__widget_type, UI_WIDGET_TEXTAREA) && ((UITextArea*)base)->focused) return 1;
        UIChildren* kids = Auto_Children(base);
        if (!strcmp(base->__widget_type, UI_WIDGET_SCROLL)) {
            UIWidget* content = ((UIScroll*)base)->content;
            if (content && content->data && content->visible) {
                kids = Auto_Children((UIWidgetBase*)content->data);
            }
        }
        if (Auto_TextFocused(kids)) return 1;
    }
    return 0;
}

int UIApp_TextHasFocusG(void) {
    return g_currentApp && g_currentApp->window && Auto_TextFocused(g_currentApp->window->children);
}

int UIApp_FindWidgetBoundsG(const char* selector, float* x, float* y, float* w, float* h) {
    if (!g_currentApp || !g_currentApp->window || !selector) return 0;
    AutoSelector sel;
    if (!Auto_ParseSelector(selector, &sel, NULL)) return 0;
    AutoNode* n = Auto_Find(g_currentApp, &sel, NULL);
    if (!n) return 0;
    float bx, by, bw, bh;
    Auto_Bounds(n->w, &bx, &by, &bw, &bh);
    if (x) *x = bx;
    if (y) *y = by;
    if (w) *w = bw;
    if (h) *h = bh;
    return 1;
}
