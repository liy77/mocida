#ifndef CANVAS_INTERNAL_H
#define CANVAS_INTERNAL_H

#include <uikit/canvas.h>

/* Internal struct definition - kept private from public API users. */
typedef struct {
    UIColor fillColor;
    UIColor strokeColor;
    float lineWidth;
    float globalAlpha;
    SDL_BlendMode blendMode;
    float matrix[6];
} UICanvasState;

typedef enum {
    UI_PATH_CMD_MOVE,
    UI_PATH_CMD_LINE,
    UI_PATH_CMD_ARC,
    UI_PATH_CMD_BEZIER,
    UI_PATH_CMD_CLOSE
} UIPathCmdType;

typedef struct {
    UIPathCmdType type;
    float x, y;
    float r, startAngle, endAngle;
    float cp1x, cp1y, cp2x, cp2y;
} UIPathCmd;

struct UICanvas {
    const char* __widget_type;
    float width, height;

    UICanvasState* states;
    int stateCount;
    int stateCap;

    UIPathCmd* path;
    int pathCount;
    int pathCap;
    float pathStartX, pathStartY;
    float pathCurX, pathCurY;
    int pathHasCur;

    float mouseX, mouseY;
    int mouseInside;

    SDL_Renderer* currentRenderer;

    UICanvasFrameCallback onFrame;
    UICanvasMouseCallback onMouseMove;
    UICanvasMouseCallback onMouseDown;
    UICanvasMouseCallback onMouseUp;
    UICanvasMouseCallback onHoverEnter;
    UICanvasMouseCallback onHoverExit;
    UICanvasKeyCallback   onKeyDown;

    void* onFrameUd;
    void* onMouseMoveUd;
    void* onMouseDownUd;
    void* onMouseUpUd;
    void* onHoverEnterUd;
    void* onHoverExitUd;
    void* onKeyDownUd;
};

#endif // CANVAS_INTERNAL_H
