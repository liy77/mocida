#ifndef UIKIT_WIDGET_H
#define UIKIT_WIDGET_H

#define UI_WIDGET_RECTANGLE "@uikit/rectangle"
#define UI_WIDGET_TEXT "@uikit/text"
#define UI_WIDGET_WIDGET "@uikit/widget" // Base widget type

#define UI_DYNAMIC_SIZE -1.0f // Dynamic size indicator

// Define a typedef for a pointer to UIWidgetData
typedef void* UIWidgetData;

#include <uikit/alignment.h>


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
    float* width;
    float* height;
    float opacity;
    UIAlignment* alignment; // Alignment properties
    UIWidgetData* parent; // Pointer to the parent widget (if any)

    UIWidgetData data; // Pointer to the actual widget data (e.g., UIRectangle, UIText, etc.)
} UIWidget;

/**
 * An alias for `UIWidget_Create` function.
 * @param data Pointer to the widget data (e.g., UIRectangle, UIText, etc.).
 * @return A pointer to the created UIWidget object.
 */
UIWidget* widgc(UIWidgetData data);

/**
 * An alias for `UIWidget_Create` function with size parameters.
 * @param data Pointer to the widget data (e.g., UIRectangle, UIText, etc.).
 * @param width Pointer to the width value (float).
 * @param height Pointer to the height value (float).
 * @return A pointer to the created UIWidget object.
 */
UIWidget* widgcs(UIWidgetData data, float width, float height);

/**
 * Creates a UIWidget object with the specified data.
 * @param data Pointer to the widget data (e.g., UIRectangle, UIText, etc.).
 * @return A pointer to the created UIWidget object.
 */
UIWidget* UIWidget_Create(UIWidgetData data);

/**
 * Sets the size of the UIWidget object.
 * @param widget Pointer to the UIWidget object.
 * @param width Pointer to the width value (float).
 * @param height Pointer to the height value (float).
 * @return Pointer to the updated UIWidget object.
 */
UIWidget* UIWidget_SetSize(UIWidget* widget, float width, float height);

/**
 * Sets the position of the UIWidget object.
 * @param widget Pointer to the UIWidget object.
 * @param x X-coordinate of the widget.
 * @param y Y-coordinate of the widget.
 * @return Pointer to the updated UIWidget object.
 */
UIWidget *UIWidget_SetPosition(UIWidget *widget, float x, float y);

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

/**
 * Sets the alignment of a widget within its parent.
 * @param widget Pointer to the UIWidget object.
 * @param alignment UIAlignment object specifying the alignment.
 * @return None.
 */
void UIWidget_SetAlignment(UIWidget* widget, UIAlignment alignment);

/**
 * Sets the parent of a UIWidget object.
 * @param widget Pointer to the UIWidget object.
 * @param parent Pointer to the parent UIWidget object.
 * @return Pointer to the updated UIWidget object.
 */
UIWidget* UIWidget_SetParent(UIWidget* widget, UIWidget* parent);

/**
 * Gets the parent of a UIWidget object.
 * @param widget Pointer to the UIWidget object.
 * @return Pointer to the parent UIWidget object.
 */
UIWidget* UIWidget_GetParent(UIWidget* widget);

#endif // UIKIT_WIDGET_H
