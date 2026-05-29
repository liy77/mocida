// =====================================================================
// macOS backend — AVFoundation (AVPlayer + AVPlayerItemVideoOutput).
//
// Compiled only when CMake defines MOCIDA_HAS_AVFOUNDATION (APPLE builds).
// The C side of UIVideo lives in video.c, whose __APPLE__ +
// MOCIDA_HAS_AVFOUNDATION branch is intentionally EMPTY so every public
// symbol resolves here exactly once.
//
// Architecture (texture pipeline, mirrors the Linux GStreamer backend):
//
//   AVURLAsset -> AVPlayerItem -> AVPlayer            (decode + clock)
//        |                          |
//        |          AVPlayerItemVideoOutput {BGRA}    (video frames)
//        |                          v
//        |              copyPixelBufferForItemTime -> SDL_Texture
//        |
//        +-- audio: AVPlayer plays the audio track NATIVELY through
//            CoreAudio. Unlike the GStreamer backend we do NOT pump PCM
//            into an SDL_AudioStream — volume/mute go straight to the
//            AVPlayer, which is simpler and keeps A/V sync in AVFoundation.
//
//   Per-frame: window.c calls UIVideo_Tick (SDL thread = main thread on
//   macOS); we pull the newest decoded frame for the player's current
//   time and upload it to a streaming SDL_Texture at native resolution.
//   AVFoundation decodes on its own threads, so playback stays smooth
//   even if a GUI tick is slow.
//
// Format coverage: anything the host's VideoToolbox can decode — MP4 /
// MOV / M4V (H.264/HEVC/AAC) out of the box, plus whatever extra codecs
// the system has. (No MKV: AVFoundation doesn't demux Matroska.)
//
// Memory management: MANUAL retain/release (no ARC). Every ObjC object
// stored in the C struct is retained on store and released in Destroy.
// =====================================================================

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#include <SDL3/SDL.h>

// video.h carries its own extern "C" guard, so the public UIVideo API is
// declared with C linkage when compiled here as Obj-C++. (Wrapping the
// include in extern "C" would wrongly force C linkage onto the C++ stdlib
// that mimalloc.h transitively pulls in.)
#include <uikit/video.h>
#include <uikit/debug.h>

#include <stdlib.h>
#include <string.h>
#include <math.h>

struct UIVideo {
    const char* __widget_type;     // == UI_WIDGET_VIDEO

    // ---- AVFoundation ----
    AVPlayer*                player;
    AVPlayerItem*            item;
    AVPlayerItemVideoOutput* output;
    id                       endObserver;   // notification token (retained)
    char*                    source;        // resolved path (heap)

    // ---- SDL sink ----
    SDL_Texture* texture;          // BGRA32 streaming, native resolution
    int          texW, texH;       // texture dimensions

    // ---- Stream info ----
    int    videoWidth;
    int    videoHeight;
    int    hasVideo;
    double durationSec;

    // ---- Playback state ----
    int        playing;
    int        loop;
    int        muted;
    int        eof;
    float      volume;
    UIFillMode fillMode;

    // ---- Callbacks ----
    UIVideoCallback onEnded;
    void*           userdata;
};

// AVFoundation needs no global init (unlike GStreamer/GTK). No-op so the
// public early-init hook stays valid on macOS.
void UIVideo_PreInit(void) { /* nothing to do */ }

// Resolve a user path: try as-is, then relative to the executable dir
// (same fallback chain spirit as UIAsset_LoadTexture). Returns a heap
// string the caller frees, or NULL if neither exists.
static char* ResolvePath(const char* path) {
    if (!path || !*path) return NULL;
    FILE* f = fopen(path, "rb");
    if (f) { fclose(f); return _strdup(path); }

    const char* base = SDL_GetBasePath();   // owned by SDL, do not free
    if (base) {
        size_t n = strlen(base) + strlen(path) + 1;
        char* joined = (char*)malloc(n);
        if (joined) {
            snprintf(joined, n, "%s%s", base, path);
            FILE* g = fopen(joined, "rb");
            if (g) { fclose(g); return joined; }
            free(joined);
        }
    }
    return NULL;
}

// Pull the video track's display size (applying the preferred transform so
// a rotated phone clip reports the right orientation). Best-effort: zero
// stays until the first decoded pixel buffer corrects it.
static void ReadNaturalSize(UIVideo* v, AVAsset* asset) {
    NSArray<AVAssetTrack*>* tracks = [asset tracksWithMediaType:AVMediaTypeVideo];
    AVAssetTrack* vt = tracks.firstObject;
    if (!vt) return;
    CGSize sz = CGSizeApplyAffineTransform(vt.naturalSize, vt.preferredTransform);
    int w = (int)lround(fabs(sz.width));
    int h = (int)lround(fabs(sz.height));
    if (w > 0 && h > 0) {
        v->videoWidth  = w;
        v->videoHeight = h;
        v->hasVideo    = 1;
    }
}

UIVideo* UIVideo_Create(const char* path) {
    char* resolved = ResolvePath(path);
    if (!resolved) {
        UI_ERROR(UI_CAT_VIDEO, "UIVideo_Create: file not found: %s",
                 path ? path : "(null)");
        return NULL;
    }

    UIVideo* v = (UIVideo*)calloc(1, sizeof(*v));
    if (!v) { free(resolved); return NULL; }
    v->__widget_type = UI_WIDGET_VIDEO;
    v->source        = resolved;
    v->volume        = 1.0f;
    v->fillMode      = FILL_FIT;

    NSString* ns  = [NSString stringWithUTF8String:resolved];
    NSURL*    url = ns ? [NSURL fileURLWithPath:ns] : nil;
    if (!url) { UIVideo_Destroy(v); return NULL; }

    AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:nil];
    if (!asset) { UIVideo_Destroy(v); return NULL; }

    // Best-effort up-front metadata (refined in Tick once the item is
    // ready / the first frame lands).
    ReadNaturalSize(v, asset);
    double d = CMTimeGetSeconds(asset.duration);
    if (!isnan(d) && d > 0) v->durationSec = d;

    v->item = [[AVPlayerItem alloc] initWithAsset:asset];
    if (!v->item) { UIVideo_Destroy(v); return NULL; }

    NSDictionary* attrs = @{
        (NSString*)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)
    };
    v->output = [[AVPlayerItemVideoOutput alloc]
                    initWithPixelBufferAttributes:attrs];
    [v->item addOutput:v->output];

    v->player = [[AVPlayer alloc] initWithPlayerItem:v->item];
    v->player.volume = v->volume;

    // End-of-playback: loop or fire onEnded. The block runs on the main
    // queue, which is the same thread UIVideo_Tick runs on, so there is no
    // cross-thread race on the UIVideo fields.
    UIVideo* weakV = v;   // C pointer; lifetime managed by the host widget
    v->endObserver = [[[NSNotificationCenter defaultCenter]
        addObserverForName:AVPlayerItemDidPlayToEndTimeNotification
                    object:v->item
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification* note) {
        (void)note;
        if (weakV->loop) {
            [weakV->player seekToTime:kCMTimeZero];
            [weakV->player play];
        } else {
            weakV->eof     = 1;
            weakV->playing = 0;
            if (weakV->onEnded) weakV->onEnded(weakV, weakV->userdata);
        }
    }] retain];

    return v;
}

UIVideo* UIVideo_Play(UIVideo* v) {
    if (!v || !v->player) return v;
    if (v->eof) { [v->player seekToTime:kCMTimeZero]; v->eof = 0; }
    [v->player play];
    v->playing = 1;
    return v;
}
UIVideo* UIVideo_Pause(UIVideo* v) {
    if (v && v->player) { [v->player pause]; v->playing = 0; }
    return v;
}
UIVideo* UIVideo_Stop(UIVideo* v) {
    if (v && v->player) {
        [v->player pause];
        [v->player seekToTime:kCMTimeZero];
        v->playing = 0;
    }
    return v;
}

void UIVideo_Seek(UIVideo* v, double seconds) {
    if (!v || !v->player) return;
    if (seconds < 0) seconds = 0;
    CMTime t = CMTimeMakeWithSeconds(seconds, NSEC_PER_SEC);
    // Precise seek (zero tolerance) so GetTime lands where the caller asked.
    [v->player seekToTime:t toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
    v->eof = 0;
}

UIVideo* UIVideo_SetLoop(UIVideo* v, int loop) {
    if (v) v->loop = loop ? 1 : 0;
    return v;
}
UIVideo* UIVideo_SetVolume(UIVideo* v, float volume) {
    if (!v) return v;
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    v->volume = volume;
    if (v->player) v->player.volume = volume;
    return v;
}
UIVideo* UIVideo_SetMuted(UIVideo* v, int muted) {
    if (!v) return v;
    v->muted = muted ? 1 : 0;
    if (v->player) v->player.muted = v->muted ? YES : NO;
    return v;
}
UIVideo* UIVideo_SetFillMode(UIVideo* v, UIFillMode m) {
    if (v) v->fillMode = m;
    return v;
}
UIVideo* UIVideo_OnEnded(UIVideo* v, UIVideoCallback cb, void* userdata) {
    if (v) { v->onEnded = cb; v->userdata = userdata; }
    return v;
}

int UIVideo_IsPlaying(UIVideo* v) { return (v && v->playing && !v->eof) ? 1 : 0; }
int UIVideo_GetWidth (UIVideo* v) { return v ? v->videoWidth  : 0; }
int UIVideo_GetHeight(UIVideo* v) { return v ? v->videoHeight : 0; }

double UIVideo_GetTime(UIVideo* v) {
    if (!v || !v->player) return 0.0;
    double t = CMTimeGetSeconds(v->player.currentTime);
    return (isnan(t) || t < 0) ? 0.0 : t;
}
double UIVideo_GetDuration(UIVideo* v) { return v ? v->durationSec : 0.0; }

void UIVideo_Tick(UIVideo* v, SDL_Renderer* renderer) {
    if (!v || !v->player || !v->output || !renderer) return;

    // Cache duration once the item resolves it (asset.duration can be
    // indefinite up front for some containers).
    if (v->durationSec <= 0.0 && v->item &&
        v->item.status == AVPlayerItemStatusReadyToPlay) {
        double d = CMTimeGetSeconds(v->item.duration);
        if (!isnan(d) && d > 0) v->durationSec = d;
    }

    CMTime now = [v->player currentTime];
    if (![v->output hasNewPixelBufferForItemTime:now]) return;

    CVPixelBufferRef pb = [v->output copyPixelBufferForItemTime:now
                                            itemTimeForDisplay:NULL];
    if (!pb) return;

    CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
    void*  base = CVPixelBufferGetBaseAddress(pb);
    size_t bpr  = CVPixelBufferGetBytesPerRow(pb);
    int    w    = (int)CVPixelBufferGetWidth(pb);
    int    h    = (int)CVPixelBufferGetHeight(pb);

    if (base && w > 0 && h > 0) {
        // First authoritative dimensions (corrects ReadNaturalSize, which
        // can be off when there's a non-trivial pixel-aspect / transform).
        v->videoWidth  = w;
        v->videoHeight = h;
        v->hasVideo    = 1;

        if (!v->texture || v->texW != w || v->texH != h) {
            if (v->texture) SDL_DestroyTexture(v->texture);
            v->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32,
                                           SDL_TEXTUREACCESS_STREAMING, w, h);
            v->texW = w;
            v->texH = h;
        }
        if (v->texture) {
            // Use the buffer's real stride — CVPixelBuffer rows are often
            // padded for alignment, so width*4 would shear the image.
            SDL_UpdateTexture(v->texture, NULL, base, (int)bpr);
        }
    }

    CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
    CVPixelBufferRelease(pb);
}

SDL_Texture* UIVideo_GetTexture(UIVideo* v) {
    return v ? v->texture : NULL;
}

void UIVideo_Destroy(UIVideo* v) {
    if (!v) return;
    if (v->player) { [v->player pause]; }
    if (v->endObserver) {
        [[NSNotificationCenter defaultCenter] removeObserver:v->endObserver];
        [v->endObserver release];
        v->endObserver = nil;
    }
    if (v->item && v->output) {
        @try { [v->item removeOutput:v->output]; }
        @catch (NSException* e) { (void)e; }
    }
    if (v->output) { [v->output release]; v->output = nil; }
    if (v->player) { [v->player release]; v->player = nil; }
    if (v->item)   { [v->item release];   v->item   = nil; }
    if (v->texture) SDL_DestroyTexture(v->texture);
    free(v->source);
    free(v);
}
