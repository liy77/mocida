#include <uikit/app.h>
#include <uikit/canvas.h>
#include <stdlib.h>

#define MAX_PARTICLES 256

typedef struct {
    float x, y;
    float vx, vy;
    float life;
    float maxLife;
} Particle;

typedef struct {
    float mouseX, mouseY;
    Particle particles[MAX_PARTICLES];
    int count;
} State;

static void SpawnParticle(State* s, float x, float y) {
    if (s->count >= MAX_PARTICLES) return;
    Particle* p = &s->particles[s->count++];
    p->x = x; p->y = y;
    p->vx = ((float)rand() / (float)RAND_MAX - 0.5f) * 4.0f;
    p->vy = ((float)rand() / (float)RAND_MAX - 0.5f) * 4.0f;
    p->maxLife = 1.0f + ((float)rand() / (float)RAND_MAX) * 1.0f;
    p->life = p->maxLife;
}

static void OnFrame(UICanvas* canvas, SDL_Renderer* renderer, void* userdata) {
    State* s = (State*)userdata;
    (void)renderer;

    /* Fundo preto */
    UICanvas_SetFillColor(canvas, UI_COLOR_BLACK);
    UICanvas_FillRect(canvas, 0, 0, UICanvas_GetWidth(canvas), UICanvas_GetHeight(canvas));

    /* Spawn em cima do mouse */
    for (int i = 0; i < 3; i++) SpawnParticle(s, s->mouseX, s->mouseY);

    /* Atualizar e desenhar partículas */
    for (int i = 0; i < s->count; ) {
        Particle* p = &s->particles[i];
        p->x += p->vx;
        p->y += p->vy;
        p->life -= 0.016f;

        if (p->life <= 0.0f) {
            *p = s->particles[--s->count];
            continue;
        }

        float a = p->life / p->maxLife;
        UIColor c = UI_COLOR_WHITE; c.a = a;
        UICanvas_SetFillColor(canvas, c);
        UICanvas_FillCircle(canvas, p->x, p->y, 4.0f * a);
        i++;
    }

    /* Texto informativo */
    UICanvas_SetFillColor(canvas, UI_COLOR_WHITE);
    UICanvas_DrawText(canvas, "Mova o mouse", 10, 10, "Arial", 18.0f);
}

static void OnMouseMove(UICanvas* canvas, UIMouseEvent ev, void* userdata) {
    State* s = (State*)userdata;
    (void)canvas;
    s->mouseX = ev.x;
    s->mouseY = ev.y;
}

int main() {
    UIApp* app = UIApp_Create("test_canvas_particles", 800, 600);
    if (!app) return 1;

    UICanvas* canvas = UICanvas_Create(800, 600);
    State state = {0};
    state.mouseX = 400.0f;
    state.mouseY = 300.0f;

    UICanvas_OnFrame(canvas, OnFrame, &state);
    UICanvas_OnMouseMove(canvas, OnMouseMove, &state);

    UIChildren* children = UIChildren_Create(1);
    UIChildren_Add(children, widgcs(canvas, 800.0f, 600.0f));
    UIApp_SetChildren(app, children);

    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
