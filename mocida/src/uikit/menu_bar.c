// menu_bar.c — cross-platform owner of the UIMenuBar / UIMenuBarItem tree.
//
// Compiled on every platform (the .c is picked up by the
// file(GLOB_RECURSE MOCIDA_SOURCES …) in CMakeLists.txt). The Cocoa
// implementation lives in menu_bar_cocoa.mm (added to the Apple-only
// block in the same CMakeLists); both .c and .mm include
// menu_bar_internal.h so the struct layout is defined in exactly one
// place.
//
// On macOS the .mm provides a set of weak "hook" symbols the C side
// calls to keep the NSMenu tree in lock-step with the C-side tree. On
// non-Apple builds those hooks are unresolved, which is fine — the C
// side treats "hook is NULL" as a no-op.

#include "menu_bar_internal.h"
#include <stdlib.h>
#include <string.h>

// ── Weak hooks (provided by menu_bar_cocoa.mm on Apple) ──────────────────
// On non-Apple the C side sees these symbols as NULL and skips the
// NSMenu/NSMenuItem bookkeeping. On Apple the .mm provides real
// implementations; the C code never calls into ObjC directly.
__attribute__((weak)) void ui_menu_bar__hook_attach_nsmenu(UIMenuBar* m);
__attribute__((weak)) void ui_menu_bar__hook_attach_nsitem(UIMenuBarItem* it,
                                                      UIMenuBar* parent);
__attribute__((weak)) void ui_menu_bar__hook_attach_nssubmenu(UIMenuBar* parent,
                                                         UIMenuBar* sub);
__attribute__((weak)) void ui_menu_bar__hook_install_root(UIMenuBar* root);
__attribute__((weak)) void ui_menu_bar__hook_uninstall_root(void);

// ── Helpers ──────────────────────────────────────────────────────────────

static char* dupstr(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char* r = (char*)malloc(n + 1);
    if (!r) return NULL;
    memcpy(r, s, n + 1);
    return r;
}

static int grow_array(void*** arr, int* cap, int needed) {
    if (needed <= *cap) return 1;
    int newcap = *cap ? *cap * 2 : 4;
    while (newcap < needed) newcap *= 2;
    void** p = (void**)realloc(*arr, (size_t)newcap * sizeof(void*));
    if (!p) return 0;
    *arr = p;
    *cap = newcap;
    return 1;
}

// ── Allocation ───────────────────────────────────────────────────────────

UIMenuBar* ui_menu_bar__alloc(const char* title) {
    UIMenuBar* m = (UIMenuBar*)calloc(1, sizeof(UIMenuBar));
    if (!m) return NULL;
    m->title = dupstr(title);
    // Title dup can fail (only on OOM). Leave title == NULL and let the
    // caller treat the menu as anonymous rather than aborting.
    if (ui_menu_bar__hook_attach_nsmenu) ui_menu_bar__hook_attach_nsmenu(m);
    return m;
}

void ui_menu_bar__free(UIMenuBar* m) {
    if (!m) return;
    // Free leaf items first, then recurse into submenus. The C side owns
    // all of these pointers — the .mm side dropped its strong refs in
    // either the matching hook call above, or via the symmetric
    // detach-hook (added in the Cocoa .mm) before this function ran.
    for (int i = 0; i < m->n_items; i++) {
        ui_menu_bar_item__free(m->items[i]);
    }
    for (int i = 0; i < m->n_subs; i++) {
        ui_menu_bar__free(m->subs[i]);
    }
    free(m->items);
    free(m->subs);
    free(m->title);
    free(m);
}

UIMenuBarItem* ui_menu_bar_item__alloc_leaf(const char* label,
                                     const char* shortcut,
                                     uint32_t id) {
    UIMenuBarItem* it = (UIMenuBarItem*)calloc(1, sizeof(UIMenuBarItem));
    if (!it) return NULL;
    it->id       = id;
    it->label    = dupstr(label);
    it->shortcut = dupstr(shortcut);
    if (!it->label) {
        // label is required to make a useful item; shortcut is optional
        // (NULL is valid → no key equivalent).
        free(it->shortcut);
        free(it);
        return NULL;
    }
    return it;
}

UIMenuBarItem* ui_menu_bar_item__alloc_separator(void) {
    UIMenuBarItem* it = (UIMenuBarItem*)calloc(1, sizeof(UIMenuBarItem));
    if (!it) return NULL;
    it->is_separator = 1;
    return it;
}

void ui_menu_bar_item__free(UIMenuBarItem* it) {
    if (!it) return;
    free(it->label);
    free(it->shortcut);
    free(it);
}

// ── Append helpers (track child in the C-side arrays) ───────────────────

void ui_menu_bar__append_item(UIMenuBar* parent, UIMenuBarItem* item) {
    if (!parent || !item) return;
    if (!grow_array((void***)&parent->items, &parent->cap_items,
                    parent->n_items + 1)) {
        return;  // OOM: drop the item rather than corrupt the array
    }
    parent->items[parent->n_items++] = item;
}

void ui_menu_bar__append_submenu(UIMenuBar* parent, UIMenuBar* sub) {
    if (!parent || !sub) return;
    if (!grow_array((void***)&parent->subs, &parent->cap_subs,
                    parent->n_subs + 1)) {
        return;
    }
    parent->subs[parent->n_subs++] = sub;
}

// ── Root replacement (the .mm detaches the previous NSMenu first) ────────

static UIMenuBar* g_root = NULL;

void ui_menu_bar__set_root(UIMenuBar* root) {
    if (g_root == root) return;
    if (ui_menu_bar__hook_uninstall_root) ui_menu_bar__hook_uninstall_root();
    g_root = root;
    if (ui_menu_bar__hook_install_root) ui_menu_bar__hook_install_root(root);
}

// ── Public C API (called by the .mm on Apple, or by the host directly
//    via the dynamic symbol on every platform) ───────────────────────────

UIMenuBar* ui_menu_bar_create(const char* title) {
    return ui_menu_bar__alloc(title);
}

void ui_menu_bar_destroy(UIMenuBar* m) {
    // If the host destroys the currently-installed root, treat it as a
    // "clear menu bar" so [NSApp mainMenu] doesn't keep pointing at a
    // tree that just got freed.
    if (m == g_root) ui_menu_bar__set_root(NULL);
    ui_menu_bar__free(m);
}

UIMenuBarItem* ui_menu_bar_item_create(const char* label,
                                const char* shortcut,
                                uint32_t id) {
    return ui_menu_bar_item__alloc_leaf(label, shortcut, id);
}

UIMenuBarItem* ui_menu_bar_separator(void) {
    return ui_menu_bar_item__alloc_separator();
}

void ui_menu_bar_item_destroy(UIMenuBarItem* it) {
    ui_menu_bar_item__free(it);
}

void ui_menu_bar_append_item(UIMenuBar* parent, UIMenuBarItem* item) {
    if (!parent || !item) return;
    // Attach the NSMenuItem view to the parent's NSMenu BEFORE the C
    // side takes ownership — if the hook is missing the leaf is just
    // tracked in the C array (the host can still inspect it).
    if (ui_menu_bar__hook_attach_nsitem) ui_menu_bar__hook_attach_nsitem(item, parent);
    ui_menu_bar__append_item(parent, item);
}

void ui_menu_bar_append_submenu(UIMenuBar* parent, UIMenuBar* sub) {
    if (!parent || !sub) return;
    if (ui_menu_bar__hook_attach_nssubmenu) ui_menu_bar__hook_attach_nssubmenu(parent, sub);
    ui_menu_bar__append_submenu(parent, sub);
}

void ui_menu_bar_append_separator(UIMenuBar* parent) {
    if (!parent) return;
    UIMenuBarItem* it = ui_menu_bar_separator();
    if (!it) return;
    // Single-step: build a separator leaf and attach it in one shot.
    // (The previous attempt recursed into ui_menu_bar_item_create here by
    // mistake — that path is wrong on two counts: it takes a non-NULL
    // label and a non-zero id, neither of which applies to a separator.)
    ui_menu_bar_append_item(parent, it);
}

void ui_menu_bar_set(UIMenuBar* root) {
    ui_menu_bar__set_root(root);
}

// ── Item-callback forwarding ─────────────────────────────────────────────
// The callback and its user pointer live in the .mm (it owns the
// NSMenuItem → UIMenuBarItem bridge via associated objects). On non-Apple
// the .mm is absent, so the weak forwarder is a no-op.
__attribute__((weak)) void ui_menu_bar__hook_set_callback(
    ui_menu_bar_item_callback_t cb, void* user);

void ui_menu_bar_set_item_callback(ui_menu_bar_item_callback_t cb, void* user) {
    if (ui_menu_bar__hook_set_callback) ui_menu_bar__hook_set_callback(cb, user);
}
