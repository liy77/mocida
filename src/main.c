#include <uikit/app.h>
#include <stdio.h>

// Define the callback function outside of main
void UpdateFrameRateText(UIEventData data) {
    double frametime = data.framerate.fps;
    UIWidget* textWidget = (UIWidget*)UIChildren_GetById(data.children, "frame_time_text");
    if (textWidget == NULL) {
        fprintf(stderr, "Text widget not found\n");
        return;
    }

    UIText* text = (UIText*)textWidget->data;
    if (text == NULL) {
        fprintf(stderr, "Text data not found\n");
        return;
    }
    char buffer[100];
    snprintf(buffer, sizeof(buffer), "Frame time: %.2f", frametime);
    UIText_SetText(text, buffer);
}

int main() {
    UIApp* app = UIApp_Create("My App", 800, 600);
    if (app == NULL) {
        return 1;
    }

    UIApp_SetRenderDriver(app, UI_RENDER_3D12);

    UIRectangle* redRect = UIRectangle_Create();
    UIRectangle_SetColor(redRect, UI_COLOR_RED);
    UIRectangle_SetRadius(redRect, 50);
    UIRectangle_SetBorderWidth(redRect, 3);
    
    UIRectangle* blueRect = UIRectangle_Create();
    UIRectangle_SetColor(blueRect, UI_COLOR_BLUE);
    UIRectangle_SetRadius(blueRect, 10);
    UIRectangle_SetMargins(blueRect, 150, 20, 0, 20);

    UIChildren* children = UIChildren_Create(10);
    if (children == NULL) {
        UIApp_Destroy(app);
        return 1;
    }

    UIWidget *redWidget = widgcs(redRect, 100, 100);
    UIWidget_SetZIndex(redWidget, 2);

    UIText* text = UIText_Create("Frame time: 0.0", 40);
    
    UIWidget *blueWidget = widgcs(blueRect, 200, 200);
    UIWidget_SetParent(redWidget, app->mainWidget);

    UIWidget_SetAlignment(redWidget, UIAlignment_Create(
        (UIAlign){.value = UI_ALIGN_V_BOTTOM, .target_widget = app->mainWidget},
        (UIAlign){.value = UI_ALIGN_H_CENTER, .target_widget = blueWidget}
    ));
    UIChildren_Add(children, redWidget);
    UIChildren_Add(children, blueWidget);

    // Register the callback to be called every frame
    UIApp_SetEventCallback(app, UI_EVENT_FRAMERATE_CHANGED, UpdateFrameRateText);

    UISearchFonts(); // Search for available fonts
    UIText_SetFontFamily(text, UIGetFont("Times New Roman"));
    UIText_SetColor(text, UI_COLOR_YELLOW);

    
    UIRectangle* textBackground = UIRectangle_Create();
    UIRectangle_SetColor(textBackground, UI_COLOR_GRAY);
    UIRectangle_SetRadius(textBackground, 5);
    UIRectangle_SetBorderColor(textBackground, UI_COLOR_BLACK);
    UIRectangle_SetBorderWidth(textBackground, 0);

    UIText_SetBackground(text, textBackground);
    UIText_SetPadding(text, 10, 10, 10, 10);
    UIWidget* textWidget = widgc(text);
    UIWidget_SetId(textWidget, "frame_time_text");
    UIWidget_SetZIndex(textWidget, 3);

    UIChildren_Add(children, textWidget);
    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, UI_COLOR_DARK_GRAY);
    UIApp_SetWindowTitle(app, "My App 2");
    UIApp_SetWindowSize(app, 1024, 768);
    UIApp_SetWindowDisplayMode(app, WINDOW_WINDOWED);

    UIApp_ShowWindow(app);
    UIApp_Run(app);

    return 0;
}