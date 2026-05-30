// test_video.c
//
// Plays a video file via UIVideo (Windows Media Foundation backend).
//
// Two ways to load a file:
//   1. Drag-and-drop a file onto the window.
//   2. Click "Open file..." to bring up the native file picker.
//
// Controls:
//   - Position slider: drag to seek to any point in the video.
//     Below it: "current / total" timestamp.
//   - Volume slider: 0..1 with live updates.
//   - Play/Pause / Stop / Mute / Loop buttons.

#include <uikit/app.h>
#include <stdio.h>
#include <string.h>

#ifdef MOCIDA_IOS
#include <SDL3/SDL_main.h>   // iOS: UIKit app delegate boots SDL_main
#endif

// ---- Globals -----------------------------------------------------------
static UIApp*      g_app        = NULL;
static UIChildren* g_children   = NULL;

static UIWidget*   g_videoSlot  = NULL;
static UIVideo*    g_video      = NULL;

static UIWidget*   g_dropZone   = NULL;
static UIWidget*   g_placeholder= NULL;
static UIWidget*   g_statusLbl  = NULL;
static UIWidget*   g_timeLbl    = NULL;

static UIButton*   g_playBtn    = NULL;
static UIButton*   g_muteBtn    = NULL;
static UIButton*   g_loopBtn    = NULL;

static UISlider*   g_posSlider  = NULL;
static UISlider*   g_volSlider  = NULL;

// Widget wrappers kept for the responsive layout (on_resize scales these).
static UIWidget* g_posW = NULL;
static UIWidget* g_openW = NULL;
static UIWidget* g_playW = NULL;
static UIWidget* g_stopW = NULL;
static UIWidget* g_muteW = NULL;
static UIWidget* g_loopW = NULL;
static UIWidget* g_vLblW = NULL;
static UIWidget* g_volW = NULL;

// Place a widget at a base (960-wide-design) position, scaled to fit the
// current window. Mirrors the demo's responsive approach.
static void vid_place(UIWidget* w, float S, float x, float y, float bw, float bh) {
    if (!w) return;
    UIWidget_SetPosition(w, x * S, y * S);
    if (bw > 0.0f) UIWidget_SetSize(w, bw * S, bh * S);
}

static void OnVideoResize(int win_w, int win_h, void* ud) {
    (void)win_h; (void)ud;
    if (win_w <= 0) return;
    float S = (float)win_w / 960.0f;
    if (S > 1.0f) S = 1.0f;
    if (S < 0.34f) S = 0.34f;
    // The video surface slot (whichever widget currently occupies it).
    vid_place(g_dropZone,    S, 40, 30, 880, 480);
    vid_place(g_videoSlot,   S, 40, 30, 880, 480);
    vid_place(g_placeholder, S, 40, 30, 880, 480);
    vid_place(g_posW,  S, 40, 530, 880, 28);
    vid_place(g_timeLbl, S, 40, 565, 0, 0);
    vid_place(g_openW, S, 40,  600, 130, 40);
    vid_place(g_playW, S, 180, 600, 100, 40);
    vid_place(g_stopW, S, 290, 600, 100, 40);
    vid_place(g_muteW, S, 400, 600, 100, 40);
    vid_place(g_loopW, S, 510, 600, 120, 40);
    vid_place(g_vLblW, S, 660, 600, 0, 0);
    vid_place(g_volW,  S, 660, 624, 220, 28);
    vid_place(g_statusLbl, S, 40, 700, 0, 0);
    // Scale label/button fonts (readability floor).
    float FS = (S < 0.62f) ? 0.62f : S;
    if (g_timeLbl && g_timeLbl->data)   UIText_SetFontSize((UIText*)g_timeLbl->data, 14.0f * FS);
    if (g_vLblW && g_vLblW->data)       UIText_SetFontSize((UIText*)g_vLblW->data, 13.0f * FS);
    if (g_statusLbl && g_statusLbl->data) UIText_SetFontSize((UIText*)g_statusLbl->data, 13.0f * FS);
    if (g_openW && g_openW->data) UIButton_SetFontSize((UIButton*)g_openW->data, 16.0f * FS);
    if (g_playBtn) UIButton_SetFontSize(g_playBtn, 16.0f * FS);
    if (g_stopW && g_stopW->data) UIButton_SetFontSize((UIButton*)g_stopW->data, 16.0f * FS);
    if (g_muteBtn) UIButton_SetFontSize(g_muteBtn, 16.0f * FS);
    if (g_loopBtn) UIButton_SetFontSize(g_loopBtn, 16.0f * FS);
}

static int g_playing = 0;
static int g_muted   = 0;
static int g_loop    = 0;

// Formats `seconds` as "M:SS" or "H:MM:SS" depending on length.
static void FormatTime(double seconds, char* out, size_t cap) {
    if (seconds < 0.0) seconds = 0.0;
    const int total = (int)seconds;
    const int h = total / 3600;
    const int m = (total % 3600) / 60;
    const int s = total % 60;
    if (h > 0) snprintf(out, cap, "%d:%02d:%02d", h, m, s);
    else       snprintf(out, cap, "%d:%02d", m, s);
}

static void RefreshStatus(void) {
    if (g_statusLbl && g_statusLbl->data) {
        char buf[200];
        if (g_video) {
            snprintf(buf, sizeof(buf),
                     "%s   |   audio: %s   |   loop: %s   |   %dx%d",
                     g_playing ? "PLAYING" : "PAUSED",
                     g_muted   ? "muted"   : "on",
                     g_loop    ? "yes"     : "no",
                     UIVideo_GetWidth(g_video),
                     UIVideo_GetHeight(g_video));
        } else {
            snprintf(buf, sizeof(buf),
                     "no video loaded - drop a file or click Open");
        }
        UIText_SetText((UIText*)g_statusLbl->data, buf);
    }

    if (g_timeLbl && g_timeLbl->data) {
        char cur[32] = "0:00", dur[32] = "0:00", buf[80];
        if (g_video) {
            FormatTime(UIVideo_GetTime(g_video),     cur, sizeof(cur));
            FormatTime(UIVideo_GetDuration(g_video), dur, sizeof(dur));
        }
        snprintf(buf, sizeof(buf), "%s / %s", cur, dur);
        UIText_SetText((UIText*)g_timeLbl->data, buf);
    }
}

// Mirrors the playback time into the position slider, unless the user
// is currently dragging it. Called from the FPS tick (~once per sec).
static void SyncPositionSlider(void) {
    if (!g_posSlider || !g_video) return;
    if (g_posSlider->dragging) return; // don't fight the user
    const double dur = UIVideo_GetDuration(g_video);
    if (dur > 0.0) {
        UISlider_SetRange(g_posSlider, 0.0f, (float)dur);
        UISlider_SetValue(g_posSlider, (float)UIVideo_GetTime(g_video));
    }
}

// ---- Loader -----------------------------------------------------------

static int LoadVideo(const char* path) {
    if (!path || !g_children) return 0;

    if (g_videoSlot) {
        UIChildren_Remove(g_children, g_videoSlot);
        g_videoSlot = NULL;
        g_video     = NULL;
    }
    if (g_placeholder) {
        UIChildren_Remove(g_children, g_placeholder);
        g_placeholder = NULL;
    }

    UIVideo* v = UIVideo_Create(path);
    if (!v) {
        UIRectangle* ph = UIRectangle_Create();
        UIRectangle_SetColor(ph, (UIColor){ 30, 41, 59, 1.0f });
        UIRectangle_SetRadius(ph, 12.0f);
        g_placeholder = widgcs(ph, 880.0f, 480.0f);
        UIWidget_SetPosition(g_placeholder, 40.0f, 30.0f);
        UIWidget_SetZIndex(g_placeholder, 1);
        UIChildren_Add(g_children, g_placeholder);
        if (g_app) OnVideoResize(UIApp_GetWidth(g_app), UIApp_GetHeight(g_app), NULL);

        printf("[video] failed to open '%s'\n", path);
        g_playing = 0;
        RefreshStatus();
        return 0;
    }

    UIVideo_SetFillMode(v, FILL_FIT);
    g_video = v;
    g_videoSlot = widgcs(v, 880.0f, 480.0f);
    UIWidget_SetPosition(g_videoSlot, 40.0f, 30.0f);
    UIWidget_SetZIndex(g_videoSlot, 1);
    UIChildren_Add(g_children, g_videoSlot);
    if (g_app) OnVideoResize(UIApp_GetWidth(g_app), UIApp_GetHeight(g_app), NULL);

    // Configure slider range from the cached duration.
    const double dur = UIVideo_GetDuration(v);
    if (g_posSlider && dur > 0.0) {
        UISlider_SetRange(g_posSlider, 0.0f, (float)dur);
        UISlider_SetValue(g_posSlider, 0.0f);
    }
    // Apply current volume slider value to the new video.
    if (g_volSlider) {
        UIVideo_SetVolume(v, g_volSlider->value);
    }

    UIVideo_Play(v);
    UIVideo_SetMuted(v, g_muted);
    UIVideo_SetLoop (v, g_loop);
    g_playing = 1;
    if (g_playBtn) {
        UIButton_SetText(g_playBtn, "Pause");
        UIText_DestroyTexture(g_playBtn->label);
    }
    printf("[video] loaded '%s' (%dx%d, %.1fs)\n", path,
           UIVideo_GetWidth(v), UIVideo_GetHeight(v), dur);
    RefreshStatus();
    return 1;
}

// ---- Drop + dialog handlers -------------------------------------------

static void OnDrop(UIFileDrop* fd, const char* path, void* ud) {
    (void)fd; (void)ud;
    printf("[drop] %s\n", path);
    LoadVideo(path);
}

static void OnFilePicked(const char* path, void* ud) {
    (void)ud;
    if (!path) {
        printf("[dialog] cancelled\n");
        return;
    }
    LoadVideo(path);
}

static void OnOpenClicked(UIButton* b, void* ud) {
    (void)b; (void)ud;
    if (!g_app || !g_app->window) return;
    UIFileDialog_OpenFile(g_app->window->sdlWindow,
                          "Video files",
                          "mp4;mkv;mov;avi;webm;wmv;m4v",
                          OnFilePicked, NULL);
}

// ---- Playback callbacks ----------------------------------------------

static void OnPlayPause(UIButton* b, void* ud) {
    (void)ud;
    if (!g_video) return;
    g_playing = !g_playing;
    if (g_playing) UIVideo_Play(g_video);
    else           UIVideo_Pause(g_video);
    UIButton_SetText(b, g_playing ? "Pause" : "Play");
    UIText_DestroyTexture(b->label);
    RefreshStatus();
}

static void OnStop(UIButton* b, void* ud) {
    (void)b; (void)ud;
    if (!g_video) return;
    UIVideo_Stop(g_video);
    g_playing = 0;
    if (g_playBtn) {
        UIButton_SetText(g_playBtn, "Play");
        UIText_DestroyTexture(g_playBtn->label);
    }
    if (g_posSlider) UISlider_SetValue(g_posSlider, 0.0f);
    RefreshStatus();
}

static void OnMute(UIButton* b, void* ud) {
    (void)ud;
    if (!g_video) return;
    g_muted = !g_muted;
    UIVideo_SetMuted(g_video, g_muted);
    UIButton_SetText(b, g_muted ? "Unmute" : "Mute");
    UIText_DestroyTexture(b->label);
    RefreshStatus();
}

static void OnLoop(UIButton* b, void* ud) {
    (void)ud;
    g_loop = !g_loop;
    if (g_video) UIVideo_SetLoop(g_video, g_loop);
    UIButton_SetText(b, g_loop ? "Loop: ON" : "Loop: OFF");
    UIText_DestroyTexture(b->label);
    RefreshStatus();
}

static void OnEnded(UIVideo* v, void* ud) {
    (void)v; (void)ud;
    printf("[video] ended\n");
    g_playing = 0;
    if (g_playBtn) {
        UIButton_SetText(g_playBtn, "Play");
        UIText_DestroyTexture(g_playBtn->label);
    }
}

// Position slider drag -> seek. UISlider only fires onChange from user
// drags (programmatic SetValue doesn't), so we don't have to filter
// out our own SyncPositionSlider updates.
static void OnPosChange(UISlider* s, float value, void* ud) {
    (void)s; (void)ud;
    if (g_video) UIVideo_Seek(g_video, (double)value);
}

static void OnVolChange(UISlider* s, float value, void* ud) {
    (void)s; (void)ud;
    if (g_video) UIVideo_SetVolume(g_video, value);
}

static void OnFpsTick(UIEventData data) {
    (void)data;
    SyncPositionSlider();
    RefreshStatus();
}

// ---- main -------------------------------------------------------------

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    // Linux: gst_init must run before SDL_Init or it segfaults inside
    // GLib's option parser on certain WSL/Ubuntu builds. UIVideo_PreInit
    // is a no-op on Windows.
    UIVideo_PreInit();

    UIApp* app = UIApp_Create("video", 960, 760);
    if (!app) return 1;
    g_app = app;
    UISearchFonts();
    UIApp_SetWindowIcon(app, "assets/logo.svg");
    UIApp_SetEventCallback(app, UI_EVENT_FRAMERATE_CHANGED, OnFpsTick);

    UIChildren* children = UIChildren_Create(20);
    g_children = children;

    // Drop zone (under the video).
    UIFileDrop* fd = UIFileDrop_Create(
        "Drop a video here, or click \"Open file...\" below");
    UIFileDrop_SetFontFamily(fd, UIGetFont("Arial"));
    UIFileDrop_SetFontSize  (fd, 16.0f);
    UIFileDrop_OnDrop(fd, OnDrop, NULL);
    g_dropZone = widgcs(fd, 880.0f, 480.0f);
    UIWidget_SetPosition(g_dropZone, 40.0f, 30.0f);
    UIWidget_SetZIndex  (g_dropZone, 0);
    UIChildren_Add(children, g_dropZone);

    // Position slider (under the video, full width).
    UISlider* pos = UISlider_Create(0.0f, 1.0f, 0.0f);
    UISlider_OnChange(pos, OnPosChange, NULL);
    g_posSlider = pos;
    g_posW = widgcs(pos, 880.0f, 28.0f);
    UIWidget_SetPosition(g_posW, 40.0f, 530.0f);
    UIChildren_Add(children, g_posW);

    // Time label "M:SS / M:SS"
    UIText* tlbl = UIText_Create("0:00 / 0:00", 14.0f);
    UIText_SetFontFamily(tlbl, UIGetFont("Arial"));
    UIText_SetColor(tlbl, (UIColor){ 71, 85, 105, 1.0f });
    g_timeLbl = widgc(tlbl);
    UIWidget_SetPosition(g_timeLbl, 40.0f, 565.0f);
    UIChildren_Add(children, g_timeLbl);

    // Toolbar row.
    UIButton* openBtn = UIButton_Create("Open file...", 16.0f);
    UIButton_SetFontFamily(openBtn, UIGetFont("Arial"));
    UIButton_SetRadius(openBtn, 8.0f);
    UIButton_SetColors(openBtn, (UIColor){59,130,246,1.0f}, UI_COLOR_WHITE);
    UIButton_OnClick (openBtn, OnOpenClicked, NULL);
    g_openW = widgcs(openBtn, 130.0f, 40.0f);
    UIWidget_SetPosition(g_openW, 40.0f, 600.0f);
    UIChildren_Add(children, g_openW);

    UIButton* playBtn = UIButton_Create("Play", 16.0f);
    UIButton_SetFontFamily(playBtn, UIGetFont("Arial"));
    UIButton_SetRadius(playBtn, 8.0f);
    UIButton_OnClick(playBtn, OnPlayPause, NULL);
    g_playBtn = playBtn;
    g_playW = widgcs(playBtn, 100.0f, 40.0f);
    UIWidget_SetPosition(g_playW, 180.0f, 600.0f);
    UIChildren_Add(children, g_playW);

    UIButton* stopBtn = UIButton_Create("Stop", 16.0f);
    UIButton_SetFontFamily(stopBtn, UIGetFont("Arial"));
    UIButton_SetRadius(stopBtn, 8.0f);
    UIButton_SetColors(stopBtn, (UIColor){239,68,68,1.0f}, UI_COLOR_WHITE);
    UIButton_OnClick (stopBtn, OnStop, NULL);
    g_stopW = widgcs(stopBtn, 100.0f, 40.0f);
    UIWidget_SetPosition(g_stopW, 290.0f, 600.0f);
    UIChildren_Add(children, g_stopW);

    UIButton* muteBtn = UIButton_Create("Mute", 16.0f);
    UIButton_SetFontFamily(muteBtn, UIGetFont("Arial"));
    UIButton_SetRadius(muteBtn, 8.0f);
    UIButton_SetColors(muteBtn, (UIColor){100,116,139,1.0f}, UI_COLOR_WHITE);
    UIButton_OnClick (muteBtn, OnMute, NULL);
    g_muteBtn = muteBtn;
    g_muteW = widgcs(muteBtn, 100.0f, 40.0f);
    UIWidget_SetPosition(g_muteW, 400.0f, 600.0f);
    UIChildren_Add(children, g_muteW);

    UIButton* loopBtn = UIButton_Create("Loop: OFF", 16.0f);
    UIButton_SetFontFamily(loopBtn, UIGetFont("Arial"));
    UIButton_SetRadius(loopBtn, 8.0f);
    UIButton_SetColors(loopBtn, (UIColor){34,197,94,1.0f}, UI_COLOR_WHITE);
    UIButton_OnClick (loopBtn, OnLoop, NULL);
    g_loopBtn = loopBtn;
    g_loopW = widgcs(loopBtn, 120.0f, 40.0f);
    UIWidget_SetPosition(g_loopW, 510.0f, 600.0f);
    UIChildren_Add(children, g_loopW);

    // Volume label + slider on the right.
    UIText* vLbl = UIText_Create("Volume", 13.0f);
    UIText_SetFontFamily(vLbl, UIGetFont("Arial"));
    UIText_SetColor(vLbl, (UIColor){ 71, 85, 105, 1.0f });
    g_vLblW = widgc(vLbl);
    UIWidget_SetPosition(g_vLblW, 660.0f, 600.0f);
    UIChildren_Add(children, g_vLblW);

    UISlider* vol = UISlider_Create(0.0f, 1.0f, 1.0f);
    UISlider_OnChange(vol, OnVolChange, NULL);
    g_volSlider = vol;
    g_volW = widgcs(vol, 220.0f, 28.0f);
    UIWidget_SetPosition(g_volW, 660.0f, 624.0f);
    UIChildren_Add(children, g_volW);

    // Status line at the very bottom.
    UIText* st = UIText_Create("no video loaded - drop a file or click Open", 13.0f);
    UIText_SetFontFamily(st, UIGetFont("Arial"));
    UIText_SetColor(st, (UIColor){71, 85, 105, 1.0f});
    g_statusLbl = widgc(st);
    UIWidget_SetPosition(g_statusLbl, 40.0f, 700.0f);
    UIChildren_Add(children, g_statusLbl);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, (UIColor){ 226, 232, 240, 1.0f });

    // Responsive layout: scale to the real window size (device screen on
    // iOS). Runs once now and on every resize.
    UIApp_OnResize(app, OnVideoResize, NULL);
    OnVideoResize(UIApp_GetWidth(app), UIApp_GetHeight(app), NULL);

    // Try presets so you can also test without dragging.
    const char* presets[] = {
        "assets/sample.mp4", "assets/sample.mkv",
        "assets/sample.mov", "assets/sample.avi",
        NULL
    };
    for (int i = 0; presets[i]; i++) {
        if (LoadVideo(presets[i])) break;
    }
    if (!g_video) printf("[video] no preset found, waiting for drop / dialog\n");

    UIApp_ShowWindow(app);
    UIApp_Run(app);
    UIApp_Destroy(app);
    return 0;
}
