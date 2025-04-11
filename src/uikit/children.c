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
        return NULL; // Memory allocation failed
    }

    for (int i = 0; i < capacity; i++) {
        children->children[i] = NULL;
    }

    return children;
}

int UIChildren_Add(UIChildren* children, UIWidget* child) {
    if (children == NULL || child == NULL || children->count >= children->capacity) {
        return 0;
    }

    // Fix negative z-index
    if (child->z < 0) {
        child->z = 0;
    }

    children->children[children->count++] = child;
    return 1;
}

int UIChildren_Remove(UIChildren* children, UIWidget* child) {
    if (children == NULL || child == NULL) {
        return 0; // Invalid arguments
    }

    for (int i = 0; i < children->capacity; i++) {
        if (children->children[i] == child) {
            free(children->children[i]); 
            children->children[i] = NULL; 
            children->count--; 
            return 1; 
        }
    }

    // Child not found in the array
    fprintf(stderr, "Child not found in the array\n");
    return 0;
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

    return;
}

void UIChildren_Clear(UIChildren* children) {
    if (children == NULL) {
        return; // Invalid argument
    }

    for (int i = 0; i < children->capacity; i++) {
        if (children->children[i] != NULL) {
            free(children->children[i]); 
            children->children[i] = NULL;
        }
    }
}

void UIChildren_SortByZ(UIChildren* children) {
    for (int i = 1; i < children->count; i++) {
        UIWidget* key = children->children[i];
        int j = i - 1;
        while (j >= 0 && children->children[j]->z > key->z) {
            children->children[j + 1] = children->children[j];
            j--;
        }
        children->children[j + 1] = key;
    }
}