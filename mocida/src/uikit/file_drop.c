#include <uikit/file_drop.h>
#include <stdlib.h>
#include <string.h>

UIFileDrop* UIFileDrop_Create(const char* prompt) {
    UIFileDrop* fd = (UIFileDrop*)calloc(1, sizeof(UIFileDrop));
    if (!fd) return NULL;
    fd->__widget_type = UI_WIDGET_FILE_DROP;
    fd->prompt    = prompt ? _strdup(prompt) : _strdup("Drop a file here");
    fd->fontSize  = 16.0f;

    fd->bgColor           = (UIColor){ 248, 250, 252, 1.0f };
    fd->activeBgColor     = (UIColor){ 219, 234, 254, 1.0f };
    fd->borderColor       = (UIColor){ 148, 163, 184, 1.0f };
    fd->activeBorderColor = (UIColor){ 59, 130, 246, 1.0f };
    fd->textColor         = (UIColor){ 71, 85, 105, 1.0f };
    fd->borderWidth       = 2.0f;
    fd->radius            = 10.0f;
    return fd;
}

UIFileDrop* UIFileDrop_SetFontFamily(UIFileDrop* fd, char* family) {
    if (!fd) return fd;
    free(fd->fontFamily);
    fd->fontFamily = family ? _strdup(family) : NULL;
    return fd;
}

UIFileDrop* UIFileDrop_SetFontSize(UIFileDrop* fd, float size) {
    if (!fd) return fd;
    fd->fontSize = size > 0.0f ? size : 16.0f;
    return fd;
}

UIFileDrop* UIFileDrop_SetColors(UIFileDrop* fd, UIColor bg, UIColor activeBg,
                                 UIColor border, UIColor activeBorder, UIColor text) {
    if (!fd) return fd;
    fd->bgColor           = bg;
    fd->activeBgColor     = activeBg;
    fd->borderColor       = border;
    fd->activeBorderColor = activeBorder;
    fd->textColor         = text;
    return fd;
}

UIFileDrop* UIFileDrop_OnDrop(UIFileDrop* fd, UIFileDropCallback cb, void* userdata) {
    if (!fd) return fd;
    fd->onDrop   = cb;
    fd->userdata = userdata;
    return fd;
}

void UIFileDrop_Destroy(UIFileDrop* fd) {
    if (!fd) return;
    free(fd->prompt);
    free(fd->fontFamily);
    free(fd);
}

// ---------------------------------------------------------------------
// Dispatchers
// ---------------------------------------------------------------------

static int InsideRect(float x, float y, float rx, float ry, float rw, float rh) {
    return (x >= rx && x < rx + rw && y >= ry && y < ry + rh);
}

static UIFileDrop* AsFileDrop(UIWidget* w) {
    if (!w || !w->data) return NULL;
    UIWidgetBase* b = (UIWidgetBase*)w->data;
    return (strcmp(b->__widget_type, UI_WIDGET_FILE_DROP) == 0) ? (UIFileDrop*)b : NULL;
}

void UIFileDrop_DispatchDragPosition(UIChildren* children, float x, float y) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        UIFileDrop* fd = AsFileDrop(w);
        if (!fd || !w->width || !w->height) continue;
        fd->dragOver = InsideRect(x, y, w->x, w->y, *w->width, *w->height);
    }
}

void UIFileDrop_DispatchDragEnd(UIChildren* children) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UIFileDrop* fd = AsFileDrop(children->children[i]);
        if (fd) fd->dragOver = 0;
    }
}

void UIFileDrop_DispatchDropFile(UIChildren* children, float x, float y, const char* path) {
    if (!children || !path) return;
    // Walk topmost first so overlapping drop zones use the highest z.
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w = children->children[i];
        UIFileDrop* fd = AsFileDrop(w);
        if (!fd || !w->width || !w->height) continue;
        fd->dragOver = 0;
        if (InsideRect(x, y, w->x, w->y, *w->width, *w->height)) {
            if (fd->onDrop) fd->onDrop(fd, path, fd->userdata);
            return;
        }
    }
    // Fallback: when no widget contains the drop point, deliver to the
    // FIRST drop zone so a window-wide listener can act as a catch-all.
    for (int i = 0; i < children->count; i++) {
        UIFileDrop* fd = AsFileDrop(children->children[i]);
        if (fd && fd->onDrop) {
            fd->onDrop(fd, path, fd->userdata);
            return;
        }
    }
}
