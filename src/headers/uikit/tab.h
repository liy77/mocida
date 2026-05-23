#ifndef UIKIT_TAB_H
#define UIKIT_TAB_H

#include <uikit/widget.h>
#include <uikit/children.h>
#include <uikit/color.h>

#define UI_WIDGET_TABVIEW "@uikit/tabview"

typedef struct UITabView UITabView;
typedef void (*UITabChangedCallback)(UITabView* tv, int activeIndex, void* userdata);

/**
 * A simple tab view: a row of clickable tab headers + a panel below
 * that shows the active tab's content. Each tab owns its own UIWidget
 * (the content), which lives in `panels`.
 */
struct UITabView {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_TABVIEW). */

    int activeIndex;           /**< Index of the currently visible tab. */
    float tabHeight;           /**< Height of the tab-header strip (pixels). */
    UIColor tabBg;             /**< Inactive tab background. */
    UIColor tabBgActive;       /**< Active tab background. */
    UIColor tabText;           /**< Inactive tab label color. */
    UIColor tabTextActive;     /**< Active tab label color. */
    UIColor panelBg;           /**< Background fill of the content panel below the headers. */
    float radius;              /**< Corner radius shared by tabs / panel. */

    char**       titles;       /**< Heap-owned array of tab titles, length tabCount. */
    UIChildren*  panels;       /**< Owned content widgets, one per tab. */
    int          tabCount;     /**< Number of tabs currently registered. */
    int          tabCapacity;  /**< Allocated capacity of `titles` / `panels`. */

    char*  fontFamily;         /**< Heap-owned font path for tab labels. */
    float  fontSize;           /**< Tab label font size in points. */

    UITabChangedCallback onTabChanged; /**< Fires when activeIndex changes. */
    void* userdata;                    /**< Opaque pointer forwarded to onTabChanged. */
};

UITabView* UITabView_Create(float tabHeight);
UITabView* UITabView_SetFont(UITabView* tv, char* family, float size);

/**
 * Adds a tab with the given title and content widget. Returns the
 * index of the new tab, or -1 on failure. Content widget is owned by
 * the tab view and destroyed with it.
 */
int UITabView_AddTab(UITabView* tv, const char* title, UIWidget* content);

UITabView* UITabView_SetActive(UITabView* tv, int index);
UITabView* UITabView_OnChange (UITabView* tv, UITabChangedCallback cb, void* userdata);
void       UITabView_Destroy  (UITabView* tv);

/** Forwarded by app.c on every left-button click. */
void UITabView_DispatchMouseDown(UIChildren* children, float x, float y, int button);

#endif // UIKIT_TAB_H
