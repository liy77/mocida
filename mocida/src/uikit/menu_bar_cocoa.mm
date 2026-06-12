// menu_bar_cocoa.mm — macOS implementation of the menu-bar Cocoa backend.
//
// Pairs with menu_bar.c, which owns the UIMenuBar / UIMenuBarItem struct layout
// and the cross-platform child-tracking logic. The .c also defines the
// public C entry points (ui_menu_bar_create, ui_menu_bar_set, ...); on Apple
// those work as-is because the C side just calls the weak hooks below
// to keep the NSMenu tree in lock-step with the C-side tree.
//
// This file provides:
//
//   1. The weak "hook" symbols menu_bar.c calls to keep the NSMenu
//      tree in sync with the C-side tree. On non-Apple builds the
//      hooks are unresolved; the C side treats that as a no-op.
//   2. The bridge from NSMenuItem activations back to the host's
//      ui_menu_bar_item_callback_t (via associated objects on each item).
//
// We deliberately do NOT redefine the public C API here — that would
// produce a duplicate symbol on Apple. The .c owns the public surface;
// this .mm only adds the Cocoa side-effects.

#import <TargetConditionals.h>
#if defined(__APPLE__) && TARGET_OS_OSX

#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "menu_bar_internal.h"

// ── Callback state ───────────────────────────────────────────────────────
// Set by ui_menu_bar_set_item_callback (the C side calls
// ui_menu_bar__hook_set_callback, which is implemented here). The action
// target (see below) looks these up when an item is activated.
static ui_menu_bar_item_callback_t g_callback     = NULL;
static void*                   g_callback_user = NULL;

// ── NSMenuItem → C UIMenuBarItem bridge ─────────────────────────────────────
// Cocoa delivers menu activations as `-target/-action:`. We use a class
// target (UIMenuActionHandler) so the same handler class can service any
// number of items; the actual C pointer is stashed on each item via
// objc_setAssociatedObject and recovered at fire time.
static const void* kUIMenuItemPtrKey = "UIMenuItemPtr";

@interface UIMenuActionHandler : NSObject
+ (void)menuItemFired:(id)sender;
@end

@implementation UIMenuActionHandler
+ (void)menuItemFired:(id)sender {
    if (![sender isKindOfClass:[NSMenuItem class]]) return;
    NSMenuItem* item = (NSMenuItem*)sender;
    UIMenuBarItem* cItem = (UIMenuBarItem*)objc_getAssociatedObject(
        item, kUIMenuItemPtrKey);
    if (!cItem || !g_callback) return;
    g_callback(cItem->id, g_callback_user);
}
@end

// ── Weak hooks (called by menu_bar.c) ────────────────────────────────────

void ui_menu_bar__hook_attach_nsmenu(UIMenuBar* m) {
    if (!m) return;
    NSString* title = m->title
        ? [NSString stringWithUTF8String:m->title]
        : @"";
    NSMenu* ns = [[NSMenu alloc] initWithTitle:title];
    // Auto-enable so individual items only need to provide a target +
    // action; otherwise NSMenu disables every item until
    // validateMenuItem: returns YES.
    ns.autoenablesItems = NO;
    // Strong assignment into the __strong field — ARC keeps the menu
    // alive for the C struct's lifetime. The C field is typed as
    // `struct objc_object*` (the underlying type behind `id`) so the
    // assignment goes through a __bridge cast to silence the C++ type
    // checker (NSMenu → objc_object is an ObjC type, not a C++ one).
    m->nsMenu = (__bridge struct objc_object*)ns;
}

void ui_menu_bar__hook_attach_nsitem(UIMenuBarItem* it, UIMenuBar* parent) {
    if (!it || !parent || !parent->nsMenu) return;
    NSMenuItem* nsItem = nil;
    if (it->is_separator) {
        nsItem = [NSMenuItem separatorItem];
    } else {
        NSString* title = it->label
            ? [NSString stringWithUTF8String:it->label]
            : @"";
        nsItem = [[NSMenuItem alloc] initWithTitle:title
                                            action:@selector(menuItemFired:)
                                     keyEquivalent:@""];
        [nsItem setTarget:[UIMenuActionHandler class]];
        // Stash the C pointer for the action handler. RETAIN keeps it
        // alive for the lifetime of the NSMenuItem (the NSMenu owns its
        // items until the menu is freed, so the pointer is valid for as
        // long as the host keeps the C tree attached).
        objc_setAssociatedObject(nsItem, (void*)kUIMenuItemPtrKey, (id)it,
                                 OBJC_ASSOCIATION_RETAIN);

        // Shortcut parsing: SDL-style "CmdOrCtrl+N" / "Alt+Shift+Z" / etc.
        // The trailing chunk is the key; everything before the final '+'
        // is a modifier name. We don't try to validate that the
        // key-character maps to a real NSEvent code — NSMenuItem will
        // just ignore malformed strings.
        if (it->shortcut) {
            NSString* s = [NSString stringWithUTF8String:it->shortcut];
            NSArray<NSString*>* parts = [s componentsSeparatedByString:@"+"];
            NSEventModifierFlags mask = 0;
            // All but the last part are modifiers. The last part is the
            // key character itself.
            for (NSUInteger i = 0; i + 1 < parts.count; i++) {
                NSString* mod = [parts[i]
                    stringByTrimmingCharactersInSet:
                        [NSCharacterSet whitespaceCharacterSet]];
                if ([mod isEqualToString:@"CmdOrCtrl"] ||
                    [mod isEqualToString:@"Cmd"]       ||
                    [mod isEqualToString:@"Ctrl"]) {
                    mask |= NSEventModifierFlagCommand;
                } else if ([mod isEqualToString:@"Shift"]) {
                    mask |= NSEventModifierFlagShift;
                } else if ([mod isEqualToString:@"Alt"] ||
                           [mod isEqualToString:@"Option"]) {
                    mask |= NSEventModifierFlagOption;
                }
                // Other modifiers (Meta / Win) are uncommon on macOS;
                // ignore them rather than guessing an NSEvent flag.
            }
            NSString* key = [[parts lastObject]
                stringByTrimmingCharactersInSet:
                    [NSCharacterSet whitespaceCharacterSet]];
            if (key.length > 0) {
                [nsItem setKeyEquivalent:key];
                [nsItem setKeyEquivalentModifierMask:mask];
            }
        }
    }
    it->nsItem = (__bridge struct objc_object*)nsItem;
    [(NSMenu*)(__bridge struct objc_object*)parent->nsMenu addItem:nsItem];
}

void ui_menu_bar__hook_attach_nssubmenu(UIMenuBar* parent, UIMenuBar* sub) {
    if (!parent || !sub) return;
    if (!parent->nsMenu || !sub->nsMenu) return;
    // An NSMenu is attached to its parent via an NSMenuItem holder.
    // The holder's title matches the submenu's title; AppKit replaces
    // the title with the NSMenu's own title once it's wired up.
    NSString* holderTitle = sub->title
        ? [NSString stringWithUTF8String:sub->title]
        : @"";
    NSMenuItem* holder = [[NSMenuItem alloc]
        initWithTitle:holderTitle
               action:nil
        keyEquivalent:@""];
    [holder setSubmenu:(NSMenu*)(__bridge struct objc_object*)sub->nsMenu];
    [(NSMenu*)(__bridge struct objc_object*)parent->nsMenu addItem:holder];
}

void ui_menu_bar__hook_install_root(UIMenuBar* root) {
    NSMenu* mainMenu = nil;
    if (root && root->nsMenu) mainMenu = (NSMenu*)(__bridge struct objc_object*)root->nsMenu;
    // [NSApp setMainMenu:] is a no-op if NSApp hasn't been initialised
    // yet (early-startup path); callers that wire the menu bar before
    // NSApp is up should re-install once the run loop is alive.
    [NSApp setMainMenu:mainMenu];
}

void ui_menu_bar__hook_uninstall_root(void) {
    [NSApp setMainMenu:nil];
}

void ui_menu_bar__hook_set_callback(ui_menu_bar_item_callback_t cb, void* user) {
    g_callback      = cb;
    g_callback_user = user;
}

#endif // __APPLE__ && TARGET_OS_OSX
