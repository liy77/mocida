#ifndef UIKIT_VIDEO_H
#define UIKIT_VIDEO_H

#include <SDL3/SDL.h>
#include <uikit/widget.h>
#include <uikit/color.h>
#include <uikit/image.h>     // reuses UIFillMode

#define UI_WIDGET_VIDEO "@uikit/video"

/**
 * Video widget backed by Windows Media Foundation (MF). Supports any
 * format / codec the host Windows install can decode - typically MP4,
 * MKV, AVI, WMV, MOV, plus their H.264 / H.265 / AAC content.
 *
 * Why MF and not pl_mpeg / FFmpeg?
 *   - pl_mpeg only handles MPEG-1, which fails the "MP4 / MKV / common
 *     formats" requirement.
 *   - FFmpeg covers everything but adds tens of MB to the binary and
 *     a non-trivial license footprint.
 *   - MF is part of Windows since Vista; it adds zero bytes to the
 *     binary, uses hardware acceleration when available, and decodes
 *     anything the user can already play in Windows Media Player.
 *
 * Trade-off: MF is Windows-only. A future Linux/macOS port would need
 * a different backend (GStreamer / AVFoundation / FFmpeg).
 *
 * Each UIVideo owns:
 *   - An IMFSourceReader that streams decoded frames as BGRA32.
 *   - A streaming SDL_Texture sized to the video's native resolution.
 *   - (Optional) an SDL_AudioStream for the audio track.
 *
 * Public API is intentionally small. The widget renders in whichever
 * fillMode you choose (same enum as UIImage).
 */
typedef struct UIVideo UIVideo;
typedef void (*UIVideoCallback)(UIVideo* v, void* userdata);

/**
 * Optional early-init hook for the video backend.
 *
 * On most platforms UIVideo_Create lazily initialises the backend on
 * first use. On Linux with GStreamer 1.28 + SDL3 (notably WSLg), the
 * lazy path crashes inside gst_init_check because SDL has already
 * touched GLib state in a way GStreamer's option parser can't recover
 * from. Calling UIVideo_PreInit at the top of main() — BEFORE any
 * SDL or other mocida call — sidesteps the conflict.
 *
 * Idempotent; safe to call zero, one, or many times. No-op on Windows
 * (Media Foundation initialises per-instance inside UIVideo_Create)
 * and on builds without a video backend.
 */
void UIVideo_PreInit(void);

/**
 * Loads a video file. Returns NULL when the format isn't supported or
 * the file is missing. Heavy initialization (MF startup, codec probe)
 * happens here; the first frame is NOT decoded yet.
 *
 * Path resolution: tries the path as-is, then relative to the .exe
 * directory (same fallback chain as UIAsset_LoadTexture).
 */
UIVideo* UIVideo_Create(const char* path);

UIVideo* UIVideo_Play (UIVideo* v);
UIVideo* UIVideo_Pause(UIVideo* v);
UIVideo* UIVideo_Stop (UIVideo* v);
UIVideo* UIVideo_SetLoop  (UIVideo* v, int loop);
UIVideo* UIVideo_SetVolume(UIVideo* v, float volume);
UIVideo* UIVideo_SetFillMode(UIVideo* v, UIFillMode mode);
UIVideo* UIVideo_SetMuted (UIVideo* v, int muted);

UIVideo* UIVideo_OnEnded  (UIVideo* v, UIVideoCallback cb, void* userdata);

int UIVideo_IsPlaying  (UIVideo* v);
int UIVideo_GetWidth   (UIVideo* v);
int UIVideo_GetHeight  (UIVideo* v);
double UIVideo_GetTime    (UIVideo* v); /**< current playback position in seconds */
double UIVideo_GetDuration(UIVideo* v); /**< total length in seconds (cached, 0 if unknown) */

/**
 * Jumps to `seconds` from the start of the file. Clears the queued
 * audio + pending frame so playback resumes at the new position with
 * a clean A/V state. Works while playing or paused.
 */
void UIVideo_Seek(UIVideo* v, double seconds);

void UIVideo_Destroy(UIVideo* v);

// ---------------------------------------------------------------------
// Internal hooks used by the renderer in window.c. Decodes the next
// few frames into the cached texture if `v` is currently playing.
// ---------------------------------------------------------------------
void          UIVideo_Tick      (UIVideo* v, SDL_Renderer* renderer);
SDL_Texture*  UIVideo_GetTexture(UIVideo* v);

#endif // UIKIT_VIDEO_H
