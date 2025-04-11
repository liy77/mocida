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
    UIWidget_SetZIndex(redWidget, 1);

    UIWidget *blueWidget = UIWidget_Create(blueRect);

    UIChildren_Add(children, redWidget);
    UIChildren_Add(children, blueWidget);
    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, &UIColorWhite);
    UIApp_SetWindowTitle(app, "My App 2");
    UIApp_SetWindowSize(app, 1024, 768);
    UIApp_SetWindowScaleMode(app, UIWindowWindowed);

    UIApp_ShowWindow(app);
    UIApp_Run(app);
}