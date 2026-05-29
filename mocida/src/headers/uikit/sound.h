#ifndef UIKIT_SOUND_H
#define UIKIT_SOUND_H

#include <SDL3/SDL.h>
#include <stdint.h>

/**
 * One-shot audio clip backed by SDL3's audio stream API. Use for
 * UI feedback (button clicks, notifications). Each UISound owns
 * decoded PCM bytes + an audio stream attached to the default
 * output device.
 *
 * Threading: SDL3 audio is callback-free for the simple "push and
 * play" usage we need here; UISound_Play just feeds new data into
 * the stream and resumes it.
 */
typedef struct UISound UISound;

/**
 * Loads a WAV file from disk. Path resolution goes through
 * UIAsset_LoadSurface's same fallback list (CWD, exe dir, exe dir/..)
 * so it works regardless of cwd. Returns NULL on failure.
 */
UISound* UISound_LoadWav(const char* path);

/**
 * Plays the sound once from the start. Calling again while it's
 * still playing restarts it cleanly (the stream is cleared first).
 */
void UISound_Play(UISound* s);

/**
 * Sets the gain (0.0 = silent, 1.0 = original level, > 1.0 amplifies).
 * Internally maps to SDL_SetAudioStreamGain.
 */
void UISound_SetGain(UISound* s, float gain);

/**
 * Stops playback if active and clears any queued bytes.
 */
void UISound_Stop(UISound* s);

/**
 * Returns 1 while audio is still queued; 0 otherwise. Cheap.
 */
int  UISound_IsPlaying(UISound* s);

void UISound_Destroy(UISound* s);

#endif // UIKIT_SOUND_H
