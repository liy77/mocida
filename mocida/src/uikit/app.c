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
#include <uikit/stack.h>
#include <uikit/container.h>
#include <uikit/asset.h>
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

// If `w` is a container that holds a child collection (Stack / Grid / Rectangle /
// Scroll content), return it so the wheel dispatch can recurse into nested
// scrolls. Mirrors button.c's ContainerChildren — containers lay out their
// children's absolute x/y during render, so the same absolute hit-test works at
// any depth. A Scroll's content is re-laid-out to absolute on-screen positions.
static UIChildren* WheelContainerChildren(UIWidget* w) {
    if (!w || !w->data) return NULL;
    UIWidgetBase* base = (UIWidgetBase*)w->data;
    const char* t = base->__widget_type;
    if (strcmp(t, UI_WIDGET_STACK) == 0)     return ((UIStack*)base)->items;
    if (strcmp(t, UI_WIDGET_GLASS) == 0)     return ((UIGlass*)base)->items;
    if (strcmp(t, UI_WIDGET_GRID) == 0)      return ((UIGrid*)base)->items;
    if (strcmp(t, UI_WIDGET_RECTANGLE) == 0) return (UIChildren*)((UIRectangle*)base)->children;
    if (strcmp(t, UI_WIDGET_SCROLL) == 0) {
        UIWidget* content = ((UIScroll*)base)->content;
        return content ? WheelContainerChildren(content) : NULL;
    }
    return NULL;
}

// Find the innermost UIScroll whose bounds contain (x, y), recursing through
// nested containers. Without the recursion a Scroll nested inside layout
// containers — the common case (e.g. a file panel deep in a Stack tree) — never
// receives the wheel, so scrolling silently does nothing.
static UIScroll* FindScrollAt(UIChildren* children, float x, float y) {
    if (!children) return NULL;
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible || !w->data) continue;
        // Descend first so a more deeply-nested scroll wins over an outer one.
        UIChildren* kids = WheelContainerChildren(w);
        if (kids) {
            UIScroll* inner = FindScrollAt(kids, x, y);
            if (inner) return inner;
        }
        UIWidgetBase* b = (UIWidgetBase*)w->data;
        if (strcmp(b->__widget_type, UI_WIDGET_SCROLL) == 0 &&
            w->width && w->height &&
            x >= w->x && x < w->x + *w->width &&
            y >= w->y && y < w->y + *w->height) {
            return (UIScroll*)b;
        }
    }
    return NULL;
}

// Forwards a mouse wheel event to the innermost UIScroll under (x, y). Vertical
// wheel goes to scrollY by default; with shift held it goes to scrollX (matching
// most browsers).
static void DispatchWheelToScrolls(UIChildren* children,
                                   float mouseX, float mouseY,
                                   float dxNotches, float dyNotches,
                                   int shift) {
    UIScroll* s = FindScrollAt(children, mouseX, mouseY);
    if (!s) return;
    const float speed = s->wheelSpeed > 0.0f ? s->wheelSpeed : 60.0f;
    if (shift) {
        if (s->allowHorizontal) s->scrollX -= dyNotches * speed;
    } else if (s->allowHorizontal && !s->allowVertical) {
        // Horizontal-only viewport (e.g. a VSCode-style tab strip): map the
        // vertical wheel onto horizontal scroll so a normal mouse wheel scrolls
        // it sideways, plus any genuine horizontal wheel delta.
        s->scrollX -= (dyNotches + dxNotches) * speed;
    } else {
        if (s->allowVertical)   s->scrollY -= dyNotches * speed;
        if (s->allowHorizontal) s->scrollX -= dxNotches * speed;
    }
}

// --- Scrollbar thumb drag --------------------------------------------------
// The vertical scrollbar thumb (drawn by RenderScroll in window_render.inc) is
// click-draggable. We recompute the SAME thumb geometry here from the scroll's
// live viewport + measured contentH, so the hit-test and the drag-to-scroll
// mapping line up exactly with the rendered bar.
typedef struct {
    int   has;
    float thumbX, thumbY, thumbW, thumbH; // thumb rect
    float trackY, viewH;                  // track origin + viewport height
} UIVThumb;

static UIVThumb ComputeVThumb(UIWidget* w, UIScroll* s) {
    UIVThumb r = {0};
    if (!s->showScrollbar || !s->allowVertical) return r;
    if (!w->width || !w->height) return r;
    const float viewW = *w->width, viewH = *w->height;
    if (s->contentH <= viewH) return r; // no overflow → no thumb
    const float barW = s->scrollbarWidth > 0.0f ? s->scrollbarWidth : 8.0f;
    float th = viewH * (viewH / s->contentH);
    if (th < 24.0f) th = 24.0f;
    const float maxYv = s->contentH - viewH;
    const float t = maxYv > 0.0f ? (s->scrollY / maxYv) : 0.0f;
    const float ty = w->y + t * (viewH - th);
    r.has = 1;
    r.thumbX = w->x + viewW - barW; r.thumbY = ty;
    r.thumbW = barW;                r.thumbH = th;
    r.trackY = w->y;                r.viewH  = viewH;
    return r;
}

// Begin dragging the vertical thumb under (x, y), if any. Recurses like
// FindScrollAt so a nested scroll's thumb wins; returns 1 if a thumb is grabbed.
static int ScrollbarDragBegin(UIChildren* children, float x, float y) {
    if (!children) return 0;
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible || !w->data) continue;
        UIChildren* kids = WheelContainerChildren(w);
        if (kids && ScrollbarDragBegin(kids, x, y)) return 1;
        UIWidgetBase* b = (UIWidgetBase*)w->data;
        if (strcmp(b->__widget_type, UI_WIDGET_SCROLL) == 0) {
            UIScroll* s = (UIScroll*)b;
            UIVThumb tb = ComputeVThumb(w, s);
            if (tb.has && x >= tb.thumbX && x < tb.thumbX + tb.thumbW &&
                          y >= tb.thumbY && y < tb.thumbY + tb.thumbH) {
                s->__barDragging = 1;
                s->__barGrabDY = y - tb.thumbY; // keep the grab point under the cursor
                return 1;
            }
        }
    }
    return 0;
}

// Apply an in-progress thumb drag. Returns 1 if some scroll consumed it.
static int ScrollbarDragMove(UIChildren* children, float y) {
    if (!children) return 0;
    int handled = 0;
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w = children->children[i];
        if (!w || !w->data) continue;
        UIChildren* kids = WheelContainerChildren(w);
        if (kids && ScrollbarDragMove(kids, y)) handled = 1;
        UIWidgetBase* b = (UIWidgetBase*)w->data;
        if (strcmp(b->__widget_type, UI_WIDGET_SCROLL) == 0) {
            UIScroll* s = (UIScroll*)b;
            if (s->__barDragging) {
                UIVThumb tb = ComputeVThumb(w, s);
                if (tb.has) {
                    const float maxYv = s->contentH - tb.viewH;
                    const float span  = tb.viewH - tb.thumbH;
                    const float desiredTop = y - s->__barGrabDY;
                    float t = span > 0.0f ? (desiredTop - tb.trackY) / span : 0.0f;
                    if (t < 0.0f) t = 0.0f;
                    if (t > 1.0f) t = 1.0f;
                    s->scrollY = t * maxYv;
                }
                handled = 1;
            }
        }
    }
    return handled;
}

// Release any thumb drag.
static void ScrollbarDragEnd(UIChildren* children) {
    if (!children) return;
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w = children->children[i];
        if (!w || !w->data) continue;
        UIChildren* kids = WheelContainerChildren(w);
        if (kids) ScrollbarDragEnd(kids);
        UIWidgetBase* b = (UIWidgetBase*)w->data;
        if (strcmp(b->__widget_type, UI_WIDGET_SCROLL) == 0)
            ((UIScroll*)b)->__barDragging = 0;
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

#if defined(MOCIDA_IOS)
    // The resize event during an orientation change can carry stale /
    // pre-rotation dimensions. SDL_GetWindowSize is authoritative for the
    // settled orientation, so trust it for the layout + presentation size.
    {
        int aw = 0, ah = 0;
        SDL_GetWindowSize(app->window->sdlWindow, &aw, &ah);
        if (aw > 0 && ah > 0) {
            new_width  = aw;
            new_height = ah;
            app->window->width  = aw;
            app->window->height = ah;
        }
    }
#endif

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

    // Always read the LOGICAL (screen-coordinate) size. SDL_EVENT_WINDOW_RESIZED
    // carries logical data1/data2, but SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED carries
    // PHYSICAL pixels — using those would set window->width to the DPI-scaled size
    // (e.g. 1035 for a 720 logical window at 144 DPI), while the renderer's logical
    // presentation (UIWindow_Render) stays at the logical size. That mismatch makes
    // Window.width report the wrong value and shifts/overflows layout. SDL_GetWindowSize
    // returns the logical size for every event type, matching the render space.
    int w = 0, h = 0;
    SDL_GetWindowSize(app->window->sdlWindow, &w, &h);
    if (w <= 0 || h <= 0) {
        w = event->window.data1;
        h = event->window.data2;
    }
    ApplyResize(app, w, h);
    UIWindow_Render(app->window);
    return false;
}

// If `w` is a layout container, return its child collection so the cursor
// hover-test can recurse into NESTED interactive widgets (e.g. a resize-handle
// MouseArea deep inside Stacks). Mirrors the dispatch's ContainerChildren.
static UIChildren* CursorContainerChildren(UIWidget* w) {
    if (!w || !w->data) return NULL;
    UIWidgetBase* base = (UIWidgetBase*)w->data;
    const char* t = base->__widget_type;
    if (!strcmp(t, UI_WIDGET_STACK))     return ((UIStack*)base)->items;
    if (!strcmp(t, UI_WIDGET_GLASS))     return ((UIGlass*)base)->items;
    if (!strcmp(t, UI_WIDGET_GRID))      return ((UIGrid*)base)->items;
    if (!strcmp(t, UI_WIDGET_RECTANGLE)) return (UIChildren*)((UIRectangle*)base)->children;
    if (!strcmp(t, UI_WIDGET_SCROLL)) {
        UIWidget* content = ((UIScroll*)base)->content;
        return content ? CursorContainerChildren(content) : NULL;
    }
    return NULL;
}

// Walks children back-to-front and returns the cursor advertised by the topmost
// widget under (x, y). Falls back to UI_CURSOR_DEFAULT when nothing interactive
// is under the cursor.
//
// `*blocked` (when non-NULL) reports whether the point was COVERED by a sized,
// opaque widget (or a container holding one). Callers use it so that an overlay
// rooted in an intrinsic (sizeless) wrapper still blocks widgets behind it:
// the Settings modal is a full-window dim layer wrapped in a sizeless view root,
// so without propagating coverage the cursor would "leak" through the modal to
// the editor's TextArea underneath, which then wrongly advertises the I-beam
// over the dialog. A sized widget knows it covers the point (its bounds were
// tested); a sizeless container inherits coverage from a child that does.
static UICursor PickHoverCursorImpl(UIChildren* children, float x, float y, bool* blocked) {
    if (blocked) *blocked = false;
    if (!children) return UI_CURSOR_DEFAULT;
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible || !w->data) continue;

        UIWidgetBase* base = (UIWidgetBase*)w->data;
        const char* type = base->__widget_type;

        // Does the point fall within this widget's own box? Intrinsic (sizeless)
        // widgets have no box of their own, so they "contain" everything and are
        // judged purely by their children.
        const bool sized = w->width && w->height;
        const bool inBounds = !sized ||
            (x >= w->x && x < w->x + *w->width && y >= w->y && y < w->y + *w->height);

        // CONTAINERS are recursed into UNCONDITIONALLY — never gated on the
        // container's own bounds. This mirrors the mouse dispatch (button.c's
        // ContainerChildren), which only bounds-tests leaf widgets: a free-layout
        // overlay (the Settings dim layer) renders where its absolute x/y say, but
        // its *stored* box may not enclose that spot, so a bounds-gated recursion
        // would skip the whole modal and let the cursor leak to the editor's
        // TextArea behind it. Child leaves carry correct absolute positions, so
        // descending unconditionally and bounds-testing them is what's reliable.
        UIChildren* kids = CursorContainerChildren(w);
        if (kids) {
            bool childBlocked = false;
            UICursor c = PickHoverCursorImpl(kids, x, y, &childBlocked);
            if (c != UI_CURSOR_DEFAULT) { if (blocked) *blocked = true; return c; }
            // Nothing inside advertised a cursor, but coverage still propagates:
            // a sized, opaque descendant (or this container itself) covering the
            // point blocks widgets BEHIND it. This is what makes an empty area of
            // the modal show the arrow instead of the editor's I-beam.
            if (childBlocked || (sized && inBounds)) {
                if (blocked) *blocked = true;
                return UI_CURSOR_DEFAULT;
            }
            continue; // didn't claim, doesn't cover this point → try siblings
        }

        // LEAF widgets: bounds-test against their own (correct) absolute box.
        if (!inBounds) continue;

        // WebView2 composition mode: defer to whatever cursor the page
        // most recently reported (link = pointer, input = text, etc).
        if (!strcmp(type, UI_WIDGET_WEBVIEW)) {
            int wvCursor = UIWebView_HoverCursorAt(children, x, y);
            if (blocked) *blocked = true;
            return (UICursor)wvCursor; /* 0 = DEFAULT if not in comp mode */
        }
        if (!strcmp(type, UI_WIDGET_BUTTON)) {
            UIButton* b = (UIButton*)base;
            if (blocked) *blocked = true;
            if (!b->enabled) return UI_CURSOR_NOT_ALLOWED;
            return b->cursor;
        }
        if (!strcmp(type, UI_WIDGET_MOUSE_AREA)) {
            UIMouseArea* m = (UIMouseArea*)base;
            if (blocked) *blocked = true;
            if (!m->enabled) return UI_CURSOR_DEFAULT;
            return m->cursor;
        }
        if (!strcmp(type, UI_WIDGET_CHECKBOX)) {
            if (blocked) *blocked = true;
            return ((UICheckbox*)base)->cursor;
        }
        if (!strcmp(type, UI_WIDGET_SLIDER)) {
            if (blocked) *blocked = true;
            return ((UISlider*)base)->cursor;
        }
        if (!strcmp(type, UI_WIDGET_SWITCH)) {
            if (blocked) *blocked = true;
            return ((UISwitch*)base)->cursor;
        }
        if (!strcmp(type, UI_WIDGET_RADIO)) {
            if (blocked) *blocked = true;
            return ((UIRadioButton*)base)->cursor;
        }
        if (!strcmp(type, UI_WIDGET_TEXTFIELD)) {
            if (blocked) *blocked = true;
            return ((UITextField*)base)->cursor;
        }
        if (!strcmp(type, UI_WIDGET_TEXTAREA)) {
            if (blocked) *blocked = true;
            UITextArea* ta = (UITextArea*)base;
            // Over an inline color swatch → the click (hand) cursor, not the I-beam.
            for (int s = 0; s < ta->__swatchRectCount; s++) {
                if (x >= ta->__swatchRX[s] && x < ta->__swatchRX[s] + ta->__swatchRW[s] &&
                    y >= ta->__swatchRY[s] && y < ta->__swatchRY[s] + ta->__swatchRH[s]) {
                    return UI_CURSOR_POINTER;
                }
            }
            return ta->cursor;
        }
        if (!strcmp(type, UI_WIDGET_TEXT)) {
            UIText* t = (UIText*)base;
            // Selectable text advertises (and covers with) the I-beam; plain text
            // is transparent to the cursor — keep looking at widgets behind it.
            if (t->selectable) { if (blocked) *blocked = true; return t->cursor; }
            continue;
        }
        // Any other sized leaf (e.g. an Image, a plain Rectangle) is opaque and
        // covers the point, blocking widgets behind it.
        if (sized) { if (blocked) *blocked = true; return UI_CURSOR_DEFAULT; }
    }
    return UI_CURSOR_DEFAULT;
}

static UICursor PickHoverCursor(UIChildren* children, float x, float y) {
    return PickHoverCursorImpl(children, x, y, NULL);
}

void HandleEvent(UIApp* app, SDL_Event* event) {
    if (!app || !app->window) return;

    switch (event->type) {
        case SDL_EVENT_MOUSE_MOTION: {
            float rx, ry;
            WindowToRenderCoords(app, event->motion.x, event->motion.y, &rx, &ry);
            // A scrollbar-thumb drag in progress owns the motion: move it and
            // swallow the event so hover/selection logic doesn't also react.
            if (ScrollbarDragMove(app->window->children, ry)) break;
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
                // A scrollbar thumb grab takes priority over any widget beneath
                // it; if it grabs, don't also click through to that widget.
                if (ScrollbarDragBegin(app->window->children, rx, ry)) break;
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
            // Generic per-widget key callbacks (MUI `onKeyInput`). The key name
            // is SDL's ("A", "Return", "Escape", "Space", …); fires for every
            // widget that registered one (keyboard isn't spatial).
            {
                const char* keyName = SDL_GetKeyName(event->key.key);
                UIWidget_DispatchKeyDown(app->window->children,
                                         keyName ? keyName : "", (int)event->key.mod);
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            float rx, ry;
            WindowToRenderCoords(app, event->button.x, event->button.y, &rx, &ry);
            // End any scrollbar-thumb drag before the usual up-dispatch.
            ScrollbarDragEnd(app->window->children);
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

/// The most-recently created app, for global window helpers (set in
/// UIApp_Create). One app per process in practice.
static UIApp* g_currentApp = NULL;

// Forward decl: defined below, but UIApp_Create installs it at create time.
static void UIApp_InstallHitTest(UIApp* app, int on);

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
    app->onTick = NULL;                        // Per-frame callback; opt-in via UIApp_OnTick.
    app->onTickUserdata = NULL;
    app->orientations = UI_ORIENTATION_ALL;    // iOS: every orientation allowed by default.
    app->statusBarHidden = 0;                  // iOS: status bar visible by default.

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

    // Auto-load an app.bundle manifest (registers mocida:// assets + sets
    // the app name/id). Try the CWD now so a manifest "name" can become the
    // window title; the in-bundle copy (iOS) is retried after SDL init.
    int bundleLoaded = UIApp_LoadBundleManifest("app.bundle");
    const char* effectiveTitle = (UIApp_GetBundleName() && *UIApp_GetBundleName())
                                 ? UIApp_GetBundleName() : title;

    app->window = UIWindow_Create(effectiveTitle, width, height);
    if (app->window == NULL) {
        UI_ERROR(UI_CAT_WINDOW, "UIWindow_Create returned NULL");
        UIWidget_Destroy(mainWidget);
        free(app);
        return NULL;
    }

    UIApp_SetBackgroundColor(app, UI_COLOR_WHITE);

    // iOS (and any case where app.bundle wasn't in the CWD): retry from the
    // app bundle dir now that SDL is initialised and SDL_GetBasePath works.
    if (!bundleLoaded) {
        const char* base = SDL_GetBasePath();
        if (base) {
            char mp[1024];
            snprintf(mp, sizeof(mp), "%sapp.bundle", base);
            if (UIApp_LoadBundleManifest(mp) &&
                UIApp_GetBundleName() && *UIApp_GetBundleName()) {
                UIApp_SetWindowTitle(app, UIApp_GetBundleName());
            }
        }
    }

    /* Register tree dumper so a crash report includes the widget tree. */
    UICrash_SetTreeDumper(mocida_crash_tree_dump, app);

    // Live-resize watch: keeps frames drawing while the user drags the
    // window border on Windows (where the OS modal sizing loop blocks
    // the normal poll loop).
    SDL_AddEventWatch(LiveResizeWatch, app);
    // Track the most-recently created app so global helpers (e.g.
    // UIApp_SetAlwaysOnTop) can reach the window without threading the handle
    // through every caller — there is effectively one app per process.
    g_currentApp = app;

    // If a custom title bar was requested before create, the window is already
    // borderless — install the hit-test now so dragging/resize work. The host
    // still feeds the live drag-region rect each frame (UIApp_SetDragRegion).
    if (UIWindow_WantsCustomTitlebar()) {
        UIApp_InstallHitTest(app, 1);
    }
    return app;
}

/// Toggle the always-on-top flag on the current app's window. Global so a UI
/// handler can call it from anywhere (`Screen.alwaysOnTop = true` in MUI).
void UIApp_SetAlwaysOnTop(int on) {
    if (g_currentApp && g_currentApp->window && g_currentApp->window->sdlWindow) {
        SDL_SetWindowAlwaysOnTop(g_currentApp->window->sdlWindow, on ? true : false);
    }
}

// --------------------------------------------------------------------
// Custom (client-side) title bar: hit-test + window controls.
//
// When the app requested a borderless window (UIWindow_RequestCustomTitlebar
// before create) it paints its own title bar. The OS still needs to know which
// pixels drag the window and which resize it: SDL_SetWindowHitTest calls back
// per mouse-down with a window-space point, and we answer DRAGGABLE / RESIZE_*
// / NORMAL. The whole behaviour (drag, double-click-maximize, Aero-snap,
// edge-resize) is then handled natively by Windows/macOS/X11.
//
// The drag region is a rect (window-logical coords) the app updates each frame
// from the title bar's live bounds (UIApp_SetDragRegion). A point inside it is
// DRAGGABLE *unless* an interactive widget (button / menu / icon) sits under it
// — we reuse PickHoverCursor to detect that, so the in-bar controls keep
// receiving their clicks instead of the hit-test swallowing them as a drag.
// --------------------------------------------------------------------
static int   g_dragRegionSet = 0;
static float g_dragX = 0, g_dragY = 0, g_dragW = 0, g_dragH = 0;

// Is an INTERACTIVE leaf widget (button / mouse-area / menu icon / input / etc.)
// under (x, y)? Used by the title-bar hit-test to decide whether a point in the
// drag region is a real control (keep it clickable → NORMAL) or empty chrome
// (→ DRAGGABLE). Unlike PickHoverCursor this recurses through EVERY container
// unconditionally and bounds-tests only leaves, with NO "opaque container
// blocks what's behind it" early-out — that coverage logic (correct for cursor
// selection) wrongly reported the whole toolbar row as blank because the
// editor's sized body/root containers short-circuit the search before the
// toolbar's buttons are reached. A plain "any interactive leaf here?" test is
// exactly what the drag-vs-click decision needs.
static int PointHitsInteractive(UIChildren* children, float x, float y) {
    if (!children) return 0;
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible || !w->data) continue;
        UIWidgetBase* base = (UIWidgetBase*)w->data;
        const char* type = base->__widget_type;

        // Recurse into any container first (free-layout overlays carry correct
        // absolute child positions, so a deep button is still found).
        UIChildren* kids = CursorContainerChildren(w);
        if (kids && PointHitsInteractive(kids, x, y)) return 1;

        // Leaf bounds test (sizeless widgets have no box → can't be "hit").
        if (!w->width || !w->height) continue;
        const bool in = (x >= w->x && x < w->x + *w->width &&
                         y >= w->y && y < w->y + *w->height);
        if (!in) continue;

        if (!strcmp(type, UI_WIDGET_BUTTON)) {
            if (((UIButton*)base)->enabled) return 1;
        } else if (!strcmp(type, UI_WIDGET_MOUSE_AREA)) {
            if (((UIMouseArea*)base)->enabled) return 1;
        } else if (!strcmp(type, UI_WIDGET_CHECKBOX) ||
                   !strcmp(type, UI_WIDGET_SWITCH)   ||
                   !strcmp(type, UI_WIDGET_RADIO)    ||
                   !strcmp(type, UI_WIDGET_SLIDER)   ||
                   !strcmp(type, UI_WIDGET_TEXTFIELD)||
                   !strcmp(type, UI_WIDGET_TEXTAREA) ||
                   !strcmp(type, UI_WIDGET_WEBVIEW)) {
            return 1;
        } else if (!strcmp(type, UI_WIDGET_TEXT)) {
            if (((UIText*)base)->selectable) return 1;
        }
        // Plain Rectangles / Images / Stacks are NOT interactive — they don't
        // block the drag decision, so keep scanning siblings (a draggable bar
        // typically has a background Rectangle the user SHOULD be able to drag).
    }
    return 0;
}
// Resize-border thickness (logical px) on the window edges of a borderless
// window. 6px matches the JetBrains/VSCode feel and is comfortable to grab.
#define MOCIDA_RESIZE_BORDER 6

static SDL_HitTestResult SDLCALL MocidaHitTest(SDL_Window* win, const SDL_Point* area, void* data) {
    UIApp* app = (UIApp*)data;
    if (!app || !app->window) return SDL_HITTEST_NORMAL;

    int w = 0, h = 0;
    SDL_GetWindowSize(win, &w, &h);
    const int x = area->x, y = area->y;
    const int b = MOCIDA_RESIZE_BORDER;

    // Resize borders take priority on the window edges so the user can always
    // grab them — but only when not maximized (a maximized window can't be
    // edge-resized, and snapping a resize there would feel broken).
    const SDL_WindowFlags flags = SDL_GetWindowFlags(win);
    const int maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;
    if (!maximized) {
        const int left   = x < b;
        const int right  = x >= w - b;
        const int top    = y < b;
        const int bottom = y >= h - b;
        if (top && left)     return SDL_HITTEST_RESIZE_TOPLEFT;
        if (top && right)    return SDL_HITTEST_RESIZE_TOPRIGHT;
        if (bottom && left)  return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (top)             return SDL_HITTEST_RESIZE_TOP;
        if (bottom)          return SDL_HITTEST_RESIZE_BOTTOM;
        if (left)            return SDL_HITTEST_RESIZE_LEFT;
        if (right)           return SDL_HITTEST_RESIZE_RIGHT;
    }

    // Inside the app-declared title-bar drag region?
    if (g_dragRegionSet &&
        (float)x >= g_dragX && (float)x < g_dragX + g_dragW &&
        (float)y >= g_dragY && (float)y < g_dragY + g_dragH) {
        // The hit-test point is in WINDOW (logical) space; the widget tree is
        // laid out in the renderer's logical space. They coincide for a
        // 1:1-presented window, but convert defensively so a letterboxed /
        // DPI-scaled presentation still maps correctly.
        float rx = (float)x, ry = (float)y;
        if (app->window->sdlRenderer) {
            SDL_RenderCoordinatesFromWindow(app->window->sdlRenderer,
                                            (float)x, (float)y, &rx, &ry);
        }
        // If an interactive widget (button, menu, icon, input) is under the
        // point, it owns the click — don't make that pixel draggable. Otherwise
        // the empty chrome drags the window.
        if (!PointHitsInteractive(app->window->children, rx, ry)) {
            return SDL_HITTEST_DRAGGABLE;
        }
    }
    return SDL_HITTEST_NORMAL;
}

// Install (or, when on==0, remove) the custom-titlebar hit-test on the current
// window. Called from UIApp_SetCustomTitlebar. Safe to call repeatedly.
static void UIApp_InstallHitTest(UIApp* app, int on) {
    if (!app || !app->window || !app->window->sdlWindow) return;
    if (on) {
        SDL_SetWindowHitTest(app->window->sdlWindow, MocidaHitTest, app);
    } else {
        SDL_SetWindowHitTest(app->window->sdlWindow, NULL, NULL);
    }
}

// Enable/disable client-side decorations on the (already created) window. The
// BORDERLESS flag itself must be requested before create (UIWindow_Request-
// CustomTitlebar); this toggles the border live and wires the hit-test. Returns
// nothing — cosmetic best-effort.
void UIApp_SetCustomTitlebar(int on) {
    if (!g_currentApp || !g_currentApp->window || !g_currentApp->window->sdlWindow) return;
    SDL_SetWindowBordered(g_currentApp->window->sdlWindow, on ? false : true);
    UIApp_InstallHitTest(g_currentApp, on);
}

// Update the title-bar drag region (window-logical coords). Call every frame
// from the host with the title bar's live bounds. A zero/negative size clears
// the region (nothing draggable).
void UIApp_SetDragRegion(float x, float y, float w, float h) {
    if (w <= 0.0f || h <= 0.0f) { g_dragRegionSet = 0; return; }
    g_dragRegionSet = 1;
    g_dragX = x; g_dragY = y; g_dragW = w; g_dragH = h;
}

// Window controls for the custom title bar's min/max/close buttons.
void UIApp_MinimizeG(void) {
    if (g_currentApp && g_currentApp->window && g_currentApp->window->sdlWindow)
        SDL_MinimizeWindow(g_currentApp->window->sdlWindow);
}

int UIApp_IsMaximizedG(void) {
    if (!g_currentApp || !g_currentApp->window || !g_currentApp->window->sdlWindow) return 0;
    return (SDL_GetWindowFlags(g_currentApp->window->sdlWindow) & SDL_WINDOW_MAXIMIZED) ? 1 : 0;
}

// Toggle maximize <-> restore. SDL_MaximizeWindow respects the work area
// (won't cover the taskbar) on Windows.
void UIApp_ToggleMaximizeG(void) {
    if (!g_currentApp || !g_currentApp->window || !g_currentApp->window->sdlWindow) return;
    SDL_Window* w = g_currentApp->window->sdlWindow;
    if (SDL_GetWindowFlags(w) & SDL_WINDOW_MAXIMIZED) {
        SDL_RestoreWindow(w);
    } else {
        SDL_MaximizeWindow(w);
    }
}

void UIApp_CloseG(void) {
    if (!g_currentApp || !g_currentApp->window) return;
    // Mirror the SDL_EVENT_QUIT path so UIApp_Run's loop exits cleanly and the
    // normal teardown (UIApp_Destroy) runs.
    g_currentApp->window->visible = 0;
    g_currentApp->runInBackground = 0;
    SDL_Event q; SDL_zero(q); q.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&q);
}

// ---- Global (current-app) convenience wrappers, for the MUI App.*/Window.*
// bridge (a handler/effect can call these from anywhere). All no-op if there's
// no current app. -----------------------------------------------------------
void UIApp_SetTitleG(const char* title) {
    if (g_currentApp && title) UIApp_SetWindowTitle(g_currentApp, title);
}
void UIApp_SetSizeG(int width, int height) {
    if (g_currentApp) UIApp_SetWindowSize(g_currentApp, width, height);
}
void UIApp_SetMaxFpsG(int fps) {
    if (g_currentApp) UIApp_SetTargetFPS(g_currentApp, fps);
}
int UIApp_GetWidthG(void) {
    return g_currentApp ? UIApp_GetWidth(g_currentApp) : 0;
}
int UIApp_GetHeightG(void) {
    return g_currentApp ? UIApp_GetHeight(g_currentApp) : 0;
}
const char* UIApp_GetTitleG(void) {
    if (g_currentApp && g_currentApp->window && g_currentApp->window->sdlWindow) {
        const char* t = SDL_GetWindowTitle(g_currentApp->window->sdlWindow);
        return t ? t : "";
    }
    return "";
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

    // The old tree (and its focused widget) is about to be freed, so drop the
    // global focus pointer first — otherwise the next focus change would blur a
    // dangling widget and crash (FocusApply → strcmp on freed memory). Hosts
    // that rebuild while a TextArea is focused (e.g. an autocomplete popup)
    // re-focus the new widget themselves after this call.
    UIWidget_InvalidateFocus();

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

void UIApp_SetName(UIApp* app, const char* name) {
    if (!name) return;
    UIApp_SetBundleName(name);            // app/bundle display name
    UIApp_SetWindowTitle(app, name);      // desktop window title
}

void UIApp_SetRunInBackground(UIApp* app, int enabled) {
    if (app) app->runInBackground = enabled ? 1 : 0;
}

// --------------------------------------------------------------------
// Desktop system-tray icon (SDL3 SDL_Tray). No-op on iOS.
// --------------------------------------------------------------------
#ifndef MOCIDA_IOS
typedef struct { UITrayCallback cb; void* ud; } MocidaTrayCb;
static void mocida_tray_trampoline(void* userdata, SDL_TrayEntry* entry) {
    (void)entry;
    MocidaTrayCb* c = (MocidaTrayCb*)userdata;
    if (c && c->cb) c->cb(c->ud);
}
#endif

int UIApp_SetTrayIcon(UIApp* app, const char* iconPath, const char* tooltip) {
#ifdef MOCIDA_IOS
    (void)app; (void)iconPath; (void)tooltip;
    return 0;   // no system tray on iOS
#else
    if (!app) return 0;
    SDL_Surface* icon = iconPath ? UIAsset_LoadSurface(iconPath) : NULL;
    SDL_Tray* tray = SDL_CreateTray(icon, tooltip);
    if (icon) SDL_DestroySurface(icon);   // SDL_CreateTray takes its own copy
    if (!tray) {
        UI_WARN(UI_CAT_CORE, "SDL_CreateTray failed: %s", SDL_GetError());
        return 0;
    }
    if (app->tray) SDL_DestroyTray((SDL_Tray*)app->tray);
    app->tray = tray;
    SDL_CreateTrayMenu(tray);             // empty menu, ready for items
    return 1;
#endif
}

void UIApp_AddTrayMenuItem(UIApp* app, const char* label,
                           UITrayCallback cb, void* userdata) {
#ifdef MOCIDA_IOS
    (void)app; (void)label; (void)cb; (void)userdata;
#else
    if (!app || !app->tray || !label) return;
    SDL_TrayMenu* menu = SDL_GetTrayMenu((SDL_Tray*)app->tray);
    if (!menu) menu = SDL_CreateTrayMenu((SDL_Tray*)app->tray);
    if (!menu) return;
    SDL_TrayEntry* e = SDL_InsertTrayEntryAt(menu, -1, label, SDL_TRAYENTRY_BUTTON);
    if (!e) return;
    if (cb) {
        // App-lifetime context (freed when the process exits / tray dies).
        MocidaTrayCb* ctx = (MocidaTrayCb*)malloc(sizeof(*ctx));
        if (ctx) {
            ctx->cb = cb; ctx->ud = userdata;
            SDL_SetTrayEntryCallback(e, mocida_tray_trampoline, ctx);
        }
    }
#endif
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

void UIApp_OnTick(UIApp* app, UIAppTickCallback cb, void* userdata) {
    if (!app) return;
    app->onTick = cb;
    app->onTickUserdata = userdata;
}

// Maps a UIOrientation bitmask to SDL's space-separated
// SDL_HINT_ORIENTATIONS string and applies it. SDL's iOS backend reads
// this hint live (each time iOS asks the view controller for its supported
// orientations), so it takes effect whether or not the window already
// exists. Off-device SDL ignores the hint, so this is a no-op on desktop.
static void UIApp_ApplyOrientationHint(unsigned orientations) {
    char buf[96];
    int n = 0;
    buf[0] = '\0';
    if (orientations & UI_ORIENTATION_PORTRAIT)
        n += SDL_snprintf(buf + n, sizeof(buf) - n, "%sPortrait", n ? " " : "");
    if (orientations & UI_ORIENTATION_PORTRAIT_UPSIDE_DOWN)
        n += SDL_snprintf(buf + n, sizeof(buf) - n, "%sPortraitUpsideDown", n ? " " : "");
    if (orientations & UI_ORIENTATION_LANDSCAPE_LEFT)
        n += SDL_snprintf(buf + n, sizeof(buf) - n, "%sLandscapeLeft", n ? " " : "");
    if (orientations & UI_ORIENTATION_LANDSCAPE_RIGHT)
        n += SDL_snprintf(buf + n, sizeof(buf) - n, "%sLandscapeRight", n ? " " : "");
    if (n > 0) SDL_SetHint(SDL_HINT_ORIENTATIONS, buf);
}

void UIApp_SetOrientation(UIApp* app, unsigned orientations) {
    if (!app) return;
    if (orientations == 0u) orientations = (unsigned)UI_ORIENTATION_ALL; // empty mask = unlock
    app->orientations = orientations;
    UIApp_ApplyOrientationHint(orientations);
}

unsigned UIApp_GetOrientation(UIApp* app) {
    return app ? app->orientations : (unsigned)UI_ORIENTATION_ALL;
}

void UIApp_SetStatusBarHidden(UIApp* app, int hidden) {
    if (!app) return;
    app->statusBarHidden = hidden ? 1 : 0;
#ifdef MOCIDA_IOS
    // SDL hides the iOS status bar when the window is fullscreen. Mocida
    // creates the window non-fullscreen (status bar visible by default), so
    // toggle fullscreen here to drive the status bar on demand.
    if (app->window && app->window->sdlWindow) {
        SDL_SetWindowFullscreen(app->window->sdlWindow, app->statusBarHidden != 0);
    }
#endif
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

    // Tear down the desktop tray icon (if any) and the bundle registry.
    if (app->tray) {
        SDL_DestroyTray((SDL_Tray*)app->tray);
        app->tray = NULL;
    }
    UIApp_BundleShutdown();

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

    // Keep looping while the window is visible OR background mode is on
    // (e.g. minimized to the tray). In background we still pump events so
    // the tray menu / "show window" works, but skip rendering.
    while (app->window->visible || app->runInBackground) {
        UIProfile_FrameBegin();

        // Per-frame user tick (opt-in). A single null-check when unused, so it
        // costs nothing for apps that don't set it; hot-reload uses it to swap
        // the root tree between frames on the UI thread.
        if (app->onTick) app->onTick(app->onTickUserdata);

        {
            UI_SCOPEC("events", UI_PROF_EVENT);
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_QUIT) {
                    app->window->visible = 0;
                    app->runInBackground = 0;   // QUIT overrides background
                }
                HandleEvent(app, &e);
            }
        }

        // Reconcile layout with the live window size AND safe-area insets.
        // On iOS rotation the window size and the safe area update on
        // DIFFERENT frames: SDL_GetWindowSafeArea keeps reporting the old
        // orientation's (transposed) insets for a frame or two after the
        // size already changed. A size-only relayout therefore sized the
        // panel against a stale safe area, so after rotating back to
        // portrait the background container came back short. Poll both and
        // relayout whenever EITHER diverges, so the layout self-heals the
        // moment the safe area settles. No-op when nothing changed.
        if (app->window->visible && app->window->sdlWindow) {
            static int lastW = -1, lastH = -1;
            static UIScreenInsets lastSafe = { -1, -1, -1, -1 };
            int liveW = 0, liveH = 0;
            SDL_GetWindowSize(app->window->sdlWindow, &liveW, &liveH);
            const UIScreenInsets s = UIScreen_GetSafeArea();
            if (liveW > 0 && liveH > 0 &&
                (liveW != lastW || liveH != lastH ||
                 s.top != lastSafe.top || s.left != lastSafe.left ||
                 s.bottom != lastSafe.bottom || s.right != lastSafe.right)) {
                lastW = liveW; lastH = liveH; lastSafe = s;
                ApplyResize(app, liveW, liveH);
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

        // Render only when the window is actually visible; in background
        // mode (hidden / minimized to tray) we skip the GPU work.
        if (app->window->visible) {
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