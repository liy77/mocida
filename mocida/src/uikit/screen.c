// screen.c — primary-display metrics for responsive layout (UIScreen).
#include <uikit/screen.h>
#include <SDL3/SDL.h>

UIScreenSize UIScreen_GetSize(void) {
    UIScreenSize s = { 0, 0 };
    SDL_DisplayID disp = SDL_GetPrimaryDisplay();
    if (disp != 0) {
        SDL_Rect bounds;
        if (SDL_GetDisplayBounds(disp, &bounds)) {
            s.width  = bounds.w;
            s.height = bounds.h;
        }
    }
    return s;
}

int UIScreen_GetWidth(void)  { return UIScreen_GetSize().width;  }
int UIScreen_GetHeight(void) { return UIScreen_GetSize().height; }
