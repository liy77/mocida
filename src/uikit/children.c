#include <uikit/children.h>
#include <uikit/debug.h>
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
            UI_WARN(UI_CAT_WIDGET, "child with ID '%s' already exists", child->id);
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
    UI_WARN(UI_CAT_WIDGET, "child with ID '%s' not found", id);
    return NULL;
}

int UIChildren_Remove(UIChildren* children, UIWidget* child) {
    if (children == NULL || child == NULL) {
        return 0;
    }

    for (int i = 0; i < children->count; i++) {
        if (children->children[i] == child) {
            // Properly tear down the widget (was leaking widget->id /
            // ->width / ->height / ->alignment / ->data).
            UIWidget_Destroy(children->children[i]);
            // Compact the array so subsequent iteration / SortByZ don't
            // run into NULL holes.
            for (int j = i; j < children->count - 1; j++) {
                children->children[j] = children->children[j + 1];
            }
            children->children[children->count - 1] = NULL;
            children->count--;
            return 1;
        }
    }

    UI_WARN(UI_CAT_WIDGET, "child not found in array");
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
    if (children == NULL) return;

    // Iterate only up to `count` (the slots actually populated) and
    // use the proper widget destructor instead of free() - the old
    // code leaked widget->id, ->width, ->height, ->alignment, ->data.
    for (int i = 0; i < children->count; i++) {
        if (children->children[i] != NULL) {
            UIWidget_Destroy(children->children[i]);
            children->children[i] = NULL;
        }
    }
    children->count = 0;
}

void UIChildren_Relayout(UIChildren* children) {
    if (children == NULL) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (w == NULL) continue;
        if (w->alignment != NULL && w->width != NULL && w->height != NULL) {
            UIAlignment_Align(w);
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