#ifndef UIKIT_WIDGET_H
#define UIKIT_WIDGET_H

#define UI_WIDGET_RECTANGLE "@uikit/rectangle"
#define UI_WIDGET_TEXT "@uikit/text"
#define UI_WIDGET_WIDGET "@uikit/widget" // Base widget type

// Define a typedef for a pointer to UIWidgetData
typedef void* UIWidgetData;

/**
 * Base structure for UI widgets.
 * This structure contains common properties for all widgets.
 */
typedef struct {
    const char* __widget_type; // Type of the widget (e.g., "UIRectangle", "UIText", etc.)
} UIWidgetBase;

/**
 * UIWidget structure representing a UI widget.
 * It contains properties for position, visibility, and a pointer to the actual widget data.
 */
typedef struct {
    float x;
    float y;
    int z;
    int visible;

    UIWidgetData data; // Pointer to the actual widget data (e.g., UIRectangle, UIText, etc.)
} UIWidget;

/**
 * Creates a UIWidget object with the specified data.
 * @param data Pointer to the widget data (e.g., UIRectangle, UIText, etc.).
 * @return A pointer to the created UIWidget object.
 */
UIWidget* UIWidget_Create(UIWidgetData data);

/**
 * Sets the position of the UIWidget object.
 * @param widget Pointer to the UIWidget object.
 * @param x X-coordinate of the widget.
 * @param y Y-coordinate of the widget.
 * @return Pointer to the updated UIWidget object.
 */
UIWidget *UIWidget_SetPosition(UIWidget *widget, int x, int y);

/**
 * Sets the z-index of the UIWidget object.
 * @param widget Pointer to the UIWidget object.
 * @param z Z-index value.
 * @return Pointer to the updated UIWidget object.
 */
UIWidget *UIWidget_SetZIndex(UIWidget* widget, int z);

/**
 * Sets the visibility of the UIWidget object.
 * @param widget Pointer to the UIWidget object.
 * @param visible Visibility flag (1 for visible, 0 for hidden).
 * @return Pointer to the updated UIWidget object.
 */
UIWidget *UIWidget_SetVisible(UIWidget *widget, int visible);

/**
 * Destroys the UIWidget object and frees its memory.
 * @param widget Pointer to the UIWidget object to be destroyed.
 * @return None.
 */
void UIWidget_Destroy(UIWidget* widget);

#endif // UIKIT_WIDGET_H
