#include <uikit/sound.h>
#include <uikit/debug.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/**
 * Decoded audio sample plus its playback stream. Created from a WAV
 * file via UISound_Load; played non-blocking through SDL's default
 * audio device.
 */
struct UISound {
    Uint8*           pcm;     /**< Decoded PCM data (heap-owned via SDL_LoadWAV). */
    Uint32           pcmLen;  /**< Length of `pcm` in bytes. */
    SDL_AudioSpec    spec;    /**< Audio format of the loaded sample. */
    SDL_AudioStream* stream;  /**< Stream bound to the default playback device. */
};

// Path candidates mirror UIAsset_LoadSurface: cwd, exe dir, exe dir/..
// Audio uses SDL_LoadWAV directly so we can't reuse the helper - the
// API takes a path, not a stream.
#define SOUND_PATH_BUF 1024

static int LoadWavCandidate(const char* path, SDL_AudioSpec* spec,
                            Uint8** out, Uint32* outLen) {
    if (!path) return 0;
    return SDL_LoadWAV(path, spec, out, outLen) ? 1 : 0;
}

static int TryLoadWav(const char* path, SDL_AudioSpec* spec,
                      Uint8** out, Uint32* outLen) {
    if (LoadWavCandidate(path, spec, out, outLen)) return 1;

    const char* base = SDL_GetBasePath();
    if (base) {
        char buf[SOUND_PATH_BUF];
        int n = snprintf(buf, sizeof(buf), "%s%s", base, path);
        if (n > 0 && n < (int)sizeof(buf) &&
            LoadWavCandidate(buf, spec, out, outLen)) return 1;

        n = snprintf(buf, sizeof(buf), "%s../%s", base, path);
        if (n > 0 && n < (int)sizeof(buf) &&
            LoadWavCandidate(buf, spec, out, outLen)) return 1;
    }
    return 0;
}

UISound* UISound_LoadWav(const char* path) {
    if (!path) return NULL;

    // Ensure audio subsystem is initialised on first use.
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            UI_ERROR(UI_CAT_SOUND, "SDL_INIT_AUDIO failed: %s", SDL_GetError());
            return NULL;
        }
    }

    UISound* s = (UISound*)calloc(1, sizeof(UISound));
    if (!s) return NULL;

    if (!TryLoadWav(path, &s->spec, &s->pcm, &s->pcmLen)) {
        UI_ERROR(UI_CAT_SOUND, "could not load '%s'", path);
        free(s);
        return NULL;
    }

    // Open an audio stream on the default playback device with the
    // file's source format. SDL handles conversion to the device's
    // native format internally.
    s->stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &s->spec, NULL, NULL);
    if (!s->stream) {
        UI_ERROR(UI_CAT_SOUND, "SDL_OpenAudioDeviceStream failed: %s",
                SDL_GetError());
        SDL_free(s->pcm);
        free(s);
        return NULL;
    }
    return s;
}

void UISound_Play(UISound* s) {
    if (!s || !s->stream || !s->pcm) return;
    // Clear anything still queued so a rapid re-trigger restarts cleanly.
    SDL_ClearAudioStream(s->stream);
    SDL_PutAudioStreamData(s->stream, s->pcm, (int)s->pcmLen);
    SDL_ResumeAudioStreamDevice(s->stream);
}

void UISound_SetGain(UISound* s, float gain) {
    if (!s || !s->stream) return;
    if (gain < 0.0f) gain = 0.0f;
    SDL_SetAudioStreamGain(s->stream, gain);
}

void UISound_Stop(UISound* s) {
    if (!s || !s->stream) return;
    SDL_ClearAudioStream(s->stream);
    SDL_PauseAudioStreamDevice(s->stream);
}

int UISound_IsPlaying(UISound* s) {
    if (!s || !s->stream) return 0;
    return SDL_GetAudioStreamAvailable(s->stream) > 0 ? 1 : 0;
}

void UISound_Destroy(UISound* s) {
    if (!s) return;
    if (s->stream) SDL_DestroyAudioStream(s->stream);
    if (s->pcm)    SDL_free(s->pcm);
    free(s);
}
