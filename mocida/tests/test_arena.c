// test_arena.c
//
// Unit tests for UIArena. Each CHECK both prints to the console AND
// records a row that is rendered into a window after the tests finish,
// so the results are visible whether the executable is launched from a
// terminal or by double-click.
//
// Coverage:
//   - Allocation respects requested alignment (up to 16).
//   - AllocZero clears the returned block.
//   - Strdup / Strndup copy correctly (and Strndup stops at NUL).
//   - Reset preserves capacity and reuses chunks.
//   - Large allocations spill into a freshly grown chunk.
//   - BytesUsed / BytesReserved track state.
//   - Destroy / Reset / BytesUsed are safe on NULL.

#include <uikit/app.h>
#include <uikit/arena.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --------------------------------------------------------------------
// Result collection
// --------------------------------------------------------------------

#define MAX_RESULTS 128

typedef struct {
    char label[120];
    int  pass;
} TestRow;

static TestRow g_results[MAX_RESULTS];
static int     g_count = 0;
static int     g_pass = 0;
static int     g_fail = 0;

static void record(const char* label, int ok) {
    if (g_count < MAX_RESULTS) {
        snprintf(g_results[g_count].label, sizeof(g_results[g_count].label),
                 "%s", label);
        g_results[g_count].pass = ok;
        g_count++;
    }
    if (ok) { printf("  PASS  %s\n", label); g_pass++; }
    else    { printf("  FAIL  %s\n", label); g_fail++; }
}

#define CHECK(cond, label) record((label), (cond) ? 1 : 0)

// --------------------------------------------------------------------
// Tests
// --------------------------------------------------------------------

static void test_basic_alloc(void) {
    printf("[basic_alloc]\n");
    UIArena* a = UIArena_Create(0);
    CHECK(a != NULL, "create");

    int* p = UIArena_New(a, int);
    CHECK(p != NULL, "New(int) returns non-null");
    *p = 42;
    CHECK(*p == 42, "writes survive read");

    void* q = UIArena_Alloc(a, 32, 16);
    CHECK(((uintptr_t)q & 15u) == 0u, "16-byte aligned alloc");

    UIArena_Destroy(a);
}

static void test_zero(void) {
    printf("[alloc_zero]\n");
    UIArena* a = UIArena_Create(0);
    char* buf = (char*)UIArena_AllocZero(a, 128, 8);
    int all_zero = 1;
    for (int i = 0; i < 128; i++) if (buf[i] != 0) { all_zero = 0; break; }
    CHECK(all_zero, "AllocZero clears block");
    UIArena_Destroy(a);
}

static void test_strdup(void) {
    printf("[strdup]\n");
    UIArena* a = UIArena_Create(0);
    char* s = UIArena_Strdup(a, "hello, mocida");
    CHECK(s != NULL, "strdup returns non-null");
    CHECK(s && strcmp(s, "hello, mocida") == 0, "strdup content matches");

    char* t = UIArena_Strndup(a, "abcdef", 3);
    CHECK(t != NULL && strcmp(t, "abc") == 0, "strndup truncates");

    char* u = UIArena_Strndup(a, "ab", 10);
    CHECK(u != NULL && strcmp(u, "ab") == 0, "strndup stops at NUL even when n bigger");

    UIArena_Destroy(a);
}

static void test_growth_and_reset(void) {
    printf("[growth_and_reset]\n");
    UIArena* a = UIArena_Create(1024);
    size_t reserved0 = UIArena_BytesReserved(a);
    CHECK(reserved0 == 1024, "initial reserved == requested");

    for (int i = 0; i < 200; i++) {
        char* p = (char*)UIArena_Alloc(a, 32, 1);
        if (!p) { CHECK(0, "alloc never returns NULL during normal use"); break; }
    }
    CHECK(UIArena_BytesReserved(a) > reserved0, "reserved grew after spill");
    CHECK(UIArena_BytesUsed(a) >= 200 * 32, "used reflects every alloc");

    size_t reserved_after_growth = UIArena_BytesReserved(a);
    UIArena_Reset(a);
    CHECK(UIArena_BytesUsed(a) == 0, "Reset zeroes BytesUsed");
    CHECK(UIArena_BytesReserved(a) == reserved_after_growth, "Reset preserves reserved capacity");

    for (int i = 0; i < 200; i++) UIArena_Alloc(a, 32, 1);
    CHECK(UIArena_BytesReserved(a) == reserved_after_growth, "post-reset reuses chunks");

    UIArena_Destroy(a);
}

static void test_oversize_alloc(void) {
    printf("[oversize_alloc]\n");
    UIArena* a = UIArena_Create(1024);
    char* big = (char*)UIArena_Alloc(a, 8192, 8);
    CHECK(big != NULL, "oversize alloc succeeds");
    memset(big, 0x5A, 8192);
    int ok = 1;
    for (int i = 0; i < 8192; i++) if ((unsigned char)big[i] != 0x5A) { ok = 0; break; }
    CHECK(ok, "oversize block fully writable");
    UIArena_Destroy(a);
}

static void test_destroy_null(void) {
    printf("[destroy_null]\n");
    UIArena_Destroy(NULL);
    UIArena_Reset(NULL);
    CHECK(UIArena_BytesUsed(NULL) == 0, "BytesUsed(NULL) is zero");
    CHECK(UIArena_BytesReserved(NULL) == 0, "BytesReserved(NULL) is zero");
    CHECK(UIArena_Alloc(NULL, 16, 8) == NULL, "Alloc(NULL) returns NULL");
    record("null-arena helpers don't crash", 1);
}

// --------------------------------------------------------------------
// Results window
// --------------------------------------------------------------------

static const UIColor COLOR_PASS = { 22, 163,  74, 1.0f }; // emerald 600
static const UIColor COLOR_FAIL = { 220,  38,  38, 1.0f }; // red 600
static const UIColor COLOR_INK  = { 15,   23,  42, 1.0f };
static const UIColor COLOR_BG   = { 248, 250, 252, 1.0f };

static UIWidget* label(UIChildren* children, const char* text, float size,
                       UIColor color, float x, float y) {
    UIText* t = UIText_Create((char*)text, size);
    UIText_SetFontFamily(t, UIGetFont("Arial"));
    UIText_SetColor(t, color);
    UIWidget* w = widgc(t);
    UIWidget_SetPosition(w, x, y);
    UIChildren_Add(children, w);
    return w;
}

static int show_results_window(void) {
    const int win_w = 760;
    const int row_h = 20;
    const int header_h = 90;
    const int win_h = header_h + (g_count + 2) * row_h + 24;

    UIApp* app = UIApp_Create("Mocida - test_arena results", win_w, win_h);
    if (!app) return 1;
    UISearchFonts();

    UIChildren* c = UIChildren_Create(g_count + 8);

    label(c, "UIArena unit tests", 22.0f, COLOR_INK, 24.0f, 20.0f);

    char summary[64];
    snprintf(summary, sizeof(summary), "%d passed   /   %d failed",
             g_pass, g_fail);
    label(c, summary, 16.0f,
          g_fail == 0 ? COLOR_PASS : COLOR_FAIL,
          24.0f, 54.0f);

    float y = (float)header_h;
    for (int i = 0; i < g_count; i++) {
        const TestRow* r = &g_results[i];
        // Status pill on the left, then the label.
        label(c, r->pass ? "PASS" : "FAIL", 13.0f,
              r->pass ? COLOR_PASS : COLOR_FAIL,
              24.0f, y);
        label(c, r->label, 13.0f, COLOR_INK, 80.0f, y);
        y += (float)row_h;
    }

    UIApp_SetChildren(app, c);
    UIApp_SetBackgroundColor(app, COLOR_BG);
    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return g_fail == 0 ? 0 : 1;
}

// --------------------------------------------------------------------
// Entry
// --------------------------------------------------------------------

int main(void) {
    printf("UIArena unit tests\n");
    printf("==================\n");

    test_basic_alloc();
    test_zero();
    test_strdup();
    test_growth_and_reset();
    test_oversize_alloc();
    test_destroy_null();

    printf("\nSummary: %d passed, %d failed\n", g_pass, g_fail);
    return show_results_window();
}
