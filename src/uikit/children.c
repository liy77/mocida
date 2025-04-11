#include <uikit/children.h>
#include <stdio.h>

UIChildren* UIChildren_Create(int capacity) {
    UIChildren* children = (UIChildren*)malloc(sizeof(UIChildren));
    if (children == NULL) {
        return NULL; // Memory allocation failed
    }

    children->capacity = capacity;
    children->count = 0; // Initialize count to 0
    children->children = (UIWidget**)malloc(sizeof(UIWidget*) * capacity);
    if (children->children == NULL) {
        free(children);
        return NULL; // Memory allocation failed
    }

    for (int i = 0; i < capacity; i++) {
        children->children[i] = NULL;
    }

    return children;
}

void UIChildren_Add(UIChildren* children, UIWidget* child) {
    if (children == NULL || child == NULL) {
        return; // Invalid arguments
    }

    for (int i = 0; i < children->capacity; i++) {
        if (children->children[i] == NULL) {
            if (child->z < 0) {
                child->z = 0; // Set z-index to 0 if it's negative
            } else if (child->z >= children->capacity) {
                child->z = children->capacity - 1; // Clamp z-index to capacity - 1
            }

            if (!(child->z > 0)) {
                child->z = i;
            } else {
                for (int j = children->capacity - 1; j > child->z; j--) {
                    children->children[j] = children->children[j - 1];
                }
            }
            
            children->children[child->z] = child;
            children->count++; // Increment the count of children
            return; // Child added successfully
        }
    }

    // No space available to add the child
    fprintf(stderr, "No space available to add the child\n");
}

void UIChildren_Remove(UIChildren* children, UIWidget* child) {
    if (children == NULL || child == NULL) {
        return; // Invalid arguments
    }

    for (int i = 0; i < children->capacity; i++) {
        if (children->children[i] == child) {
            free(children->children[i]); // Free the memory of the removed child
            children->children[i] = NULL; // Set the pointer to NULL
            children->count--; // Decrement the count of children
            return; // Child removed successfully
        }
    }

    // Child not found in the array
    fprintf(stderr, "Child not found in the array\n");
    return;
}

void UIChildren_Destroy(UIChildren* children) {
    if (children == NULL) {
        return; // Invalid argument
    }

    for (int i = 0; i < children->capacity; i++) {
        if (children->children[i] != NULL) {
            free(children->children[i]); // Free each child
        }
    }

    free(children->children); // Free the array of children
    free(children); // Free the UIChildren object

    return; // Indicate destruction
}

void UIChildren_Clear(UIChildren* children) {
    if (children == NULL) {
        return; // Invalid argument
    }

    for (int i = 0; i < children->capacity; i++) {
        if (children->children[i] != NULL) {
            free(children->children[i]); // Free each child
            children->children[i] = NULL; // Set the pointer to NULL
        }
    }
}