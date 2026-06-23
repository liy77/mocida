#ifndef UIKIT_CANVAS_H
#define UIKIT_CANVAS_H

#include <SDL3/SDL.h>
#include <uikit/widget.h>
#include <uikit/children.h>
#include <uikit/mouse_area.h>
#include <uikit/color.h>
#include <uikit/image.h>

#define UI_WIDGET_CANVAS "@uikit/canvas"

typedef struct UICanvas UICanvas;

typedef void (*UICanvasFrameCallback)(UICanvas* canvas, SDL_Renderer* renderer, void* userdata);
typedef void (*UICanvasMouseCallback)(UICanvas* canvas, UIMouseEvent ev, void* userdata);
typedef void (*UICanvasKeyCallback)(UICanvas* canvas, const char* key, int mods, void* userdata);

/* Criação e conversão */
UICanvas* UICanvas_Create(float width, float height);
UIWidget* UICanvas_AsWidget(UICanvas* canvas);
UIWidget* UICanvas_ToWidget(UICanvas* canvas);
UICanvas* UICanvas_FromWidget(UIWidget* widget);
void      UICanvas_Destroy(UICanvas* canvas);

/* Escape hatch */
SDL_Renderer* UICanvas_GetRenderer(const UICanvas* canvas);

/* Estado do contexto */
UICanvas* UICanvas_SetFillColor  (UICanvas* canvas, UIColor color);
UICanvas* UICanvas_SetStrokeColor(UICanvas* canvas, UIColor color);
UICanvas* UICanvas_SetLineWidth  (UICanvas* canvas, float width);
UICanvas* UICanvas_SetGlobalAlpha(UICanvas* canvas, float alpha);
UICanvas* UICanvas_SetBlendMode  (UICanvas* canvas, SDL_BlendMode mode);
UICanvas* UICanvas_ClearRect     (UICanvas* canvas, float x, float y, float w, float h);

/* Primitivas imediatas */
UICanvas* UICanvas_FillRect  (UICanvas* canvas, float x, float y, float w, float h);
UICanvas* UICanvas_StrokeRect(UICanvas* canvas, float x, float y, float w, float h);

UICanvas* UICanvas_BeginPath(UICanvas* canvas);
UICanvas* UICanvas_MoveTo   (UICanvas* canvas, float x, float y);
UICanvas* UICanvas_LineTo   (UICanvas* canvas, float x, float y);
UICanvas* UICanvas_Arc      (UICanvas* canvas, float x, float y, float r,
                             float startAngle, float endAngle);
UICanvas* UICanvas_BezierCurveTo(UICanvas* canvas, float cp1x, float cp1y,
                                 float cp2x, float cp2y, float x, float y);
UICanvas* UICanvas_ClosePath(UICanvas* canvas);
UICanvas* UICanvas_Fill     (UICanvas* canvas);
UICanvas* UICanvas_Stroke   (UICanvas* canvas);

UICanvas* UICanvas_FillCircle  (UICanvas* canvas, float x, float y, float r);
UICanvas* UICanvas_StrokeCircle(UICanvas* canvas, float x, float y, float r);

UICanvas* UICanvas_DrawImage(UICanvas* canvas, UIImage* image, float x, float y,
                             float w, float h);
UICanvas* UICanvas_DrawText (UICanvas* canvas, const char* text, float x, float y,
                             const char* fontFamily, float size);

/* Coordenadas e transformação */
UICanvas* UICanvas_Save   (UICanvas* canvas);
UICanvas* UICanvas_Restore(UICanvas* canvas);
UICanvas* UICanvas_Translate(UICanvas* canvas, float x, float y);
UICanvas* UICanvas_Rotate  (UICanvas* canvas, float radians);
UICanvas* UICanvas_Scale   (UICanvas* canvas, float x, float y);

/* Callbacks */
UICanvas* UICanvas_OnFrame     (UICanvas* canvas, UICanvasFrameCallback cb, void* userdata);
UICanvas* UICanvas_OnMouseMove (UICanvas* canvas, UICanvasMouseCallback cb, void* userdata);
UICanvas* UICanvas_OnMouseDown (UICanvas* canvas, UICanvasMouseCallback cb, void* userdata);
UICanvas* UICanvas_OnMouseUp   (UICanvas* canvas, UICanvasMouseCallback cb, void* userdata);
UICanvas* UICanvas_OnHoverEnter(UICanvas* canvas, UICanvasMouseCallback cb, void* userdata);
UICanvas* UICanvas_OnHoverExit (UICanvas* canvas, UICanvasMouseCallback cb, void* userdata);
void      UICanvas_SetOnKeyDown(UICanvas* canvas, UICanvasKeyCallback cb, void* userdata);

/* Utilitários */
float  UICanvas_GetWidth      (const UICanvas* canvas);
float  UICanvas_GetHeight     (const UICanvas* canvas);
void   UICanvas_GetMousePos   (const UICanvas* canvas, float* x, float* y);
int    UICanvas_IsMouseInside (const UICanvas* canvas);

/* Interno: usado por app.c / window_render.inc */
void UICanvas_DispatchMouseMove(UIChildren* children, float x, float y);
void UICanvas_DispatchMouseDown(UIChildren* children, float x, float y, int button);
void UICanvas_DispatchMouseUp  (UIChildren* children, float x, float y, int button);

#endif // UIKIT_CANVAS_H
