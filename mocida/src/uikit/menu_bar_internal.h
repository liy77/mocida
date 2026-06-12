// uikit/menu_bar_internal.h — internal layout for the menu bar tree.
//
// The PUBLIC header (uikit/menu_bar.h) keeps `UIMenu` and `UIMenuItem`
// opaque to consumers. This internal header is shared between menu_bar.c
// (cross-platform allocation / freeing) and menu_bar_cocoa.mm (Apple-only
// NSMenu / NSMenuItem views) so both translation units agree on the
// struct layout without the public API leaking the fields.
//
// Two separate child arrays on UIMenu (`items[]` for UIMenuItem leaves and
// `subs[]` for UIMenu submenus) keep the free / detach logic obvious — no
// need to tag every child with a type bit just to know which destructor
// to call.

#ifndef UIKIT_MENU_BAR_INTERNAL_H
#define UIKIT_MENU_BAR_INTERNAL_H

#include <uikit/menu_bar.h>
#include <stdint.h>

#ifdef __OBJC__
#import <objc/objc.h>  // for `struct objc_object` (id-equivalent for the
                        // strong fields below — avoids the `id` keyword
                        // ambiguity when the .mm file is parsed in mixed
                        // C++ struct-layout mode by clang).
#endif

// Struct definitions are NOT wrapped in `extern "C"`: that block is for
// function declarations (it controls name mangling), and C++ parsing of
// the ObjC `id` keyword inside an `extern "C"` block fails on clang.
struct UIMenu {
    char*      title;
    int        n_items;
    int        cap_items;
    UIMenuItem** items;        // leaf items in display order
    int        n_subs;
    int        cap_subs;
    UIMenu**   subs;           // submenus in display order
#ifdef __OBJC__
    // `id` is the cleanest spelling but clang's parser sometimes treats
    // the keyword as a C++ identifier inside struct bodies that include
    // other non-ObjC fields. `struct objc_object*` is the underlying
    // typedef-equivalent and parses cleanly in both C and C++ struct
    // bodies. We use `__strong` (which is a no-op without ARC) so the
    // assignment in menu_bar_cocoa.mm still keeps the object alive under
    // ARC if it ever gets enabled.
    __strong struct objc_object* nsMenu;  // NSMenu* on Mac
#else
    void*      nsMenu;         // opaque; undefined semantics
#endif
};

struct UIMenuItem {
    char*    label;
    char*    shortcut;         // SDL-style "CmdOrCtrl+N" or NULL
    uint32_t id;
    int      is_separator;
#ifdef __OBJC__
    __strong struct objc_object* nsItem;  // NSMenuItem* on Mac
#else
    void*    nsItem;           // opaque
#endif
};

#ifdef __cplusplus
extern "C" {
#endif

// ── Internal allocators (implemented in menu_bar.c) ─────────────────────
// The .mm layer uses these to construct its NSMenu / NSMenuItem objects
// without duplicating the C allocation logic.
UIMenu*     ui_menu__alloc(const char* title);
void        ui_menu__free(UIMenu* m);
UIMenuItem* ui_menu_item__alloc_leaf(const char* label,
                                     const char* shortcut,
                                     uint32_t id);
UIMenuItem* ui_menu_item__alloc_separator(void);
void        ui_menu_item__free(UIMenuItem* it);

// Append helpers track the child in the C-side arrays. They are the only
// way to add a child — direct manipulation would leave the arrays out of
// sync and ui_menu__free() would leak.
void ui_menu__append_item(UIMenu* parent, UIMenuItem* item);
void ui_menu__append_submenu(UIMenu* parent, UIMenu* sub);

// Replace the current root (NULL = clear). On Apple the .mm side
// detaches the previous root's NSMenu from [NSApp mainMenu] before
// installing the new one.
void ui_menu__set_root(UIMenu* root);

#ifdef __cplusplus
}
#endif

#endif // UIKIT_MENU_BAR_INTERNAL_H
