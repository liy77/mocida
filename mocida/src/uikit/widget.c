#include <uikit/widget.h>
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
    free(widget);
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

UIWidget* UIWidget_SetFocus(UIWidget* widget, int focused) {
    focused = focused ? 1 : 0;

    if (!focused) {
        if (!widget) return NULL;
        if (widget->focused) {
            widget->focused = 0;
            UIWidget_FocusApply(widget, 0);
        }
        if (g_focusedWidget == widget) g_focusedWidget = NULL;
        return widget;
    }

    if (!widget) return NULL;

    // Blur the previous focus owner first so SDL_StopTextInput happens
    // before SDL_StartTextInput on the same window.
    if (g_focusedWidget && g_focusedWidget != widget) {
        g_focusedWidget->focused = 0;
        UIWidget_FocusApply(g_focusedWidget, 0);
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

