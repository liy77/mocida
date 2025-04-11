#ifndef UIKIT_APP_H
#define UIKIT_APP_H

#include <uikit/color.h>
#include <uikit/window.h>
#include <uikit/children.h>

typedef int UIRenderDriver;

#define UIRenderSoftware (UIRenderDriver)0
#define UIRenderOpenGL (UIRenderDriver)1
#define UIRenderVulkan (UIRenderDriver)2

#ifdef WIN32
#define UIRender3D12 (UIRenderDriver)3
#define UIRender3D11 (UIRenderDriver)4
#endif

#ifdef __MACH__
#define UIRenderMetal (UIRenderDriver)5
#endif


typedef struct {
    UIWindow* window;
    UIColor* backgroundColor;
} UIApp;

UIApp* UIApp_Create(const char* title, int width, int height);
void UIApp_SetChildren(UIApp* app, UIChildren* children);
void UIApp_SetBackgroundColor(UIApp* app, UIColor* color);
void UIApp_SetWindowTitle(UIApp* app, const char* title);
void UIApp_SetWindowSize(UIApp* app, int width, int height);
void UIApp_SetWindowPosition(UIApp* app, int x, int y);
void UIApp_SetWindowScaleMode(UIApp* app, UIWindowScaleMode scaleMode);
void UIApp_ShowWindow(UIApp* app);
void UIApp_HideWindow(UIApp* app);
void UIApp_Destroy(UIApp* app);
void UIApp_Run(UIApp* app);

#endif