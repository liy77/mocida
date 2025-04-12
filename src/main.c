#include <uikit/app.h>
#include <stdio.h>

void main() {
    UIApp* app = UIApp_Create("My App", 800, 600);
    if (app == NULL) {
        return;
    }

    UIRectangle* redRect = UIRectangle_Create(100, 100);
    UIRectangle_SetColor(redRect, UIColorRed);
    UIRectangle_SetRadius(redRect, 10);
    UIRectangle_SetBorderWidth(redRect, 3);
    
    UIRectangle* blueRect = UIRectangle_Create(200, 200);
    UIRectangle_SetColor(blueRect, UIColorBlue);
    UIRectangle_SetRadius(blueRect, 10);
    UIRectangle_SetMargins(blueRect, 20, 20, 20, 20);

    UIChildren* children = UIChildren_Create(10);
    if (children == NULL) {
        UIApp_Destroy(app);
        return;
    }

    UIWidget *redWidget = UIWidget_Create(redRect);
    UIWidget_SetZIndex(redWidget, 2);

    UIWidget *blueWidget = UIWidget_Create(blueRect);

    UIChildren_Add(children, redWidget);
    UIChildren_Add(children, blueWidget);

    UIText* text = UIText_Create("Hello, World!", 40);
    UIText_SetFontFamily(text, DEFAULT_FONT_PATH);
    UIText_SetColor(text, &UIColorYellow);

    
    UIRectangle* textBackground = UIRectangle_Create(0, 0);
    UIRectangle_SetColor(textBackground, UIColorGray);
    UIRectangle_SetRadius(textBackground, 0);
    UIRectangle_SetBorderColor(textBackground, UIColorBlack);
    UIRectangle_SetBorderWidth(textBackground, 0);

    UIText_SetBackground(text, textBackground);
    UIText_SetPadding(text, 10, 10, 10, 10);
    UIWidget* textWidget = UIWidget_Create(text);
    UIWidget_SetZIndex(textWidget, 3);

    UIChildren_Add(children, textWidget);
    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, &UIColorWhite);
    UIApp_SetWindowTitle(app, "My App 2");
    UIApp_SetWindowSize(app, 1024, 768);
    UIApp_SetWindowScaleMode(app, UIWindowWindowed);

    UIApp_ShowWindow(app);
    UIApp_Run(app);
}