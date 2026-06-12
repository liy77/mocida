// uikit/menu_bar.h — public C API for the application menu bar.
//
// Builds a global menu bar (Cocoa NSMenu on macOS, no-op elsewhere) from
// a tree of menus and items constructed by the host. The host passes a
// callback that is invoked on the main thread when the user clicks any
// item; the callback receives the item's opaque `id` (an arbitrary u32
// the host picked at construction time) and the `user` pointer it
// registered. The host is responsible for routing the id to whatever
// action makes sense (e.g. setting a signal in the .mui runtime).
//
// The API is intentionally imperative and one-shot: the host builds the
// full tree and calls `ui_menu_bar_set` once at startup. Rebuilding the
// menu bar (e.g. to react to a language change) is supported — just
// call `ui_menu_bar_set` again with a fresh tree; the previous tree is
// destroyed by the implementation.

#ifndef UIKIT_MENU_BAR_H
#define UIKIT_MENU_BAR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque types — the C side never touches internals.
typedef struct UIMenu UIMenu;
typedef struct UIMenuItem UIMenuItem;

// Callback fired when the user activates a menu item. `id` is the value
// the host passed to `ui_menu_item_create`. `user` is the value the host
// passed to `ui_menu_bar_set_item_callback`. Called on the main thread.
typedef void (*ui_menu_item_callback_t)(uint32_t id, void* user);

// ── Menu ─────────────────────────────────────────────────────────────────

// Create a (sub)menu with the given title. Pass NULL or "" for the root
// menu (the application's main menu). Returns NULL on allocation failure.
UIMenu* ui_menu_create(const char* title);

// Release the menu. Safe to call on any menu the host still owns; safe
// to call on the root menu (it's removed from the app first if it was
// installed). Idempotent on NULL.
void ui_menu_destroy(UIMenu* m);

// ── Items ────────────────────────────────────────────────────────────────

// Create a leaf menu item. `label` is the visible text. `shortcut` is
// an SDL-style "CmdOrCtrl+N" / "CmdOrCtrl+Shift+Z" / "Alt+Z" string, or
// NULL for no shortcut. `id` is opaque to the C side; the host picks a
// u32 (e.g. an enum) and routes the id back to an action in its
// callback.
UIMenuItem* ui_menu_item_create(const char* label,
                                const char* shortcut,
                                uint32_t id);

// Create a separator (NSMenuItem separator on Cocoa; a no-op item on
// other platforms).
UIMenuItem* ui_menu_separator(void);

// Release an item the host still owns (typically: items not yet attached
// to a menu, or after the menu that owns them was destroyed). Idempotent
// on NULL.
void ui_menu_item_destroy(UIMenuItem* it);

// Attach a leaf item to a parent menu. Takes ownership of the item;
// the host must NOT destroy it after attaching.
void ui_menu_append_item(UIMenu* parent, UIMenuItem* item);

// Attach a submenu to a parent menu. Takes ownership of the submenu;
// the host must NOT destroy it after attaching.
void ui_menu_append_submenu(UIMenu* parent, UIMenu* sub);

// Append a separator to the parent. Convenience wrapper.
void ui_menu_append_separator(UIMenu* parent);

// ── Install / callback ───────────────────────────────────────────────────

// Set (or replace) the application's main menu. Pass NULL to clear the
// menu bar. The previous root menu is destroyed. Ownership of `root`
// transfers to the menu-bar subsystem; the host must NOT destroy it
// after this call.
//
// On non-macOS platforms this is a no-op (the menus are tracked but
// never displayed). On macOS this sets `[NSApp mainMenu]`.
void ui_menu_bar_set(UIMenu* root);

// Register the callback fired when any item attached to the current menu
// bar is activated. Either of `cb` or `user` may be NULL to unregister.
// The callback fires on the main thread.
void ui_menu_bar_set_item_callback(ui_menu_item_callback_t cb, void* user);

#ifdef __cplusplus
}
#endif

#endif // UIKIT_MENU_BAR_H
