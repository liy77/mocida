#ifndef UIKIT_CHILDREN_H
#define UIKIT_CHILDREN_H

#include <uikit/rect.h>
#include <uikit/widget.h>

/**
 * UIChildren structure representing a collection of child widgets.
 * It contains properties for capacity, count, and an array of child widgets.
 */
typedef struct {
    int capacity;
    int count;
    UIWidget** children;
} UIChildren;

/**
 * Creates a UIChildren object with the specified capacity.
 * @param capacity Maximum number of child widgets.
 * @return A pointer to the created UIChildren object.
 */
UIChildren* UIChildren_Create(int capacity);

/**
 * Adds a child widget to the UIChildren object.
 * @param children Pointer to the UIChildren object.
 * @param child Pointer to the child widget to be added.
 * @return 0 on success, -1 on failure.
 */
int UIChildren_Add(UIChildren* children, UIWidget* child);

/**
 * Removes a child widget from the UIChildren object.
 * @param children Pointer to the UIChildren object.
 * @param child Pointer to the child widget to be removed.
 * @return 0 on success, -1 on failure.
 */
int UIChildren_Remove(UIChildren* children, UIWidget* child);

/**
 * Destroys the UIChildren object and frees its resources.
 * @param children Pointer to the UIChildren object to be destroyed.
 * @return None.
 */
void UIChildren_Destroy(UIChildren* children);
/**
 * Clears all child widgets from the UIChildren object.
 * @param children Pointer to the UIChildren object to be cleared.
 * @return None.
 */
void UIChildren_Clear(UIChildren* children);

/**
 * Sorts the child widgets in the UIChildren object by their z-index.
 * @param children Pointer to the UIChildren object to be sorted.
 * @return None.
 */
void UIChildren_SortByZ(UIChildren* children);

#endif // UIKIT_CHILDREN_H