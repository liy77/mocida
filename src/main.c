#include <uikit/app.h>
#include <stdio.h>

void main() {

    UIApp* app = UIApp_Create("My App", 800, 600);
    if (app == NULL) {
        return;
    }

    UIRectangle* redRect = UIRectangle_Create();
    UIRectangle_SetColor(redRect, UIColorRed);
    UIRectangle_SetRadius(redRect, 0);
    UIRectangle_SetBorderWidth(redRect, 3);
    
    UIRectangle* blueRect = UIRectangle_Create();
    UIRectangle_SetColor(blueRect, UIColorBlue);
    UIRectangle_SetRadius(blueRect, 10);
    UIRectangle_SetMargins(blueRect, 150, 20, 0, 20);

    UIChildren* children = UIChildren_Create(10);
    if (children == NULL) {
        UIApp_Destroy(app);
        return;
    }

    UIWidget *redWidget = widgcs(redRect, 100, 100);
    UIWidget_SetZIndex(redWidget, 2);

    UIWidget *blueWidget = widgcs(blueRect, 200, 200);
    UIWidget_SetParent(redWidget, app->mainWidget);

    UIWidget_SetAlignment(redWidget, UIAlignment_Create(UI_ALIGN_V_TOP, UI_ALIGN_H_RIGHT));
    UIChildren_Add(children, redWidget);
    UIChildren_Add(children, blueWidget);

    UIText* text = UIText_Create("Hello, World!", 40);
    UISearchFonts(); // Search for available fonts
    UIText_SetFontFamily(text, UIGetFont("Times New Roman"));
    UIText_SetColor(text, UIColorYellow);

    
    UIRectangle* textBackground = UIRectangle_Create();
    UIRectangle_SetColor(textBackground, UIColorGray);
    UIRectangle_SetRadius(textBackground, 0);
    UIRectangle_SetBorderColor(textBackground, UIColorBlack);
    UIRectangle_SetBorderWidth(textBackground, 0);

    UIText_SetBackground(text, textBackground);
    UIText_SetPadding(text, 10, 10, 10, 10);
    UIWidget* textWidget = widgc(text);
    UIWidget_SetZIndex(textWidget, 3);

    UIChildren_Add(children, textWidget);
    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, UIColorWhite);
    UIApp_SetWindowTitle(app, "My App 2");
    UIApp_SetWindowSize(app, 1024, 768);
    UIApp_SetWindowDisplayMode(app, UIWindowWindowed);

    UIApp_ShowWindow(app);
    UIApp_Run(app);
}