/*
 * Recursive widget-tree walker. Pulled into its own translation unit so
 * the overlay, the crash handler, and any future inspector all share
 * the same definition of "what counts as a child".
 *
 * Walking cost is bounded: each node is visited once, recursion depth
 * is the actual tree depth. The visitor decides whether to descend, so
 * users can prune (e.g. only top 2 levels). Zero cost when no one calls
 * the walker — it's just a function.
 */

#include <string.h>

#include <uikit/walker.h>
#include <uikit/widget.h>
#include <uikit/children.h>

#include <uikit/stack.h>
#include <uikit/container.h>
#include <uikit/tab.h>
#include <uikit/dialog.h>

/* Forward decls — recursion goes through these. */
static int walk_node(UIWidget* w, int depth, UIWidgetVisitor visit, void* user);
static int walk_children(UIChildren* c, int depth, UIWidgetVisitor visit, void* user);
static int walk_widget_children(UIWidget* w, int depth, UIWidgetVisitor visit, void* user);

/* Hands every direct child of `w` (if any) to the walker. Returns 1 if
 * the walk should stop. */
static int walk_widget_children(UIWidget* w, int depth, UIWidgetVisitor visit, void* user) {
    if (!w || !w->data) return 0;
    UIWidgetBase* base = (UIWidgetBase*)w->data;
    if (!base->__widget_type) return 0;
    const char* t = base->__widget_type;

    if (!strcmp(t, UI_WIDGET_STACK)) {
        UIStack* s = (UIStack*)base;
        return walk_children(s->items, depth + 1, visit, user);
    }
    if (!strcmp(t, UI_WIDGET_GRID)) {
        UIGrid* g = (UIGrid*)base;
        return walk_children(g->items, depth + 1, visit, user);
    }
    if (!strcmp(t, UI_WIDGET_SCROLL)) {
        UIScroll* s = (UIScroll*)base;
        if (s->content) return walk_node(s->content, depth + 1, visit, user);
        return 0;
    }
    if (!strcmp(t, UI_WIDGET_TABVIEW)) {
        UITabView* tv = (UITabView*)base;
        return walk_children(tv->panels, depth + 1, visit, user);
    }
    if (!strcmp(t, UI_WIDGET_DIALOG)) {
        UIDialog* d = (UIDialog*)base;
        return walk_children(d->content, depth + 1, visit, user);
    }
    /* Leaf-equivalent: tooltip targets a borrowed widget, menus/dropdowns
     * hold label strings not children, image/text/button etc. don't
     * compose. */
    return 0;
}

static int walk_node(UIWidget* w, int depth, UIWidgetVisitor visit, void* user) {
    if (!w || !visit) return 0;
    UIWalkResult r = visit(w, depth, user);
    if (r == UI_WALK_STOP) return 1;
    if (r == UI_WALK_SKIP_CHILDREN) return 0;
    return walk_widget_children(w, depth, visit, user);
}

static int walk_children(UIChildren* c, int depth, UIWidgetVisitor visit, void* user) {
    if (!c) return 0;
    for (int i = 0; i < c->count; ++i) {
        if (walk_node(c->children[i], depth, visit, user)) return 1;
    }
    return 0;
}

int UIWidget_WalkTree(UIWidget* root, UIWidgetVisitor visit, void* user) {
    return walk_node(root, 0, visit, user);
}

int UIChildren_WalkTree(UIChildren* children, int depth,
                        UIWidgetVisitor visit, void* user) {
    return walk_children(children, depth, visit, user);
}
