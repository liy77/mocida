#include <uikit/alignment.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

UIAlignment UIAlignment_Create(int vertical, int horizontal) {
    UIAlignment alignment;
    alignment.vertical = vertical;
    alignment.horizontal = horizontal;
    return alignment;
}

UIAlignment UIAlignment_VerticalCenter() {
    return UIAlignment_Create(UI_ALIGN_V_CENTER, 0);
}

UIAlignment UIAlignment_VerticalTop() {
    return UIAlignment_Create(UI_ALIGN_V_TOP, 0);
}

UIAlignment UIAlignment_VerticalBottom() {
    return UIAlignment_Create(UI_ALIGN_V_BOTTOM, 0);
}

UIAlignment UIAlignment_HorizontalCenter() {
    return UIAlignment_Create(0, UI_ALIGN_H_CENTER);
}

UIAlignment UIAlignment_HorizontalLeft() {
    return UIAlignment_Create(0, UI_ALIGN_H_LEFT);
}

UIAlignment UIAlignment_HorizontalRight() {
    return UIAlignment_Create(0, UI_ALIGN_H_RIGHT);
}

void UIAlignment_SetWidgetAlignment(UIWidget* widget, UIAlignment alignment) {
    if (widget == NULL) {
        return;
    }

    UIWidgetBase* base = (UIWidgetBase*)widget->data;
    if (base == NULL) {
        fprintf(stderr, "Widget data is NULL\n");
        return; 
    }


    const char* widgetType = base->__widget_type;
    if (widgetType == NULL) {
        fprintf(stderr, "Widget type is NULL\n");
        return; 
    }
    
    if (strcmp(widgetType, UI_WIDGET_RECTANGLE) == 0) {
        UIRectangle* rect = (UIRectangle*)widget->data;
        if (rect == NULL) {
            fprintf(stderr, "Rectangle data is NULL\n");
            return; 
        }

        // TODO: Implement alignment logic for UIRectangle
    } else if (strcmp(widgetType, UI_WIDGET_TEXT) == 0) {
        UIText* text = (UIText*)widget->data;
        if (text == NULL) {
            fprintf(stderr, "Text data is NULL\n");
            return; 
        }

        // TODO: Implement alignment logic for UIText
    } else {
        fprintf(stderr, "Unknown widget type: %s. Cannot set alignment \n", widgetType);
        return; 
    }
}