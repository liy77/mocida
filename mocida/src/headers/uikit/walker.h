#ifndef UIKIT_WALKER_H
#define UIKIT_WALKER_H

/**
 * Generic recursive widget-tree walker.
 *
 * Used by the debug overlay (to draw bounds for nested widgets) and by
 * the crash handler (to dump the whole tree on fault). Plain library
 * code can also reuse it — e.g. "find every UIButton with id starting
 * with 'submit_'".
 *
 * The walker is shallow-by-default for performance and predictability:
 * each containing widget type that owns children advertises them
 * through walker.c's internal switch on __widget_type. Today the
 * supported parents are UIStack, UIGrid, UIScroll, UITabView and
 * UIDialog. Adding a new parent type means appending a `case` in
 * walker.c — keeping the knowledge in one place avoids per-widget
 * include cycles.
 *
 * Visitor contract
 * ----------------
 * The visitor is called once per widget with its depth in the tree.
 * Return UI_WALK_CONTINUE to descend, SKIP_CHILDREN to visit the
 * widget but not its subtree, or STOP to terminate the whole walk.
 */

#include <uikit/widget.h>
#include <uikit/children.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_WALK_CONTINUE      = 0,
    UI_WALK_SKIP_CHILDREN = 1,
    UI_WALK_STOP          = 2
} UIWalkResult;

typedef UIWalkResult (*UIWidgetVisitor)(UIWidget* widget, int depth, void* user);

/** Walks `root` and all nested children. Returns 1 if the walk stopped
 *  early via UI_WALK_STOP, 0 if it ran to completion. */
int UIWidget_WalkTree(UIWidget* root, UIWidgetVisitor visit, void* user);

/** Same, but starting from a UIChildren* collection (e.g.
 *  window->children). */
int UIChildren_WalkTree(UIChildren* children, int depth,
                        UIWidgetVisitor visit, void* user);

#ifdef __cplusplus
}
#endif

#endif /* UIKIT_WALKER_H */
