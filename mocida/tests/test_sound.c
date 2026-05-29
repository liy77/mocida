// test_sound.c
//
// One-shot audio: a button plays a short WAV via UISound. The test
// generates a tiny in-memory WAV file at startup so it has something to
// play without shipping a binary asset.
//
// Drop your own .wav file into assets/click.wav to swap in a different
// sound effect - the test falls back to a synthesised beep if the file
// isn't present.

#include <uikit/app.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

static UISound* g_sound = NULL;

static void OnPlay(UIButton* b, void* ud) {
    (void)b; (void)ud;
    if (g_sound) UISound_Play(g_sound);
}

// Writes a tiny 220 Hz square-wave PCM WAV to the given path so the
// test always has something audible to play.
static void EnsureBeepFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f) { fclose(f); return; } // already there

    const int sampleRate = 44100;
    const int durationMs = 120;
    const int numSamples = sampleRate * durationMs / 1000;
    const int dataBytes  = numSamples * 2; // 16-bit mono

    f = fopen(path, "wb");
    if (!f) return;

    // WAV header (RIFF / fmt / data) - little-endian.
    const Uint32 chunkSize = 36 + dataBytes;
    const Uint16 audioFormat = 1; // PCM
    const Uint16 numChannels = 1;
    const Uint32 byteRate = sampleRate * 2;
    const Uint16 blockAlign = 2;
    const Uint16 bitsPerSample = 16;

    fwrite("RIFF", 1, 4, f);
    fwrite(&chunkSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    Uint32 sub1 = 16; fwrite(&sub1, 4, 1, f);
    fwrite(&audioFormat, 2, 1, f);
    fwrite(&numChannels, 2, 1, f);
    Uint32 sr = sampleRate; fwrite(&sr, 4, 1, f);
    fwrite(&byteRate, 4, 1, f);
    fwrite(&blockAlign, 2, 1, f);
    fwrite(&bitsPerSample, 2, 1, f);
    fwrite("data", 1, 4, f);
    Uint32 db = dataBytes; fwrite(&db, 4, 1, f);

    // Sine wave samples for a soft beep.
    for (int i = 0; i < numSamples; i++) {
        const float t = (float)i / sampleRate;
        const float env = (i < numSamples / 8)
            ? (float)i / (numSamples / 8)                     // fade-in
            : (i > numSamples * 7 / 8)
                ? (float)(numSamples - i) / (numSamples / 8)  // fade-out
                : 1.0f;
        const float sample = sinf(t * 2.0f * 3.14159f * 660.0f) * env * 0.4f;
        const Sint16 pcm = (Sint16)(sample * 32760.0f);
        fwrite(&pcm, 2, 1, f);
    }
    fclose(f);
}

int main(void) {
    UIApp* app = UIApp_Create("sound", 480, 240);
    if (!app) return 1;
    UISearchFonts();
    UIApp_SetWindowIcon(app, "assets/logo.svg");

    EnsureBeepFile("assets/click.wav");
    g_sound = UISound_LoadWav("assets/click.wav");
    if (!g_sound) {
        fprintf(stderr, "[sound] WAV failed to load - check assets/click.wav\n");
    }

    UIChildren* children = UIChildren_Create(4);

    UIText* hint = UIText_Create("Click the button to play a short beep.", 14.0f);
    UIText_SetFontFamily(hint, UIGetFont("Arial"));
    UIText_SetColor(hint, (UIColor){ 71, 85, 105, 1.0f });
    UIWidget* hintW = widgc(hint);
    UIWidget_SetPosition(hintW, 30.0f, 30.0f);
    UIChildren_Add(children, hintW);

    UIButton* btn = UIButton_Create("Play sound", 18.0f);
    UIButton_SetFontFamily(btn, UIGetFont("Arial"));
    UIButton_SetRadius(btn, 8.0f);
    UIButton_OnClick(btn, OnPlay, NULL);
    UIWidget* btnW = widgcs(btn, 220.0f, 56.0f);
    UIWidget_SetPosition(btnW, 30.0f, 100.0f);
    UIChildren_Add(children, btnW);

    UIApp_SetChildren(app, children);
    UIApp_SetBackgroundColor(app, (UIColor){ 248, 250, 252, 1.0f });
    UIApp_ShowWindow(app);
    UIApp_Run(app);

    if (g_sound) UISound_Destroy(g_sound);
    UIApp_Destroy(app);
    return 0;
}
