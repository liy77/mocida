#ifndef UIKIT_CHILDREN_H
#define UIKIT_CHILDREN_H

#include <uikit/rect.h>
#include <uikit/widget.h>

typedef struct {
    int capacity;
    int count;
    UIWidget** children;
} UIChildren;

UIChildren* UIChildren_Create(int capacity);
void UIChildren_Add(UIChildren* children, UIWidget* child);
void UIChildren_Remove(UIChildren* children, UIWidget* child);
void UIChildren_Destroy(UIChildren* children);
void UIChildren_Clear(UIChildren* children);

#endif // UIKIT_CHILDREN_H