#include <uikit/children.h>
#include <stdio.h>

UIChildren* UIChildren_Create(int capacity) {
    UIChildren* children = (UIChildren*)malloc(sizeof(UIChildren));
    if (children == NULL) {
        return NULL; // Memory allocation failed
    }

    children->capacity = capacity;
    children->count = 0; // Initialize count to 0
    children->children = (UIWidget**)calloc(capacity, sizeof(UIWidget*));
    if (children->children == NULL) {
        free(children); // Free previously allocated memory
        return NULL; // Memory allocation failed
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
    // Check if children with that id already exists
    for (int i = 0; i < children->count; i++) {
        if (children->children[i] != NULL && 
            children->children[i]->id != NULL && 
            child->id != NULL && 
            strcmp(children->children[i]->id, child->id) == 0) {
            fprintf(stderr, "Child with ID '%s' already exists\n", child->id);
            return 0;
        }
    }

    children->children[children->count++] = child;
    return 1;
}

UIWidget* UIChildren_GetById(UIChildren* children, const char* id) {
    for (int i = 0; i < children->count; i++) {
        if (children->children[i] != NULL && 
            children->children[i]->id != NULL && 
            strcmp(children->children[i]->id, id) == 0) {
            return children->children[i]; 
        }
    }

    // Child not found in the array
    fprintf(stderr, "Child with ID '%s' not found in the children\n", id);
    return NULL;
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
    if (!children) return;
    
    for (int i = 0; i < children->count; i++) {
        if (children->children[i]) {
            UIWidget_Destroy(children->children[i]);
        }
    }
    
    free(children->children);
    free(children);
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