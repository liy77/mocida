// test_mimalloc.c
//
// Confirms that mimalloc is wired into Mocida's allocator path:
//
//   1. Stress-allocates a lot of widgets up front (1000 rectangles +
//      1000 mouse areas + 1000 texts). All those mallocs go through
//      mi_malloc because of the mocida_alloc.h override.
//   2. Dumps mimalloc's process-wide stats via mi_stats_print so you
//      can see the allocation traffic (block sizes, peak RSS, etc.).
//   3. Opens a small window where you can verify that everything still
//      renders normally; closing the window prints stats one more time
//      from the cleanup path.
//
// If MOCIDA_USE_MIMALLOC is not defined (e.g. you opted out via
// `cmake -DMOCIDA_USE_MIMALLOC=OFF`), the test still runs but the
// stats print is replaced by a "not enabled" line.

#include <uikit/app.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(MOCIDA_USE_MIMALLOC)
#  include <mimalloc.h>
#endif

static void PrintMimallocBanner(const char* phase) {
#if defined(MOCIDA_USE_MIMALLOC)
    printf("\n==================== mimalloc stats (%s) ====================\n",
           phase);
    mi_stats_print(NULL);
    printf("=========================================================================\n\n");
#else
    printf("[%s] mimalloc NOT enabled (build with MOCIDA_USE_MIMALLOC=ON).\n", phase);
#endif
}

#define STRESS_COUNT 1000

int main(void) {
    UIApp* app = UIApp_Create("Mocida - mimalloc", 640, 360);
    if (!app) return 1;
    UISearchFonts();
    UIApp_SetWindowIcon(app, "assets/logo.svg");

    // ---------------- Stress: allocate / free a lot ----------------
    printf("Allocating %d widgets via Mocida (every malloc routes to mi_malloc) ...\n",
           STRESS_COUNT * 3);

    // Burn through a bunch of allocations to give mimalloc real traffic.
    UIRectangle** rects   = malloc(sizeof(UIRectangle*) * STRESS_COUNT);
    UIText**      texts   = malloc(sizeof(UIText*)      * STRESS_COUNT);
    UIMouseArea** areas   = malloc(sizeof(UIMouseArea*) * STRESS_COUNT);

    for (int i = 0; i < STRESS_COUNT; i++) {
        rects[i] = UIRectangle_Create();
        UIRectangle_SetColor(rects[i], (UIColor){ i & 0xFF, (i*7) & 0xFF, (i*13) & 0xFF, 1.0f });
        UIRectangle_SetRadius(rects[i], (float)(i % 16));

        char buf[24];
        snprintf(buf, sizeof(buf), "Label %d", i);
        texts[i] = UIText_Create(buf, 12.0f + (i % 8));

        areas[i] = UIMouseArea_Create();
    }

    // Throw half of them away to give mi_free traffic too.
    for (int i = 0; i < STRESS_COUNT; i += 2) {
        UIRectangle_Destroy(rects[i]);   rects[i] = NULL;
        UIText_Destroy(texts[i]);        texts[i] = NULL;
        UIMouseArea_Destroy(areas[i]);   areas[i] = NULL;
    }

    PrintMimallocBanner("after stress test");

    // Free what's left so we don't leak (the framework would handle
    // widgets attached to the window; these standalone ones we own).
    for (int i = 0; i < STRESS_COUNT; i++) {
        if (rects[i]) UIRectangle_Destroy(rects[i]);
        if (texts[i]) UIText_Destroy(texts[i]);
        if (areas[i]) UIMouseArea_Destroy(areas[i]);
    }
    free(rects);
    free(texts);
    free(areas);

    // ---------------- Render a normal scene ----------------
    UIChildren* children = UIChildren_Create(4);

    UIRectangle* card = UIRectangle_Create();
    UIRectangle_SetColor(card, UI_COLOR_WHITE);
    UIRectangle_SetRadius(card, 12.0f);
    UIRectangle_SetShadow(card, UI_SHADOW_DEFAULT);
    UIWidget* cardW = widgcs(card, 480.0f, 220.0f);
    UIWidget_SetPosition(cardW, 80.0f, 70.0f);
    UIChildren_Add(children, cardW);

    UIText* label = UIText_Create("mimalloc enabled - see console for stats", 18.0f);
    UIText_SetFontFamily(label, UIGetFont("Arial"));
    UIText_SetColor(label, (UIColor){ 30, 41, 59, 1.0f });
    UIWidget* lw = widgc(label);
    UIWidget_SetPosition(lw, 110.0f, 160.0f);
    UIChildren_Add(children, lw);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, (UIColor){ 226, 232, 240, 1.0f });
    UIApp_ShowWindow(app);
    UIApp_Run(app);

    UIApp_Destroy(app);

    PrintMimallocBanner("after window destroy");
    return 0;
}
