#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>  // SetCurrentProcessExplicitAppUserModelID via shell32
#endif

#include <uikit/app.h>
#include <uikit/color.h>
#include <uikit/rect.h>
#include <uikit/window.h>
#include <uikit/widget.h>
#include <uikit/button.h>
#include <uikit/mouse_area.h>
#include <uikit/controls.h>
#include <uikit/textfield.h>
#include <uikit/textarea.h>
#include <uikit/text.h>
#include <uikit/cursor.h>
#include <uikit/dialog.h>
#include <uikit/tab.h>
#include <uikit/popup.h>
#include <uikit/file_drop.h>
#include <uikit/anim.h>
#include <uikit/asset.h>
#include <uikit/crash.h>
#include <uikit/font.h>

// Converts window coordinates (coming from SDL_Event) into the
// renderer's logical space. Important because UIApp_HandleEvent sets
// SDL_LOGICAL_PRESENTATION_LETTERBOX on resize. Without this conversion
// button hit tests would drift once the window is resized.
static void WindowToRenderCoords(UIApp* app, float wx, float wy, float* rx, float* ry) {
    if (app && app->window && app->window->sdlRenderer) {
        SDL_RenderCoordinatesFromWindow(app->window->sdlRenderer, wx, wy, rx, ry);
    } else {
        *rx = wx;
        *ry = wy;
    }
}

// Forwards a mouse wheel event to every UIScroll whose bounds contain
// (x, y). Vertical wheel goes to scrollY by default; with shift held
// it goes to scrollX (matching most browsers).
static void DispatchWheelToScrolls(UIChildren* children,
                                   float mouseX, float mouseY,
                                   float dxNotches, float dyNotches,
                                   int shift) {
    if (!children) return;
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible || !w->data || !w->width || !w->height) continue;
        UIWidgetBase* b = (UIWidgetBase*)w->data;
        if (strcmp(b->__widget_type, UI_WIDGET_SCROLL) != 0) continue;

        const float wW = *w->width, hH = *w->height;
        if (mouseX < w->x || mouseX >= w->x + wW ||
            mouseY < w->y || mouseY >= w->y + hH) continue;

        UIScroll* s = (UIScroll*)b;
        const float speed = s->wheelSpeed > 0.0f ? s->wheelSpeed : 60.0f;
        if (shift) {
            if (s->allowHorizontal) s->scrollX -= dyNotches * speed;
        } else {
            if (s->allowVertical)   s->scrollY -= dyNotches * speed;
            if (s->allowHorizontal) s->scrollX -= dxNotches * speed;
        }
        return; // first hit captures
    }
}

// Applies the new size to internal state and renders a frame. Called
// both from the live-resize watch below and from the queued
// SDL_EVENT_WINDOW_RESIZED handler.
static void ApplyResize(UIApp* app, int new_width, int new_height) {
    if (!app || !app->window) return;
    if (new_width <= 0 || new_height <= 0) return;

    app->window->width  = new_width;
    app->window->height = new_height;

    if (app->mainWidget) {
        UIWidget_SetSize(app->mainWidget, (float)new_width, (float)new_height);
    }
    if (app->window->sdlRenderer) {
        SDL_SetRenderLogicalPresentation(app->window->sdlRenderer,
                                         new_width, new_height,
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
    }
    // Fire user callback BEFORE relayout so the user can adjust widget
    // sizes that the alignment pass should then position.
    if (app->onResize) app->onResize(new_width, new_height, app->onResizeUserdata);
    UIChildren_Relayout(app->window->children);
}

// SDL fires this watch the moment a window event is posted - including
// from inside Windows' modal sizing loop, where SDL_PollEvent is
// blocked. We use it to keep the frame redrawing live while the user
// drags the window edge.
static bool LiveResizeWatch(void* userdata, SDL_Event* event) {
    UIApp* app = (UIApp*)userdata;
    if (!app || !app->window || !app->window->sdlWindow) return false;
    if (event->type != SDL_EVENT_WINDOW_RESIZED &&
        event->type != SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED &&
        event->type != SDL_EVENT_WINDOW_EXPOSED) {
        return false;
    }
    if (event->window.windowID != SDL_GetWindowID(app->window->sdlWindow)) {
        return false;
    }

    int w = event->window.data1;
    int h = event->window.data2;
    if (w <= 0 || h <= 0) {
        SDL_GetWindowSize(app->window->sdlWindow, &w, &h);
    }
    ApplyResize(app, w, h);
    UIWindow_Render(app->window);
    return false;
}

// Walks children back-to-front and returns the cursor advertised by
// the topmost widget under (x, y). Falls back to UI_CURSOR_DEFAULT when
// nothing interactive is under the cursor.
static UICursor PickHoverCursor(UIChildren* children, float x, float y) {
    if (!children) return UI_CURSOR_DEFAULT;
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible || !w->data || !w->width || !w->height) continue;
        const float ww = *w->width, hh = *w->height;
        if (x < w->x || x >= w->x + ww || y < w->y || y >= w->y + hh) continue;

        UIWidgetBase* base = (UIWidgetBase*)w->data;
        // WebView2 composition mode: defer to whatever cursor the page
        // most recently reported (link = pointer, input = text, etc).
        if (!strcmp(base->__widget_type, UI_WIDGET_WEBVIEW)) {
            int wvCursor = UIWebView_HoverCursorAt(children, x, y);
            return (UICursor)wvCursor; /* 0 = DEFAULT if not in comp mode */
        }
        if (!strcmp(base->__widget_type, UI_WIDGET_BUTTON)) {
            UIButton* b = (UIButton*)base;
            if (!b->enabled) return UI_CURSOR_NOT_ALLOWED;
            return b->cursor;
        }
        if (!strcmp(base->__widget_type, UI_WIDGET_MOUSE_AREA)) {
            UIMouseArea* m = (UIMouseArea*)base;
            if (!m->enabled) return UI_CURSOR_DEFAULT;
            return m->cursor;
        }
        if (!strcmp(base->__widget_type, UI_WIDGET_CHECKBOX)) {
            return ((UICheckbox*)base)->cursor;
        }
        if (!strcmp(base->__widget_type, UI_WIDGET_SLIDER)) {
            return ((UISlider*)base)->cursor;
        }
        if (!strcmp(base->__widget_type, UI_WIDGET_SWITCH)) {
            return ((UISwitch*)base)->cursor;
        }
        if (!strcmp(base->__widget_type, UI_WIDGET_RADIO)) {
            return ((UIRadioButton*)base)->cursor;
        }
        if (!strcmp(base->__widget_type, UI_WIDGET_TEXTFIELD)) {
            return ((UITextField*)base)->cursor;
        }
        if (!strcmp(base->__widget_type, UI_WIDGET_TEXTAREA)) {
            return ((UITextArea*)base)->cursor;
        }
        if (!strcmp(base->__widget_type, UI_WIDGET_TEXT)) {
            UIText* t = (UIText*)base;
            if (t->selectable) return t->cursor;
            // Non-selectable text doesn't claim the cursor; keep
            // looking at widgets behind it.
            continue;
        }
        // Hit a non-interactive widget on top - stop here so widgets
        // below don't accidentally claim the cursor.
        return UI_CURSOR_DEFAULT;
    }
    return UI_CURSOR_DEFAULT;
}

void HandleEvent(UIApp* app, SDL_Event* event) {
    if (!app || !app->window) return;

    switch (event->type) {
        case SDL_EVENT_MOUSE_MOTION: {
            float rx, ry;
            WindowToRenderCoords(app, event->motion.x, event->motion.y, &rx, &ry);
            // Mouse areas first so a draggable area can capture the
            // motion even when a button widget is underneath.
            UIMouseArea_DispatchMouseMotion(app->window->children, rx, ry);
            UIButton_DispatchMouseMotion   (app->window->children, rx, ry);
            UIControls_DispatchMouseMotion (app->window->children, rx, ry);
            UIPopup_DispatchMouseMotion    (app->window->children, rx, ry);
            UITextField_DispatchMouseMotion(app->window->children, rx, ry);
            UITextArea_DispatchMouseMotion (app->window->children, rx, ry);
            UIText_DispatchMouseMotion     (app->window->children, rx, ry);
            // WebView2 composition-mode visuals have no native message
            // loop - forward mouse motion via SendMouseInput.
            UIWebView_DispatchMouseMotion  (app->window->children, rx, ry);
            UICursor_Apply(PickHoverCursor(app->window->children, rx, ry));
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            float rx, ry;
            WindowToRenderCoords(app, event->button.x, event->button.y, &rx, &ry);
            // UIMouseArea handles every button (its callbacks see the
            // button index). The rest are left-only by design.
            UIMouseArea_DispatchMouseDown(app->window->children, rx, ry, event->button.button);
            // Forward to webview unconditionally - the webview itself
            // decides if the click landed on a button/link/etc.
            UIWebView_DispatchMouseDown  (app->window->children, rx, ry, event->button.button);
            if (event->button.button == SDL_BUTTON_LEFT) {
                UIButton_DispatchMouseDown   (app->window->children, rx, ry);
                UIControls_DispatchMouseDown (app->window->children, rx, ry, event->button.button);
                UITextField_DispatchMouseDown(app->window->children, app->window->sdlWindow,
                                              rx, ry, event->button.button);
                UITextArea_DispatchMouseDown (app->window->children, app->window->sdlWindow,
                                              rx, ry, event->button.button);
                UIText_DispatchMouseDown     (app->window->children, app->window->sdlWindow,
                                              rx, ry, event->button.button);
                UITabView_DispatchMouseDown  (app->window->children, rx, ry, event->button.button);
                UIDialog_DispatchMouseDown   (app->window->children, rx, ry, event->button.button);
                UIPopup_DispatchMouseDown    (app->window->children, rx, ry, event->button.button);
            }
            break;
        }
        case SDL_EVENT_TEXT_INPUT: {
            UITextField_DispatchTextInput(app->window->children, event->text.text);
            UITextArea_DispatchTextInput (app->window->children, event->text.text);
            break;
        }
        case SDL_EVENT_KEY_DOWN: {
            // Debug overlay hotkeys (F9-F12). Swallow the key so it
            // doesn't also propagate to text widgets. In release builds
            // this is a no-op that returns 0.
            if (UIDebugOverlay_HandleScancode((int)event->key.scancode)) break;
            UITextField_DispatchKeyDown(app->window->children, app->window->sdlWindow,
                                        event->key.scancode, event->key.mod);
            UITextArea_DispatchKeyDown (app->window->children, app->window->sdlWindow,
                                        event->key.scancode, event->key.mod);
            UIText_DispatchKeyDown     (app->window->children, app->window->sdlWindow,
                                        event->key.scancode, event->key.mod);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            float rx, ry;
            WindowToRenderCoords(app, event->button.x, event->button.y, &rx, &ry);
            UIMouseArea_DispatchMouseUp(app->window->children, rx, ry, event->button.button);
            UIWebView_DispatchMouseUp  (app->window->children, rx, ry, event->button.button);
            if (event->button.button == SDL_BUTTON_LEFT) {
                UIButton_DispatchMouseUp    (app->window->children, rx, ry);
                UIControls_DispatchMouseUp  (app->window->children, rx, ry, event->button.button);
                UIPopup_DispatchMouseUp     (app->window->children, rx, ry, event->button.button);
                UITextField_DispatchMouseUp (app->window->children, rx, ry, event->button.button);
                UITextArea_DispatchMouseUp  (app->window->children, rx, ry, event->button.button);
                UIText_DispatchMouseUp      (app->window->children, rx, ry, event->button.button);
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            float rx, ry;
            WindowToRenderCoords(app, event->wheel.mouse_x, event->wheel.mouse_y, &rx, &ry);
            const SDL_Keymod mods = SDL_GetModState();
            const int shift = (mods & SDL_KMOD_SHIFT) != 0;
            DispatchWheelToScrolls(app->window->children, rx, ry,
                                   event->wheel.x, event->wheel.y, shift);
            UITextArea_DispatchMouseWheel(app->window->children, rx, ry,
                                          event->wheel.y);
            UIWebView_DispatchMouseWheel (app->window->children, rx, ry,
                                          event->wheel.x, event->wheel.y);
            break;
        }
        case SDL_EVENT_DROP_POSITION: {
            // Forward to UIFileDrop widgets so they can flash their
            // active-state border while the user is still holding the
            // file over the window.
            float rx, ry;
            WindowToRenderCoords(app, event->drop.x, event->drop.y, &rx, &ry);
            UIFileDrop_DispatchDragPosition(app->window->children, rx, ry);

            // Also keep the legacy window-position update so old code
            // that relied on it still works.
            app->window->x = event->drop.x;
            app->window->y = event->drop.y;
            break;
        }
        case SDL_EVENT_DROP_FILE: {
            float rx, ry;
            WindowToRenderCoords(app, event->drop.x, event->drop.y, &rx, &ry);
            UIFileDrop_DispatchDropFile(app->window->children, rx, ry,
                                        event->drop.data);
            break;
        }
        case SDL_EVENT_DROP_COMPLETE:
        case SDL_EVENT_DROP_BEGIN: {
            // Reset hover state when the drag ends or starts so we
            // never get a stuck "drag-over" highlight.
            if (event->type == SDL_EVENT_DROP_COMPLETE) {
                UIFileDrop_DispatchDragEnd(app->window->children);
            }
            break;
        }
        case SDL_EVENT_WINDOW_RESIZED: {
            // The actual resize work was already done from the event
            // watch (so it fires live during Windows' modal sizing
            // loop). We still get a queued copy of the event here once
            // the user releases the mouse; just no-op so we don't pay
            // for the work twice.
            (void)event;
            break;
        }
        default:
            break;
    }
}

void UIApp_EmitEvent(UIApp* app, UI_EVENT event, UIEventData data) {
    if (!app || !app->window || !app->window->events) return;

    UIWindow_EmitEvent(app->window, event, data);
}

/* Visitor used by the crash tree dumper. */
static UIWalkResult mocida_crash_visit(UIWidget* w, int depth, void* user) {
    FILE* f = (FILE*)user;
    if (!w) {
        fprintf(f, "  %*s(null)\n", depth * 2, "");
        return UI_WALK_SKIP_CHILDREN;
    }
    const char* type = "?";
    if (w->data) {
        UIWidgetBase* b = (UIWidgetBase*)w->data;
        if (b->__widget_type) type = b->__widget_type;
    }
    float ww = w->width  ? *w->width  : -1.0f;
    float hh = w->height ? *w->height : -1.0f;
    fprintf(f, "  %*s%s  id=%s  x=%.1f y=%.1f w=%.1f h=%.1f z=%d vis=%d\n",
            depth * 2, "",
            type, w->id ? w->id : "(none)",
            w->x, w->y, ww, hh, w->z, w->visible);
    return UI_WALK_CONTINUE;
}

/* Tree dumper invoked by the crash handler. Walks the FULL tree via the
 * shared UIChildren_WalkTree machinery so nested children (Stack, Grid,
 * TabView panels, Dialog content, Scroll content) all show up. */
static void mocida_crash_tree_dump(FILE* f, void* user) {
    UIApp* app = (UIApp*)user;
    if (!app || !app->window) { fputs("  (no app)\n", f); return; }
    UIChildren* c = app->window->children;
    fprintf(f, "  app=%p window=%p size=%dx%d\n",
            (void*)app, (void*)app->window, app->window->width, app->window->height);
    if (!c) { fputs("  (no children)\n", f); return; }
    fprintf(f, "  total top-level children: %d\n", c->count);
    UIChildren_WalkTree(c, 0, mocida_crash_visit, f);
}

// --------------------------------------------------------------------
// Console window control (Windows). Mocida apps link as the GUI
// subsystem (via WIN32_EXECUTABLE TRUE in CMake), so no console is
// allocated at process start. In Debug builds we attach one at
// runtime so logs are visible; the user can opt out with the
// MOCIDA_NO_CONSOLE env var or by calling UIApp_HideConsole().
// --------------------------------------------------------------------
#ifdef _WIN32
static int g_consoleOwned = 0;   /* 1 if Mocida itself allocated the console */

/* Turn on ANSI escape processing on the freshly-attached console so
 * the debug subsystem's coloured log output ("\033[32mINFO\033[0m"
 * etc.) renders as actual colours instead of literal `←[32m` glyphs.
 * Available since Windows 10 build 16257; older builds silently
 * ignore the flag and just print uncoloured text — acceptable. */
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef DISABLE_NEWLINE_AUTO_RETURN
#define DISABLE_NEWLINE_AUTO_RETURN        0x0008
#endif

static void EnableVTProcessing(void) {
    HANDLE handles[2] = { GetStdHandle(STD_OUTPUT_HANDLE),
                          GetStdHandle(STD_ERROR_HANDLE) };
    for (int i = 0; i < 2; i++) {
        HANDLE h = handles[i];
        if (h == INVALID_HANDLE_VALUE || h == NULL) continue;
        DWORD mode = 0;
        if (GetConsoleMode(h, &mode)) {
            SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING
                                  | DISABLE_NEWLINE_AUTO_RETURN);
        }
    }
}

void UIApp_EnableConsole(void) {
    HWND existing = GetConsoleWindow();
    if (existing) {
        ShowWindow(existing, SW_SHOW);
        EnableVTProcessing();
        return;
    }
    if (AllocConsole()) {
        FILE* f = NULL;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        freopen_s(&f, "CONIN$",  "r", stdin);
        g_consoleOwned = 1;
        EnableVTProcessing();
    }
}

void UIApp_HideConsole(void) {
    HWND h = GetConsoleWindow();
    if (h) ShowWindow(h, SW_HIDE);
}

int UIApp_IsConsoleVisible(void) {
    HWND h = GetConsoleWindow();
    return (h && IsWindowVisible(h)) ? 1 : 0;
}

/* Called by UIApp_Create — auto-attach in debug unless opted out. */
static void EnsureDebugConsole(void) {
#ifdef MOCIDA_DEBUG
    /* Already running under a console (launched from cmd / Windows
     * Terminal etc.) — leave it alone, the process inherits stdio. */
    if (GetConsoleWindow() != NULL) return;

    /* MOCIDA_NO_CONSOLE=1 / true silences the auto-allocate. */
    const char* opt = getenv("MOCIDA_NO_CONSOLE");
    if (opt && *opt && opt[0] != '0' && opt[0] != 'f' && opt[0] != 'F') return;

    UIApp_EnableConsole();
#endif
}
#else
void UIApp_EnableConsole    (void) { /* no-op outside Windows */ }
void UIApp_HideConsole      (void) { /* no-op outside Windows */ }
int  UIApp_IsConsoleVisible (void) { return 0; }
static void EnsureDebugConsole(void) { /* no-op */ }
#endif

UIApp* UIApp_Create(const char* title, int width, int height) {
    /* Debug-only console attach. No-op in release (no logs to see
     * anyway; the WIN32 subsystem suppressed any auto-console). */
    EnsureDebugConsole();

    /* Crash handler first — anything that explodes after this point
     * (including the debug subsystem) gets caught. */
    UICrash_Install();

    /* Force-init the debug subsystem so env-var sinks (port/file) are
     * live before any other init code logs anything. */
    UIDebug_SetLevel(UIDebug_GetLevel());
    UI_INFO(UI_CAT_CORE, "UIApp_Create '%s' (%dx%d)", title ? title : "", width, height);

    /* calloc, not malloc — any field we add to UIApp later must start
     * out zeroed. The original code missed app->onResize when that
     * member was added, which let LiveResizeWatch dispatch through a
     * garbage function pointer (0xffffffffffffffff) on the first
     * window event before UIApp_Create could even return. */
    UIApp* app = (UIApp*)calloc(1, sizeof(UIApp));
    if (!app) {
        UI_ERROR(UI_CAT_CORE, "failed to allocate UIApp");
        return NULL;
    }
    UI_TRACK_ALLOC(UI_CAT_CORE);

    // Initialize properties before creating the window, to avoid accessing uninitialized memory
    app->mainWidget = NULL;
    app->window = NULL;
    app->backgroundColor = UI_COLOR_WHITE;
    app->targetFps = 60;                       // 60 FPS by default; SetTargetFPS(app, 0) unlocks.
    app->msaaSamples = UI_QUALITY_HIGH;        // 4x4 = 16 SPP by default.
    app->aaMode = UI_AA_COVERAGE;              // No full-frame postfx by default.
    app->taaBlend = 0.5f;
    app->onResize = NULL;                      // User callback; opt-in via UIApp_SetResizeCallback.
    app->onResizeUserdata = NULL;

    // Mirror the value into the window's global state before the
    // window is created so the OpenGL MSAA hint picks up the right N.
    UIWindow_SetMSAASamples(app->msaaSamples);
    UIWindow_SetAAMode((int)app->aaMode);
    UIWindow_SetTAABlend(app->taaBlend);

    UIWidget* mainWidget = widgc(NULL);
    if (!mainWidget) {
        UI_ERROR(UI_CAT_CORE, "failed to create main widget");
        free(app);
        return NULL;
    }
    UIWidget_SetSize(mainWidget, (float)width, (float)height);
    app->mainWidget = mainWidget;

    app->window = UIWindow_Create(title, width, height);
    if (app->window == NULL) {
        UI_ERROR(UI_CAT_WINDOW, "UIWindow_Create returned NULL");
        UIWidget_Destroy(mainWidget);
        free(app);
        return NULL;
    }

    UIApp_SetBackgroundColor(app, UI_COLOR_WHITE);

    /* Register tree dumper so a crash report includes the widget tree. */
    UICrash_SetTreeDumper(mocida_crash_tree_dump, app);

    // Live-resize watch: keeps frames drawing while the user drags the
    // window border on Windows (where the OS modal sizing loop blocks
    // the normal poll loop).
    SDL_AddEventWatch(LiveResizeWatch, app);
    return app;
}

UIWidget* UIApp_GetWindow(UIApp* app) {
    if (!app || !app->window) {
        UI_WARN(UI_CAT_CORE, "UIApp_GetWindow called with NULL app/window");
        return NULL;
    }

    UIWidget* widget = widgcs(app->window, (float)app->window->width, (float)app->window->height); 
    return widget;
}

void UIApp_SetChildren(UIApp* app, UIChildren* children) {
    if (!app || !app->window || !children) return;
    
    // Free the previous children if they exist
    if (app->window->children) {
        UIChildren_Destroy(app->window->children);
    }
    
    app->window->children = children;
}

void UIApp_SetBackgroundColor(UIApp* app, UIColor color) {
    if (!app || !app->window) {
        UI_WARN(UI_CAT_CORE, "UIApp_SetBackgroundColor called with NULL app/window");
        return;
    };
    
    app->backgroundColor = app->window->backgroundColor = color;
}

void UIApp_SetWindowTitle(UIApp* app, const char* title) {
    if (!app || !app->window || !app->window->sdlWindow || !title) return;
    SDL_SetWindowTitle(app->window->sdlWindow, title);
}

int UIApp_SetWindowIconFromSurface(UIApp* app, SDL_Surface* surface) {
    if (!app || !app->window || !app->window->sdlWindow || !surface) return 0;
    if (!SDL_SetWindowIcon(app->window->sdlWindow, surface)) {
        UI_ERROR(UI_CAT_WINDOW, "SDL_SetWindowIcon failed: %s", SDL_GetError());
        return 0;
    }
    return 1;
}

int UIApp_SetWindowIcon(UIApp* app, const char* path) {
    if (!app || !app->window || !app->window->sdlWindow || !path) return 0;

    SDL_Surface* surf = UIAsset_LoadSurface(path);
    if (!surf) return 0; // UIAsset_LoadSurface ja loga

    const int ok = UIApp_SetWindowIconFromSurface(app, surf);
    SDL_DestroySurface(surf);
    return ok;
}

void UIApp_SetWindowSize(UIApp* app, int width, int height) {
    if (!app || !app->window || !app->window->sdlWindow) return;
    SDL_SetWindowSize(app->window->sdlWindow, width, height);
}

void UIApp_SetWindowPosition(UIApp* app, int x, int y) {
    if (!app || !app->window || !app->window->sdlWindow) return;
    SDL_SetWindowPosition(app->window->sdlWindow, x, y);
}

void UIApp_SetResizable(UIApp* app, int resizable) {
    if (!app || !app->window || !app->window->sdlWindow) return;
    SDL_SetWindowResizable(app->window->sdlWindow, resizable ? true : false);
}

void UIApp_SetMinSize(UIApp* app, int width, int height) {
    if (!app || !app->window || !app->window->sdlWindow) return;
    if (width < 1)  width  = 1;
    if (height < 1) height = 1;
    SDL_SetWindowMinimumSize(app->window->sdlWindow, width, height);
}

void UIApp_SetMaxSize(UIApp* app, int width, int height) {
    if (!app || !app->window || !app->window->sdlWindow) return;
    if (width < 1)  width  = 1;
    if (height < 1) height = 1;
    SDL_SetWindowMaximumSize(app->window->sdlWindow, width, height);
}

void UIApp_SetEventCallback(UIApp* app, UI_EVENT event, UIEventCallback callback) {
    if (!app || !app->window || !callback) return;
    UIWindow_SetEventCallback(app->window, event, callback);
}

void UIApp_OnResize(UIApp* app, UIAppResizeCallback cb, void* userdata) {
    if (!app) return;
    app->onResize = cb;
    app->onResizeUserdata = userdata;
}

void UIApp_SetAppId(UIApp* app, const char* aumid) {
    (void)app;
    if (!aumid || !*aumid) return;
#ifdef _WIN32
    // SetCurrentProcessExplicitAppUserModelID is wide-char; convert.
    int n = MultiByteToWideChar(CP_UTF8, 0, aumid, -1, NULL, 0);
    if (n <= 0) return;
    wchar_t* wide = (wchar_t*)malloc(sizeof(wchar_t) * (size_t)n);
    if (!wide) return;
    MultiByteToWideChar(CP_UTF8, 0, aumid, -1, wide, n);

    // Resolve dynamically to skip a hard shell32 import (every
    // supported Windows ships the symbol; this avoids a load-time
    // dependency).
    typedef HRESULT (WINAPI* SetAumidFn)(PCWSTR);
    HMODULE shell = GetModuleHandleW(L"shell32.dll");
    if (!shell) shell = LoadLibraryW(L"shell32.dll");
    if (shell) {
        SetAumidFn fn = (SetAumidFn)(void*)GetProcAddress(shell,
            "SetCurrentProcessExplicitAppUserModelID");
        if (fn) fn(wide);
    }
    free(wide);
#endif
}

void UIApp_SetWindowDisplayMode(UIApp* app, UIWindowDisplayMode displayMode) {
    if (!app || !app->window || !app->window->sdlWindow) return;

    SDL_Window* w = app->window->sdlWindow;
    switch (displayMode) {
        case WINDOW_WINDOWED:
            SDL_SetWindowBordered(w, 1);
            SDL_SetWindowFullscreen(w, 0);
            break;
        case WINDOW_FULLSCREEN:
            // SDL3 has two flavours of fullscreen:
            //   - Exclusive (a specific SDL_DisplayMode is set first):
            //     OS switches the actual display resolution. Heavy
            //     transition + on Linux/WSLg engages the compositor's
            //     vsync path even when SDL_SetRenderVSync(0) was set,
            //     because the compositor takes over the swap chain.
            //   - "Desktop" (fullscreen mode == NULL): borderless
            //     windowed at the desktop resolution. Identical visual,
            //     no modeset, and on Linux the compositor leaves us in
            //     the same fast present path as windowed mode.
            //
            // On Windows D3D11/12 the two paths perform similarly so we
            // ifdef the explicit NULL-mode call to Linux/macOS only.
            // Setting it on Windows would still work (NULL is the
            // documented default), but the explicit call is just noise.
#if defined(__linux__) || defined(__APPLE__)
            SDL_SetWindowFullscreenMode(w, NULL);
#endif
            SDL_SetWindowFullscreen(w, 1);
            break;
        case WINDOW_BORDERLESS:
            SDL_SetWindowBordered(w, 0);
            break;
    }
    app->window->displayMode = displayMode;
}

void UIApp_SetRenderDriver(UIApp* app, UIRenderDriver renderDriver) {
    if (!app || !app->window || !app->window->sdlWindow) return;

    // Store the current renderer to free it only if we successfully create a new one
    SDL_Renderer* currentRenderer = app->window->sdlRenderer;
    SDL_Renderer* newRenderer = NULL;
    const char* driverName = NULL;

    switch (renderDriver) {
        case UI_RENDER_OPENGL:
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
            driverName = "opengl";
            break;
        case UI_RENDER_SOFTWARE:
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
            driverName = "software";
            break;
        case UI_RENDER_VULKAN:
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "vulkan");
            driverName = "vulkan";
            break;
        #ifdef _WIN32
        case UI_RENDER_3D9:
            // Used for Direct3D 9 - For legacy systems
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d");
            driverName = "direct3d";
            break;
        case UI_RENDER_3D11:
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
            driverName = "direct3d11";
            break;
        case UI_RENDER_3D12:
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d12");
            driverName = "direct3d12";
            break;
        #endif
        case UI_RENDER_GPU:
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "gpu");
            driverName = "gpu";
            break;
        #ifdef __APPLE__
        case UI_RENDER_METAL:
            // Note: Metal is only available on macOS and iOS
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
            driverName = "metal";
            break;
        #endif
        default:
            UI_WARN(UI_CAT_RENDER, "unknown render driver %d", (int)renderDriver);
            return;
    }

    // Create a new renderer with the specified driver
    if (driverName) {
        newRenderer = SDL_CreateRenderer(app->window->sdlWindow, driverName);
        if (newRenderer) {
            // Destroys the current renderer only if the new renderer is created successfully
            if (currentRenderer) {
                SDL_DestroyRenderer(currentRenderer);
            }
            app->window->sdlRenderer = newRenderer;
        } else {
            UI_ERROR(UI_CAT_RENDER, "failed to create renderer with driver %s: %s",
                     driverName, SDL_GetError());
        }
    }
}

void UIApp_ShowWindow(UIApp* app) {
    if (!app || !app->window || !app->window->sdlWindow) return;
    app->window->visible = 1;
    SDL_ShowWindow(app->window->sdlWindow);
}

void UIApp_HideWindow(UIApp* app) {
    if (!app || !app->window || !app->window->sdlWindow) return;
    app->window->visible = 0;
    SDL_HideWindow(app->window->sdlWindow);
}

void UIApp_SetProperty(UIApp* app, const char* property, void* value) {
    if (!app || !app->window || !property || !value) return;

    UIWindow_SetProperty(app->window, property, value);
}

void* UIApp_GetProperty(UIApp* app, const char* property) {
    if (!app || !app->window || !property) return NULL;

    return UIWindow_GetProperty(app->window, property);
}

void UIApp_Destroy(UIApp* app) {
    if (!app) return;

    SDL_RemoveEventWatch(LiveResizeWatch, app);

    // Drop any in-flight tweens so they don't try to write to freed
    // float pointers after the widget tree comes down.
    UIAnim_ClearAll();

    if (app->window) {
        UIWindow_Destroy(app->window);
        app->window = NULL;
    }
    
    if (app->mainWidget) {
        UIWidget_Destroy(app->mainWidget);
        app->mainWidget = NULL;
    }
    
    // Tear down the system-font registry built by UISearchFonts.
    // Was previously orphaned (the function existed but no caller
    // invoked it) — under LSan/ASan that showed up as ~hundreds of
    // unfreed allocations at shutdown (one entry per installed font,
    // plus its family_name + file_path strings).
    UIFonts_Destroy();

    UICursor_Shutdown();
    SDL_Quit();
    UI_TRACK_FREE(UI_CAT_CORE);
    free(app);

    /* Last chance to catch lifecycle bugs — emits a WARN per category
     * that still has live allocations registered via UI_TRACK_ALLOC.
     * No-op in release builds. */
    UIDebug_ReportLeaks();
    UIDebug_Flush();
    UIDebug_Close();
}

void UIApp_SetTargetFPS(UIApp* app, int fps) {
    if (!app) return;
    app->targetFps = (fps > 0) ? fps : 0;
}

int UIApp_GetTargetFPS(UIApp* app) {
    if (!app) return 0;
    return app->targetFps;
}

// Current window size in logical points. On iOS this is the real device
// screen size adopted at creation (not the requested desktop default), so
// it's the right value to drive responsive initial layout from.
int UIApp_GetWidth(UIApp* app) {
    return (app && app->window) ? app->window->width : 0;
}
int UIApp_GetHeight(UIApp* app) {
    return (app && app->window) ? app->window->height : 0;
}

void UIApp_SetMSAASamples(UIApp* app, int samples) {
    if (!app) return;
    if (samples < 1)  samples = 1;
    if (samples > 16) samples = 16;
    app->msaaSamples = samples;
    UIWindow_SetMSAASamples(samples);
}

int UIApp_GetMSAASamples(UIApp* app) {
    if (!app) return 0;
    return app->msaaSamples;
}

void UIApp_SetRenderQuality(UIApp* app, UIRenderQuality quality) {
    UIApp_SetMSAASamples(app, (int)quality);
}

void UIApp_SetAAMode(UIApp* app, UIAAMode mode) {
    if (!app) return;
    app->aaMode = mode;
    UIWindow_SetAAMode((int)mode);
}

UIAAMode UIApp_GetAAMode(UIApp* app) {
    if (!app) return UI_AA_COVERAGE;
    return (UIAAMode)app->aaMode;
}

void UIApp_SetTAABlend(UIApp* app, float alpha) {
    if (!app) return;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    app->taaBlend = alpha;
    UIWindow_SetTAABlend(alpha);
}

void UIApp_TrimCaches(UIApp* app) {
    (void)app;
    UIWindow_TrimCaches();
}

void UIApp_GetMemoryStats(UIApp* app, UIMemoryStats* out) {
    (void)app;
    if (!out) return;
    out->current = 0;
    out->peak = 0;
    out->reserved = 0;
    out->committed = 0;
    out->mallocRequests = 0;
#if defined(MOCIDA_USE_MIMALLOC)
    // Best-effort: mimalloc exposes only aggregate counters via the
    // public API. mi_process_info gives us peak/current bytes.
    size_t elapsed = 0, user = 0, sys = 0, current = 0, peak = 0;
    size_t pageFaults = 0, pageReclaim = 0, peakCommit = 0;
    mi_process_info(&elapsed, &user, &sys, &current, &peak,
                    &pageFaults, &pageReclaim, &peakCommit);
    out->current   = current;
    out->peak      = peak;
    out->committed = peakCommit;
#endif
}

// Hybrid sleep + spin used by the frame pacer. SDL_DelayNS on Windows
// is bound to the OS scheduler granularity (~1ms by default), which is
// enough to oversleep by 1-2ms per frame and turn a 60 FPS cap into a
// jittery ~57. We sleep all but the last ~500us, then busy-wait until
// the precise target counter is reached. The spin is short (under
// 1% CPU at 60 Hz) and gives sub-millisecond pacing accuracy.
static void PreciseDelay(Uint64 ns, Uint64 freq, Uint64 targetCounter) {
    const Uint64 nsPerSec    = 1000000000ULL;
    const Uint64 spinMarginNs = 500000ULL; // 0.5 ms safety margin

    if (ns > spinMarginNs) {
        SDL_DelayNS(ns - spinMarginNs);
    }
    // Busy-wait until we hit the exact target counter.
    while (SDL_GetPerformanceCounter() < targetCounter) {
        // tight loop; could yield with SDL_DelayNS(0) but on Windows
        // that doesn't pay off and just adds overhead.
    }
    (void)freq;
    (void)nsPerSec;
}

void UIApp_Run(UIApp* app) {
    if (!app || !app->window) return;

    SDL_Event e;
    const Uint64 freq      = SDL_GetPerformanceFrequency();
    const Uint64 nsPerSec  = 1000000000ULL;

    // `nextFrameTick` tracks the performance counter value at which the
    // *next* frame should start. By accumulating fixed-size budgets we
    // self-correct timing drift: a short frame doesn't borrow time from
    // the next, and a single long frame doesn't permanently shift the
    // cadence.
    Uint64 nextFrameTick = SDL_GetPerformanceCounter();

    Uint64 lastTickPC = SDL_GetPerformanceCounter();

    while (app->window->visible) {
        UIProfile_FrameBegin();

        {
            UI_SCOPEC("events", UI_PROF_EVENT);
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_QUIT) {
                    app->window->visible = 0;
                }
                HandleEvent(app, &e);
            }
        }

        // Advance any in-flight animations with the real elapsed time.
        const Uint64 nowPC = SDL_GetPerformanceCounter();
        const Uint32 dtMs  = (Uint32)((nowPC - lastTickPC) * 1000ULL / freq);
        lastTickPC = nowPC;
        if (dtMs > 0) {
            UI_SCOPEC("anim", UI_PROF_LAYOUT);
            UIAnim_Tick(dtMs);
        }

        {
            UI_SCOPEC("render", UI_PROF_RENDER);
            UIWindow_Render(app->window);
        }

        UIProfile_FrameEnd();

        if (app->targetFps > 0) {
            const Uint64 targetNs    = nsPerSec / (Uint64)app->targetFps;
            const Uint64 targetTicks = targetNs * freq / nsPerSec;

            nextFrameTick += targetTicks;

            const Uint64 now = SDL_GetPerformanceCounter();
            if (now < nextFrameTick) {
                const Uint64 waitTicks = nextFrameTick - now;
                const Uint64 waitNs    = waitTicks * nsPerSec / freq;
                PreciseDelay(waitNs, freq, nextFrameTick);
            } else if (now > nextFrameTick + 2 * targetTicks) {
                // We're more than two frames behind - resync instead of
                // frantically rendering catch-up frames.
                nextFrameTick = now;
            }
        } else {
            // Unlocked mode: keep nextFrameTick aligned so a later
            // re-enable of the cap doesn't try to catch up huge debt.
            nextFrameTick = SDL_GetPerformanceCounter();
        }
    }
}