#include <uikit/app.h>
#include <uikit/canvas.h>

static void OnFrame(UICanvas* canvas, SDL_Renderer* renderer, void* userdata) {
    (void)renderer; (void)userdata;

    /* Fundo azul */
    UICanvas_SetFillColor(canvas, UI_COLOR_NAVY);
    UICanvas_FillRect(canvas, 0, 0, UICanvas_GetWidth(canvas), UICanvas_GetHeight(canvas));

    /* Retângulo vermelho */
    UICanvas_SetFillColor(canvas, UI_COLOR_RED);
    UICanvas_FillRect(canvas, 50, 50, 200, 120);

    /* Círculo verde */
    UICanvas_SetFillColor(canvas, UI_COLOR_GREEN);
    UICanvas_FillCircle(canvas, 400, 250, 80);

    /* Path amarelo em forma de triângulo */
    UICanvas_SetFillColor(canvas, UI_COLOR_YELLOW);
    UICanvas_BeginPath(canvas);
    UICanvas_MoveTo(canvas, 600, 100);
    UICanvas_LineTo(canvas, 700, 300);
    UICanvas_LineTo(canvas, 500, 300);
    UICanvas_ClosePath(canvas);
    UICanvas_Fill(canvas);

    /* Texto branco */
    UICanvas_SetFillColor(canvas, UI_COLOR_WHITE);
    UICanvas_DrawText(canvas, "Canvas 2D OK", 50, 400, "Arial", 32.0f);
}

int main() {
    UIApp* app = UIApp_Create("test_canvas_basic", 800, 600);
    if (!app) return 1;

    UICanvas* canvas = UICanvas_Create(800, 600);
    UICanvas_OnFrame(canvas, OnFrame, NULL);

    UIChildren* children = UIChildren_Create(1);
    UIChildren_Add(children, widgcs(canvas, 800.0f, 600.0f));
    UIApp_SetChildren(app, children);

    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
