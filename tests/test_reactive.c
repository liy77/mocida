// test_reactive.c
//
// Unit tests for UISignal + UIBind_*. Console output + a results window
// (same pattern as test_arena.c). The bindings still touch real widgets
// (UIText, UIWidget) but the renderer isn't involved - we only assert on
// post-Set fields.

#include <uikit/app.h>
#include <uikit/reactive.h>
#include <uikit/bind.h>
#include <uikit/text.h>
#include <uikit/widget.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --------------------------------------------------------------------
// Result collection (mirrors test_arena.c)
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
// Helpers
// --------------------------------------------------------------------

typedef struct {
    int   count;
    int   last_int;
    float last_float;
    char  last_string[64];
} ObserverState;

static void observe(UISignal* s, void* ud) {
    ObserverState* st = (ObserverState*)ud;
    st->count++;
    switch (UISignal_GetType(s)) {
        case UI_SIGNAL_INT:    st->last_int    = UISignal_GetInt(s);    break;
        case UI_SIGNAL_FLOAT:  st->last_float  = UISignal_GetFloat(s);  break;
        case UI_SIGNAL_STRING: {
            const char* v = UISignal_GetString(s);
            snprintf(st->last_string, sizeof(st->last_string), "%s", v ? v : "");
            break;
        }
        case UI_SIGNAL_POINTER: break;
    }
}

// --------------------------------------------------------------------
// Tests
// --------------------------------------------------------------------

static void test_basic_types(void) {
    printf("[basic_types]\n");
    UISignal* si = UISignal_CreateInt(7);
    UISignal* sf = UISignal_CreateFloat(3.14f);
    UISignal* ss = UISignal_CreateString("hi");
    int x = 0;
    UISignal* sp = UISignal_CreatePointer(&x);

    CHECK(UISignal_GetInt(si) == 7, "int initial value");
    CHECK(UISignal_GetFloat(sf) == 3.14f, "float initial value");
    CHECK(UISignal_GetString(ss) != NULL && strcmp(UISignal_GetString(ss), "hi") == 0, "string initial value");
    CHECK(UISignal_GetPointer(sp) == &x, "pointer initial value");

    UISignal_SetInt(si, 9);
    UISignal_SetFloat(sf, 2.5f);
    UISignal_SetString(ss, "ho");
    int y = 0;
    UISignal_SetPointer(sp, &y);

    CHECK(UISignal_GetInt(si) == 9, "int updated");
    CHECK(UISignal_GetFloat(sf) == 2.5f, "float updated");
    CHECK(strcmp(UISignal_GetString(ss), "ho") == 0, "string updated");
    CHECK(UISignal_GetPointer(sp) == &y, "pointer updated");

    UISignal_Destroy(si);
    UISignal_Destroy(sf);
    UISignal_Destroy(ss);
    UISignal_Destroy(sp);
}

static void test_dedupe(void) {
    printf("[dedupe]\n");
    ObserverState st = { 0 };
    UISignal* s = UISignal_CreateInt(0);
    UISubscription* sub = UISignal_Subscribe(s, observe, &st);

    UISignal_SetInt(s, 1);
    UISignal_SetInt(s, 1);
    UISignal_SetInt(s, 1);
    CHECK(st.count == 1, "int dedupe collapses repeats");

    UISignal_SetInt(s, 2);
    CHECK(st.count == 2, "different value notifies");

    UISignal* sf = UISignal_CreateFloat(1.0f);
    UISubscription* subf = UISignal_Subscribe(sf, observe, &st);
    st.count = 0;
    UISignal_SetFloat(sf, 1.0f);
    CHECK(st.count == 0, "float dedupe collapses identical writes");
    UISignal_SetFloat(sf, 2.0f);
    CHECK(st.count == 1, "float notifies on change");

    UISignal* ss = UISignal_CreateString("a");
    UISubscription* subs = UISignal_Subscribe(ss, observe, &st);
    st.count = 0;
    UISignal_SetString(ss, "a");
    CHECK(st.count == 0, "string dedupe collapses identical writes");
    UISignal_SetString(ss, "b");
    CHECK(st.count == 1, "string notifies on change");

    UISignal_Unsubscribe(sub);
    UISignal_Unsubscribe(subf);
    UISignal_Unsubscribe(subs);
    UISignal_Destroy(s);
    UISignal_Destroy(sf);
    UISignal_Destroy(ss);
}

static void test_multi_subscribers(void) {
    printf("[multi_subscribers]\n");
    ObserverState a = { 0 }, b = { 0 }, c = { 0 };
    UISignal* s = UISignal_CreateInt(0);
    UISubscription* sa = UISignal_Subscribe(s, observe, &a);
    UISubscription* sb = UISignal_Subscribe(s, observe, &b);
    UISubscription* sc = UISignal_Subscribe(s, observe, &c);

    UISignal_SetInt(s, 5);
    CHECK(a.count == 1 && b.count == 1 && c.count == 1, "all subscribers fired");
    CHECK(a.last_int == 5 && b.last_int == 5 && c.last_int == 5, "values match");

    UISignal_Unsubscribe(sb);
    UISignal_SetInt(s, 6);
    CHECK(a.count == 2 && b.count == 1 && c.count == 2, "unsubscribed sub no longer fires");

    UISignal_Unsubscribe(sa);
    UISignal_Unsubscribe(sc);
    UISignal_Destroy(s);
}

static UISubscription* g_self_unsub = NULL;
static int g_self_unsub_fires = 0;
static void self_unsub_cb(UISignal* s, void* ud) {
    (void)s; (void)ud;
    g_self_unsub_fires++;
    UISignal_Unsubscribe(g_self_unsub);
}

static void test_unsubscribe_during_notify(void) {
    printf("[unsubscribe_during_notify]\n");
    UISignal* s = UISignal_CreateInt(0);
    g_self_unsub = UISignal_Subscribe(s, self_unsub_cb, NULL);
    UISignal_SetInt(s, 1);
    CHECK(g_self_unsub_fires == 1, "self-unsub callback fired once");
    UISignal_SetInt(s, 2);
    CHECK(g_self_unsub_fires == 1, "after self-unsub the cb is silent");
    UISignal_Destroy(s);
}

static int g_recursive_fires = 0;
static void recursive_cb(UISignal* s, void* ud) {
    (void)ud;
    g_recursive_fires++;
    UISignal_SetInt(s, UISignal_GetInt(s) + 1);
}

static void test_reentrancy_guard(void) {
    printf("[reentrancy_guard]\n");
    UISignal* s = UISignal_CreateInt(0);
    UISubscription* sub = UISignal_Subscribe(s, recursive_cb, NULL);
    UISignal_SetInt(s, 1);
    CHECK(g_recursive_fires == 1, "recursive Set inside callback was dropped");
    CHECK(UISignal_GetInt(s) == 2, "inner Set still mutated value (just no recursive notify)");
    UISignal_Unsubscribe(sub);
    UISignal_Destroy(s);
}

static void test_force_notify(void) {
    printf("[force_notify]\n");
    ObserverState st = { 0 };
    int payload = 0;
    UISignal* s = UISignal_CreatePointer(&payload);
    UISubscription* sub = UISignal_Subscribe(s, observe, &st);

    UISignal_Notify(s);
    CHECK(st.count == 1, "Notify fires without value change");
    UISignal_Notify(s);
    CHECK(st.count == 2, "Notify can be called repeatedly");

    UISignal_Unsubscribe(sub);
    UISignal_Destroy(s);
}

static void test_bind_text_string(void) {
    printf("[bind_text_string]\n");
    UIText* t = UIText_Create("initial", 14.0f);
    UISignal* s = UISignal_CreateString("hello");
    UIBinding* b = UIBind_TextToSignal(t, s);
    CHECK(b != NULL, "bind created");
    CHECK(strcmp(t->text, "hello") == 0, "initial apply syncs widget");

    UISignal_SetString(s, "world");
    CHECK(strcmp(t->text, "world") == 0, "update propagates to widget");

    UIBind_Destroy(b);
    UISignal_SetString(s, "ignored");
    CHECK(strcmp(t->text, "world") == 0, "after Destroy further updates no-op");

    UISignal_Destroy(s);
    UIText_Destroy(t);
}

static void test_bind_text_format(void) {
    printf("[bind_text_format]\n");
    UIText* t = UIText_Create("", 14.0f);
    UISignal* fps = UISignal_CreateInt(60);
    UIBinding* b = UIBind_TextToFormat(t, fps, "FPS: %d");
    CHECK(strcmp(t->text, "FPS: 60") == 0, "initial format applied");

    UISignal_SetInt(fps, 144);
    CHECK(strcmp(t->text, "FPS: 144") == 0, "format reapplied on update");

    UIBind_Destroy(b);
    UISignal_Destroy(fps);
    UIText_Destroy(t);
}

static void test_bind_visible_opacity(void) {
    printf("[bind_visible_opacity]\n");
    UIRectangle* r = UIRectangle_Create();
    UIWidget* w = widgcs(r, 10.0f, 10.0f);

    UISignal* vis = UISignal_CreateInt(0);
    UIBinding* bv = UIBind_VisibleToSignal(w, vis);
    CHECK(w->visible == 0, "visible=0 applied");
    UISignal_SetInt(vis, 1);
    CHECK(w->visible == 1, "visible=1 propagated");
    UISignal_SetInt(vis, 5);
    CHECK(w->visible == 1, "non-zero int normalized to 1");

    UISignal* op = UISignal_CreateFloat(0.25f);
    UIBinding* bo = UIBind_OpacityToSignal(w, op);
    CHECK(w->opacity == 0.25f, "opacity initial applied");
    UISignal_SetFloat(op, 1.0f);
    CHECK(w->opacity == 1.0f, "opacity update propagated");

    UIBind_Destroy(bv);
    UIBind_Destroy(bo);
    UISignal_Destroy(vis);
    UISignal_Destroy(op);
    UIWidget_Destroy(w);
}

// --------------------------------------------------------------------
// Results window (live mini-demo + listing)
// --------------------------------------------------------------------

static const UIColor COLOR_PASS = { 22, 163,  74, 1.0f };
static const UIColor COLOR_FAIL = { 220,  38,  38, 1.0f };
static const UIColor COLOR_INK  = { 15,   23,  42, 1.0f };
static const UIColor COLOR_MUTED= {100, 116, 139, 1.0f };
static const UIColor COLOR_BG   = { 248, 250, 252, 1.0f };

static UIWidget* mklabel(UIChildren* children, const char* text, float size,
                         UIColor color, float x, float y) {
    UIText* t = UIText_Create((char*)text, size);
    UIText_SetFontFamily(t, UIGetFont("Arial"));
    UIText_SetColor(t, color);
    UIWidget* w = widgc(t);
    UIWidget_SetPosition(w, x, y);
    UIChildren_Add(children, w);
    return w;
}

// Live binding state - a signal driven by the framerate callback so the
// user can SEE reactivity working in the result window itself.
static UISignal*   g_demo_fps_signal = NULL;
static UIBinding*  g_demo_fps_bind   = NULL;

static void OnFrameTick(UIEventData data) {
    if (g_demo_fps_signal) UISignal_SetInt(g_demo_fps_signal, (int)data.framerate.fps);
}

// --------------------------------------------------------------------
// Interactive playground state — globals so the cleanup at the end of
// show_results_window can tear everything down, mirroring how the FPS
// demo above is wired.
// --------------------------------------------------------------------
static UISignal*  g_play_text_signal   = NULL;
static UIBinding* g_play_text_bind     = NULL;

static UISignal*  g_play_color_signal  = NULL;       /* int — palette index */
static UISubscription* g_play_color_sub = NULL;

static UISignal*  g_play_visible_signal = NULL;
static UIBinding* g_play_visible_bind   = NULL;

static UISignal*  g_play_opacity_signal = NULL;
static UIBinding* g_play_opacity_bind   = NULL;

/* Subscriber for the color signal — calls UIText_SetColor on the
 * mirror label. Exercises the cache-invalidation fix in
 * UIText_SetColor: without it the colour changes wouldn't show until
 * a subsequent text edit forced a glyph rebuild. */
static const UIColor g_play_palette[] = {
    {  15,  23,  42, 1.0f },  /* slate-900 ink */
    {  59, 130, 246, 1.0f },  /* blue   */
    {  22, 163,  74, 1.0f },  /* green  */
    { 217, 119,   6, 1.0f },  /* amber  */
    { 220,  38,  38, 1.0f },  /* red    */
    { 168,  85, 247, 1.0f },  /* purple */
};
#define PLAY_PALETTE_N ((int)(sizeof(g_play_palette)/sizeof(g_play_palette[0])))

static void OnPlayColorChanged(UISignal* s, void* ud) {
    UIText* target = (UIText*)ud;
    int idx = UISignal_GetInt(s);
    if (idx < 0) idx = 0;
    idx %= PLAY_PALETTE_N;
    UIText_SetColor(target, g_play_palette[idx]);
}

/* UITextField onChange forwards the new text into the string signal,
 * which is in turn bound to the mirror UIText via UIBind_TextToSignal.
 * Two hops, both reactive. */
static void OnPlayTextFieldChange(UITextField* tf, const char* text, void* ud) {
    (void)tf; (void)ud;
    if (g_play_text_signal) UISignal_SetString(g_play_text_signal, text ? text : "");
}

static void OnPlayCycleColor(UIButton* btn, void* ud) {
    (void)btn; (void)ud;
    if (!g_play_color_signal) return;
    int next = (UISignal_GetInt(g_play_color_signal) + 1) % PLAY_PALETTE_N;
    UISignal_SetInt(g_play_color_signal, next);
}

static void OnPlayToggleVisible(UIButton* btn, void* ud) {
    (void)btn; (void)ud;
    if (!g_play_visible_signal) return;
    UISignal_SetInt(g_play_visible_signal,
                    UISignal_GetInt(g_play_visible_signal) ? 0 : 1);
}

static const float g_play_opacities[] = { 1.0f, 0.7f, 0.4f, 0.15f };
#define PLAY_OPS_N ((int)(sizeof(g_play_opacities)/sizeof(g_play_opacities[0])))
static int g_play_op_idx = 0;
static void OnPlayCycleOpacity(UIButton* btn, void* ud) {
    (void)btn; (void)ud;
    if (!g_play_opacity_signal) return;
    g_play_op_idx = (g_play_op_idx + 1) % PLAY_OPS_N;
    UISignal_SetFloat(g_play_opacity_signal, g_play_opacities[g_play_op_idx]);
}

static int show_results_window(void) {
    const int win_w     = 780;
    const int row_h     = 20;
    const int header_h  = 130;
    const int play_h    = 230;      /* interactive playground panel */
    const int results_h = (g_count + 2) * row_h + 24;
    const int win_h     = header_h + play_h + results_h;

    UIApp* app = UIApp_Create("Mocida - test_reactive results", win_w, win_h);
    if (!app) return 1;
    UISearchFonts();

    UIChildren* c = UIChildren_Create(g_count + 32);

    mklabel(c, "UISignal / UIBind unit tests", 22.0f, COLOR_INK, 24.0f, 20.0f);

    char summary[80];
    snprintf(summary, sizeof(summary), "%d passed   /   %d failed", g_pass, g_fail);
    mklabel(c, summary, 16.0f, g_fail == 0 ? COLOR_PASS : COLOR_FAIL, 24.0f, 54.0f);

    // Live binding demo: text bound to a signal that ticks with the
    // frame counter. Proves the wiring works end-to-end in a real app.
    mklabel(c, "Live demo:", 13.0f, COLOR_MUTED, 24.0f, 90.0f);
    UIText* fpsText = UIText_Create("waiting...", 14.0f);
    UIText_SetFontFamily(fpsText, UIGetFont("Arial"));
    UIText_SetColor(fpsText, COLOR_INK);
    UIWidget* fpsW = widgc(fpsText);
    UIWidget_SetPosition(fpsW, 110.0f, 88.0f);
    UIChildren_Add(c, fpsW);

    g_demo_fps_signal = UISignal_CreateInt(0);
    g_demo_fps_bind   = UIBind_TextToFormat(fpsText, g_demo_fps_signal,
                                            "FPS bound via UISignal -> %d");

    // ----------------------------------------------------------------
    // Interactive playground — type into the field, watch the mirror
    // label update via UIBind_TextToSignal. Click the colour button to
    // see UIText_SetColor invalidate the cached glyph texture (without
    // the fix the colour wouldn't change until another edit forced a
    // rebuild). The visibility and opacity buttons exercise
    // UIBind_VisibleToSignal / UIBind_OpacityToSignal.
    // ----------------------------------------------------------------
    const float play_y = (float)header_h;
    mklabel(c, "Interactive playground:", 13.0f, COLOR_MUTED, 24.0f, play_y);

    /* Row 1: textfield + reactive mirror label */
    mklabel(c, "Type:", 13.0f, COLOR_INK, 24.0f, play_y + 36.0f);
    UITextField* tf = UITextField_Create("Hello, signals!", 14.0f);
    UITextField_SetFontFamily(tf, UIGetFont("Arial"));
    UITextField_SetPlaceholder(tf, "Type and watch the mirror update");
    UIWidget* tfW = widgcs(tf, 320.0f, 32.0f);
    UIWidget_SetPosition(tfW, 70.0f, play_y + 26.0f);
    UIChildren_Add(c, tfW);

    g_play_text_signal = UISignal_CreateString("Hello, signals!");
    UITextField_OnChange(tf, OnPlayTextFieldChange, NULL);

    mklabel(c, "Mirror:", 13.0f, COLOR_MUTED, 410.0f, play_y + 36.0f);
    UIText* mirror = UIText_Create("placeholder", 16.0f);
    UIText_SetFontFamily(mirror, UIGetFont("Arial"));
    UIText_SetColor(mirror, COLOR_INK);
    UIWidget* mirrorW = widgc(mirror);
    UIWidget_SetPosition(mirrorW, 460.0f, play_y + 32.0f);
    UIChildren_Add(c, mirrorW);

    g_play_text_bind = UIBind_TextToSignal(mirror, g_play_text_signal);

    /* Row 2: 3 buttons that drive 3 signals → 3 reactive sinks */
    const float btn_y = play_y + 80.0f;
    const float btn_h = 36.0f;

    UIButton* colorBtn = UIButton_Create("Cycle colour", 14.0f);
    UIButton_SetFontFamily(colorBtn, UIGetFont("Arial"));
    UIButton_SetRadius   (colorBtn, 6.0f);
    UIButton_SetColors   (colorBtn, (UIColor){59,130,246,1.0f}, UI_COLOR_WHITE);
    UIButton_OnClick     (colorBtn, OnPlayCycleColor, NULL);
    UIWidget* colorBtnW = widgcs(colorBtn, 150.0f, btn_h);
    UIWidget_SetPosition(colorBtnW, 24.0f, btn_y);
    UIChildren_Add(c, colorBtnW);

    UIButton* visBtn = UIButton_Create("Toggle visible", 14.0f);
    UIButton_SetFontFamily(visBtn, UIGetFont("Arial"));
    UIButton_SetRadius   (visBtn, 6.0f);
    UIButton_SetColors   (visBtn, (UIColor){22,163,74,1.0f}, UI_COLOR_WHITE);
    UIButton_OnClick     (visBtn, OnPlayToggleVisible, NULL);
    UIWidget* visBtnW = widgcs(visBtn, 160.0f, btn_h);
    UIWidget_SetPosition(visBtnW, 184.0f, btn_y);
    UIChildren_Add(c, visBtnW);

    UIButton* opBtn = UIButton_Create("Cycle opacity", 14.0f);
    UIButton_SetFontFamily(opBtn, UIGetFont("Arial"));
    UIButton_SetRadius   (opBtn, 6.0f);
    UIButton_SetColors   (opBtn, (UIColor){168,85,247,1.0f}, UI_COLOR_WHITE);
    UIButton_OnClick     (opBtn, OnPlayCycleOpacity, NULL);
    UIWidget* opBtnW = widgcs(opBtn, 150.0f, btn_h);
    UIWidget_SetPosition(opBtnW, 354.0f, btn_y);
    UIChildren_Add(c, opBtnW);

    /* Row 3: the "target" widget the buttons drive via signals.
     * Its visibility and opacity are reactive; its colour is updated
     * by the color-signal subscriber. */
    UIRectangle* target = UIRectangle_Create();
    UIRectangle_SetColor (target, (UIColor){ 226, 232, 240, 1.0f });
    UIRectangle_SetRadius(target, 10.0f);
    UIWidget* targetW = widgcs(target, win_w - 48.0f, 56.0f);
    UIWidget_SetPosition(targetW, 24.0f, btn_y + 50.0f);
    UIChildren_Add(c, targetW);

    UIText* targetLabel = UIText_Create("react target", 16.0f);
    UIText_SetFontFamily(targetLabel, UIGetFont("Arial"));
    UIText_SetColor     (targetLabel, COLOR_INK);
    UIWidget* targetLabelW = widgc(targetLabel);
    UIWidget_SetPosition(targetLabelW, 40.0f, btn_y + 67.0f);
    UIChildren_Add(c, targetLabelW);

    /* Wire the color signal subscriber AFTER the target text exists so
     * the userdata pointer is valid. Initial value = 0 → ink slate. */
    g_play_color_signal = UISignal_CreateInt(0);
    g_play_color_sub    = UISignal_Subscribe(g_play_color_signal,
                                             OnPlayColorChanged, targetLabel);

    g_play_visible_signal = UISignal_CreateInt(1);
    g_play_visible_bind   = UIBind_VisibleToSignal(targetW, g_play_visible_signal);

    g_play_opacity_signal = UISignal_CreateFloat(1.0f);
    g_play_opacity_bind   = UIBind_OpacityToSignal(targetW, g_play_opacity_signal);

    /* ----------- Test result listing below the playground ---------- */
    float y = (float)(header_h + play_h);
    for (int i = 0; i < g_count; i++) {
        const TestRow* r = &g_results[i];
        mklabel(c, r->pass ? "PASS" : "FAIL", 13.0f,
                r->pass ? COLOR_PASS : COLOR_FAIL, 24.0f, y);
        mklabel(c, r->label, 13.0f, COLOR_INK, 80.0f, y);
        y += (float)row_h;
    }

    UIApp_SetChildren(app, c);
    UIApp_SetBackgroundColor(app, COLOR_BG);
    UIApp_SetEventCallback(app, UI_EVENT_FRAMERATE_CHANGED, OnFrameTick);

    UIApp_ShowWindow(app);
    UIApp_Run(app);

    UIBind_Destroy(g_demo_fps_bind);
    UISignal_Destroy(g_demo_fps_signal);
    UIBind_Destroy(g_play_text_bind);
    UISignal_Destroy(g_play_text_signal);
    UISignal_Unsubscribe(g_play_color_sub);
    UISignal_Destroy(g_play_color_signal);
    UIBind_Destroy(g_play_visible_bind);
    UISignal_Destroy(g_play_visible_signal);
    UIBind_Destroy(g_play_opacity_bind);
    UISignal_Destroy(g_play_opacity_signal);
    UIApp_Destroy(app);
    return g_fail == 0 ? 0 : 1;
}

// --------------------------------------------------------------------
// Entry
// --------------------------------------------------------------------

int main(void) {
    printf("UISignal / UIBind unit tests\n");
    printf("============================\n");

    test_basic_types();
    test_dedupe();
    test_multi_subscribers();
    test_unsubscribe_during_notify();
    test_reentrancy_guard();
    test_force_notify();
    test_bind_text_string();
    test_bind_text_format();
    test_bind_visible_opacity();

    printf("\nSummary: %d passed, %d failed\n", g_pass, g_fail);
    return show_results_window();
}
