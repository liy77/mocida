#include <uikit/widget.h>
#include <uikit/glass.h>
#include <uikit/debug.h>
#include <uikit/text.h>
#include <uikit/button.h>
#include <uikit/image.h>
#include <uikit/mouse_area.h>
#include <uikit/controls.h>
#include <uikit/textfield.h>
#include <uikit/textarea.h>
#include <uikit/webview.h>
#include <uikit/stack.h>
#include <uikit/dialog.h>
#include <uikit/tab.h>
#include <uikit/popup.h>
#include <uikit/video.h>
#include <uikit/file_drop.h>
#include <uikit/container.h>
#include <uikit/window.h>
#include <SDL3/SDL.h>
#include <stdint.h>

UIWidget* widgc(UIWidgetData data) {
    return UIWidget_Create(data);
}

UIWidget* widgcs(UIWidgetData data, float width, float height) {
    UIWidget* widget = UIWidget_Create(data);
    return UIWidget_SetSize(widget, width, height);
}

UIWidget* UIWidget_Create(UIWidgetData data)  {
    UIWidget* widget = (UIWidget*)malloc(sizeof(UIWidget));
    if (widget == NULL) {
        UI_ERROR(UI_CAT_WIDGET, "out of memory allocating UIWidget");
        return NULL;
    }
    UI_TRACK_ALLOC(UI_CAT_WIDGET);

    widget->width = NULL;     // dynamic
    widget->height = NULL;    // dynamic
    widget->x = 0;
    widget->y = 0;
    widget->z = 0;
    widget->visible = 1;
    widget->opacity = 1.0f;
    widget->rotation = 0.0f;
    widget->alignment = NULL;
    widget->parent = NULL;    // !! Without this, parent holds junk and Destroy crashes.
    widget->data = data;
    widget->id = NULL;
    widget->focused = 0;
    widget->clipChildren = 0;
    widget->propagatingEvents = 0; // default: occlude events (don't leak to widgets below)
    // Container-layout fields — MUST be zeroed: the Stack/Rectangle render reads
    // them every frame (cross-axis align + outer margins). Left uninitialised
    // (malloc, not calloc) they hold heap garbage, which makes layout random per
    // allocation: items jump around and spacing appears even with gap:0.
    widget->selfAlign = 0;
    widget->marginLeft = 0.0f;
    widget->marginTop = 0.0f;
    widget->marginRight = 0.0f;
    widget->marginBottom = 0.0f;
    widget->onKeyDown = NULL;
    widget->onKeyDownUserdata = NULL;

    return widget;
}

UIWidget* UIWidget_SetId(UIWidget* widget, const char* id) {
    if (widget == NULL || id == NULL) {
        return NULL;
    }

    free(widget->id); // free(NULL) is safe
    widget->id = _strdup(id);
    return widget;
}

UIWidget* UIWidget_SetSize(UIWidget* widget, float width, float height) {
    if (widget == NULL) {
        return NULL;
    }

    // Free previous width and height if allocated
    free(widget->width);
    free(widget->height);

    // Allocate or set width
    if (width <= UI_DYNAMIC_SIZE) {
        widget->width = NULL; // Dynamic size
    } else {
        widget->width = malloc(sizeof(float));
        if (widget->width == NULL) {
            UI_ERROR(UI_CAT_WIDGET, "out of memory allocating widget width");
            return NULL;
        }
        *widget->width = width;
    }

    // Allocate or set height
    if (height <= UI_DYNAMIC_SIZE) {
        widget->height = NULL; // Dynamic size
    } else {
        widget->height = malloc(sizeof(float));
        if (widget->height == NULL) {
            UI_ERROR(UI_CAT_WIDGET, "out of memory allocating widget height");
            free(widget->width); // Clean up width allocation
            return NULL;
        }
        *widget->height = height;
    }

    return widget;
}

UIWidget* UIWidget_SetPosition(UIWidget* widget, float x, float y) {
    if (widget == NULL) {
        return NULL;
    }

    widget->x = x;
    widget->y = y;
    return widget;
}

UIWidget* UIWidget_SetZIndex(UIWidget* widget, int z) {
    if (widget == NULL) {
        return NULL;
    }

    widget->z = z;
    return widget;
}

UIWidget* UIWidget_SetVisible(UIWidget* widget, int visible) {
    if (widget == NULL) {
        return NULL;
    }

    widget->visible = visible;
    return widget;
}

UIWidget* UIWidget_SetClipChildren(UIWidget* widget, int enabled) {
    if (widget == NULL) return NULL;
    widget->clipChildren = enabled ? 1 : 0;
    return widget;
}

int UIWidget_GetClipChildren(const UIWidget* widget) {
    return (widget && widget->clipChildren) ? 1 : 0;
}

UIWidget* UIWidget_SetPropagatingEvents(UIWidget* widget, int enabled) {
    if (widget == NULL) return NULL;
    widget->propagatingEvents = enabled ? 1 : 0;
    return widget;
}

int UIWidget_GetPropagatingEvents(const UIWidget* widget) {
    return (widget && widget->propagatingEvents) ? 1 : 0;
}

UIWidget* UIWidget_GetParent(UIWidget* widget) {
    if (widget == NULL) return NULL;
    // The previous check returned NULL when parent->width / height were
    // unset, which rejected legitimate parents with dynamic sizing.
    // Callers that need a concrete size should check width/height
    // themselves.
    return (UIWidget*)widget->parent;
}

UIWidget* UIWidget_SetParent(UIWidget* widget, UIWidget* parent) {
    if (widget == NULL || parent == NULL) {
        return NULL;
    }

    widget->parent = (UIWidgetData)parent;
    return widget;
}

void UIWidget_Destroy(UIWidget* widget) {
    if (!widget) {
        UI_WARN_ONCE("widget-destroy-null", UI_CAT_WIDGET,
                     "UIWidget_Destroy(NULL) — usually a sign of a double-free");
        return;
    }
    UI_TRACK_FREE(UI_CAT_WIDGET);

    if (widget->data) {
        UIWidgetBase* base = (UIWidgetBase*)widget->data;
        const char* t = base->__widget_type;
        if (UIWidget_TypeIs(t, UI_WIDGET_TEXT)) {
            UIText_Destroy((UIText*)base);           // already frees 'base'
        } else if (UIWidget_TypeIs(t, UI_WIDGET_RECTANGLE)) {
            UIRectangle_Destroy((UIRectangle*)base); // already frees 'base'
        } else if (UIWidget_TypeIs(t, UI_WIDGET_BUTTON)) {
            UIButton_Destroy((UIButton*)base);       // already frees 'base'
        } else if (UIWidget_TypeIs(t, UI_WIDGET_IMAGE)) {
            UIImage_Destroy((UIImage*)base);         // already frees 'base'
        } else if (UIWidget_TypeIs(t, UI_WIDGET_MOUSE_AREA)) {
            UIMouseArea_Destroy((UIMouseArea*)base); // already frees 'base'
        } else if (UIWidget_TypeIs(t, UI_WIDGET_CHECKBOX)) {
            UICheckbox_Destroy((UICheckbox*)base);
        } else if (UIWidget_TypeIs(t, UI_WIDGET_SWITCH)) {
            UISwitch_Destroy((UISwitch*)base);
        } else if (UIWidget_TypeIs(t, UI_WIDGET_RADIO)) {
            UIRadio_Destroy((UIRadioButton*)base);
        } else if (UIWidget_TypeIs(t, UI_WIDGET_SLIDER)) {
            UISlider_Destroy((UISlider*)base);
        } else if (UIWidget_TypeIs(t, UI_WIDGET_PROGRESS_BAR)) {
            UIProgressBar_Destroy((UIProgressBar*)base);
        } else if (UIWidget_TypeIs(t, UI_WIDGET_SPINNER)) {
            UISpinner_Destroy((UISpinner*)base);
        } else if (UIWidget_TypeIs(t, UI_WIDGET_TEXTFIELD)) {
            UITextField_Destroy((UITextField*)base);
        } else if (UIWidget_TypeIs(t, UI_WIDGET_TEXTAREA)) {
            UITextArea_Destroy((UITextArea*)base);
        } else if (UIWidget_TypeIs(t, UI_WIDGET_STACK)) {
            UIStack_Destroy((UIStack*)base);
        } else if (UIWidget_TypeIs(t, UI_WIDGET_GLASS)) {
            UIGlass_Destroy((UIGlass*)base);
        } else if (UIWidget_TypeIs(t, UI_WIDGET_DIALOG)) {
            UIDialog_Destroy((UIDialog*)base);
        } else if (UIWidget_TypeIs(t, UI_WIDGET_TABVIEW)) {
            UITabView_Destroy((UITabView*)base);
        } else if (UIWidget_TypeIs(t, UI_WIDGET_TOOLTIP)) {
            UITooltip_Destroy((UITooltip*)base);
        } else if (UIWidget_TypeIs(t, UI_WIDGET_MENU)) {
            UIMenu_Destroy((UIMenu*)base);
        } else if (UIWidget_TypeIs(t, UI_WIDGET_DROPDOWN)) {
            UIDropdown_Destroy((UIDropdown*)base);
        } else if (UIWidget_TypeIs(t, UI_WIDGET_VIDEO)) {
            UIVideo_Destroy((UIVideo*)base);
        } else if (UIWidget_TypeIs(t, UI_WIDGET_WEBVIEW)) {
            UIWebView_Destroy((UIWebView*)base);
        } else if (UIWidget_TypeIs(t, UI_WIDGET_FILE_DROP)) {
            UIFileDrop_Destroy((UIFileDrop*)base);
        } else if (UIWidget_TypeIs(t, UI_WIDGET_GRID)) {
            // Previously fell through to `free(base)` which kept g->items
            // and every child widget inside it alive. Hits any code path
            // that wraps a UIGrid in a UIWidget (UIListView, custom grid
            // layouts) on destroy.
            UIGrid_Destroy((UIGrid*)base);
        } else if (UIWidget_TypeIs(t, UI_WIDGET_SCROLL)) {
            // Same story as GRID — UIScroll owns content + background;
            // the default path leaked both.
            UIScroll_Destroy((UIScroll*)base);
        } else {
            // Unknown type - at least free the base struct so it doesn't leak.
            free(base);
        }
    }

    // Important: do NOT destroy widget->parent. Children don't own the
    // parent - the old code had a recursive free here that caused
    // double-free, and also crashed when 'parent' was uninitialized.

    free(widget->width);     // free(NULL) is safe
    free(widget->height);
    free(widget->id);
    free(widget->alignment);
    // Binding-side state (closures behind callback userdata) dies with the
    // widget. Released after the C object is gone so nothing can call into it.
    UIWidget__ReleaseOwned(widget->data);
    UIWidget__ReleaseOwned(widget);
    free(widget);
}

// ---------------------------------------------------------------------
// Owned userdata registry (language bindings)
// ---------------------------------------------------------------------
// Open hash of owner pointer -> chain of (userdata, freeFn). Owners are widget
// data pointers / UIWidget pointers; only the address is used as the key.

typedef struct UIOwnedNode {
    const void* owner;
    void* userdata;
    UIOwnedFreeFn freeFn;
    struct UIOwnedNode* next;
} UIOwnedNode;

static UIOwnedNode** s_owned = NULL;
static size_t s_ownedCap = 0;     // bucket count (power of two)
static size_t s_ownedCount = 0;

static size_t OwnedSlot(const void* p, size_t cap) {
    uintptr_t h = (uintptr_t)p;
    h ^= h >> 17;
    h *= (uintptr_t)0xed5ad4bbu;
    h ^= h >> 11;
    return (size_t)h & (cap - 1);
}

static void OwnedRehash(size_t newCap) {
    UIOwnedNode** nb = (UIOwnedNode**)calloc(newCap, sizeof(UIOwnedNode*));
    if (!nb) return;
    for (size_t i = 0; i < s_ownedCap; i++) {
        UIOwnedNode* n = s_owned[i];
        while (n) {
            UIOwnedNode* next = n->next;
            const size_t k = OwnedSlot(n->owner, newCap);
            n->next = nb[k];
            nb[k] = n;
            n = next;
        }
    }
    free(s_owned);
    s_owned = nb;
    s_ownedCap = newCap;
}

void UIWidget_AttachOwned(const void* owner, void* userdata, UIOwnedFreeFn freeFn) {
    if (!owner || !freeFn) return;
    if (s_ownedCount + 1 > s_ownedCap) OwnedRehash(s_ownedCap ? s_ownedCap * 2 : 256);
    if (!s_owned) { freeFn(userdata); return; }   // OOM: free now, not leak
    UIOwnedNode* n = (UIOwnedNode*)malloc(sizeof(UIOwnedNode));
    if (!n) { freeFn(userdata); return; }
    n->owner = owner;
    n->userdata = userdata;
    n->freeFn = freeFn;
    const size_t k = OwnedSlot(owner, s_ownedCap);
    n->next = s_owned[k];
    s_owned[k] = n;
    s_ownedCount++;
}

// Released state is parked here and freed by UIWidget_FlushOwned at the top of
// the next frame: a callback may destroy its own widget (e.g. a click handler
// that swaps the tree), so freeing its closure on the spot would pull the code
// out from under the running call.
static UIOwnedNode* s_retired = NULL;

void UIWidget__ReleaseOwned(const void* owner) {
    if (!owner || !s_ownedCount) return;
    UIOwnedNode** link = &s_owned[OwnedSlot(owner, s_ownedCap)];
    while (*link) {
        UIOwnedNode* n = *link;
        if (n->owner != owner) { link = &n->next; continue; }
        *link = n->next;
        s_ownedCount--;
        n->next = s_retired;
        s_retired = n;
    }
}

void UIWidget_FlushOwned(void) {
    // freeFn runs binding code that may destroy more widgets (retiring more
    // nodes) or attach new state; pop one node at a time until empty.
    static int flushing = 0;
    if (flushing) return;
    flushing = 1;
    while (s_retired) {
        UIOwnedNode* n = s_retired;
        s_retired = n->next;
        UIOwnedFreeFn fn = n->freeFn;
        void* ud = n->userdata;
        free(n);
        fn(ud);
    }
    flushing = 0;
}

// ---------------------------------------------------------------------
// Generic focus
// ---------------------------------------------------------------------

static UIWidget* g_focusedWidget = NULL;

// Type-specific blur. Resets internal "focused" mirrors and selection
// state so an unfocused widget doesn't keep behaving like the focused
// one.
static void UIWidget_FocusApply(UIWidget* widget, int focused) {
    if (!widget || !widget->data) return;
    UIWindow*   win  = UIWindow_GetActive();
    SDL_Window* sdlw = win ? win->sdlWindow : NULL;

    UIWidgetBase* b = (UIWidgetBase*)widget->data;
    const char* t = b->__widget_type;
    if (!t) return;   // defensive: a stale/half-freed widget has no type tag

    if (!strcmp(t, UI_WIDGET_TEXTFIELD)) {
        UITextField* tf = (UITextField*)b;
        tf->focused = focused;
        if (!focused) {
            tf->selAnchor      = -1;
            tf->mouseSelecting = 0;
            tf->clickCount     = 0;
        }
        if (sdlw) {
            if (focused) SDL_StartTextInput(sdlw);
            else         SDL_StopTextInput(sdlw);
        }
        return;
    }
    if (!strcmp(t, UI_WIDGET_TEXTAREA)) {
        UITextArea* ta = (UITextArea*)b;
        ta->focused = focused;
        if (!focused) {
            ta->selAnchor      = -1;
            ta->mouseSelecting = 0;
            ta->clickCount     = 0;
        }
        if (sdlw) {
            if (focused) SDL_StartTextInput(sdlw);
            else         SDL_StopTextInput(sdlw);
        }
        return;
    }
    if (!strcmp(t, UI_WIDGET_TEXT)) {
        UIText* tx = (UIText*)b;
        if (!tx->selectable) return;
        tx->focused = focused;
        if (!focused) {
            tx->mouseSelecting = 0;
        }
        return;
    }
    // Other widget types don't carry their own focused mirror today.
    // The generic UIWidget.focused flag is enough; widget-specific
    // visuals can read it via UIWidget_IsFocused.
}

static int UIWidget_ChildrenHave(UIChildren* children, const UIWidget* target);

// Recursively searches a live subtree for the exact widget pointer `target`,
// comparing by identity ONLY. `target` is never dereferenced, so this is safe
// to call with a possibly-freed cached pointer — the tree nodes we walk (`w`)
// are live, but `target` may be a stale address.
static int UIWidget_SubtreeHas(UIWidget* w, const UIWidget* target) {
    if (!w) return 0;
    if (w == target) return 1;
    if (!w->data) return 0;
    UIWidgetBase* base = (UIWidgetBase*)w->data;
    const char* t = base->__widget_type;
    if (!t) return 0;
    if (strcmp(t, UI_WIDGET_STACK) == 0)     return UIWidget_ChildrenHave(((UIStack*)base)->items, target);
    if (strcmp(t, UI_WIDGET_GLASS) == 0)     return UIWidget_ChildrenHave(((UIGlass*)base)->items, target);
    if (strcmp(t, UI_WIDGET_GRID) == 0)      return UIWidget_ChildrenHave(((UIGrid*)base)->items, target);
    if (strcmp(t, UI_WIDGET_RECTANGLE) == 0) return UIWidget_ChildrenHave((UIChildren*)((UIRectangle*)base)->children, target);
    if (strcmp(t, UI_WIDGET_SCROLL) == 0)    return UIWidget_SubtreeHas(((UIScroll*)base)->content, target);
    return 0;
}

static int UIWidget_ChildrenHave(UIChildren* children, const UIWidget* target) {
    if (!children) return 0;
    for (int i = 0; i < children->count; i++)
        if (UIWidget_SubtreeHas(children->children[i], target)) return 1;
    return 0;
}

// 1 if `widget` is still present in the active window's live tree. Used to
// confirm a cached focus pointer hasn't been freed by a tree rebuild before we
// touch it: blurring/focusing a stale widget runs FocusApply → strcmp on freed
// memory (ACCESS_VIOLATION). Pointer-identity only — `widget` is not read.
static int UIWidget_IsLive(const UIWidget* widget) {
    if (!widget) return 0;
    UIWindow* win = UIWindow_GetActive();
    if (!win || !win->children) return 0;
    return UIWidget_ChildrenHave(win->children, widget);
}

UIWidget* UIWidget_SetFocus(UIWidget* widget, int focused) {
    focused = focused ? 1 : 0;

    if (!focused) {
        if (!widget) return NULL;
        // Only blur a widget that is still in the live tree. A stale pointer
        // (its subtree was rebuilt out from under us) must not be focus-applied.
        if (!UIWidget_IsLive(widget)) {
            if (g_focusedWidget == widget) g_focusedWidget = NULL;
            return NULL;
        }
        if (widget->focused) {
            widget->focused = 0;
            UIWidget_FocusApply(widget, 0);
        }
        if (g_focusedWidget == widget) g_focusedWidget = NULL;
        return widget;
    }

    if (!widget) return NULL;

    // Refuse to focus a widget that isn't part of the live tree. Hosts that
    // rebuild the tree (e.g. an autocomplete popup) sometimes re-focus a cached
    // pointer freed by the rebuild; storing it would leave g_focusedWidget
    // dangling and crash on the next blur (FocusApply → strcmp on freed memory).
    if (!UIWidget_IsLive(widget)) return NULL;

    // Blur the previous focus owner first so SDL_StopTextInput happens
    // before SDL_StartTextInput on the same window. Guard against a stale
    // previous owner the same way — drop it without dereferencing if it's gone.
    if (g_focusedWidget && g_focusedWidget != widget) {
        if (UIWidget_IsLive(g_focusedWidget)) {
            g_focusedWidget->focused = 0;
            UIWidget_FocusApply(g_focusedWidget, 0);
        }
        g_focusedWidget = NULL;
    }

    widget->focused = 1;
    g_focusedWidget = widget;
    UIWidget_FocusApply(widget, 1);
    return widget;
}

int UIWidget_IsFocused(const UIWidget* widget) {
    return widget ? widget->focused : 0;
}

UIWidget* UIWidget_GetFocused(void) {
    return g_focusedWidget;
}

void UIWidget_ClearFocus(void) {
    if (!g_focusedWidget) return;
    UIWidget* w = g_focusedWidget;
    g_focusedWidget = NULL;
    w->focused = 0;
    UIWidget_FocusApply(w, 0);
}

void UIWidget_InvalidateFocus(void) {
    // Drop the cached focus owner WITHOUT dereferencing it — used when the whole
    // widget tree is replaced (UIApp_SetChildren), since the previously-focused
    // widget is about to be freed and blurring it (FocusApply → strcmp on its
    // type tag) would read freed memory.
    g_focusedWidget = NULL;
}

UIWidget* UIWidget_FindByData(void* data) {
    if (!data) return NULL;
    UIWindow* win = UIWindow_GetActive();
    if (!win || !win->children) return NULL;
    UIChildren* ch = win->children;
    for (int i = 0; i < ch->count; i++) {
        UIWidget* w = ch->children[i];
        if (w && w->data == data) return w;
    }
    return NULL;
}


// ---------------------------------------------------------------------------
// Generic per-widget key-down callback + recursive dispatch.
// ---------------------------------------------------------------------------

void UIWidget_SetOnKeyDown(UIWidget* widget,
                           void (*cb)(void* self, const char* key, int mods, void* userdata),
                           void* userdata) {
    if (!widget) return;
    widget->onKeyDown = cb;
    widget->onKeyDownUserdata = userdata;
}

// Returns a container widget's child collection so the key dispatch can recurse
// into nested widgets (keyboard isn't spatial, so it visits the whole tree).
static UIChildren* KeyContainerChildren(UIWidget* w) {
    if (!w || !w->data) return NULL;
    UIWidgetBase* base = (UIWidgetBase*)w->data;
    const char* t = base->__widget_type;
    if (strcmp(t, UI_WIDGET_STACK) == 0)     return ((UIStack*)base)->items;
    if (strcmp(t, UI_WIDGET_GLASS) == 0)     return ((UIGlass*)base)->items;
    if (strcmp(t, UI_WIDGET_GRID) == 0)      return ((UIGrid*)base)->items;
    if (strcmp(t, UI_WIDGET_RECTANGLE) == 0) return (UIChildren*)((UIRectangle*)base)->children;
    return NULL;
}

void UIWidget_DispatchKeyDown(UIChildren* children, const char* key, int mods) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w) continue;
        if (w->onKeyDown) {
            w->onKeyDown((void*)w, key, mods, w->onKeyDownUserdata);
        }
        UIChildren* kids = KeyContainerChildren(w);
        if (kids) UIWidget_DispatchKeyDown(kids, key, mods);
    }
}
